/*
 * iwinfo - Wireless Information Library - NLWiFi Backend
 *
 *   Copyright (C) 2014-2016
 *
 * The iwinfo library is free software: you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.
 *
 * The iwinfo library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with the iwinfo library. If not, see http://www.gnu.org/licenses/.
 *
 * The signal handling code is derived from the official madwifi tools,
 * wlanconfig.c in particular. The encryption property handling was
 * inspired by the hostapd madwifi driver.
 *
 * Parts of this code are derived from the Linux iw utility.
 */

#include "iwinfo_nlwifi.h"
#include <libubox/list.h>

static int nlwifi_init(void);
static void nlwifi_callback_close(struct nlwifi_callback *call);

static struct nlwifi_state *nls = NULL;
static struct nlwifi_callback *probe = NULL;
static struct nlwifi_callback *action = NULL;
static struct nlwifi_callback *event = NULL;

LIST_HEAD(iface_list);

struct nlwifi_iface* _get_iface(const char *ifname)
{
	struct nlwifi_iface *iface;
	struct iwreq wrq;
	int family = -1;
	const char *val;

	list_for_each_entry(iface, &iface_list, list)
	{
		if (strcmp(ifname, iface->ifname) == 0)
			return iface;
	}

	if (nlwifi_init() < 0)
		return NULL;

	if (strstr(ifname, "radio"))
	{
		val = iwinfo_uci_get_device_config(ifname, "phyname");
		if (!val)
			return NULL;
	}
	else
	{
		strncpy(wrq.ifr_name, ifname, IFNAMSIZ);
		if (iwinfo_ioctl(SIOCGIWNAME, &wrq) < 0) {
			return NULL;
		}

		val = wrq.u.name;
	}

	family = genl_family_get_id(nls->nlctrl);

	iface = (struct nlwifi_iface *)malloc(sizeof(struct nlwifi_iface));
	if (!iface)
		return NULL;

	memset(iface, 0, sizeof(struct nlwifi_iface));
	snprintf(iface->ifname, sizeof(iface->ifname), "%s", ifname);
	snprintf(iface->phyname, sizeof(iface->phyname), "%s", val);
	iface->family = family;
	list_add(&iface->list, &iface_list);
	return iface;
}

static void free_ifaces(void)
{
	struct nlwifi_iface *iface, *next;

	list_for_each_entry_safe(iface, next, &iface_list, list)
	{
		list_del(&iface->list);
		free(iface);
	}
}

static void nlwifi_close(void)
{
	if (nls)
	{
		if (nls->nl_sock)
			nl_socket_free(nls->nl_sock);
		free(nls);
		nls = NULL;
	}

	nlwifi_callback_close(probe);
	nlwifi_callback_close(action);
	nlwifi_callback_close(event);
	free_ifaces();
}

static int nlwifi_init(void)
{
	int err, fd;

	if (!nls)
	{
		nls = malloc(sizeof(struct nlwifi_state));
		if (!nls)
		{
			err = -ENOMEM;
			goto err;
		}
		memset(nls, 0, sizeof(struct nlwifi_state));

		nls->nl_sock = nl_socket_alloc();
		if (!nls->nl_sock) {
			err = -ENOMEM;
			goto err;
		}

		if (genl_connect(nls->nl_sock)) {
			err = -ENOLINK;
			goto err;
		}

		fd = nl_socket_get_fd(nls->nl_sock);
		if (fcntl(fd, F_SETFD, fcntl(fd, F_GETFD) | FD_CLOEXEC) < 0) {
			err = -EINVAL;
			goto err;
		}

		if (genl_ctrl_alloc_cache(nls->nl_sock, &nls->nl_cache)) {
			err = -ENOMEM;
			goto err;
		}

		nls->nl80211 = genl_ctrl_search_by_name(nls->nl_cache, "nlwifi");
		if (!nls->nl80211) {
			err = -ENOENT;
			goto err;
		}

		nls->nlctrl = genl_ctrl_search_by_name(nls->nl_cache, "nlctrl");
		if (!nls->nlctrl) {
			err = -ENOENT;
			goto err;
		}
	}

	return 0;

err:
	nlwifi_close();
	return err;
}

const char *nlwifi_ifname2phyname(const char *ifname)
{
	struct nlwifi_iface *iface;

	if (ifname == NULL)
		return NULL;

	iface = _get_iface(ifname);
	if (!iface)
		return NULL;

	return iface->phyname;
}

static int nlwifi_ifname2family(const char *ifname)
{
	struct nlwifi_iface *iface;

	if (ifname == NULL)
		return -1;

	iface = _get_iface(ifname);
	if (!iface)
		return -1;

	return iface->family;
}

static int nlwifi_msg_error(struct sockaddr_nl *nla,
	struct nlmsgerr *err, void *arg)
{
	int *ret = arg;
	*ret = err->error;
	return NL_STOP;
}

static int nlwifi_msg_finish(struct nl_msg *msg, void *arg)
{
	int *ret = arg;
	*ret = 0;
	return NL_SKIP;
}

static int nlwifi_msg_ack(struct nl_msg *msg, void *arg)
{
	int *ret = arg;
	*ret = 0;
	return NL_STOP;
}

static int nlwifi_msg_response(struct nl_msg *msg, void *arg)
{
	return NL_SKIP;
}

static void nlwifi_free(struct nlwifi_msg_conveyor *cv)
{
	if (cv)
	{
		if (cv->cb)
			nl_cb_put(cv->cb);

		if (cv->msg)
			nlmsg_free(cv->msg);

		cv->cb  = NULL;
		cv->msg = NULL;
	}
}

static struct nlwifi_msg_conveyor * nlwifi_new(struct genl_family *family, int cmd, int flags)
{
	static struct nlwifi_msg_conveyor cv;

	struct nl_msg *req = NULL;
	struct nl_cb *cb = NULL;

	req = nlmsg_alloc();
	if (!req)
		goto err;

	cb = nl_cb_alloc(NL_CB_DEFAULT);
	if (!cb)
		goto err;

	genlmsg_put(req, 0, 0, genl_family_get_id(family), 0, flags, cmd, 0);

	cv.msg = req;
	cv.cb  = cb;

	return &cv;

err:
	if (cb)
		nl_cb_put(cb);

	if (req)
		nlmsg_free(req);

	return NULL;
}

static struct nlwifi_msg_conveyor * nlwifi_ctl(int cmd, int flags)
{
	if (nlwifi_init() < 0)
		return NULL;

	return nlwifi_new(nls->nlctrl, cmd, flags);
}

static struct nlwifi_msg_conveyor * nlwifi_msg(const char *ifname, int cmd, int flags)
{
	struct nlwifi_msg_conveyor *cv;
	char nif[IFNAMSIZ] = { 0 };

	if (ifname == NULL)
		return NULL;

	if (nlwifi_init() < 0)
		return NULL;

	cv = nlwifi_new(nls->nl80211, cmd, flags);
	if (!cv)
		return NULL;

	memcpy(nif, ifname, strlen(ifname));

	NLA_PUT_STRING(cv->msg, NLWIFI_ATTR_IFNAME, nif);

	return cv;

nla_put_failure:
	nlwifi_free(cv);
	return NULL;
}

static struct nlwifi_msg_conveyor * nlwifi_send(
	struct nlwifi_msg_conveyor *cv,
	int (*cb_func)(struct nl_msg *, void *), void *cb_arg
) {
	static struct nlwifi_msg_conveyor rcv;
	int err = 1;

	if (cb_func)
		nl_cb_set(cv->cb, NL_CB_VALID, NL_CB_CUSTOM, cb_func, cb_arg);
	else
		nl_cb_set(cv->cb, NL_CB_VALID, NL_CB_CUSTOM, nlwifi_msg_response, &rcv);

	if (nl_send_auto_complete(nls->nl_sock, cv->msg) < 0)
		goto err;

	nl_cb_err(cv->cb,               NL_CB_CUSTOM, nlwifi_msg_error,  &err);
	nl_cb_set(cv->cb, NL_CB_FINISH, NL_CB_CUSTOM, nlwifi_msg_finish, &err);
	nl_cb_set(cv->cb, NL_CB_ACK,    NL_CB_CUSTOM, nlwifi_msg_ack,    &err);

	while (err > 0)
		nl_recvmsgs(nls->nl_sock, cv->cb);

	return &rcv;

err:
	nl_cb_put(cv->cb);
	nlmsg_free(cv->msg);

	return NULL;
}

static struct nlattr ** nlwifi_parse(struct nl_msg *msg)
{
	struct genlmsghdr *gnlh = nlmsg_data(nlmsg_hdr(msg));
	static struct nlattr *attr[NLWIFI_ATTR_MAX + 1];

	nla_parse(attr, NLWIFI_ATTR_MAX, genlmsg_attrdata(gnlh, 0),
	          genlmsg_attrlen(gnlh, 0), NULL);

	return attr;
}

static int nlwifi_subscribe_cb(struct nl_msg *msg, void *arg)
{
	struct nlwifi_group_conveyor *cv = arg;

	struct nlattr **attr = nlwifi_parse(msg);
	struct nlattr *mgrpinfo[CTRL_ATTR_MCAST_GRP_MAX + 1];
	struct nlattr *mgrp;
	int mgrpidx;

	if (!attr[CTRL_ATTR_MCAST_GROUPS])
		return NL_SKIP;

	nla_for_each_nested(mgrp, attr[CTRL_ATTR_MCAST_GROUPS], mgrpidx)
	{
		nla_parse(mgrpinfo, CTRL_ATTR_MCAST_GRP_MAX,
		          nla_data(mgrp), nla_len(mgrp), NULL);

		if (mgrpinfo[CTRL_ATTR_MCAST_GRP_ID] &&
		    mgrpinfo[CTRL_ATTR_MCAST_GRP_NAME] &&
		    !strncmp(nla_data(mgrpinfo[CTRL_ATTR_MCAST_GRP_NAME]),
		             cv->name, nla_len(mgrpinfo[CTRL_ATTR_MCAST_GRP_NAME])))
		{
			cv->id = nla_get_u32(mgrpinfo[CTRL_ATTR_MCAST_GRP_ID]);
			break;
		}
	}

	return NL_SKIP;
}

static int nlwifi_subscribe(struct nl_sock *sk, const char *family, const char *group)
{
	struct nlwifi_group_conveyor cv = { .name = group, .id = -ENOENT };
	struct nlwifi_msg_conveyor *req;

	if (!family || !group)
		return -1;

	req = nlwifi_ctl(CTRL_CMD_GETFAMILY, 0);
	if (req)
	{
		NLA_PUT_STRING(req->msg, CTRL_ATTR_FAMILY_NAME, family);
		nlwifi_send(req, nlwifi_subscribe_cb, &cv);

nla_put_failure:
		nlwifi_free(req);
	}

	return nl_socket_add_membership(sk, cv.id);
}

static int nlwifi_wait_cb(struct nl_msg *msg, void *arg)
{
	struct nlwifi_event_conveyor *cv = arg;
	struct genlmsghdr *gnlh = nlmsg_data(nlmsg_hdr(msg));

	if (gnlh->cmd == cv->wait)
		cv->recv = gnlh->cmd;

	return NL_SKIP;
}

static int nlwifi_wait_seq_check(struct nl_msg *msg, void *arg)
{
	return NL_OK;
}

static int nlwifi_wait(const char *ifname, const char *group, int cmd)
{
	struct nlwifi_event_conveyor cv = { .wait = cmd };
	struct nl_cb *cb;
	const char *phyname;

	phyname = nlwifi_ifname2phyname(ifname);
	if (phyname == NULL)
		return -ENOENT;

	if (nlwifi_subscribe(nls->nl_sock, phyname, group))
		return -ENOENT;

	cb = nl_cb_alloc(NL_CB_DEFAULT);

 	if (!cb)
		return -ENOMEM;

	nl_cb_set(cb, NL_CB_SEQ_CHECK, NL_CB_CUSTOM, nlwifi_wait_seq_check, NULL);
	nl_cb_set(cb, NL_CB_VALID,     NL_CB_CUSTOM, nlwifi_wait_cb,        &cv );

	while (!cv.recv)
		nl_recvmsgs(nls->nl_sock, cb);

	nl_cb_put(cb);

	return 0;
}

static int nlwifi_callback_handle(struct nl_msg *msg, void *argu)
{
	struct nlattr **attr = nlwifi_parse(msg);
	struct nlwifi_callback *call = argu;

	if (!attr[NLWIFI_ATTR_IFNAME] ||
		(!attr[call->attr1] && !attr[call->attr2] && !attr[call->attr3]))
		return NL_SKIP;

	call->callback(nla_get_string(attr[NLWIFI_ATTR_IFNAME]), nla_data(attr[NLWIFI_ATTR_MAC]),
					attr[call->attr1] ? nla_data(attr[call->attr1]) : NULL,
					attr[call->attr2] ? nla_data(attr[call->attr2]) : NULL,
					attr[call->attr3] ? nla_get_u32(attr[call->attr3]) :
					(attr[call->attr2] ? nla_len(attr[call->attr2]) : 0));

	return NL_SKIP;
}

static void nlwifi_callback_close(struct nlwifi_callback *call)
{
	if (call)
	{
		if (call->cb)
			nl_cb_put(call->cb);
		if (call->sock)
			nl_socket_free(call->sock);
		free(call);
		call = NULL;
	}
}

static void nlwifi_data_receive(struct uloop_fd *ufd, unsigned int events)
{
	struct nlwifi_callback *call = container_of(ufd, struct nlwifi_callback, ufd);

	nl_recvmsgs(call->sock, call->cb);
}

struct nlwifi_callback *nlwifi_callback_init(const char *group)
{
	struct nlwifi_callback *call;
	struct nlwifi_iface *iface;
	int val = 1;

	call = malloc(sizeof(struct nlwifi_callback));
	if (!call)
		goto error;

	memset(call, 0, sizeof(struct nlwifi_callback));

	call->sock = nl_socket_alloc();
	if (!call->sock)
		goto error;

	if (genl_connect(call->sock))
		goto error;

	setsockopt(call->sock->s_fd, SOL_NETLINK, NETLINK_NO_ENOBUFS, &val, sizeof(val));

	call->cb = nl_cb_alloc(NL_CB_DEFAULT);
	if (!call->cb)
		goto error;

	nl_cb_set(call->cb, NL_CB_SEQ_CHECK, NL_CB_CUSTOM, nlwifi_wait_seq_check, NULL);
	nl_cb_set(call->cb, NL_CB_VALID,	 NL_CB_CUSTOM, nlwifi_callback_handle, call);

	list_for_each_entry(iface, &iface_list, list)
	{
		nlwifi_subscribe(call->sock, iface->phyname, group);
	}

	call->ufd.fd = call->sock->s_fd;
	call->ufd.cb = nlwifi_data_receive;
	uloop_fd_add(&call->ufd, ULOOP_READ);

	return call;

error:
	nlwifi_callback_close(call);
	return NULL;
}

int nlwifi_probe_callback_set(int (*callback)(char *, u_int8_t *, void *, void *, u_int32_t))
{
	if (!probe)
	{
		probe = nlwifi_callback_init("probe");
		if (!probe)
			return -1;

		probe->attr2 = NLWIFI_ATTR_HOSTNAME;
		probe->attr3 = NLWIFI_ATTR_RSSI;
		probe->callback = callback;
	}

	return 0;
}

int nlwifi_action_callback_set(int (*callback)(char *, u_int8_t *, void *, void *, u_int32_t))
{
	if (!action)
	{
		action = nlwifi_callback_init("action");
		if (!action)
			return -1;

		action->attr1 = NLWIFI_ATTR_ACTION;
		action->callback = callback;
	}

	return 0;
}

int nlwifi_event_callback_set(int (*callback)(char *, u_int8_t *, void *, void *, u_int32_t))
{
	if (!event)
	{
		event = nlwifi_callback_init("event");
		if (!event)
			return -1;

		event->attr1 = NLWIFI_ATTR_ACTION;
		event->attr2 = NLWIFI_ATTR_DATA;
		event->callback = callback;
	}

	return 0;
}

static int nlwifi_get_int_cb(struct nl_msg *msg, void *arg)
{
	struct nlwifi_buf *nlbuf = arg;
	struct nlattr **tb = nlwifi_parse(msg);

	if (tb[nlbuf->attr])
	{
		*(int *)(nlbuf->buf) = (int)nla_get_u32(tb[nlbuf->attr]);
	}

	return NL_SKIP;
}

static int nlwifi_get_str_cb(struct nl_msg *msg, void *argv)
{
	struct nlwifi_buf *nlbuf = argv;
	struct nlattr **tb = nlwifi_parse(msg);

	if (tb[nlbuf->attr])
	{
		memcpy(nlbuf->buf, nla_data(tb[nlbuf->attr]), nla_len(tb[nlbuf->attr]));
		nlbuf->len = nla_len(tb[nlbuf->attr]);
	}

	return NL_SKIP;
}

static int nlwifi_get_macstr_cb(struct nl_msg *msg, void *argv)
{
	struct nlwifi_buf *nlbuf = argv;
	struct nlattr **tb = nlwifi_parse(msg);
	u_int8_t *macaddr;

	if (tb[nlbuf->attr])
	{
		macaddr = nla_data(tb[nlbuf->attr]);
		sprintf(nlbuf->buf, "%02X:%02X:%02X:%02X:%02X:%02X",
			macaddr[0],macaddr[1],macaddr[2], macaddr[3],macaddr[4],macaddr[5]);
	}

	return NL_SKIP;
}

static int nlwifi_get_mac_cb(struct nl_msg *msg, void *argv)
{
	struct nlwifi_buf *nlbuf = argv;
	struct nlattr **tb = nlwifi_parse(msg);
	u_int8_t *macaddr;

	if (tb[nlbuf->attr])
	{
		macaddr = nla_data(tb[nlbuf->attr]);
		sprintf(nlbuf->buf, "%02X:%02X:%02X:%02X:%02X:%02X",
			macaddr[0],macaddr[1],macaddr[2], macaddr[3],macaddr[4],macaddr[5]);
	}

	return NL_SKIP;
}

static int nlwifi_get_list_cb(struct nl_msg *msg, void *argu)
{
	struct nlwifi_buf *nlarr = argu;
	struct nlattr **attr = nlwifi_parse(msg);

	if (attr[nlarr->attr])
	{
		memcpy(nlarr->buf + (nlarr->count * nla_len(attr[nlarr->attr])), nla_data(attr[nlarr->attr]), nla_len(attr[nlarr->attr]));
		nlarr->count++;
	}

	return NL_SKIP;
}

static int nlwifi_send_cmd(const char *ifname, int nlcmd, int flags, int (*cb_func)(struct nl_msg *, void *), void *cb_arg)
{
	struct nlwifi_msg_conveyor *req;

	req = nlwifi_msg(ifname, nlcmd, flags);
	if (!req) {
		return -1;
	}

	nlwifi_send(req, cb_func, cb_arg);
	nlwifi_free(req);

	return 0;
}

int nlwifi_set_pmk(const char *ifname, u_int8_t *macaddr, int len, void *data)
{
	struct nlwifi_msg_conveyor *req;

	req = nlwifi_msg(ifname, NLWIFI_CMD_SET_PMK, 0);
	if (!req)
		return -1;

	NLA_PUT(req->msg, NLWIFI_ATTR_MAC, MAC_ADDR_LEN, macaddr);
	if (data && len)
		NLA_PUT(req->msg, NLWIFI_ATTR_KEY, len, data);

	nlwifi_send(req, NULL, NULL);
	nlwifi_free(req);

	return 0;
nla_put_failure:
	nlwifi_free(req);
	return -1;
}

int nlwifi_set_auth(const char *ifname, u_int8_t *macaddr, int sucess)
{
	struct nlwifi_msg_conveyor *req;

	req = nlwifi_msg(ifname, NLWIFI_CMD_SET_AUTH, 0);
	if (!req)
		return -1;

	NLA_PUT(req->msg, NLWIFI_ATTR_MAC, MAC_ADDR_LEN, macaddr);
	if (sucess)
		NLA_PUT_FLAG(req->msg, NLWIFI_ATTR_TRUE);

	nlwifi_send(req, NULL, NULL);
	nlwifi_free(req);

	return 0;
nla_put_failure:
	nlwifi_free(req);
	return -1;
}

static int nlwifi_freq2channel(int channel)
{
	if (channel == 14)
		return 2484;
	else if (channel < 14)
		return (channel * 5) + 2407;
	else if ((channel >= 182) && (channel <= 196))
		return (channel * 5) + 4000;
	else
		return (channel * 5) + 5000;
}

static int nlwifi_probe(const char *ifname)
{
	const char *phyname;

	phyname = nlwifi_ifname2phyname(ifname);
	if (phyname == NULL)
		return 0;

	return 1;
}

static int nlwifi_get_channel(const char *ifname, int *buf)
{
	struct nlwifi_buf nlbuf = { .buf = buf, .attr = NLWIFI_ATTR_CHANNEL };

	return nlwifi_send_cmd(ifname, NLWIFI_CMD_GET_DEVICE, 0, nlwifi_get_int_cb, &nlbuf);
}

static int nlwifi_get_frequency(const char *ifname, int *buf)
{
	if (!nlwifi_get_channel(ifname, buf))
	{
		*buf = nlwifi_freq2channel(*buf);

		return 0;
	}

	return -1;
}

static int nlwifi_get_frequency_offset(const char *ifname, int *buf)
{
	*buf = 0;
	return 0;
}

static int nlwifi_get_txpower(const char *ifname, int *buf)
{
	*buf = 20;
	return 0;
}

static int nlwifi_get_txpower_offset(const char *ifname, int *buf)
{
	*buf = 0;
	return 0;
}

static int nlwifi_get_bitrate(const char *ifname, int *buf)
{
	return wext_ops.bitrate(ifname, buf);
}

static int nlwifi_get_signal(const char *ifname, int *buf)
{
	struct nlwifi_buf nlbuf = { .buf = buf, .attr = NLWIFI_ATTR_RSSI };

	return nlwifi_send_cmd(ifname, NLWIFI_CMD_GET_DEVICE, 0, nlwifi_get_int_cb, &nlbuf);
}

static int nlwifi_get_noise(const char *ifname, int *buf)
{
	struct nlwifi_buf nlbuf = { .buf = buf, .attr = NLWIFI_ATTR_NOISE };

	return nlwifi_send_cmd(ifname, NLWIFI_CMD_GET_DEVICE, 0, nlwifi_get_int_cb, &nlbuf);
}

static int nlwifi_get_quality(const char *ifname, int *buf)
{
	struct nlwifi_buf nlbuf = { .buf = buf, .attr = NLWIFI_ATTR_QUALITY };

	return nlwifi_send_cmd(ifname, NLWIFI_CMD_GET_DEVICE, 0, nlwifi_get_int_cb, &nlbuf);
}

static int nlwifi_get_quality_max(const char *ifname, int *buf)
{
	*buf = 100;
	return 0;
}

static int nlwifi_get_mbssid_support(const char *ifname, int *buf)
{
	return -1;
}

static int nlwifi_get_hwmodelist(const char *ifname, int *buf)
{
	enum nlwifi_hwmode hwmodes = NLWIFI_HWMODE_11BGN;
	struct nlwifi_buf nlbuf = { .buf = &hwmodes, .attr = NLWIFI_ATTR_HWMODES };

	nlwifi_send_cmd(ifname, NLWIFI_CMD_GET_DEVICE, 0, nlwifi_get_int_cb, &nlbuf);

	*buf = 0;
	if ((NLWIFI_HWMODE_11B == hwmodes) || (NLWIFI_HWMODE_11BG == hwmodes) || (NLWIFI_HWMODE_11BGN == hwmodes))
		*buf |= IWINFO_80211_B;
	if ((NLWIFI_HWMODE_11G == hwmodes) || (NLWIFI_HWMODE_11BG == hwmodes) ||
		(NLWIFI_HWMODE_11BGN == hwmodes) || (NLWIFI_HWMODE_11GN == hwmodes))
		*buf |= IWINFO_80211_G;
	if ((NLWIFI_HWMODE_11N == hwmodes) || (NLWIFI_HWMODE_11GN == hwmodes) ||
		(NLWIFI_HWMODE_11BGN == hwmodes) || (NLWIFI_HWMODE_11AN == hwmodes) ||
		(NLWIFI_HWMODE_11ANAC == hwmodes) || (NLWIFI_HWMODE_11NAC == hwmodes))
		*buf |= IWINFO_80211_N;
	if ((NLWIFI_HWMODE_11ANAC == hwmodes) || (NLWIFI_HWMODE_11AN == hwmodes))
		*buf |= IWINFO_80211_A;
	if ((NLWIFI_HWMODE_11ANAC == hwmodes) || (NLWIFI_HWMODE_11NAC == hwmodes) || (NLWIFI_HWMODE_11AC == hwmodes))
		*buf |= IWINFO_80211_AC;

	return 0;
}

static int nlwifi_get_mode(const char *ifname, int *buf)
{
	struct nlwifi_buf nlbuf = { .buf = buf, .attr = NLWIFI_ATTR_IFTYPE };

	*buf = IWINFO_OPMODE_UNKNOWN;

	nlwifi_send_cmd(ifname, NLWIFI_CMD_GET_IFACE, 0, nlwifi_get_int_cb, &nlbuf);

	return (*buf == IWINFO_OPMODE_UNKNOWN) ? -1 : 0;
}

static int nlwifi_get_ssid(const char *ifname, char *buf)
{
	struct nlwifi_buf nlbuf = { .buf = buf, .attr = NLWIFI_ATTR_SSID };

	nlwifi_send_cmd(ifname, NLWIFI_CMD_GET_IFACE, 0, nlwifi_get_str_cb, &nlbuf);

	return (*buf == 0) ? -1 : 0;
}

static int nlwifi_get_bssid(const char *ifname, char *buf)
{
	struct nlwifi_buf nlbuf = { .buf = buf, .attr = NLWIFI_ATTR_MAC };

	nlwifi_send_cmd(ifname, NLWIFI_CMD_GET_IFACE, 0, nlwifi_get_mac_cb, &nlbuf);

	return 0;
}

int nlwifi_get_Essid(const char * ifname, void *buf, uint32_t *len)
{
	struct nlwifi_buf nlbuf = { .buf = buf, .attr = NLWIFI_ATTR_SSID };
	if (!nlwifi_send_cmd(ifname, NLWIFI_CMD_GET_IFACE, 0, nlwifi_get_str_cb, &nlbuf))
	{
		*len = nlbuf.len;
		return 0;
	}

	return -1;
}

static int nlwifi_get_country(const char *ifname, char *buf)
{
	struct nlwifi_buf nlbuf = { .buf = buf, .attr = NLWIFI_ATTR_COUNTRY };

	nlwifi_send_cmd(ifname, NLWIFI_CMD_GET_REGION, 0, nlwifi_get_str_cb, &nlbuf);

	return 0;
}

static int nlwifi_get_hardware_id_cb(struct nl_msg *msg, void *arg)
{
	struct iwinfo_hardware_id *id = arg;
	struct nlattr **tb = nlwifi_parse(msg);

	if (tb[NLWIFI_ATTR_VENDOR_ID])
		id->vendor_id = nla_get_u16(tb[NLWIFI_ATTR_VENDOR_ID]);

	if (tb[NLWIFI_ATTR_DEVICE_ID])
		id->device_id = nla_get_u16(tb[NLWIFI_ATTR_DEVICE_ID]);

	if (tb[NLWIFI_ATTR_DEVICE_VER])
		id->subsystem_vendor_id = nla_get_u16(tb[NLWIFI_ATTR_DEVICE_VER]);

	if (tb[NLWIFI_ATTR_DEVICE_REV])
		id->subsystem_device_id = nla_get_u16(tb[NLWIFI_ATTR_DEVICE_REV]);

	return NL_SKIP;
}

static int nlwifi_get_hardware_id(const char *ifname, char *buf)
{
	return nlwifi_send_cmd(ifname, NLWIFI_CMD_GET_DEVICE, 0, nlwifi_get_hardware_id_cb, buf);
}

static int nlwifi_get_hardware_name(const char *ifname, char *buf)
{
	sprintf(buf, "%s", nlwifi_ifname2phyname(ifname));

	return 0;
}

static int nlwifi_get_encryption(const char *ifname, char *buf)
{
	struct iwinfo_crypto_entry *c = (struct iwinfo_crypto_entry *)buf;
	enum nlwifi_auth auth = NLWIFI_AUTH_OPEN;
	struct nlwifi_buf nlbuf = { .buf = &auth, .attr = NLWIFI_ATTR_AUTH_TYPE };

	nlwifi_send_cmd(ifname, NLWIFI_CMD_GET_IFACE, 0, nlwifi_get_int_cb, &nlbuf);

	if (NLWIFI_AUTH_OPEN == auth)
	{
		c->enabled = 0;
		c->wpa_version = 0;
		c->group_ciphers = IWINFO_CIPHER_NONE;
		c->pair_ciphers = IWINFO_CIPHER_NONE;
		c->auth_suites = IWINFO_KMGMT_NONE;
		c->auth_algs = IWINFO_AUTH_OPEN;
	}
	else
	{
		c->enabled = 1;
		c->group_ciphers = IWINFO_CIPHER_CCMP;
		c->pair_ciphers = IWINFO_CIPHER_CCMP;
		c->auth_algs = IWINFO_AUTH_OPEN;

		if (NLWIFI_AUTH_WPA == auth)
		{
			c->wpa_version = 1;
			c->auth_suites = IWINFO_KMGMT_8021x;
		}
		else if (NLWIFI_AUTH_WPA2 == auth)
		{
			c->wpa_version = 2;
			c->auth_suites = IWINFO_KMGMT_8021x;
		}
		else if (NLWIFI_AUTH_WPAWPA2 == auth)
		{
			c->wpa_version = 3;
			c->auth_suites = IWINFO_KMGMT_8021x;
		}
		else if (NLWIFI_AUTH_PSK == auth)
		{
			c->wpa_version = 1;
			c->auth_suites = IWINFO_KMGMT_PSK;
		}
		else if (NLWIFI_AUTH_PSK2 == auth)
		{
			c->wpa_version = 2;
			c->auth_suites = IWINFO_KMGMT_PSK;
		}
		else if (NLWIFI_AUTH_PSKPSK2 == auth)
		{
			c->wpa_version = 3;
			c->auth_suites = IWINFO_KMGMT_PSK;
		}
		else if (NLWIFI_AUTH_WPA3PSK == auth)
		{
			c->wpa_version = 3;
			c->auth_suites = IWINFO_KMGMT_PSK;
		}
		else if (NLWIFI_AUTH_WPA2PSKWPA3PSK == auth)
		{
			c->wpa_version = 3;
			c->auth_suites = IWINFO_KMGMT_PSK;
		}
	}

	return 0;
}

static int nlwifi_get_phyname(const char *ifname, char *buf)
{
	const char *phyname;

	phyname = nlwifi_ifname2phyname(ifname);
	strcpy(buf, phyname);

	return 0;
}

#if 0
static int nlwifi_get_stalist(const char *ifname, char *buf, int *len)
{
	struct nlwifi_buf nlarr = { .buf = buf, .attr = NLWIFI_ATTR_STA_INFO, .count = 0 };

	nlwifi_send_cmd(ifname, NLWIFI_CMD_GET_STALIST, NLM_F_DUMP, nlwifi_get_list_cb, &nlarr);

	*len = (nlarr.count * sizeof(sta_info_t));
	return 0;
}
#endif

static int nlwifi_get_assoclist(const char *ifname, char *buf, int *len)
{
	char data[IWINFO_BUFSIZE];
	struct nlwifi_buf nlarr = { .buf = data, .attr = NLWIFI_ATTR_STA_INFO, .count = 0 };
	sta_info_t *stainfo = (sta_info_t *)data;
	struct iwinfo_assoclist_entry *e = (struct iwinfo_assoclist_entry *)buf;
	int i;

	nlwifi_send_cmd(ifname, NLWIFI_CMD_GET_STALIST, NLM_F_DUMP, nlwifi_get_list_cb, &nlarr);

	for (i = 0; i < nlarr.count; i++)
	{
		memset(e, 0, sizeof(struct iwinfo_assoclist_entry));
		memcpy(e->mac, stainfo->mac, MAC_ADDR_LEN);
		e->signal = stainfo->signal;
		e->noise = stainfo->noise;
		e->inactive = stainfo->inact;
#if 0
		e->assoctime = stainfo->assoctime;
#endif
		e->rx_bytes = stainfo->rxbytes;
		e->tx_bytes = stainfo->txbytes;
		e->rx_rate.rate = stainfo->rxrate;
		e->rx_rate.mcs = stainfo->rxmcs;
		e->rx_rate.is_40mhz = stainfo->rxbw ? 1 : 0;
		e->tx_rate.rate = stainfo->txrate;
		e->tx_rate.mcs = stainfo->txmcs;
		e->tx_rate.is_40mhz = stainfo->txbw ? 1 : 0;
		stainfo++;
		e++;
	}

	*len = (nlarr.count * sizeof(struct iwinfo_assoclist_entry));
	return 0;
}

static int nlwifi_get_txpwrlist(const char *ifname, char *buf, int *len)
{
	return wext_ops.txpwrlist(ifname, buf, len);
}

static int nlwifi_get_scanlist(const char *ifname, char *buf, int *len)
{
	char data[IWINFO_BUFSIZE];
	struct nlwifi_buf nlarr = { .buf = data, .attr = NLWIFI_ATTR_AP_INFO, .count = 0 };
	ap_info_t *apinfo = (ap_info_t *)data;
	struct iwinfo_scanlist_entry *e = (struct iwinfo_scanlist_entry *)buf;
	int i;

	nlwifi_send_cmd(ifname, NLWIFI_CMD_TRIGGER_SCAN, 0, NULL, NULL);
	nlwifi_wait(ifname, "scan", NLWIFI_CMD_SEND_EVENT);
	nlwifi_send_cmd(ifname, NLWIFI_CMD_GET_APLIST, NLM_F_DUMP, nlwifi_get_list_cb, &nlarr);

	for (i = 0; i < nlarr.count; i++)
	{
		memset(e, 0, sizeof(struct iwinfo_scanlist_entry));
		memcpy(e->mac, apinfo->bssid, MAC_ADDR_LEN);
		memcpy(e->ssid, apinfo->ssid, IWINFO_ESSID_MAX_SIZE);
		e->mode = apinfo->bsstype;
		e->channel = apinfo->channel;
		e->signal = apinfo->rssi + 0x100;
		e->quality_max = 100;
		if (apinfo->rssi < -90)
			e->quality = 0;
		else if (apinfo->rssi < -70)
			e->quality = (apinfo->rssi + 90) * 5;
		else
			e->quality = 100;

		if (NLWIFI_AUTH_OPEN == apinfo->auth)
		{
			if (NLWIFI_CIPHER_WEP == apinfo->cipher)
			{
				e->crypto.auth_algs    = IWINFO_AUTH_OPEN | IWINFO_AUTH_SHARED;
				e->crypto.pair_ciphers = IWINFO_CIPHER_WEP40 | IWINFO_CIPHER_WEP104;
			}
		}
		else
		{
			e->crypto.enabled = 1;
			switch (apinfo->auth)
			{
			case NLWIFI_AUTH_WPA:
				e->crypto.wpa_version = 1;
				e->crypto.auth_suites = IWINFO_KMGMT_8021x;
				break;
			case NLWIFI_AUTH_WPA2:
				e->crypto.wpa_version = 2;
				e->crypto.auth_suites = IWINFO_KMGMT_8021x;
				break;
			case NLWIFI_AUTH_WPAWPA2:
				e->crypto.wpa_version = 3;
				e->crypto.auth_suites = IWINFO_KMGMT_8021x;
				break;
			case NLWIFI_AUTH_PSK:
				e->crypto.wpa_version = 1;
				e->crypto.auth_suites = IWINFO_KMGMT_PSK;
				break;
			case NLWIFI_AUTH_PSK2:
				e->crypto.wpa_version = 2;
				e->crypto.auth_suites = IWINFO_KMGMT_PSK;
				break;
			case NLWIFI_AUTH_PSKPSK2:
				e->crypto.wpa_version = 3;
				e->crypto.auth_suites = IWINFO_KMGMT_PSK;
				break;
			case NLWIFI_AUTH_WPA3PSK:
				e->crypto.wpa_version = 3;
				e->crypto.auth_suites = IWINFO_KMGMT_PSK;
				break;
			case NLWIFI_AUTH_WPA2PSKWPA3PSK:
				e->crypto.wpa_version = 3;
				e->crypto.auth_suites = IWINFO_KMGMT_PSK;
				break;
			case NLWIFI_AUTH_WEPSHARED:
			case NLWIFI_AUTH_WEPMIX:
				e->crypto.auth_algs = IWINFO_AUTH_SHARED;
				break;
			default:
				break;
			}

			switch (apinfo->cipher)
			{
			case NLWIFI_CIPHER_WEP:
				e->crypto.pair_ciphers = IWINFO_CIPHER_WEP40 | IWINFO_CIPHER_WEP104;
				break;
			case NLWIFI_CIPHER_TKIP:
				e->crypto.group_ciphers = IWINFO_CIPHER_TKIP;
				e->crypto.pair_ciphers = IWINFO_CIPHER_TKIP;
				break;
			case NLWIFI_CIPHER_AES:
				e->crypto.group_ciphers = IWINFO_CIPHER_CCMP;
				e->crypto.pair_ciphers = IWINFO_CIPHER_CCMP;
				break;
			case NLWIFI_CIPHER_TKIPAES:
				e->crypto.group_ciphers = IWINFO_CIPHER_TKIP;
				e->crypto.pair_ciphers = IWINFO_CIPHER_TKIP | IWINFO_CIPHER_CCMP;
				break;
			default:
				break;
			}
		}

		apinfo++;
		e++;
	}

	*len = nlarr.count * sizeof(struct iwinfo_scanlist_entry);
	return 0;
}

static int nlwifi_get_freqlist_cb(struct nl_msg *msg, void *argu)
{
	struct nlwifi_buf *nlarr = argu;
	struct iwinfo_freqlist_entry *e = nlarr->buf;
	struct nlattr **attr = nlwifi_parse(msg);
	struct nlattr *chan;
	int tmp;

	if (attr[nlarr->attr])
	{
		nla_for_each_nested(chan, attr[nlarr->attr], tmp)
		{
			e->channel = nla_get_u8(chan);
			e->mhz = nlwifi_freq2channel(e->channel);
			e->restricted = 0;

			e++;
			nlarr->count++;
		}
	}

	return NL_SKIP;
}

static int nlwifi_get_freqlist(const char *ifname, char *buf, int *len)
{
	struct nlwifi_buf nlarr = { .buf = buf, .attr = NLWIFI_ATTR_CHANNEL_LIST, .count = 0 };

	nlwifi_send_cmd(ifname, NLWIFI_CMD_GET_DEVICE, 0, nlwifi_get_freqlist_cb, &nlarr);

	*len = (nlarr.count * sizeof(struct iwinfo_freqlist_entry));
	return 0;
}

static int nlwifi_get_countrylist(const char *ifname, char *buf, int *len)
{
	int count;
	struct iwinfo_country_entry *e = (struct iwinfo_country_entry *)buf;
	const struct iwinfo_iso3166_label *l;

	for (l = IWINFO_ISO3166_NAMES, count = 0; l->iso3166; l++, e++, count++)
	{
		e->iso3166 = l->iso3166;
		e->ccode[0] = (l->iso3166 / 256);
		e->ccode[1] = (l->iso3166 % 256);
	}

	*len = (count * sizeof(struct iwinfo_country_entry));
	return 0;
}

/* no need to restart iface */
static int nlwifi_update_iface(const char *ifname)
{
	struct nlwifi_msg_conveyor *req;
	struct uci_section *s;
	struct uci_option *o;
	struct uci_element *e;
	u_int8_t macaddr[MAC_ADDR_LEN];
	const char *val;
	void *attr;
	int i = 0;

	s = iwinfo_uci_get_section(ifname);
	if (!s)
		return -1;

	req = nlwifi_msg(ifname, NLWIFI_CMD_UPDATE_IFACE, 0);
	if (!req)
		return -1;

	val = uci_lookup_option_string(uci_ctx, s, "hidden");
	if (val && !strcmp(val, "1"))
		NLA_PUT_U32(req->msg, NLWIFI_ATTR_HIDDEN_SSID, 1);
	else
		NLA_PUT_U32(req->msg, NLWIFI_ATTR_HIDDEN_SSID, 0);

	val = uci_lookup_option_string(uci_ctx, s, "isolate");
	if (val && !strcmp(val, "1"))
		NLA_PUT_U32(req->msg, NLWIFI_ATTR_AP_ISOLATE, 1);
	else
		NLA_PUT_U32(req->msg, NLWIFI_ATTR_AP_ISOLATE, 0);

	val = uci_lookup_option_string(uci_ctx, s, "maxstanum");
	if (val)
		NLA_PUT_U32(req->msg, NLWIFI_ATTR_STANUM, atoi(val));
	else
		NLA_PUT_U32(req->msg, NLWIFI_ATTR_STANUM, 0);

	val = uci_lookup_option_string(uci_ctx, s, "lowrssi");
	if (val)
		NLA_PUT_U32(req->msg, NLWIFI_ATTR_RSSI, atoi(val));
	else
		NLA_PUT_U32(req->msg, NLWIFI_ATTR_RSSI, 0);

	val = uci_lookup_option_string(uci_ctx, s, "macfilter");
	if (val)
	{
		if (!strcmp(val, "allow"))
			NLA_PUT_U32(req->msg, NLWIFI_ATTR_ACL_POLICY, NLWIFI_ACL_POLICY_ALLOW);
		else if (!strcmp(val, "deny"))
			NLA_PUT_U32(req->msg, NLWIFI_ATTR_ACL_POLICY, NLWIFI_ACL_POLICY_DENY);
		else
			NLA_PUT_U32(req->msg, NLWIFI_ATTR_ACL_POLICY, NLWIFI_ACL_POLICY_OPEN);
	}
	else
		NLA_PUT_U32(req->msg, NLWIFI_ATTR_ACL_POLICY, NLWIFI_ACL_POLICY_OPEN);

	attr = nla_nest_start(req->msg, NLWIFI_ATTR_ACL_MACLIST);
	if (!attr)
		goto nla_put_failure;

	o = uci_lookup_option(uci_ctx, s, "maclist");
	if (o && (o->type == UCI_TYPE_LIST))
	{
		uci_foreach_element(&o->v.list, e)
		{
			iwinfo_mac_str2eth(e->name, macaddr);
			NLA_PUT(req->msg, i++, MAC_ADDR_LEN, macaddr);
		}
	}

	nla_nest_end(req->msg, attr);

	nlwifi_send(req, NULL, NULL);
	nlwifi_free(req);

	return 0;
nla_put_failure:
	nlwifi_free(req);
	return -1;
}

/* need to restart iface */
static int nlwifi_setup_iface(const char *ifname)
{
	struct nlwifi_msg_conveyor *req;
	struct uci_section *s;
	const char *val;
	char ssid[MAX_SSID_LEN + 1];
	u_int8_t macaddr[MAC_ADDR_LEN];

	s = iwinfo_uci_get_section(ifname);
	if (!s)
		return -1;

	req = nlwifi_msg(ifname, NLWIFI_CMD_SET_IFACE, 0);
	if (!req)
		return -1;

	memset(ssid, 0, sizeof(ssid));
	val = uci_lookup_option_string(uci_ctx, s, "ssidprefix");
	if (val)
		snprintf(ssid, sizeof(ssid), "%s", val);

	val = uci_lookup_option_string(uci_ctx, s, "ssid");
	if (val)
	{
		if (!strlen(ssid) || !strncmp(val, ssid, strlen(ssid)))
			snprintf(ssid, sizeof(ssid), "%s", val);
		else
			snprintf(ssid, sizeof(ssid), "%s%s", ssid, val);
		NLA_PUT_STRING(req->msg, NLWIFI_ATTR_SSID, ssid);
	}

	val = uci_lookup_option_string(uci_ctx, s, "bssid");
	if (val)
	{
		iwinfo_mac_str2eth(val, macaddr);
		NLA_PUT(req->msg, NLWIFI_ATTR_MAC, MAC_ADDR_LEN, macaddr);
	}

	val = uci_lookup_option_string(uci_ctx, s, "key");
	if (val)
	{
		NLA_PUT_STRING(req->msg, NLWIFI_ATTR_KEY, val);

		val = uci_lookup_option_string(uci_ctx, s, "encryption");
		if (val)
		{
			if (strstr(val, "wpa2pskwpa3psk"))
				NLA_PUT_U32(req->msg, NLWIFI_ATTR_AUTH_TYPE, NLWIFI_AUTH_WPA2PSKWPA3PSK);
			else if (strstr(val, "wpa3psk"))
				NLA_PUT_U32(req->msg, NLWIFI_ATTR_AUTH_TYPE, NLWIFI_AUTH_WPA3PSK);
			else if (strstr(val, "mixed-psk"))
				NLA_PUT_U32(req->msg, NLWIFI_ATTR_AUTH_TYPE, NLWIFI_AUTH_PSKPSK2);
			else if (strstr(val, "psk2"))
				NLA_PUT_U32(req->msg, NLWIFI_ATTR_AUTH_TYPE, NLWIFI_AUTH_PSK2);
			else if (strstr(val, "psk"))
				NLA_PUT_U32(req->msg, NLWIFI_ATTR_AUTH_TYPE, NLWIFI_AUTH_PSK);
			else if (strstr(val, "mixed-wpa"))
				NLA_PUT_U32(req->msg, NLWIFI_ATTR_AUTH_TYPE, NLWIFI_AUTH_WPAWPA2);
			else if (!strcmp(val, "wpa2"))
				NLA_PUT_U32(req->msg, NLWIFI_ATTR_AUTH_TYPE, NLWIFI_AUTH_WPA2);
			else if (!strcmp(val, "wpa"))
				NLA_PUT_U32(req->msg, NLWIFI_ATTR_AUTH_TYPE, NLWIFI_AUTH_WPA);
			else if (!strcmp(val, "wep-shared"))
				NLA_PUT_U32(req->msg, NLWIFI_ATTR_AUTH_TYPE, NLWIFI_AUTH_WEPSHARED);
			else if (!strcmp(val, "wep-mixed"))
				NLA_PUT_U32(req->msg, NLWIFI_ATTR_AUTH_TYPE, NLWIFI_AUTH_WEPMIX);
			else
				NLA_PUT_U32(req->msg, NLWIFI_ATTR_AUTH_TYPE, NLWIFI_AUTH_OPEN);

			if (strstr(val, "auto"))
				NLA_PUT_U32(req->msg, NLWIFI_ATTR_CIPHER_TYPE, NLWIFI_CIPHER_TKIPAES);
			else if (strstr(val, "tkip"))
				NLA_PUT_U32(req->msg, NLWIFI_ATTR_CIPHER_TYPE, NLWIFI_CIPHER_TKIP);
			else if (strstr(val, "wep"))
				NLA_PUT_U32(req->msg, NLWIFI_ATTR_CIPHER_TYPE, NLWIFI_CIPHER_WEP);
			else
				NLA_PUT_U32(req->msg, NLWIFI_ATTR_CIPHER_TYPE, NLWIFI_CIPHER_AES);
		}
		else
		{
			NLA_PUT_U32(req->msg, NLWIFI_ATTR_AUTH_TYPE, NLWIFI_AUTH_PSKPSK2);
			NLA_PUT_U32(req->msg, NLWIFI_ATTR_CIPHER_TYPE, NLWIFI_CIPHER_AES);
		}

		val = uci_lookup_option_string(uci_ctx, s, "ppk");
		if (val)
			NLA_PUT_STRING(req->msg, NLWIFI_ATTR_PPK, val);
	}

	val = uci_lookup_option_string(uci_ctx, s, "hostname");
	if (val)
		NLA_PUT_STRING(req->msg, NLWIFI_ATTR_HOSTNAME, val);

	nlwifi_send(req, NULL, NULL);
	nlwifi_free(req);

	return 0;
nla_put_failure:
	nlwifi_free(req);
	return -1;
}

static int nlwifi_update_device(const char *device)
{
	struct nlwifi_msg_conveyor *req;
	struct uci_section *s;
	const char *val;

	s = iwinfo_uci_get_section(device);
	if (!s)
		return -1;

	req = nlwifi_msg(device, NLWIFI_CMD_UPDATE_DEVICE, 0);
	if (!req)
		return -1;

	val = uci_lookup_option_string(uci_ctx, s, "channel");
	if (val)
		NLA_PUT_U32(req->msg, NLWIFI_ATTR_CHANNEL, atoi(val));

	val = uci_lookup_option_string(uci_ctx, s, "country");
	if (val)
		NLA_PUT_STRING(req->msg, NLWIFI_ATTR_COUNTRY, val);

	val = uci_lookup_option_string(uci_ctx, s, "htmode");
	if (val)
	{
		if (!strcmp(val, "auto"))
			NLA_PUT_U32(req->msg, NLWIFI_ATTR_CHANNEL_TYPE, NLWIFI_CHAN_AUTO);
		else if (!strcmp(val, "HT20") || !strcmp(val, "VHT20"))
			NLA_PUT_U32(req->msg, NLWIFI_ATTR_CHANNEL_TYPE, NLWIFI_CHAN_HT20);
		else if (!strcmp(val, "VHT80") || !strcmp(val, "HT80"))
			NLA_PUT_U32(req->msg, NLWIFI_ATTR_CHANNEL_TYPE, NLWIFI_CHAN_VHT80);
		else
			NLA_PUT_U32(req->msg, NLWIFI_ATTR_CHANNEL_TYPE, NLWIFI_CHAN_HT40MINUS);
	}
	else
	{
		NLA_PUT_U32(req->msg, NLWIFI_ATTR_CHANNEL_TYPE, NLWIFI_CHAN_AUTO);
	}

	val = uci_lookup_option_string(uci_ctx, s, "txpower");
	if (val)
		NLA_PUT_U32(req->msg, NLWIFI_ATTR_TXPWR, atoi(val));
	else
		NLA_PUT_U32(req->msg, NLWIFI_ATTR_TXPWR, 100);

	nlwifi_send(req, NULL, NULL);
	nlwifi_free(req);

	return 0;
nla_put_failure:
	nlwifi_free(req);
	return -1;
}

static int nlwifi_setup_device(const char *device)
{
	struct nlwifi_msg_conveyor *req;
	struct uci_section *s;
	const char *val;

	s = iwinfo_uci_get_section(device);
	if (!s)
		return -1;

	req = nlwifi_msg(device, NLWIFI_CMD_SET_DEVICE, 0);
	if (!req)
		return -1;

	val = uci_lookup_option_string(uci_ctx, s, "channel");
	if (val)
	{
		NLA_PUT_U32(req->msg, NLWIFI_ATTR_CHANNEL, atoi(val));
	}

	val = uci_lookup_option_string(uci_ctx, s, "country");
	if (val)
		NLA_PUT_STRING(req->msg, NLWIFI_ATTR_COUNTRY, val);

	val = uci_lookup_option_string(uci_ctx, s, "htmode");
	if (val)
	{
#if 0
		if (!strcmp(val, "HT20"))
		{
			val = uci_lookup_option_string(uci_ctx, s, "hwmode");
			if (val)
			{
				if (!strcmp(val, "11g"))
					NLA_PUT_U32(req->msg, NLWIFI_ATTR_HWMODE, NLWIFI_HWMODE_11BGN);
				else
					NLA_PUT_U32(req->msg, NLWIFI_ATTR_HWMODE, NLWIFI_HWMODE_11AN);
			}
			NLA_PUT_U32(req->msg, NLWIFI_ATTR_CHANNEL_TYPE, NLWIFI_CHAN_HT20);
		}
		else if (!strcmp(val, "HT40"))
		{
			val = uci_lookup_option_string(uci_ctx, s, "hwmode");
			if (val)
			{
				if (!strcmp(val, "11g"))
					NLA_PUT_U32(req->msg, NLWIFI_ATTR_HWMODE, NLWIFI_HWMODE_11BGN);
				else
					NLA_PUT_U32(req->msg, NLWIFI_ATTR_HWMODE, NLWIFI_HWMODE_11AN);
			}
		}
#endif
		if (!strcmp(val, "auto")) {
			NLA_PUT_U32(req->msg, NLWIFI_ATTR_HWMODE, NLWIFI_HWMODE_11ANAC);
			NLA_PUT_U32(req->msg, NLWIFI_ATTR_CHANNEL_TYPE, NLWIFI_CHAN_AUTO);			
		} else if (!strcmp(val, "VHT20") || !strcmp(val, "HT20")) {
			NLA_PUT_U32(req->msg, NLWIFI_ATTR_HWMODE, NLWIFI_HWMODE_11ANAC);
			NLA_PUT_U32(req->msg, NLWIFI_ATTR_CHANNEL_TYPE, NLWIFI_CHAN_HT20);
		} else if (!strcmp(val, "VHT40") || !strcmp(val, "HT40")) {
			NLA_PUT_U32(req->msg, NLWIFI_ATTR_HWMODE, NLWIFI_HWMODE_11ANAC);
			NLA_PUT_U32(req->msg, NLWIFI_ATTR_CHANNEL_TYPE, NLWIFI_CHAN_HT40);
		} else if (!strcmp(val, "VHT80") || !strcmp(val, "HT80")) {
			NLA_PUT_U32(req->msg, NLWIFI_ATTR_HWMODE, NLWIFI_HWMODE_11ANAC);
			NLA_PUT_U32(req->msg, NLWIFI_ATTR_CHANNEL_TYPE, NLWIFI_CHAN_VHT80);
		}
	}
	else
	{
		val = uci_lookup_option_string(uci_ctx, s, "hwmode");
		if (val)
		{
			if (!strcmp(val, "11g"))
				NLA_PUT_U32(req->msg, NLWIFI_ATTR_HWMODE, NLWIFI_HWMODE_11BG);
			else
				NLA_PUT_U32(req->msg, NLWIFI_ATTR_HWMODE, NLWIFI_HWMODE_11A);
		}
		NLA_PUT_U32(req->msg, NLWIFI_ATTR_CHANNEL_TYPE, NLWIFI_CHAN_AUTO);
	}

	val = uci_lookup_option_string(uci_ctx, s, "txpower");
	if (val)
	{
		NLA_PUT_U32(req->msg, NLWIFI_ATTR_TXPWR, atoi(val));
	}

	val = uci_lookup_option_string(uci_ctx, s, "rts");
	if (val)
	{
		NLA_PUT_U32(req->msg, NLWIFI_ATTR_RTS_THRESHOLD, atoi(val));
	}

	nlwifi_send(req, NULL, NULL);
	nlwifi_free(req);

	return 0;
nla_put_failure:
	nlwifi_free(req);
	return -1;
}

int nlwifi_set_channel(const char *device, u_int32_t channel)
{
	struct nlwifi_msg_conveyor *req;

	req = nlwifi_msg(device, NLWIFI_CMD_UPDATE_DEVICE, 0);
	if (!req)
		return -1;

	NLA_PUT_U32(req->msg, NLWIFI_ATTR_CHANNEL, channel);

	nlwifi_send(req, NULL, NULL);
	nlwifi_free(req);

	return 0;
nla_put_failure:
	nlwifi_free(req);
	return -1;
}

int nlwifi_set_txpwr(const char *device, u_int32_t txpwr)
{
	struct nlwifi_msg_conveyor *req;

	req = nlwifi_msg(device, NLWIFI_CMD_UPDATE_DEVICE, 0);
	if (!req)
		return -1;

	NLA_PUT_U32(req->msg, NLWIFI_ATTR_TXPWR, txpwr);

	nlwifi_send(req, NULL, NULL);
	nlwifi_free(req);

	return 0;
nla_put_failure:
	nlwifi_free(req);
	return -1;
}

int nlwifi_set_htbw(const char *device, const char *htbw)
{
	struct nlwifi_msg_conveyor *req;

	req = nlwifi_msg(device, NLWIFI_CMD_UPDATE_DEVICE, 0);
	if (!req)
		return -1;

	if (!strcmp(htbw, "auto"))
		NLA_PUT_U32(req->msg, NLWIFI_ATTR_CHANNEL_TYPE, NLWIFI_CHAN_AUTO);	
	else if (!strcmp(htbw, "HT20") || !strcmp(htbw, "VHT20"))
		NLA_PUT_U32(req->msg, NLWIFI_ATTR_CHANNEL_TYPE, NLWIFI_CHAN_HT20);
	else if (!strcmp(htbw, "VHT80") || !strcmp(htbw, "HT80"))
		NLA_PUT_U32(req->msg, NLWIFI_ATTR_CHANNEL_TYPE, NLWIFI_CHAN_VHT80);
	else
		NLA_PUT_U32(req->msg, NLWIFI_ATTR_CHANNEL_TYPE, NLWIFI_CHAN_HT40MINUS);

	nlwifi_send(req, NULL, NULL);
	nlwifi_free(req);

	return 0;
nla_put_failure:
	nlwifi_free(req);
	return -1;
}

int nlwifi_set_hidden(const char *ifname, u_int32_t hidden)
{
	struct nlwifi_msg_conveyor *req;

	req = nlwifi_msg(ifname, NLWIFI_CMD_UPDATE_IFACE, 0);
	if (!req)
		return -1;

	NLA_PUT_U32(req->msg, NLWIFI_ATTR_HIDDEN_SSID, hidden);

	nlwifi_send(req, NULL, NULL);
	nlwifi_free(req);

	return 0;
nla_put_failure:
	nlwifi_free(req);
	return -1;
}

int nlwifi_set_isolate(const char *ifname, u_int32_t isolate)
{
	struct nlwifi_msg_conveyor *req;

	req = nlwifi_msg(ifname, NLWIFI_CMD_UPDATE_IFACE, 0);
	if (!req)
		return -1;

	NLA_PUT_U32(req->msg, NLWIFI_ATTR_AP_ISOLATE, isolate);

	nlwifi_send(req, NULL, NULL);
	nlwifi_free(req);

	return 0;
nla_put_failure:
	nlwifi_free(req);
	return -1;
}

int nlwifi_set_maxstanum(const char *ifname, u_int32_t maxstanum)
{
	struct nlwifi_msg_conveyor *req;

	req = nlwifi_msg(ifname, NLWIFI_CMD_UPDATE_IFACE, 0);
	if (!req)
		return -1;

	NLA_PUT_U32(req->msg, NLWIFI_ATTR_STANUM, maxstanum);

	nlwifi_send(req, NULL, NULL);
	nlwifi_free(req);

	return 0;
nla_put_failure:
	nlwifi_free(req);
	return -1;
}

int nlwifi_set_lowrssi(const char *ifname, u_int32_t lowrssi)
{
	struct nlwifi_msg_conveyor *req;

	req = nlwifi_msg(ifname, NLWIFI_CMD_UPDATE_IFACE, 0);
	if (!req)
		return -1;

	NLA_PUT_U32(req->msg, NLWIFI_ATTR_RSSI, lowrssi);

	nlwifi_send(req, NULL, NULL);
	nlwifi_free(req);

	return 0;
nla_put_failure:
	nlwifi_free(req);
	return -1;
}

int nlwifi_set_macfilter(const char *ifname, const char *macfilter)
{
	struct nlwifi_msg_conveyor *req;

	req = nlwifi_msg(ifname, NLWIFI_CMD_UPDATE_IFACE, 0);
	if (!req)
		return -1;

	if (!strcmp(macfilter, "allow"))
		NLA_PUT_U32(req->msg, NLWIFI_ATTR_ACL_POLICY, NLWIFI_ACL_POLICY_ALLOW);
	else if (!strcmp(macfilter, "deny"))
		NLA_PUT_U32(req->msg, NLWIFI_ATTR_ACL_POLICY, NLWIFI_ACL_POLICY_DENY);
	else
		NLA_PUT_U32(req->msg, NLWIFI_ATTR_ACL_POLICY, NLWIFI_ACL_POLICY_OPEN);

	nlwifi_send(req, NULL, NULL);
	nlwifi_free(req);

	return 0;
nla_put_failure:
	nlwifi_free(req);
	return -1;
}

int nlwifi_add_aclmac(const char *ifname, const char *strmac)
{
	struct nlwifi_msg_conveyor *req;
	u_int8_t macaddr[MAC_ADDR_LEN];

	req = nlwifi_msg(ifname, NLWIFI_CMD_UPDATE_IFACE, 0);
	if (!req)
		return -1;

	iwinfo_mac_str2eth(strmac, macaddr);
	NLA_PUT(req->msg, NLWIFI_ATTR_ADD_MAC, MAC_ADDR_LEN, macaddr);

	nlwifi_send(req, NULL, NULL);
	nlwifi_free(req);

	return 0;
nla_put_failure:
	nlwifi_free(req);
	return -1;
}

int nlwifi_del_aclmac(const char *ifname, const char *strmac)
{
	struct nlwifi_msg_conveyor *req;
	u_int8_t macaddr[MAC_ADDR_LEN];

	req = nlwifi_msg(ifname, NLWIFI_CMD_UPDATE_IFACE, 0);
	if (!req)
		return -1;

	iwinfo_mac_str2eth(strmac, macaddr);
	NLA_PUT(req->msg, NLWIFI_ATTR_DEL_MAC, MAC_ADDR_LEN, macaddr);

	nlwifi_send(req, NULL, NULL);
	nlwifi_free(req);

	return 0;
nla_put_failure:
	nlwifi_free(req);
	return -1;
}

int nlwifi_deauth_mac(const char *ifname, const char *strmac)
{
	struct nlwifi_msg_conveyor *req;
	u_int8_t macaddr[MAC_ADDR_LEN];

	req = nlwifi_msg(ifname, NLWIFI_CMD_UPDATE_IFACE, 0);
	if (!req)
		return -1;

	iwinfo_mac_str2eth(strmac, macaddr);
	NLA_PUT(req->msg, NLWIFI_ATTR_DEAUTH_MAC, MAC_ADDR_LEN, macaddr);

	nlwifi_send(req, NULL, NULL);
	nlwifi_free(req);

	return 0;
nla_put_failure:
	nlwifi_free(req);
	return -1;
}

int nlwifi_set_wsc(const char *ifname, const char *key)
{
	struct nlwifi_msg_conveyor *req;

	req = nlwifi_msg(ifname, NLWIFI_CMD_SET_WSC, 0);
	if (!req)
		return -1;

	if (key && (strlen(key) >= 8) && (strlen(key) <= 64))
		NLA_PUT_STRING(req->msg, NLWIFI_ATTR_KEY, key);

	nlwifi_send(req, NULL, NULL);
	nlwifi_free(req);

	return 0;
nla_put_failure:
	nlwifi_free(req);
	return -1;
}

int nlwifi_trigger_scan(const char *ifname)
{
	struct nlwifi_msg_conveyor *req;

	req = nlwifi_msg(ifname, NLWIFI_CMD_TRIGGER_SCAN, 0);
	if (!req)
		return -1;

	nlwifi_send(req, NULL, NULL);
	nlwifi_free(req);

	return 0;
nla_put_failure:
	nlwifi_free(req);
	return -1;
}

int nlwifi_set_meshEnable(const char *ifname, int enable)
{
	struct nlwifi_msg_conveyor *req;

	req = nlwifi_msg(ifname, NLWIFI_CMD_SET_MESH_ENABLE, 0);
	if (!req)
		return -1;

	NLA_PUT_U32(req->msg, NLWIFI_ATTR_VALUE, enable);

	nlwifi_send(req, NULL, NULL);
	nlwifi_free(req);

	return 0;
nla_put_failure:
	nlwifi_free(req);
	return -1;
}

int nlwifi_set_meshEvent_Enable(const char *ifname, int enable)
{
	struct nlwifi_msg_conveyor *req;

	req = nlwifi_msg(ifname, NLWIFI_CMD_SET_MESH_EVENT_ENABLE, 0);
	if (!req)
		return -1;

	NLA_PUT_U32(req->msg, NLWIFI_ATTR_VALUE, enable);

	nlwifi_send(req, NULL, NULL);
	nlwifi_free(req);

	return 0;
nla_put_failure:
	nlwifi_free(req);
	return -1;
}

int nlwifi_wifi_down(const char *device)
{
	struct uci_element *e;
	struct uci_section *s = NULL;
	const char *val;

	if (iwinfo_uci_init() < 0)
		return -1;

	uci_foreach_element(&uci_pkg->sections, e)
	{
		s = uci_to_section(e);
		if (strcmp(s->type, "wifi-iface") != 0)
			continue;

		val = uci_lookup_option_string(uci_ctx, s, "device");
		if (!val || (device && (strcmp(val, device) != 0)))
			continue;

		val = uci_lookup_option_string(uci_ctx, s, "ifname");
		if (val)
			iwinfo_ifdown(val);
	}

	return 0;
}

int nlwifi_wifi_down_iface(const char *iface)
{
	struct uci_element *e;
	struct uci_section *s = NULL;
	const char *val;
	const char *ifname;

	if (iwinfo_uci_init() < 0)
		return -1;

	uci_foreach_element(&uci_pkg->sections, e)
	{
		s = uci_to_section(e);
		if (strcmp(s->type, "wifi-iface") != 0)
			continue;

		if (!s->e.name || (iface && (strcmp(s->e.name, iface) != 0)))
			continue;

		ifname = uci_lookup_option_string(uci_ctx, s, "ifname");
		if (ifname)
			iwinfo_ifdown(ifname);
	}
}

int nlwifi_wifi_up_iface(const char *iface)
{
	struct uci_element *e;
	struct uci_section *s = NULL;
	const char *val;
	const char *ifname;

	if (iwinfo_uci_init() < 0)
		return -1;

	uci_foreach_element(&uci_pkg->sections, e)
	{
		s = uci_to_section(e);
		if (strcmp(s->type, "wifi-iface") != 0)
			continue;

		if (!s->e.name || (iface && (strcmp(s->e.name, iface) != 0)))
			continue;

		ifname = uci_lookup_option_string(uci_ctx, s, "ifname");
		if (ifname)
			iwinfo_ifdown(ifname);

		val = uci_lookup_option_string(uci_ctx, s, "disabled");
		if (val && (strcmp(val, "1") == 0))
			continue;

		if (ifname) {
			nlwifi_setup_iface(ifname);
			nlwifi_update_iface(ifname);
			iwinfo_ifup(ifname);
		}
	}
}

int nlwifi_wifi_up(const char *device)
{
	struct uci_element *e;
	struct uci_section *s = NULL;
	const char *val;

	if (iwinfo_uci_init() < 0)
		return -1;

	nlwifi_wifi_down(device);

	uci_foreach_element(&uci_pkg->sections, e)
	{
		s = uci_to_section(e);
		if (strcmp(s->type, "wifi-iface") != 0)
			continue;

		val = uci_lookup_option_string(uci_ctx, s, "device");
		if (!val || (device && (strcmp(val, device) != 0)))
			continue;

		val = uci_lookup_option_string(uci_ctx, s, "disabled");
		if (val && (strcmp(val, "1") == 0))
			continue;

		val = uci_lookup_option_string(uci_ctx, s, "ifname");
		if (val)
		{
			nlwifi_setup_iface(val);
			nlwifi_update_iface(val);
		}
	}

	uci_foreach_element(&uci_pkg->sections, e)
	{
		s = uci_to_section(e);
		if (strcmp(s->type, "wifi-iface") != 0)
			continue;

		val = uci_lookup_option_string(uci_ctx, s, "device");
		if (!val || (device && (strcmp(val, device) != 0)))
			continue;

		val = uci_lookup_option_string(uci_ctx, s, "disabled");
		if (val && (strcmp(val, "1") == 0))
			continue;

		val = uci_lookup_option_string(uci_ctx, s, "ifname");
		if (val)
			iwinfo_ifup(val);
	}

	uci_foreach_element(&uci_pkg->sections, e)
	{
		s = uci_to_section(e);
		if (strcmp(s->type, "wifi-device") != 0)
			continue;

		if (!s->e.name || (device && (strcmp(s->e.name, device) != 0)))
			continue;

		nlwifi_setup_device(s->e.name);
	}

	return 0;
}

int nlwifi_wifi_sync(const char *device)
{
	struct uci_element *e;
	struct uci_section *s = NULL;
	const char *val;

	if (iwinfo_uci_init() < 0)
		return -1;

	uci_foreach_element(&uci_pkg->sections, e)
	{
		s = uci_to_section(e);
		if (strcmp(s->type, "wifi-iface") != 0)
			continue;

		val = uci_lookup_option_string(uci_ctx, s, "disabled");
		if (val && (strcmp(val, "1") == 0))
			continue;

		val = uci_lookup_option_string(uci_ctx, s, "ifname");
		if (!val ||
			(device && (strcmp(val, device) != 0) &&
			s->e.name && (strcmp(s->e.name, device) != 0)))
			continue;

		nlwifi_update_iface(val);
	}

	uci_foreach_element(&uci_pkg->sections, e)
	{
		s = uci_to_section(e);
		if (strcmp(s->type, "wifi-device") != 0)
			continue;

		if (!s->e.name || (device && (strcmp(s->e.name, device) != 0)))
			continue;

		nlwifi_update_device(s->e.name);
	}

	return 0;
}

const struct iwinfo_ops nlwifi_ops = {
	.name             = "nlwifi",
	.probe            = nlwifi_probe,
	.channel          = nlwifi_get_channel,
	.frequency        = nlwifi_get_frequency,
	.frequency_offset = nlwifi_get_frequency_offset,
	.txpower          = nlwifi_get_txpower,
	.txpower_offset   = nlwifi_get_txpower_offset,
	.bitrate          = nlwifi_get_bitrate,
	.signal           = nlwifi_get_signal,
	.noise            = nlwifi_get_noise,
	.quality          = nlwifi_get_quality,
	.quality_max      = nlwifi_get_quality_max,
	.mbssid_support   = nlwifi_get_mbssid_support,
	.hwmodelist       = nlwifi_get_hwmodelist,
	.mode             = nlwifi_get_mode,
	.ssid             = nlwifi_get_ssid,
	.bssid            = nlwifi_get_bssid,
	.country          = nlwifi_get_country,
	.hardware_id      = nlwifi_get_hardware_id,
	.hardware_name    = nlwifi_get_hardware_name,
	.encryption       = nlwifi_get_encryption,
	.phyname          = nlwifi_get_phyname,
	.assoclist        = nlwifi_get_assoclist,
	.txpwrlist        = nlwifi_get_txpwrlist,
	.scanlist         = nlwifi_get_scanlist,
	.freqlist         = nlwifi_get_freqlist,
	.countrylist      = nlwifi_get_countrylist,
	.close            = nlwifi_close
};

const struct iwinfo_ops *mtk_nlwifi_ops()
{
	return &nlwifi_ops;
}


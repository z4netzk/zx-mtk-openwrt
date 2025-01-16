/*
 * iwinfo - Wireless Information Library - nlwifi Headers
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
 */

#ifndef __IWINFO_NLWIFI_H_
#define __IWINFO_NLWIFI_H_

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <dirent.h>
#include <signal.h>
#include <sys/un.h>
#include <netlink/netlink.h>
#include <netlink/genl/genl.h>
#include <netlink/genl/family.h>
#include <netlink/genl/ctrl.h>
#include <libubox/uloop.h>

#include "iwinfo.h"
#include "iwinfo/utils.h"
#include "api/wext.h"
#include "api/nlwifi.h"

#ifndef SOL_NETLINK
#define SOL_NETLINK 270
#endif

#ifndef NETLINK_NO_ENOBUFS
#define NETLINK_NO_ENOBUFS 5
#endif

struct nlwifi_state {
	struct nl_sock *nl_sock;
	struct nl_cache *nl_cache;
	struct genl_family *nl80211;
	struct genl_family *nlctrl;
};

struct nlwifi_callback {
	struct nl_sock	*sock;
	struct nl_cb	*cb;
	struct uloop_fd	ufd;
	int (*callback)(char *, u_int8_t *, void *, void *, u_int32_t);
	int attr1;
	int attr2;
	int attr3; /* 0 means nla_len(attr2) */
};

struct nlwifi_buf {
	void *buf;
	int len;
	int attr;
	int count;
};

struct nlwifi_msg_conveyor {
	struct nl_msg *msg;
	struct nl_cb *cb;
};

struct nlwifi_event_conveyor {
	int wait;
	int recv;
};

struct nlwifi_group_conveyor {
	const char *name;
	int id;
};

struct nlwifi_iface {
	struct list_head list;
	char ifname[IFNAMSIZ];
	char phyname[IFNAMSIZ];
	int family;
};

#endif /* __IWINFO_NLWIFI_H_ */

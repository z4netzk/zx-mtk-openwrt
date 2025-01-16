/******************************************************************************
 *
 * Copyright (c) 2020-2025
 * All rights reserved.
 *
 * FILE NAME  :   nlwifi.c
 * VERSION    :   1.0
 * DESCRIPTION:   This is the netlink-based wireless configuration interface.
 * AUTHOR     :
 *
 ******************************************************************************/

#include "rt_config.h"
#include <linux/version.h>
#include <linux/ieee80211.h>
#include <linux/rtnetlink.h>
#include <net/genetlink.h>
#include <linux/kobject.h>
#include <linux/kernel.h>
#include <linux/reboot.h>
#include "nlwifi.h"

#define NLWIFI_FLAG_NEED_IFACE		 0x01
#define NLWIFI_FLAG_IFACE_DOWN		 0x02

#define AP_DEVICE_NAME	"mt7981"
static PRTMP_ADAPTER g_pAd = NULL;

enum
{
	PHY_MODE_BASIC,
	PHY_MODE_HT,
	PHY_MODE_VHT,
	PHY_MODE_HE,
	PHY_MODE_INVALID,
};

typedef struct GNU_PACKED
{
	UINT8	maxChWidth;
	UINT8	numStreams;
	UINT8	phyMode;
	UINT8	maxMCS;
	UINT8	maxTxPower;
} iface_rate_t;

struct GNU_PACKED mesh_channel_change
{
	UINT8	ifindex;
	UINT8	channel;
	UINT16	freq;
};

typedef struct GNU_PACKED
{
	u_int16_t	lastTxRate;
	u_int16_t	lastRxRate;
	u_int32_t	packetsSent;
	u_int32_t	packetsReceived;
	u_int32_t	txPacketsErrors;
	u_int32_t	rxPacketsErrors;
	u_int32_t	retransmissionCount;
	u_int32_t	timeDelta;
	u_int64_t	txBytes;
	u_int64_t	rxBytes;
} Sta_Stats_t;

typedef struct GNU_PACKED
{
	u_int8_t	macAddr[ETH_ALEN];
	u_int8_t	isBTMSupported;
	u_int8_t	isRRMSupported;
	u_int8_t	isMUMIMOSupported;
	u_int8_t	isStaticSMPS;
	u_int8_t	bandCap;
	time_t		assocAge;
	u_int8_t	rssi;
	u_int8_t	maxChWidth;
	u_int8_t	numStreams;
	u_int8_t	phyMode;
	u_int8_t	maxMCS;
	u_int8_t	maxTxPower;
	u_int16_t	idle;
	Sta_Stats_t stats;
} sta_ability_t;

static int nlwifi_prepare_dump(struct netlink_callback *cb);
extern COUNTRY_CODE_TO_COUNTRY_REGION *RTMPGetCountryByIsoName(RTMP_ADAPTER *pAd, RTMP_STRING *arg);
extern INT Set_RadioOn_Proc(IN	PRTMP_ADAPTER	pAd, IN	RTMP_STRING *arg);
extern INT Set_CountryString_Proc(RTMP_ADAPTER *pAd, RTMP_STRING *arg);

static int nlwifi_pre_doit(const struct genl_ops *ops, struct sk_buff *skb, struct genl_info *info);
static void nlwifi_post_doit(const struct genl_ops *ops, struct sk_buff *skb, struct genl_info *info);

static struct genl_family nlwifi_fam;

/* multicast groups */
enum {
	NLWIFI_MCGRP_SCAN,
	NLWIFI_MCGRP_PROBE,
	NLWIFI_MCGRP_ACTION,
	NLWIFI_MCGRP_EVENT,
	NLWIFI_MCGRP_MESH,
};

static const struct genl_multicast_group nlwifi_mcgrps[] = {
	[NLWIFI_MCGRP_SCAN]    = { .name = "scan", },
	[NLWIFI_MCGRP_PROBE]   = { .name = "probe", },
	[NLWIFI_MCGRP_ACTION]  = { .name = "action", },
	[NLWIFI_MCGRP_EVENT]   = { .name = "event", }
};

static struct wifi_dev *get_first_wdev_by_band(PRTMP_ADAPTER pAd, INT band)
{
	INT idx;
	struct wifi_dev *wdev = NULL;

	for (idx = 0; idx < WDEV_NUM_MAX; idx++) {
		wdev = pAd->wdev_list[idx];
		if (wdev && (HcGetBandByWdev(wdev) == band))
			return wdev;
	}

	return NULL;
}

static INT get_bandIdx_by_ifname(char *ifname) {
	INT BandIdx = DBDC_BAND0;

	if (!ifname) {
		return DBDC_BAND0;
	}

	if (!strcmp(ifname, "radio1") || !strncmp(ifname, "rax", 3)) {
		BandIdx = DBDC_BAND1;
	} else if (!strcmp(ifname, "radio0") || !strncmp(ifname, "ra", 2)) {
		BandIdx = DBDC_BAND0;
	}

	return BandIdx;
}

static struct net_device *nlwifi_prepare_netdev(PRTMP_ADAPTER pAd, char *ifname)
{
	POS_COOKIE pObj = (POS_COOKIE)pAd->OS_Cookie;
	INT idx;

	for (idx = 0; idx < WDEV_NUM_MAX; idx++) {
		if (pAd->wdev_list[idx] && (WDEV_TYPE_AP == pAd->wdev_list[idx]->wdev_type)
			&& !strcmp(pAd->wdev_list[idx]->if_dev->name, ifname)) {
			pObj->ioctl_if_type = INT_MBSSID;
			pObj->ioctl_if = idx;
			return pAd->wdev_list[idx]->if_dev;
		}
	}

#ifdef APCLI_SUPPORT
	for (idx = 0; idx < MAX_MULTI_STA; idx++) {
		if (!strcmp(pAd->StaCfg[idx].wdev.if_dev->name, ifname)) {
			pObj->ioctl_if_type = INT_APCLI;
			pObj->ioctl_if = idx;
			return pAd->StaCfg[idx].wdev.if_dev;
		}
	}
#endif /* APCLI_SUPPORT */

	return NULL;
}

static int nlwifi_pre_doit(const struct genl_ops *ops, struct sk_buff *skb, struct genl_info *info)
{
	PRTMP_ADAPTER pAd = g_pAd;
	struct net_device *dev = NULL;

	if (!info->attrs[NLWIFI_ATTR_IFNAME]) {
		return -EINVAL;
	}

	rtnl_lock();
	if (ops->internal_flags & NLWIFI_FLAG_NEED_IFACE) {
		dev = nlwifi_prepare_netdev(pAd, (char *)nla_data(info->attrs[NLWIFI_ATTR_IFNAME]));

		if (!dev) {
			rtnl_unlock();
			return -ENODEV;
		}
		if ((ops->internal_flags & NLWIFI_FLAG_IFACE_DOWN) && netif_running(dev)) {
			rtnl_unlock();
			return -ENETDOWN;
		}
	}
	return 0;
}

static void nlwifi_post_doit(const struct genl_ops *ops, struct sk_buff *skb, struct genl_info *info)
{
	rtnl_unlock();
}

int nlwifi_scan_notify(void)
{
	struct sk_buff *msg;
	void *hdr;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_ATOMIC);
	if (!msg)
		return -1;

	hdr = genlmsg_put(msg, 0, 0, &nlwifi_fam, 0, NLWIFI_CMD_SEND_EVENT);
	if (!hdr)
		goto put_failure;

	genlmsg_end(msg, hdr);

	genlmsg_multicast(&nlwifi_fam, msg, 0, NLWIFI_MCGRP_SCAN, GFP_ATOMIC);

	return 0;

put_failure:
	nlmsg_free(msg);
	return -1;
}

int nlwifi_probe_notify(u_int8_t *macaddr, char *device, int rssi)
{
	struct sk_buff *msg;
	void *hdr;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_ATOMIC);
	if (!msg)
		return -1;

	hdr = genlmsg_put(msg, 0, 0, &nlwifi_fam, 0, NLWIFI_CMD_SEND_EVENT);
	if (!hdr)
		goto put_failure;

	if (nla_put_string(msg, NLWIFI_ATTR_IFNAME, AP_DEVICE_NAME) ||
		nla_put(msg, NLWIFI_ATTR_MAC, MAC_ADDR_LEN, macaddr) ||
		nla_put_string(msg, NLWIFI_ATTR_HOSTNAME, device) ||
		nla_put_u32(msg, NLWIFI_ATTR_RSSI, 0 - rssi))
		goto nla_put_failure;

	genlmsg_end(msg, hdr);

	genlmsg_multicast(&nlwifi_fam, msg, 0, NLWIFI_MCGRP_PROBE, GFP_ATOMIC);

	return 0;

nla_put_failure:
	genlmsg_cancel(msg, hdr);
put_failure:
	nlmsg_free(msg);
	return -1;
}

int nlwifi_action_notify(PRTMP_ADAPTER pAd, UCHAR apidx, u_int8_t *macaddr, const char *action)
{
	struct sk_buff *msg;
	void *hdr;
	char *ifname;

#ifdef APCLI_SUPPORT
	if (apidx >= MIN_NET_DEVICE_FOR_APCLI)
	{
		//ifname = pAd->ApCfg.ApCliTab[apidx - MIN_NET_DEVICE_FOR_APCLI].wdev.if_dev->name;
		ifname = pAd->StaCfg[0].wdev.if_dev->name;
	}
	else
#endif /* APCLI_SUPPORT */
	{
		ifname = pAd->ApCfg.MBSSID[apidx].wdev.if_dev->name;
	}

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_ATOMIC);
	if (!msg)
		return -1;

	hdr = genlmsg_put(msg, 0, 0, &nlwifi_fam, 0, NLWIFI_CMD_SEND_EVENT);
	if (!hdr)
		goto put_failure;

	if (nla_put_string(msg, NLWIFI_ATTR_IFNAME, ifname) ||
		nla_put(msg, NLWIFI_ATTR_MAC, MAC_ADDR_LEN, macaddr) ||
		nla_put_string(msg, NLWIFI_ATTR_ACTION, action))
		goto nla_put_failure;

	genlmsg_end(msg, hdr);

	genlmsg_multicast(&nlwifi_fam, msg, 0, NLWIFI_MCGRP_ACTION, GFP_ATOMIC);

	return 0;

nla_put_failure:
	genlmsg_cancel(msg, hdr);
put_failure:
	nlmsg_free(msg);
	return -1;
}

int nlwifi_event_notify(const char *ifname, const char *event, const char *data, unsigned int len)
{
	struct sk_buff *msg;
	void *hdr;
	u_int8_t macaddr[MAC_ADDR_LEN] = {0};

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_ATOMIC);
	if (!msg)
		return -1;

	hdr = genlmsg_put(msg, 0, 0, &nlwifi_fam, 0, NLWIFI_CMD_SEND_EVENT);
	if (!hdr)
		goto put_failure;

	if (nla_put_string(msg, NLWIFI_ATTR_IFNAME, ifname) ||
		nla_put_string(msg, NLWIFI_ATTR_ACTION, event) ||
		nla_put(msg, NLWIFI_ATTR_MAC, MAC_ADDR_LEN, macaddr) ||
		nla_put(msg, NLWIFI_ATTR_DATA, len, data))
		goto nla_put_failure;

	genlmsg_end(msg, hdr);

	genlmsg_multicast(&nlwifi_fam, msg, 0, NLWIFI_MCGRP_EVENT, GFP_ATOMIC);

	return 0;

nla_put_failure:
	genlmsg_cancel(msg, hdr);
put_failure:
	nlmsg_free(msg);
	return -1;
}

static int nlwifi_get_device(struct sk_buff *skb, struct genl_info *info)
{
	PRTMP_ADAPTER pAd = g_pAd;
	struct wifi_dev *wdev = NULL;
	struct sk_buff *msg;
	void *hdr = NULL;
	void *attr;
	int i, err = -EINVAL;
	CHAR Rssi;
	u_int32_t noise;
	enum nlwifi_hwmode hwmode;
	enum nlwifi_chan_type chan_type;
	POS_COOKIE pObj = (POS_COOKIE) pAd->OS_Cookie;
	UCHAR BandIdx = 0;
	CHANNEL_CTRL *pChCtrl = NULL;
	char *ifname = (char *)nla_data(info->attrs[NLWIFI_ATTR_IFNAME]);

	if (!ifname) {
		MTWF_LOG(DBG_CAT_MLME, DBG_SUBCAT_ALL, DBG_LVL_OFF, ("%s(): ifname is null!\n", __func__));
		goto out;
	}

	BandIdx = get_bandIdx_by_ifname(ifname);
	wdev = get_first_wdev_by_band(pAd, BandIdx);
	if (!wdev) {
		MTWF_LOG(DBG_CAT_MLME, DBG_SUBCAT_ALL, DBG_LVL_WARN, ("%s():BandIdx %d wdev null!\n", __func__, BandIdx));
		goto out;
	}

	pChCtrl = hc_get_channel_ctrl(pAd->hdev_ctrl, BandIdx);
	if (DBDC_BAND0 == BandIdx) {
		hwmode = NLWIFI_HWMODE_11BGNAX;

		if (wlan_config_get_ht_bw(wdev) == BW_40)
			chan_type = NLWIFI_CHAN_HT40;
		else
			chan_type = NLWIFI_CHAN_HT20;
	} else if (DBDC_BAND1 == BandIdx) {
		hwmode = NLWIFI_HWMODE_11ACAX;

		if (wlan_config_get_vht_bw(wdev) == VHT_BW_160)
			chan_type = NLWIFI_CHAN_VHT160;
		else if (wlan_config_get_vht_bw(wdev) == VHT_BW_80)
			chan_type = NLWIFI_CHAN_VHT80;
		else if (wlan_config_get_ht_bw(wdev) == BW_40)
			chan_type = NLWIFI_CHAN_HT40;
		else
			chan_type = NLWIFI_CHAN_HT20;
	}

	Rssi = RTMPMaxRssi(pAd, pAd->ApCfg.RssiSample.AvgRssi[0],
						pAd->ApCfg.RssiSample.AvgRssi[1],
						pAd->ApCfg.RssiSample.AvgRssi[2]);
	noise = RTMPMaxRssi(pAd, pAd->ApCfg.RssiSample.AvgRssi[0],
						pAd->ApCfg.RssiSample.AvgRssi[1],
						pAd->ApCfg.RssiSample.AvgRssi[2]) -
			RTMPMinSnr(pAd, pAd->ApCfg.RssiSample.AvgSnr[0],
						pAd->ApCfg.RssiSample.AvgSnr[1]);

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg) {
		err = -ENOMEM;
		goto out;
	}

	hdr = genlmsg_put(msg, info->snd_portid, info->snd_seq, &nlwifi_fam, 0, NLWIFI_CMD_GET_DEVICE);

	if (!hdr)
		goto put_failure;

	if (nla_put_u32(msg, NLWIFI_ATTR_CHANNEL, wdev->channel) ||
		nla_put_u32(msg, NLWIFI_ATTR_CHANNEL_TYPE, chan_type) ||
		nla_put_u32(msg, NLWIFI_ATTR_HWMODE, hwmode) ||
		nla_put_u32(msg, NLWIFI_ATTR_HWMODES, hwmode) ||
		nla_put_u32(msg, NLWIFI_ATTR_RSSI, Rssi) ||
		nla_put_u32(msg, NLWIFI_ATTR_NOISE, noise))
		goto nla_put_failure;

	if (nla_put_u16(msg, NLWIFI_ATTR_VENDOR_ID, 0x14C3) ||
		nla_put_u16(msg, NLWIFI_ATTR_DEVICE_ID, pObj->DeviceID) ||
		nla_put_u16(msg, NLWIFI_ATTR_DEVICE_VER, (pAd->MACVersion >> 16)) ||
		nla_put_u16(msg, NLWIFI_ATTR_DEVICE_REV, (pAd->MACVersion & 0xffff)))
		goto nla_put_failure;

	attr = nla_nest_start(msg, NLWIFI_ATTR_CHANNEL_LIST);
	if (!attr)
		goto nla_put_failure;

	for (i = 0; i < pChCtrl->ChListNum; i++){
		if (nla_put_u8(msg, i, pChCtrl->ChList[i].Channel))
			goto nla_put_failure;
	}
	nla_nest_end(msg, attr);

	genlmsg_end(msg, hdr);
	err = genlmsg_reply(msg, info);
	goto out;

nla_put_failure:
	genlmsg_cancel(msg, hdr);
put_failure:
	nlmsg_free(msg);
	err = -EMSGSIZE;
out:
	return err;
}

static int nlwifi_set_device(struct sk_buff *skb, struct genl_info *info)
{
	PRTMP_ADAPTER pAd = g_pAd;
	enum nlwifi_hwmode hwmode = PHY_11VHT_N_A_MIXED;
	enum nlwifi_chan_type chan_type = NLWIFI_CHAN_HT40;
	struct wifi_dev *wdev = NULL;
	UCHAR channel;
	UINT8 BandIdx = 0;
	char *ifname = (char *)nla_data(info->attrs[NLWIFI_ATTR_IFNAME]);

	if (!ifname) {
		MTWF_LOG(DBG_CAT_MLME, DBG_SUBCAT_ALL, DBG_LVL_OFF, ("%s(): ifname is null!\n", __func__));
		return -1;
	}

	BandIdx = get_bandIdx_by_ifname(ifname);

	wdev = get_first_wdev_by_band(pAd, BandIdx);
	if (!wdev) {
		MTWF_LOG(DBG_CAT_MLME, DBG_SUBCAT_ALL, DBG_LVL_OFF, ("%s(): wdev null!\n", __func__));
		return -1;
	}

	if (info->attrs[NLWIFI_ATTR_HWMODE])
		hwmode = nla_get_u32(info->attrs[NLWIFI_ATTR_HWMODE]);

	if (info->attrs[NLWIFI_ATTR_TXPWR])
		pAd->CommonCfg.ucTxPowerPercentage[BandIdx] = nla_get_u32(info->attrs[NLWIFI_ATTR_TXPWR]);
	else
		pAd->CommonCfg.ucTxPowerPercentage[BandIdx] = 100;

	if (info->attrs[NLWIFI_ATTR_COUNTRY]){
		COUNTRY_CODE_TO_COUNTRY_REGION *pCountry;
		pCountry = RTMPGetCountryByIsoName(pAd, (char *)nla_data(info->attrs[NLWIFI_ATTR_COUNTRY]));
		if (pCountry) {
			if (WMODE_CAP_2G(wdev->PhyMode))
			{
				if (pCountry->SupportGBand == TRUE)
				{
					pAd->CommonCfg.CountryRegion = (UCHAR) pCountry->RegDomainNum11G;
				}
			}

			if (WMODE_CAP_5G(wdev->PhyMode))
			{
				if (pCountry->SupportABand == TRUE)
				{
					pAd->CommonCfg.CountryRegionForABand = (UCHAR) pCountry->RegDomainNum11A;
				}
			}

			NdisZeroMemory(pAd->CommonCfg.CountryCode, sizeof(pAd->CommonCfg.CountryCode));
			NdisMoveMemory(pAd->CommonCfg.CountryCode, pCountry->IsoName, 2);
			pAd->CommonCfg.CountryCode[2] = ' ';
			pAd->CommonCfg.bCountryFlag = TRUE;

			BuildChannelList(pAd, wdev);
		}
	}

	if (info->attrs[NLWIFI_ATTR_CHANNEL]) {
		channel = (UCHAR)nla_get_u32(info->attrs[NLWIFI_ATTR_CHANNEL]);
		MTWF_LOG(DBG_CAT_MLME, DBG_SUBCAT_ALL, DBG_LVL_WARN, ("%s(): channel[%d] wdev_idx[%d]!\n",
				__func__, channel, wdev->wdev_idx));

		if (channel > 0) {
			MTWF_LOG(DBG_CAT_MLME, DBG_SUBCAT_ALL, DBG_LVL_WARN, ("%s(): rtmp_set_channel channel[%d]!\n",
				__func__, channel));
			rtmp_set_channel(pAd, wdev, channel);
		} else {
			MTWF_LOG(DBG_CAT_MLME, DBG_SUBCAT_ALL, DBG_LVL_WARN, ("%s(): AutoChannelAlg = ChannelAlgBusyTime!\n",__func__));
			pAd->ApCfg.AutoChannelAlg[BandIdx] = ChannelAlgBusyTime;
			AutoChSelScanStart(pAd, wdev);
		}
	}

	if (info->attrs[NLWIFI_ATTR_CHANNEL_TYPE]) {
		chan_type = nla_get_u32(info->attrs[NLWIFI_ATTR_CHANNEL_TYPE]);
		MTWF_LOG(DBG_CAT_MLME, DBG_SUBCAT_ALL, DBG_LVL_WARN, ("%s(): chan_type[%d] wdev_idx[%d]!\n",
			__func__, chan_type, wdev->wdev_idx));
	}

	if (DBDC_BAND0 == BandIdx) {
		if (NLWIFI_CHAN_HT20 == chan_type) {
			wlan_config_set_ht_bw(wdev, BW_20);
			wlan_operate_set_ht_bw(wdev, HT_BW_20, EXTCHA_NONE);
		} else {
			wlan_config_set_ht_bw(wdev, BW_40);
			wlan_operate_set_ht_bw(wdev, HT_BW_40, wlan_operate_get_ext_cha(wdev));
		}
		SetCommonHtVht(pAd, wdev);
	} else if (DBDC_BAND1 == BandIdx) {
		if (NLWIFI_CHAN_HT20 == chan_type) {
			wlan_config_set_vht_bw(wdev, VHT_BW_2040);
			wlan_config_set_ext_cha(wdev, EXTCHA_NONE);
			wlan_config_set_ht_bw(wdev, BW_20);
			wlan_operate_set_vht_bw(wdev, VHT_BW_2040);
			wlan_operate_set_ht_bw(wdev, BW_20, EXTCHA_NONE);
			SetCommonHtVht(pAd, wdev);
		} else if (NLWIFI_CHAN_HT40 == chan_type) {
			wlan_config_set_vht_bw(wdev, VHT_BW_2040);
			wlan_config_set_ht_bw(wdev, BW_40);
			wlan_operate_set_vht_bw(wdev, VHT_BW_2040);
			SetCommonHtVht(pAd, wdev);
		} else {
			wlan_config_set_vht_bw(wdev, VHT_BW_80);
			wlan_config_set_ht_bw(wdev, BW_40);
			wlan_operate_set_vht_bw(wdev, VHT_BW_80);
			SetCommonHtVht(pAd, wdev);
		}
	}

	return 0;
}

static int _getIfaceRateInfo(struct wifi_dev *wdev, iface_rate_t *rate)
{
	struct _RTMP_ADAPTER *pAd = NULL;

	if ((NULL == wdev) || (NULL == rate))
	{
		return -1;
	}

	pAd = (struct _RTMP_ADAPTER *)wdev->sys_handle;
	if (NULL != pAd)
	{
		//RTMP_ShowPower(pAd, &rate->maxTxPower);
		rate->maxTxPower = 30;
	}
	else
	{
		rate->maxTxPower = 30;
	}

	rate->numStreams = wlan_config_get_rx_stream(wdev);

	if (wdev->PhyMode & (WMODE_AX_24G|WMODE_AX_5G|WMODE_AX_6G))
	{
		/* 5: 11ax HE mode*/
		rate->phyMode = PHY_MODE_HE;
		rate->maxMCS = MCS_11;
		rate->maxChWidth = wlan_config_get_he_bw(wdev);
	}
	else if (wdev->PhyMode & WMODE_AC)
	{
		/* 4: 11ac VHT mode*/
		rate->phyMode = PHY_MODE_VHT;
		rate->maxMCS = MCS_9;
		if (wlan_config_get_vht_bw(wdev))
		{
			rate->maxChWidth = BW_80;
		}
		else if (wlan_config_get_ht_bw(wdev))
		{
			rate->maxChWidth = BW_40;
		}
		else
		{
			rate->maxChWidth = BW_20;
		}
	}
	else if (wdev->PhyMode & (WMODE_GN|WMODE_AN))
	{
		/* 2: 11n HT MODE*/
		rate->phyMode = PHY_MODE_HT;
		rate->maxMCS = MCS_15;
		if (wlan_config_get_ht_bw(wdev))
		{
			rate->maxChWidth = BW_40;
		}
		else
		{
			rate->maxChWidth = BW_20;
		}
	}
	else
	{
		/* 1: legacy */
		rate->phyMode = PHY_MODE_BASIC;
		rate->maxMCS = MCS_7;
		rate->maxChWidth = BW_20;
	}

	return 0;
}

static int nlwifi_get_interface(struct sk_buff *skb, struct genl_info *info)
{
	PRTMP_ADAPTER pAd = g_pAd;
	POS_COOKIE pObj = (POS_COOKIE)pAd->OS_Cookie;
	struct sk_buff *msg;
	void *hdr = NULL;
	int err = -EINVAL;
	enum nlwifi_auth auth_type;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg) {
		err = -ENOMEM;
		goto out;
	}

	hdr = genlmsg_put(msg, info->snd_portid, info->snd_seq, &nlwifi_fam, 0, NLWIFI_CMD_GET_IFACE);
	if (!hdr)
		goto put_failure;

	if (pObj->ioctl_if_type == INT_MBSSID) {
		BSS_STRUCT *pMbss = &pAd->ApCfg.MBSSID[pObj->ioctl_if];
		struct wifi_dev *wdev = &pMbss->wdev;
		UINT32 main_authMode = pMbss->wdev.SecConfig.AKMMap;
		iface_rate_t rate;

		if (IS_AKM_WPA2PSK(main_authMode) && IS_AKM_WPA3PSK(main_authMode)) {
			auth_type = NLWIFI_AUTH_WPA2PSKWPA3PSK;
		} else if (IS_AKM_WPA3PSK(main_authMode)) {
			auth_type = NLWIFI_AUTH_WPA3PSK;
		} else if (IS_AKM_WPA1(main_authMode)) {
			auth_type = NLWIFI_AUTH_WPA;
		} else if (IS_AKM_WPA2(main_authMode)) {
			auth_type = NLWIFI_AUTH_WPA2;
		} else if (IS_AKM_WPA1PSK(main_authMode)) {
			auth_type = NLWIFI_AUTH_PSK;
		} else if (IS_AKM_WPA2PSK(main_authMode)) {
			auth_type = NLWIFI_AUTH_PSK2;
		}

		if (nla_put_u32(msg, NLWIFI_ATTR_IFTYPE, NLWIFI_OPMODE_MASTER) ||
			nla_put_string(msg, NLWIFI_ATTR_IFNAME, pMbss->wdev.if_dev->name) ||
			nla_put(msg, NLWIFI_ATTR_SSID, pMbss->SsidLen, pMbss->Ssid) ||
			nla_put(msg, NLWIFI_ATTR_MAC, ETH_ALEN, pMbss->wdev.bssid) ||
			nla_put_u32(msg, NLWIFI_ATTR_AUTH_TYPE, auth_type))
			goto nla_put_failure;

		NdisZeroMemory(&rate, sizeof(rate));
		_getIfaceRateInfo(wdev, &rate);
		if (nla_put(msg, NLWIFI_ATTR_IFACE_RATE, sizeof(iface_rate_t), &rate))
		{
			goto nla_put_failure;
		}
	}
#ifdef APCLI_SUPPORT
	else if (pObj->ioctl_if_type == INT_APCLI)
	{
		STA_ADMIN_CONFIG *pApCliTab = &pAd->StaCfg[pObj->ioctl_if];
		UINT32 apcli_authMode = pApCliTab->wdev.SecConfig.AKMMap;

		if (IS_AKM_WPA2PSK(apcli_authMode) && IS_AKM_WPA3PSK(apcli_authMode)) {
			auth_type = NLWIFI_AUTH_WPA2PSKWPA3PSK;
		} else if (IS_AKM_WPA3PSK(apcli_authMode)) {
			auth_type = NLWIFI_AUTH_WPA3PSK;
		} else if (IS_AKM_WPA1(apcli_authMode)) {
			auth_type = NLWIFI_AUTH_WPA;
		} else if (IS_AKM_WPA2(apcli_authMode)) {
			auth_type = NLWIFI_AUTH_WPA2;
		} else if (IS_AKM_WPA1PSK(apcli_authMode)) {
			auth_type = NLWIFI_AUTH_PSK;
		} else if (IS_AKM_WPA2PSK(apcli_authMode)) {
			auth_type = NLWIFI_AUTH_PSK2;
		}

		// TODO: apcli will match later
		if (nla_put_u32(msg, NLWIFI_ATTR_IFTYPE, NLWIFI_OPMODE_CLIENT) ||
			nla_put_string(msg, NLWIFI_ATTR_IFNAME, pApCliTab->wdev.if_dev->name) ||
			nla_put(msg, NLWIFI_ATTR_SSID, pApCliTab->CfgSsidLen, pApCliTab->CfgSsid) ||
			//nla_put(msg, NLWIFI_ATTR_MAC, ETH_ALEN, pApCliTab->Valid ? APCLI_ROOT_BSSID_GET(pAd, pApCliTab->MacTabWCID) : ZERO_MAC_ADDR) ||
			nla_put(msg, NLWIFI_ATTR_MAC, ETH_ALEN, ZERO_MAC_ADDR) ||
			nla_put_u32(msg, NLWIFI_ATTR_AUTH_TYPE, auth_type))
			goto nla_put_failure;
	}
#endif /* APCLI_SUPPORT */

	genlmsg_end(msg, hdr);
	err = genlmsg_reply(msg, info);
	goto out;

nla_put_failure:
	genlmsg_cancel(msg, hdr);
put_failure:
	nlmsg_free(msg);
	err = -EMSGSIZE;
out:
	return err;
}

extern INT Set_AP_SSID_Proc(RTMP_ADAPTER *pAd, RTMP_STRING *arg);
static int nlwifi_set_interface(struct sk_buff *skb, struct genl_info *info)
{
	PRTMP_ADAPTER pAd = g_pAd;
	POS_COOKIE pObj = (POS_COOKIE)pAd->OS_Cookie;
	UCHAR *ssid = NULL;
	struct _SECURITY_CONFIG *pSecConfig = NULL;
	enum nlwifi_auth auth_type = NLWIFI_AUTH_OPEN;
	enum nlwifi_cipher cipher_type = NLWIFI_CIPHER_AES;

	if (info->attrs[NLWIFI_ATTR_AUTH_TYPE])
		auth_type = nla_get_u32(info->attrs[NLWIFI_ATTR_AUTH_TYPE]);
	if (info->attrs[NLWIFI_ATTR_CIPHER_TYPE])
		cipher_type = nla_get_u32(info->attrs[NLWIFI_ATTR_CIPHER_TYPE]);

	if (pObj->ioctl_if_type == INT_MBSSID) {
		BSS_STRUCT *pMbss = &pAd->ApCfg.MBSSID[pObj->ioctl_if];
		switch (auth_type) {
			case NLWIFI_AUTH_WPA2PSKWPA3PSK:
				SetWdevAuthMode(&pMbss->wdev.SecConfig, "WPA2PSKWPA3PSK");
				break;
			case NLWIFI_AUTH_WPA3PSK:
				SetWdevAuthMode(&pMbss->wdev.SecConfig, "WPA3PSK");
				break;
			case NLWIFI_AUTH_PSKPSK2:
				SetWdevAuthMode(&pMbss->wdev.SecConfig, "WPAPSKWPA2PSK");
				break;
			case NLWIFI_AUTH_PSK2:
				SetWdevAuthMode(&pMbss->wdev.SecConfig, "WPA2PSK");
				break;
			case NLWIFI_AUTH_PSK:
				SetWdevAuthMode(&pMbss->wdev.SecConfig, "WPAPSK");
				break;
			case NLWIFI_AUTH_WPAWPA2:
				SetWdevAuthMode(&pMbss->wdev.SecConfig, "WPA1WPA2");
				break;
			case NLWIFI_AUTH_WPA2:
				SetWdevAuthMode(&pMbss->wdev.SecConfig, "WPA2");
				break;
			case NLWIFI_AUTH_WPA:
				SetWdevAuthMode(&pMbss->wdev.SecConfig, "WPA");
				break;
			case NLWIFI_AUTH_OPEN:
			default:
				SetWdevAuthMode(&pMbss->wdev.SecConfig, "OPEN");
				cipher_type = NLWIFI_CIPHER_NONE;
				break;
		}

		switch (cipher_type) {
			case NLWIFI_CIPHER_TKIPAES:
				SetWdevEncrypMode(&pMbss->wdev.SecConfig, "TKIPAES");
				break;
			case NLWIFI_CIPHER_AES:
				SetWdevEncrypMode(&pMbss->wdev.SecConfig, "AES");
				break;
			case NLWIFI_CIPHER_TKIP:
				SetWdevEncrypMode(&pMbss->wdev.SecConfig, "TKIP");
				break;
			case NLWIFI_CIPHER_NONE:
			default:
				SetWdevEncrypMode(&pMbss->wdev.SecConfig, "NONE");
				break;
		}

		if (info->attrs[NLWIFI_ATTR_KEY]){
			pSecConfig = &pMbss->wdev.SecConfig;
			os_move_mem(pSecConfig->PSK, (UCHAR *)nla_data(info->attrs[NLWIFI_ATTR_KEY]),
					nla_len(info->attrs[NLWIFI_ATTR_KEY]) - 1);
			pSecConfig->PSK[nla_len(info->attrs[NLWIFI_ATTR_KEY]) - 1] = '\0';
		}

		if (info->attrs[NLWIFI_ATTR_SSID]) {
			pMbss->SsidLen = nla_len(info->attrs[NLWIFI_ATTR_SSID]) - 1;
			NdisMoveMemory(pMbss->Ssid, nla_data(info->attrs[NLWIFI_ATTR_SSID]), pMbss->SsidLen);
			os_alloc_mem(NULL, (UCHAR **)&ssid, nla_len(info->attrs[NLWIFI_ATTR_SSID]));
			sprintf(ssid, "%s", (UCHAR *)nla_data(info->attrs[NLWIFI_ATTR_SSID]));
			Set_AP_SSID_Proc(pAd, ssid);
			os_free_mem(ssid);
		}
	}
#ifdef APCLI_SUPPORT
	else if (pObj->ioctl_if_type == INT_APCLI) {
		STA_ADMIN_CONFIG *pApCliTab = &pAd->StaCfg[pObj->ioctl_if];

		pApCliTab->CfgSsidLen = 0;
		if (info->attrs[NLWIFI_ATTR_SSID]) {
			pApCliTab->CfgSsidLen = nla_len(info->attrs[NLWIFI_ATTR_SSID]) - 1;
			pApCliTab->ApcliInfStat.bPeerExist = FALSE;
			NdisZeroMemory(pApCliTab->CfgSsid, MAX_LEN_OF_SSID);
			NdisMoveMemory(pApCliTab->CfgSsid, nla_data(info->attrs[NLWIFI_ATTR_SSID]), pApCliTab->CfgSsidLen);
		}

		NdisZeroMemory(pApCliTab->CfgApCliBssid, MAC_ADDR_LEN);
		if (info->attrs[NLWIFI_ATTR_MAC])
			NdisMoveMemory(pApCliTab->CfgApCliBssid, nla_data(info->attrs[NLWIFI_ATTR_MAC]), MAC_ADDR_LEN);

		switch (auth_type) {
			case NLWIFI_AUTH_WPA2PSKWPA3PSK:
				SetWdevAuthMode(&pApCliTab->wdev.SecConfig, "WPA2PSKWPA3PSK");
				break;
			case NLWIFI_AUTH_WPA3PSK:
				SetWdevAuthMode(&pApCliTab->wdev.SecConfig, "WPA3PSK");
				break;
			case NLWIFI_AUTH_PSKPSK2:
				SetWdevAuthMode(&pApCliTab->wdev.SecConfig, "WPAPSKWPA2PSK");
				break;
			case NLWIFI_AUTH_PSK2:
				SetWdevAuthMode(&pApCliTab->wdev.SecConfig, "WPA2PSK");
				break;
			case NLWIFI_AUTH_PSK:
				SetWdevAuthMode(&pApCliTab->wdev.SecConfig, "WPAPSK");
				break;
			case NLWIFI_AUTH_WPAWPA2:
				SetWdevAuthMode(&pApCliTab->wdev.SecConfig, "WPA1WPA2");
				break;
			case NLWIFI_AUTH_WPA2:
				SetWdevAuthMode(&pApCliTab->wdev.SecConfig, "WPA2");
				break;
			case NLWIFI_AUTH_WPA:
				SetWdevAuthMode(&pApCliTab->wdev.SecConfig, "WPA");
				break;
			case NLWIFI_AUTH_OPEN:
			default:
				SetWdevAuthMode(&pApCliTab->wdev.SecConfig, "OPEN");
				cipher_type = NLWIFI_CIPHER_NONE;
				break;
		}

		switch (cipher_type) {
			case NLWIFI_CIPHER_TKIPAES:
				SetWdevEncrypMode(&pApCliTab->wdev.SecConfig, "TKIPAES");
				break;
			case NLWIFI_CIPHER_AES:
				SetWdevEncrypMode(&pApCliTab->wdev.SecConfig, "AES");
				break;
			case NLWIFI_CIPHER_TKIP:
				SetWdevEncrypMode(&pApCliTab->wdev.SecConfig, "TKIP");
				break;
			case NLWIFI_CIPHER_NONE:
			default:
				SetWdevEncrypMode(&pApCliTab->wdev.SecConfig, "NONE");
				break;
		}

		if (info->attrs[NLWIFI_ATTR_KEY]) {
			pSecConfig = &pApCliTab->wdev.SecConfig;
			os_move_mem(pSecConfig->PSK, (UCHAR *)nla_data(info->attrs[NLWIFI_ATTR_KEY]),
							nla_len(info->attrs[NLWIFI_ATTR_KEY]) - 1);
			pSecConfig->PSK[nla_len(info->attrs[NLWIFI_ATTR_KEY]) - 1] = '\0';
		}

		pApCliTab->ApcliInfStat.Enable = TRUE;
	}
#endif /* APCLI_SUPPORT */
	else
		return -EINVAL;

	return 0;
}

static int nlwifi_get_region(struct sk_buff *skb, struct genl_info *info)
{
	PRTMP_ADAPTER pAd = g_pAd;
	struct sk_buff *msg;
	void *hdr = NULL;
	int err = -EINVAL;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg) {
		err = -ENOMEM;
		goto out;
	}

#if LINUX_VERSION_CODE > KERNEL_VERSION(3,7,0)
	hdr = genlmsg_put(msg, info->snd_portid, info->snd_seq, &nlwifi_fam, 0, NLWIFI_CMD_GET_REGION);
#else
	hdr = genlmsg_put(msg, info->snd_pid, info->snd_seq, &nlwifi_fam, 0, NLWIFI_CMD_GET_REGION);
#endif
	if (!hdr)
		goto put_failure;

	if (nla_put_string(msg, NLWIFI_ATTR_COUNTRY, pAd->CommonCfg.CountryCode))
		goto nla_put_failure;

	genlmsg_end(msg, hdr);
	err = genlmsg_reply(msg, info);
	goto out;

nla_put_failure:
	genlmsg_cancel(msg, hdr);
put_failure:
	nlmsg_free(msg);
	err = -EMSGSIZE;
out:
	return err;
}

static int nlwifi_set_scan(struct sk_buff *skb, struct genl_info *info)
{
	PRTMP_ADAPTER pAd = g_pAd;

	if (info->attrs[NLWIFI_ATTR_SSID])
		Set_SiteSurvey_Proc(pAd, nla_data(info->attrs[NLWIFI_ATTR_SSID]));
	else
		Set_SiteSurvey_Proc(pAd, NULL);

	return 0;
}

PMAC_TABLE_ENTRY _get_entry_by_idx(PRTMP_ADAPTER pAd, int idx)
{
	POS_COOKIE pObj = (POS_COOKIE)pAd->OS_Cookie;
	PMAC_TABLE_ENTRY pEntry;
	INT i, index = 0;

	for (i=0; VALID_UCAST_ENTRY_WCID(pAd, i); i++) {
		pEntry = &pAd->MacTab.Content[i];
		if ((pObj->ioctl_if_type == INT_MBSSID) && IS_ENTRY_CLIENT(pEntry)
			&& (pEntry->func_tb_idx == pObj->ioctl_if) && (pEntry->Sst == SST_ASSOC)
			&& (pEntry->PrivacyFilter == Ndis802_11PrivFilterAcceptAll)) {
			if (index == idx)
				return pEntry;

			index++;
		}
#ifdef APCLI_SUPPORT
		else if ((pObj->ioctl_if_type == INT_APCLI) && IS_ENTRY_APCLI(pEntry) &&
		(pEntry->Sst == SST_ASSOC) && (pEntry->PrivacyFilter == Ndis802_11PrivFilterAcceptAll)) {
			if (index == idx)
				return pEntry;
			index++;
		}
#endif /* APCLI_SUPPORT */
	}

	return NULL;
}

static int _nlwifi_dump_stainfo(int idx, sta_info_t *stainfo)
{
	PRTMP_ADAPTER pAd = g_pAd;
	PMAC_TABLE_ENTRY pEntry;
	ULONG DataRate = 0;
	ULONG DataRate_r = 0;
	u_int8_t rxs = 1;
#ifdef RACTRL_FW_OFFLOAD_SUPPORT
	struct _RTMP_CHIP_CAP *cap = hc_get_chip_cap(pAd->hdev_ctrl);
#endif

	pEntry = _get_entry_by_idx(pAd, idx);
	if (!pEntry)
		return -EINVAL;

	if (pEntry->SupportVHTMCS4SS)
		rxs = 4;
	else if (pEntry->SupportVHTMCS3SS)
		rxs = 3;
	else if (pEntry->SupportVHTMCS2SS)
		rxs = 2;

	NdisMoveMemory(stainfo->mac, pEntry->Addr, MAC_ADDR_LEN);
	stainfo->assoctime = pEntry->StaConnectTime;
	stainfo->inact = pEntry->NoDataIdleCount;
	stainfo->rssi[0] = pEntry->RssiSample.AckRssi[0];
	stainfo->rssi[1] = pEntry->RssiSample.AckRssi[1];
	stainfo->rssi[2] = pEntry->RssiSample.AckRssi[2];

	stainfo->txbytes = (u_int32_t)pEntry->TxBytes;
	stainfo->rxbytes = (u_int32_t)pEntry->RxBytes;
	stainfo->signal = RTMPAvgRssi(pAd, &pEntry->RssiSample);
	stainfo->noise = RTMPMaxRssi(pAd, pEntry->RssiSample.AvgRssi[0], pEntry->RssiSample.AvgRssi[1],
		pEntry->RssiSample.AvgRssi[2]) - RTMPMinSnr(pAd, pEntry->RssiSample.AvgSnr[0], pEntry->RssiSample.AvgSnr[1]);

	if (pEntry->SupportRateMode & SUPPORT_VHT_MODE)
		stainfo->hwmode = NLWIFI_HWMODE_11AC;
	else if (pEntry->SupportRateMode & SUPPORT_HT_MODE)
		stainfo->hwmode = NLWIFI_HWMODE_11N;
	else
		stainfo->hwmode = NLWIFI_HWMODE_11A;

#ifdef RACTRL_FW_OFFLOAD_SUPPORT
	if ((cap->fgRateAdaptFWOffload == TRUE) && (pEntry->bAutoTxRateSwitch == TRUE)) {
		UCHAR phy_mode, rate, bw, sgi, stbc;
		UCHAR phy_mode_r, rate_r, bw_r, sgi_r, stbc_r;
		UCHAR nss;
		UCHAR nss_r;
		UINT32 RawData;
		UINT32 lastTxRate = pEntry->LastTxRate;
		UINT32 lastRxRate = pEntry->LastRxRate;
		UCHAR ucBand = HcGetBandByWdev(pEntry->wdev);

		if (pEntry->bAutoTxRateSwitch == TRUE) {
			EXT_EVENT_TX_STATISTIC_RESULT_T rTxStatResult;
			EXT_EVENT_PHY_STATE_RX_RATE rRxStatResult;
			HTTRANSMIT_SETTING LastTxRate;
			HTTRANSMIT_SETTING LastRxRate;

			MtCmdGetTxStatistic(pAd, GET_TX_STAT_ENTRY_TX_RATE, 0/*Don't Care*/, pEntry->wcid, &rTxStatResult);
			LastTxRate.field.MODE = rTxStatResult.rEntryTxRate.MODE;
			LastTxRate.field.BW = rTxStatResult.rEntryTxRate.BW;
			LastTxRate.field.ldpc = rTxStatResult.rEntryTxRate.ldpc ? 1 : 0;
			LastTxRate.field.ShortGI = rTxStatResult.rEntryTxRate.ShortGI ? 1 : 0;
			LastTxRate.field.STBC = rTxStatResult.rEntryTxRate.STBC;

			if (LastTxRate.field.MODE >= MODE_VHT)
				LastTxRate.field.MCS = (((rTxStatResult.rEntryTxRate.VhtNss - 1) & 0x3) << 4) + rTxStatResult.rEntryTxRate.MCS;
			else if (LastTxRate.field.MODE == MODE_OFDM)
				LastTxRate.field.MCS = getLegacyOFDMMCSIndex(rTxStatResult.rEntryTxRate.MCS) & 0x0000003F;
			else
				LastTxRate.field.MCS = rTxStatResult.rEntryTxRate.MCS;

			lastTxRate = (UINT32)(LastTxRate.word);
			LastRxRate.word = (USHORT)lastRxRate;
			RawData = lastTxRate;
			phy_mode = (RawData >> 13) & 0x7;
			rate = RawData & 0x3F;
			bw = (RawData >> 7) & 0x3;
			sgi = rTxStatResult.rEntryTxRate.ShortGI;
			stbc = ((RawData >> 10) & 0x1);
			nss = rTxStatResult.rEntryTxRate.VhtNss;

			MtCmdPhyGetRxRate(pAd, CMD_PHY_STATE_CONTENTION_RX_PHYRATE, ucBand, pEntry->wcid, &rRxStatResult);
			LastRxRate.field.MODE = rRxStatResult.u1RxMode;
			LastRxRate.field.BW = rRxStatResult.u1BW;
			LastRxRate.field.ldpc = rRxStatResult.u1Coding;
			LastRxRate.field.ShortGI = rRxStatResult.u1Gi ? 1 : 0;
			LastRxRate.field.STBC = rRxStatResult.u1Stbc;
		
			if (LastRxRate.field.MODE >= MODE_VHT)
				LastRxRate.field.MCS = ((rRxStatResult.u1RxNsts & 0x3) << 4) + rRxStatResult.u1RxRate;
			else if (LastRxRate.field.MODE == MODE_OFDM)
				LastRxRate.field.MCS = getLegacyOFDMMCSIndex(rRxStatResult.u1RxRate) & 0x0000003F;
			else
				LastRxRate.field.MCS = rRxStatResult.u1RxRate;

			phy_mode_r = rRxStatResult.u1RxMode;
			rate_r = rRxStatResult.u1RxRate & 0x3F;
			bw_r = rRxStatResult.u1BW;
			sgi_r = rRxStatResult.u1Gi;
			stbc_r = rRxStatResult.u1Stbc;
			nss_r = rRxStatResult.u1RxNsts + 1;

			stainfo->txbw = bw;
			stainfo->rxbw = bw_r;
#ifdef DOT11_VHT_AC
			if (phy_mode >= MODE_VHT)
				stainfo->txmcs = rate & 0xF;
			else
#endif
				stainfo->txmcs = rate;

#ifdef DOT11_VHT_AC
			if (phy_mode_r >= MODE_VHT)
				stainfo->rxmcs = rate_r & 0xF;
			else
#endif

#if DOT11_N_SUPPORT
			if (phy_mode_r >= MODE_HTMIX)
				stainfo->rxmcs = rate_r;
			else
#endif
			if (phy_mode_r == MODE_OFDM) {
				if (rate_r == TMI_TX_RATE_OFDM_6M)
					LastRxRate.field.MCS = 0;
				else if (rate_r == TMI_TX_RATE_OFDM_9M)
					LastRxRate.field.MCS = 1;
				else if (rate_r == TMI_TX_RATE_OFDM_12M)
					LastRxRate.field.MCS = 2;
				else if (rate_r == TMI_TX_RATE_OFDM_18M)
					LastRxRate.field.MCS = 3;
				else if (rate_r == TMI_TX_RATE_OFDM_24M)
					LastRxRate.field.MCS = 4;
				else if (rate_r == TMI_TX_RATE_OFDM_36M)
					LastRxRate.field.MCS = 5;
				else if (rate_r == TMI_TX_RATE_OFDM_48M)
					LastRxRate.field.MCS = 6;
				else if (rate_r == TMI_TX_RATE_OFDM_54M)
					LastRxRate.field.MCS = 7;
				else
					LastRxRate.field.MCS = 0;

				stainfo->rxmcs = LastRxRate.field.MCS;
			} else if (phy_mode_r == MODE_CCK) {
				if (rate_r == TMI_TX_RATE_CCK_1M_LP)
					LastRxRate.field.MCS = 0;
				else if (rate_r == TMI_TX_RATE_CCK_2M_LP)
					LastRxRate.field.MCS = 1;
				else if (rate_r == TMI_TX_RATE_CCK_5M_LP)
					LastRxRate.field.MCS = 2;
				else if (rate_r == TMI_TX_RATE_CCK_11M_LP)
					LastRxRate.field.MCS = 3;
				else if (rate_r == TMI_TX_RATE_CCK_2M_SP)
					LastRxRate.field.MCS = 1;
				else if (rate_r == TMI_TX_RATE_CCK_5M_SP)
					LastRxRate.field.MCS = 2;
				else if (rate_r == TMI_TX_RATE_CCK_11M_SP)
					LastRxRate.field.MCS = 3;
				else
					LastRxRate.field.MCS = 0;

				stainfo->rxmcs = LastRxRate.field.MCS;
			}

			if (phy_mode >= MODE_HE) {
				get_rate_he((rate & 0xf), bw, nss, 0, &DataRate);
				if (sgi == 1)
					DataRate = (DataRate * 967) >> 10;
				if (sgi == 2)
					DataRate = (DataRate * 870) >> 10;

				get_rate_he((rate_r & 0xf), bw_r, nss_r, 0, &DataRate_r);
				if (sgi_r == 1)
					DataRate_r = (DataRate_r * 967) >> 10;
				if (sgi_r == 2)
					DataRate_r = (DataRate_r * 870) >> 10;
			} else {
				getRate(LastTxRate, &DataRate);
				getRate(LastRxRate, &DataRate_r);
			}
		}
	} else
#endif /* RACTRL_FW_OFFLOAD_SUPPORT */
	{
		ULONG DataRate = 0;
		stainfo->txbw =pEntry->HTPhyMode.field.BW;
		stainfo->rxbw = pEntry->HTPhyMode.field.BW;

		if (pEntry->MaxHTPhyMode.field.MODE == MODE_VHT){
			stainfo->txmcs = pEntry->HTPhyMode.field.MCS & 0xf;
			stainfo->rxmcs = pEntry->HTPhyMode.field.MCS & 0xf;
		} else {
			stainfo->txmcs = pEntry->HTPhyMode.field.MCS;
			stainfo->rxmcs = pEntry->HTPhyMode.field.MCS;
		}

		getRate(pEntry->HTPhyMode, &DataRate);
		stainfo->txrate = DataRate * 1000;
		stainfo->rxrate = DataRate * 1000;
	}

	stainfo->pwr = (u_int8_t)pEntry->PsMode;
	stainfo->rxs = rxs;

	return 0;
}

static int nlwifi_dump_stalist(struct sk_buff *skb, struct netlink_callback *cb)
{
	sta_info_t stainfo;
	int dump_idx = cb->args[1];
	void *hdr = NULL;
	int err = -EINVAL;

	err = nlwifi_prepare_dump(cb);
	if (err)
		goto out;

	while(1) {
		memset(&stainfo, 0, sizeof(stainfo));
		err = _nlwifi_dump_stainfo(dump_idx, &stainfo);
		if (err)
			goto out;

#if LINUX_VERSION_CODE > KERNEL_VERSION(3,7,0)
		hdr = genlmsg_put(skb, NETLINK_CB(cb->skb).portid, cb->nlh->nlmsg_seq, &nlwifi_fam, NLM_F_MULTI, NLWIFI_CMD_GET_STALIST);
#else
		hdr = genlmsg_put(skb, NETLINK_CB(cb->skb).pid, cb->nlh->nlmsg_seq, &nlwifi_fam, NLM_F_MULTI, NLWIFI_CMD_GET_STALIST);
#endif
		if (!hdr)
			goto out;

		if (nla_put(skb, NLWIFI_ATTR_STA_INFO, sizeof(sta_info_t), &stainfo))
			goto nla_put_failure;

		genlmsg_end(skb, hdr);
		dump_idx++;
	}
	goto out;

nla_put_failure:
	genlmsg_cancel(skb, hdr);
out:
	cb->args[1] = dump_idx;
	return skb->len;
}

static int nlwifi_update_device(struct sk_buff *skb, struct genl_info *info)
{
	PRTMP_ADAPTER pAd = g_pAd;
	char channel = -1;
	UINT32 BandIdx = 0;
	struct wifi_dev *wdev = NULL;
	enum nlwifi_chan_type chan_type;

	char *ifname = (char *)nla_data(info->attrs[NLWIFI_ATTR_IFNAME]);

	if (!ifname) {
		MTWF_LOG(DBG_CAT_MLME, DBG_SUBCAT_ALL, DBG_LVL_OFF, ("%s(): ifname is null!\n", __func__));
		return -1;
	}

	BandIdx = get_bandIdx_by_ifname(ifname);

	wdev = get_first_wdev_by_band(pAd, BandIdx);
	if (!wdev) {
		MTWF_LOG(DBG_CAT_MLME, DBG_SUBCAT_ALL, DBG_LVL_OFF, ("%s(): wdev = null!\n", __func__));
		return -1;
	}

	if (info->attrs[NLWIFI_ATTR_TXPWR])
		pAd->CommonCfg.ucTxPowerPercentage[BandIdx] = nla_get_u32(info->attrs[NLWIFI_ATTR_TXPWR]);
	else
		pAd->CommonCfg.ucTxPowerPercentage[BandIdx] = 100;

	if (info->attrs[NLWIFI_ATTR_COUNTRY]){
		COUNTRY_CODE_TO_COUNTRY_REGION *pCountry;
		pCountry = RTMPGetCountryByIsoName(pAd, (char *)nla_data(info->attrs[NLWIFI_ATTR_COUNTRY]));
		if (pCountry) {
			if (WMODE_CAP_2G(wdev->PhyMode))
			{
				if (pCountry->SupportGBand == TRUE)
				{
					pAd->CommonCfg.CountryRegion = (UCHAR) pCountry->RegDomainNum11G;
				}
			}

			if (WMODE_CAP_5G(wdev->PhyMode))
			{
				if (pCountry->SupportABand == TRUE)
				{
					pAd->CommonCfg.CountryRegionForABand = (UCHAR) pCountry->RegDomainNum11A;
				}
			}

			NdisZeroMemory(pAd->CommonCfg.CountryCode, sizeof(pAd->CommonCfg.CountryCode));
			NdisMoveMemory(pAd->CommonCfg.CountryCode, pCountry->IsoName, 2);
			pAd->CommonCfg.CountryCode[2] = ' ';
			pAd->CommonCfg.bCountryFlag = TRUE;

			BuildChannelList(pAd, wdev);
		}
	}

	if (info->attrs[NLWIFI_ATTR_CHANNEL]) {
		channel = (UCHAR)nla_get_u32(info->attrs[NLWIFI_ATTR_CHANNEL]);
		MTWF_LOG(DBG_CAT_MLME, DBG_SUBCAT_ALL, DBG_LVL_OFF, ("%s(): channel[%d] wdev_idx[%d]!\n",
				__func__, channel, wdev->wdev_idx));

		if (channel > 0) {
			MTWF_LOG(DBG_CAT_MLME, DBG_SUBCAT_ALL, DBG_LVL_OFF, ("%s(): rtmp_set_channel channel[%d]!\n",
				__func__, channel));
			rtmp_set_channel(pAd, wdev, channel);
		} else {
			MTWF_LOG(DBG_CAT_MLME, DBG_SUBCAT_ALL, DBG_LVL_OFF, ("%s(): AutoChannelAlg = ChannelAlgBusyTime!\n",__func__));
			pAd->ApCfg.AutoChannelAlg[BandIdx] = ChannelAlgBusyTime;
			AutoChSelScanStart(pAd, wdev);
		}
	}

	if (info->attrs[NLWIFI_ATTR_CHANNEL_TYPE]) {
		chan_type = nla_get_u32(info->attrs[NLWIFI_ATTR_CHANNEL_TYPE]);

		MTWF_LOG(DBG_CAT_MLME, DBG_SUBCAT_ALL, DBG_LVL_OFF, ("%s(): chan_type[%d] BandIdx[%d] wdev_idx[%d]!\n",
					__func__, chan_type, BandIdx, wdev->wdev_idx));

		if (DBDC_BAND0 == BandIdx) {
			if (NLWIFI_CHAN_HT20 == chan_type) {
				wlan_config_set_ht_bw(wdev, BW_20);
				wlan_operate_set_ht_bw(wdev, HT_BW_20, EXTCHA_NONE);
			} else {
				wlan_config_set_ht_bw(wdev, BW_40);
				wlan_operate_set_ht_bw(wdev, HT_BW_40, wlan_operate_get_ext_cha(wdev));
			}
		} else if (DBDC_BAND1 == BandIdx) {
			if (NLWIFI_CHAN_HT20 == chan_type) {
				wlan_config_set_vht_bw(wdev, VHT_BW_2040);
				wlan_config_set_ext_cha(wdev, EXTCHA_NONE);
				wlan_config_set_ht_bw(wdev, BW_20);
				wlan_operate_set_vht_bw(wdev, VHT_BW_2040);
				wlan_operate_set_ht_bw(wdev, BW_20, EXTCHA_NONE);
				SetCommonHtVht(pAd, wdev);
			} else if (NLWIFI_CHAN_HT40 == chan_type) {
				wlan_config_set_vht_bw(wdev, VHT_BW_2040);
				wlan_config_set_ht_bw(wdev, BW_40);
				wlan_operate_set_vht_bw(wdev, VHT_BW_2040);
				SetCommonHtVht(pAd, wdev);
			} else {
				wlan_config_set_vht_bw(wdev, VHT_BW_80);
				wlan_config_set_ht_bw(wdev, BW_40);
				wlan_operate_set_vht_bw(wdev, VHT_BW_80);
				SetCommonHtVht(pAd, wdev);
			}
		}
	}

	return 0;
}

static int nlwifi_update_iface(struct sk_buff *skb, struct genl_info *info)
{
	PRTMP_ADAPTER pAd = g_pAd;
	POS_COOKIE pObj = (POS_COOKIE)pAd->OS_Cookie;
	struct nlattr *attr;
	int i, tmp;

	if (pObj->ioctl_if_type == INT_MBSSID) {
		BSS_STRUCT *pMbss;
		pMbss = &pAd->ApCfg.MBSSID[pObj->ioctl_if];

		if (info->attrs[NLWIFI_ATTR_HIDDEN_SSID])
			pMbss->bHideSsid = nla_get_u32(info->attrs[NLWIFI_ATTR_HIDDEN_SSID]);

		if (info->attrs[NLWIFI_ATTR_SSID])
			Set_AP_SSID_Proc(pAd, (RTMP_STRING *)nla_data(info->attrs[NLWIFI_ATTR_SSID]));

		if (info->attrs[NLWIFI_ATTR_AP_ISOLATE])
			pMbss->IsolateInterStaTraffic = nla_get_u32(info->attrs[NLWIFI_ATTR_AP_ISOLATE]);

		if (info->attrs[NLWIFI_ATTR_STANUM])
			pMbss->MaxStaNum = nla_get_u32(info->attrs[NLWIFI_ATTR_STANUM]);

		if (info->attrs[NLWIFI_ATTR_DEAUTH_MAC]) {
			MAC_TABLE_ENTRY *pEntry = NULL;

			pEntry = MacTableLookup(pAd, nla_data(info->attrs[NLWIFI_ATTR_DEAUTH_MAC]));
			if (pEntry && IS_ENTRY_CLIENT(pEntry) && (pEntry->func_tb_idx == pObj->ioctl_if))
				MlmeDeAuthAction(pAd, pEntry, REASON_DISASSOC_STA_LEAVING, FALSE);
		}

		if (info->attrs[NLWIFI_ATTR_ACL_POLICY]) {
			pMbss->AccessControlList.Policy = nla_get_u32(info->attrs[NLWIFI_ATTR_ACL_POLICY]);
			pMbss->AccessControlList.Num = 0;
		}

		if (pMbss->AccessControlList.Policy != 0) {
			if (info->attrs[NLWIFI_ATTR_ACL_MACLIST]) {
				nla_for_each_nested(attr, info->attrs[NLWIFI_ATTR_ACL_MACLIST], tmp) {
					if (nla_len(attr) != ETH_ALEN)
						return -EINVAL;

					if (pMbss->AccessControlList.Num < MAX_NUM_OF_ACL_LIST) {
						for (i = 0; i < pMbss->AccessControlList.Num; i++) {
							if (NdisEqualMemory(pMbss->AccessControlList.Entry[i].Addr,
									nla_data(attr), MAC_ADDR_LEN))
								break;
						}
						if (i == pMbss->AccessControlList.Num)
							NdisMoveMemory(pMbss->AccessControlList.Entry[pMbss->AccessControlList.Num++].Addr,
							nla_data(attr),  MAC_ADDR_LEN);
					}
				}
			}

			if (info->attrs[NLWIFI_ATTR_ADD_MAC]) {
				if (pMbss->AccessControlList.Num < MAX_NUM_OF_ACL_LIST) {
					for (i = 0; i < pMbss->AccessControlList.Num; i++) {
						if (NdisEqualMemory(pMbss->AccessControlList.Entry[i].Addr,
							nla_data(info->attrs[NLWIFI_ATTR_ADD_MAC]), MAC_ADDR_LEN))
							break;
					}
					if (i == pMbss->AccessControlList.Num)
						NdisMoveMemory(pMbss->AccessControlList.Entry[pMbss->AccessControlList.Num++].Addr,
							nla_data(info->attrs[NLWIFI_ATTR_ADD_MAC]), MAC_ADDR_LEN);
				}
			}

			if (info->attrs[NLWIFI_ATTR_DEL_MAC]) {
				for (i = 0; i < pMbss->AccessControlList.Num; i++) {
					if (NdisEqualMemory(pMbss->AccessControlList.Entry[i].Addr,
						nla_data(info->attrs[NLWIFI_ATTR_DEL_MAC]), MAC_ADDR_LEN))
						break;
				}
				if (i < pMbss->AccessControlList.Num) {
					pMbss->AccessControlList.Num--;
					for (; i < pMbss->AccessControlList.Num; i++)
						NdisMoveMemory(pMbss->AccessControlList.Entry[i].Addr,
							pMbss->AccessControlList.Entry[i+1].Addr, MAC_ADDR_LEN);
				}
			}

			/* check if the change in ACL affects any existent association */
			ApUpdateAccessControlList(pAd, pObj->ioctl_if);
		}
	}

	return 0;
}

PMAC_TABLE_ENTRY _getEntryByIdx(PRTMP_ADAPTER pAd, int idx)
{
	POS_COOKIE pObj = (POS_COOKIE)pAd->OS_Cookie;
	struct wifi_dev *wdev = get_wdev_by_ioctl_idx_and_iftype(pAd, pObj->ioctl_if, pObj->ioctl_if_type);
	PMAC_TABLE_ENTRY pEntry;
	INT i, index = 0;

	for (i=0; VALID_UCAST_ENTRY_WCID(pAd, i); i++) {
		pEntry = &pAd->MacTab.Content[i];
		if ((pObj->ioctl_if_type == INT_MBSSID) && IS_ENTRY_CLIENT(pEntry)
			&& (pEntry->Sst == SST_ASSOC) && (pEntry->EntryType == ENTRY_CLIENT)
			&& wdev && (wdev == pEntry->wdev))
		{
			if (index == idx)
				return pEntry;

			index++;
		}
	}

	return NULL;
}

static ULONG _getStaLastTxRate(PMAC_TABLE_ENTRY pEntry)
{
	PRTMP_ADAPTER pAd = g_pAd;
	ULONG TxDataRate = 0;
	HTTRANSMIT_SETTING HTPhyMode;
	EXT_EVENT_TX_STATISTIC_RESULT_T rTxStatResult;
	HTTRANSMIT_SETTING LastTxRate;
#ifdef DOT11_HE_AX
	UINT8 he_dcm = 0;
	UINT8 he_mcs = 0;
#endif

	MtCmdGetTxStatistic(pAd, GET_TX_STAT_ENTRY_TX_RATE, 0/*Don't Care*/, pEntry->wcid, &rTxStatResult);
	LastTxRate.field.MODE = rTxStatResult.rEntryTxRate.MODE;
	LastTxRate.field.BW = rTxStatResult.rEntryTxRate.BW;
	LastTxRate.field.ldpc = rTxStatResult.rEntryTxRate.ldpc ? 1 : 0;
	LastTxRate.field.ShortGI = rTxStatResult.rEntryTxRate.ShortGI ? 1 : 0;
	LastTxRate.field.STBC = rTxStatResult.rEntryTxRate.STBC;

#ifdef DOT11_HE_AX
	if (LastTxRate.field.MODE == MODE_HE_SU_REMAPPING)
	{
		he_mcs = rTxStatResult.rEntryTxRate.MCS & 0xf;
		he_dcm = rTxStatResult.rEntryTxRate.MCS & 0x10 ? 1 : 0;
		get_rate_he(he_mcs, rTxStatResult.rEntryTxRate.BW, rTxStatResult.rEntryTxRate.VhtNss, he_dcm, &TxDataRate);
	} else
#endif
	if (LastTxRate.field.MODE == MODE_VHT)
	{
		LastTxRate.field.MCS = (((rTxStatResult.rEntryTxRate.VhtNss - 1) & 0x3) << 4) + rTxStatResult.rEntryTxRate.MCS;
	}
	else if (LastTxRate.field.MODE == MODE_OFDM)
	{
		LastTxRate.field.MCS = getLegacyOFDMMCSIndex(rTxStatResult.rEntryTxRate.MCS) & 0x0000003F;
	}
	else
	{
		LastTxRate.field.MCS = rTxStatResult.rEntryTxRate.MCS;
	}

	pEntry->LastTxRate = (UINT32)LastTxRate.word;

	if (LastTxRate.field.MODE != MODE_HE_SU_REMAPPING)
	{
		HTPhyMode.word = (USHORT)pEntry->LastTxRate;
		getRate(HTPhyMode, &TxDataRate);
		TxDataRate = (UINT16)TxDataRate;
	}

	/* Though NSS1VHT20MCS9 and NSS2VHT20MCS9 rates are not specified in
	 * IEEE802.11, we do use them */
	if ((HTPhyMode.field.MODE == MODE_VHT)
		&& (HTPhyMode.field.BW == BW_20)
		&& ((HTPhyMode.field.MCS & 0xf) == 9))
	{
		UINT8 vht_nss = ((HTPhyMode.field.MCS & (0x3 << 4)) >> 4) + 1;
		if (vht_nss == 1)
		{
			TxDataRate = HTPhyMode.field.ShortGI ? 96 : 86;
		}
		else if (vht_nss == 2)
		{
			TxDataRate = HTPhyMode.field.ShortGI ? 192 : 173;
		}
	}

	return TxDataRate;
}

static ULONG _getStaLastRxRate(PMAC_TABLE_ENTRY pEntry)
{
	ULONG RxDataRate = 0;

#ifdef RACTRL_FW_OFFLOAD_SUPPORT
	PRTMP_ADAPTER pAd = g_pAd;
	struct _RTMP_CHIP_CAP *cap = hc_get_chip_cap(pAd->hdev_ctrl);

	if (cap->fgRateAdaptFWOffload == TRUE)
	{
		UCHAR phy_mode_r, rate_r, bw_r, sgi_r, stbc_r, nss_r;
		UCHAR ucBand = HcGetBandByWdev(pEntry->wdev);

		EXT_EVENT_PHY_STATE_RX_RATE rRxStatResult = {0, 0, 0, 0, 0, 0, 0, 0};
		HTTRANSMIT_SETTING LastRxRate;

		MtCmdPhyGetRxRate(pAd, CMD_PHY_STATE_CONTENTION_RX_PHYRATE, ucBand, pEntry->wcid, &rRxStatResult);
		LastRxRate.word = (USHORT)pEntry->LastRxRate;
		LastRxRate.field.MODE = rRxStatResult.u1RxMode;
		LastRxRate.field.BW = rRxStatResult.u1BW;
		LastRxRate.field.ldpc = rRxStatResult.u1Coding;
		LastRxRate.field.ShortGI = rRxStatResult.u1Gi ? 1 : 0;
		LastRxRate.field.STBC = rRxStatResult.u1Stbc;

		if (LastRxRate.field.MODE >= MODE_VHT)
			LastRxRate.field.MCS = ((rRxStatResult.u1RxNsts & 0x3) << 4) + rRxStatResult.u1RxRate;
		else if (LastRxRate.field.MODE == MODE_OFDM)
			LastRxRate.field.MCS = getLegacyOFDMMCSIndex(rRxStatResult.u1RxRate & 0xF);
		else
			LastRxRate.field.MCS = rRxStatResult.u1RxRate;

		phy_mode_r = rRxStatResult.u1RxMode;
		rate_r = rRxStatResult.u1RxRate & 0x3F;
		bw_r = rRxStatResult.u1BW;
		sgi_r = rRxStatResult.u1Gi;
		stbc_r = rRxStatResult.u1Stbc;

#ifdef DOT11_VHT_AC
		if (phy_mode_r >= MODE_VHT)
		{
			nss_r = (rRxStatResult.u1RxNsts + 1) / (rRxStatResult.u1Stbc + 1);
			rate_r = rate_r & 0xF;
		} else
#endif /* DOT11_VHT_AC */
		if (phy_mode_r == MODE_OFDM)
		{
			rate_r = rate_r & 0xF;
			if (rate_r == TMI_TX_RATE_OFDM_6M)
				LastRxRate.field.MCS = 0;
			else if (rate_r == TMI_TX_RATE_OFDM_9M)
				LastRxRate.field.MCS = 1;
			else if (rate_r == TMI_TX_RATE_OFDM_12M)
				LastRxRate.field.MCS = 2;
			else if (rate_r == TMI_TX_RATE_OFDM_18M)
				LastRxRate.field.MCS = 3;
			else if (rate_r == TMI_TX_RATE_OFDM_24M)
				LastRxRate.field.MCS = 4;
			else if (rate_r == TMI_TX_RATE_OFDM_36M)
				LastRxRate.field.MCS = 5;
			else if (rate_r == TMI_TX_RATE_OFDM_48M)
				LastRxRate.field.MCS = 6;
			else if (rate_r == TMI_TX_RATE_OFDM_54M)
				LastRxRate.field.MCS = 7;
			else
				LastRxRate.field.MCS = 0;
		}
		else if (phy_mode_r == MODE_CCK)
		{
			rate_r = rate_r & 0x7;
			if (rate_r == TMI_TX_RATE_CCK_1M_LP)
				LastRxRate.field.MCS = 0;
			else if (rate_r == TMI_TX_RATE_CCK_2M_LP)
				LastRxRate.field.MCS = 1;
			else if (rate_r == TMI_TX_RATE_CCK_5M_LP)
				LastRxRate.field.MCS = 2;
			else if (rate_r == TMI_TX_RATE_CCK_11M_LP)
				LastRxRate.field.MCS = 3;
			else if (rate_r == TMI_TX_RATE_CCK_2M_SP)
				LastRxRate.field.MCS = 1;
			else if (rate_r == TMI_TX_RATE_CCK_5M_SP)
				LastRxRate.field.MCS = 2;
			else if (rate_r == TMI_TX_RATE_CCK_11M_SP)
				LastRxRate.field.MCS = 3;
			else
				LastRxRate.field.MCS = 0;
		}

		if (phy_mode_r >= MODE_HE)
		{
			get_rate_he((rate_r & 0xf), bw_r, nss_r, 0, &RxDataRate);
			if (sgi_r == 1)
				RxDataRate = (RxDataRate * 967) >> 10;
			if (sgi_r == 2)
				RxDataRate = (RxDataRate * 870) >> 10;
		}
		else
		{
			getRate(LastRxRate, &RxDataRate);
		}
	}
#endif /* #ifdef RACTRL_FW_OFFLOAD_SUPPORT */

	return RxDataRate;
}

static void _FillStaStats(PMAC_TABLE_ENTRY pEntry, Sta_Stats_t *stats)
{
	stats->txBytes = pEntry->TxBytes;
	stats->rxBytes = pEntry->RxBytes;

	stats->packetsSent = pEntry->TxPackets.u.LowPart;
	stats->packetsReceived = pEntry->RxPackets.u.LowPart;

	stats->lastTxRate = _getStaLastTxRate(pEntry);
	stats->lastRxRate = _getStaLastRxRate(pEntry);

	/* driver not implement yet, uppper roaming module not used yet */
	stats->txPacketsErrors = 0;
	stats->rxPacketsErrors = 0;
	stats->retransmissionCount = 0;
	stats->timeDelta = 0;
}

void meshPackStainfo(sta_ability_t *stainfo, PMAC_TABLE_ENTRY pEntry)
{
	PRTMP_ADAPTER pAd = g_pAd;

	NdisMoveMemory(stainfo->macAddr, pEntry->Addr, MAC_ADDR_LEN);
	stainfo->rssi = 95 + RTMPAvgRssi(pAd, &pEntry->RssiSample);
	stainfo->assocAge = RTMPMsecsToJiffies(1000 * pEntry->StaConnectTime);
	stainfo->idle = pEntry->NoDataIdleCount;
	stainfo->maxChWidth = pEntry->MaxHTPhyMode.field.BW;
	stainfo->phyMode = pEntry->MaxHTPhyMode.field.MODE;

	stainfo->isBTMSupported = (pEntry->bBSSMantSTASupport == TRUE) ? 1 : 0;
#ifdef DOT11K_RRM_SUPPORT
	if ((pEntry->RrmEnCap.field.BeaconPassiveMeasureCap
		|| pEntry->RrmEnCap.field.BeaconActiveMeasureCap))
	{
		stainfo->isRRMSupported = 1;
	}
	else
	{
		stainfo->isRRMSupported = 0;
	}
#endif
	stainfo->isMUMIMOSupported = pEntry->vht_cap_ie.vht_cap.bfee_cap_su;
	stainfo->isStaticSMPS = pEntry->HTCapability.HtCapInfo.MimoPs;

	if (WMODE_CAP_5G(pEntry->wdev->PhyMode) && (pEntry->wdev->channel > 14))
	{
		stainfo->bandCap = 2; /* BAND_5G*/
	}
	else
	{
		stainfo->bandCap = 1; /* BAND_24G */
	}

	stainfo->numStreams = (pEntry->MaxHTPhyMode.field.MCS >> 4) + 1;

#ifdef DOT11_VHT_AC
	if (pEntry->MaxHTPhyMode.field.MODE >= MODE_VHT)
	{
		stainfo->maxMCS = pEntry->MaxHTPhyMode.field.MCS & 0xf;
	}
	else
#endif /* DOT11_VHT_AC */
	{
		stainfo->maxMCS = pEntry->MaxHTPhyMode.field.MCS;
	}

	stainfo->maxTxPower = 0;

	_FillStaStats(pEntry, &stainfo->stats);
}

static int _nlwifi_dump_sta_ability(int idx, sta_ability_t *stainfo)
{
	PRTMP_ADAPTER pAd = g_pAd;
	PMAC_TABLE_ENTRY pEntry;

	pEntry = _getEntryByIdx(pAd, idx);
	if (!pEntry)
		return -EINVAL;

	meshPackStainfo(stainfo, pEntry);
	return 0;
}

static int nlwifi_get_sta_ability(struct sk_buff *skb, struct netlink_callback *cb)
{
	sta_ability_t stainfo;
	int dump_idx = cb->args[1];
	void *hdr = NULL;
	int err = -EINVAL;

	err = nlwifi_prepare_dump(cb);
	if (err)
	{
		goto out;
	}

	while(1)
	{
		memset(&stainfo, 0, sizeof(stainfo));
		err = _nlwifi_dump_sta_ability(dump_idx, &stainfo);
		if (err)
			goto out;

		hdr = genlmsg_put(skb, NETLINK_CB(cb->skb).portid, cb->nlh->nlmsg_seq, &nlwifi_fam,
							NLM_F_MULTI, NLWIFI_CMD_GET_STA_ABILITYLIST);

		if (!hdr)
			goto out;

		if (nla_put(skb, NLWIFI_ATTR_STA_ABILITYLIST, sizeof(sta_ability_t), &stainfo))
			goto nla_put_failure;

		genlmsg_end(skb, hdr);
		dump_idx++;
	}
	goto out;

nla_put_failure:
	genlmsg_cancel(skb, hdr);
out:
	cb->args[1] = dump_idx;
	return skb->len;
}

static int nlwifi_send_btm_request(struct sk_buff *skb, struct genl_info *info)
{
	PRTMP_ADAPTER pAd = g_pAd;
	p_btm_reqinfo_t btm_req = NULL;
	UINT32 len = 0;

	if (!info->attrs[NLWIFI_ATTR_BTM_REQUEST])
	{
		return -EINVAL;
	}

	btm_req = (p_btm_reqinfo_t)nla_data(info->attrs[NLWIFI_ATTR_BTM_REQUEST]);
	len = nla_len(info->attrs[NLWIFI_ATTR_BTM_REQUEST]);

	send_btm_req_param(pAd, btm_req, len);
	return 0;
}

static int nlwifi_send_bcn_measreq(struct sk_buff *skb, struct genl_info *info)
{
	PRTMP_ADAPTER pAd = g_pAd;
	POS_COOKIE pObj = (POS_COOKIE)pAd->OS_Cookie;
	BSS_STRUCT *pMbss = &pAd->ApCfg.MBSSID[pObj->ioctl_if];
	bcn_req_info *bcnreq_info = NULL;
	UINT32 bcnreq_len = 0;

	if (!info->attrs[NLWIFI_ATTR_BCN_REQ_DATA])
	{
		return -EINVAL;
	}

	bcnreq_info = (bcn_req_info *)nla_data(info->attrs[NLWIFI_ATTR_BCN_REQ_DATA]);
	if (NULL == bcnreq_info)
	{
		MTWF_LOG(DBG_CAT_ALL, DBG_SUBCAT_ALL, DBG_LVL_ERROR, ("%s() Error: bcnreq_info NULL\n", __func__));
		return -EINVAL;
	}

	bcnreq_len = nla_len(info->attrs[NLWIFI_ATTR_BCN_REQ_DATA]);
	if (bcnreq_len != sizeof(bcn_req_info))
	{
		MTWF_LOG(DBG_CAT_ALL, DBG_SUBCAT_ALL, DBG_LVL_ERROR, ("%s() Error: bcnreq_len %d invalid\n", __func__, bcnreq_len));
		return -EINVAL;
	}

	if ((bcnreq_info->req_ssid_len == 0) && (NULL != pMbss))
	{
		bcnreq_info->req_ssid_len = pMbss->SsidLen;
		NdisMoveMemory(bcnreq_info->req_ssid, pMbss->Ssid, pMbss->SsidLen);
	}

	rrm_send_beacon_req_param(pAd, bcnreq_info, bcnreq_len);
	return 0;
}

static int nlwifi_send_neigh_response(struct sk_buff *skb, struct genl_info *info)
{
	PRTMP_ADAPTER pAd = g_pAd;
	p_rrm_nrrsp_info_custom_t nrrsp = NULL;
	UINT32 nrrsp_len = 0;

	if (!info->attrs[NLWIFI_ATTR_NEIGHBOR_RESPONSE])
	{
		return -EINVAL;
	}

	nrrsp = (p_rrm_nrrsp_info_custom_t)nla_data(info->attrs[NLWIFI_ATTR_NEIGHBOR_RESPONSE]);
	nrrsp_len = nla_len(info->attrs[NLWIFI_ATTR_NEIGHBOR_RESPONSE]);

	rrm_send_nr_rsp_param(pAd, nrrsp, nrrsp_len);
	return 0;
}

static int nlwifi_send_qosnull(struct sk_buff *skb, struct genl_info *info)
{
	PRTMP_ADAPTER pAd = g_pAd;
	MAC_TABLE_ENTRY *pEntry = NULL;
	UINT8 macaddr[MAC_ADDR_LEN] = {0};
	UINT32 qosnull_num = 0;

	if (!info->attrs[NLWIFI_ATTR_VALUE] || !info->attrs[NLWIFI_ATTR_MAC])
	{
		return -EINVAL;
	}

	qosnull_num = nla_get_u32(info->attrs[NLWIFI_ATTR_VALUE]);
	NdisMoveMemory(macaddr, nla_data(info->attrs[NLWIFI_ATTR_MAC]), MAC_ADDR_LEN);
	pEntry = MacTableLookup(pAd, macaddr);

	if (!pEntry)
	{
		MTWF_LOG(DBG_CAT_CFG, DBG_SUBCAT_ALL, DBG_LVL_ERROR, ("%s given station not found!\n", __func__));
		return -1;
	}

	if (pEntry->PsMode == PWR_SAVE)
	{
		/* use TIM bit to detect the PS station */
		MTWF_LOG(DBG_CAT_CFG, DBG_SUBCAT_ALL, DBG_LVL_WARN, ("%s use TIM bit to detect PS station "
					"[%02x:%02x:%02x:%02x:%02x:%02x]!\n", __func__, PRINT_MAC(macaddr)));
		WLAN_MR_TIM_BIT_SET(pAd, pEntry->func_tb_idx, pEntry->Aid);
		OS_WAIT(200);
	}
	else
	{
		/* use Null or QoS Null to detect the ACTIVE station */
		BOOLEAN bQosNull = FALSE;
		int count = 0;

		if (CLIENT_STATUS_TEST_FLAG(pEntry, fCLIENT_STATUS_WMM_CAPABLE))
			bQosNull = TRUE;

		MTWF_LOG(DBG_CAT_CFG, DBG_SUBCAT_ALL, DBG_LVL_WARN, ("%s send Qos Data To station "
				"[%02x:%02x:%02x:%02x:%02x:%02x]!\n", __func__, PRINT_MAC(macaddr)));
		for (count = 0; count < qosnull_num; count++)
		{
			/* TODO status */
			RtmpEnqueueNullFrame(pAd, pEntry->Addr, pEntry->CurrTxRate,
					pEntry->Aid, pEntry->func_tb_idx, bQosNull, TRUE, 0);
		}
	}

	return 0;
}

static int nlwifi_set_disassoc_sta(struct sk_buff *skb, struct genl_info *info)
{
	PRTMP_ADAPTER pAd = g_pAd;
	MAC_TABLE_ENTRY *pEntry = NULL;
	POS_COOKIE pObj = (POS_COOKIE)pAd->OS_Cookie;
	struct wifi_dev *wdev = get_wdev_by_ioctl_idx_and_iftype(pAd, pObj->ioctl_if, pObj->ioctl_if_type);
	UCHAR *stamac = NULL;

	if (!info->attrs[NLWIFI_ATTR_MAC])
	{
		return -EINVAL;
	}

	if (pObj->ioctl_if_type != INT_MBSSID)
	{
		return -EINVAL;
	}

	stamac = nla_data(info->attrs[NLWIFI_ATTR_MAC]);
	pEntry = MacTableLookup(pAd, stamac);
	if (!pEntry)
	{
		MTWF_LOG(DBG_CAT_ALL, DBG_SUBCAT_ALL, DBG_LVL_ERROR, ("%s MacTable not found %02X:%02X:%02X:%02X:%02X:%02X!\n",
				__func__, stamac[0], stamac[1], stamac[2], stamac[3], stamac[4], stamac[5]));
		return -EINVAL;
	}

	if (pEntry && IS_ENTRY_CLIENT(pEntry) && (pEntry->wdev == wdev))
	{
		MTWF_LOG(DBG_CAT_ALL, DBG_SUBCAT_ALL, DBG_LVL_ERROR, ("%s stamac %02X:%02X:%02X:%02X:%02X:%02X!\n", __func__,
					stamac[0], stamac[1], stamac[2], stamac[3], stamac[4], stamac[5]));
		APMlmeKickOutSta(pAd, pEntry->Addr, pEntry->wcid, REASON_DISASSOC_STA_LEAVING);
	}

	return 0;
}

/* policy for the attributes */
static const struct nla_policy nlwifi_policy[NLWIFI_ATTR_MAX+1] = {
	[NLWIFI_ATTR_IFNAME] = { .type = NLA_NUL_STRING, .len = IFNAMSIZ-1 },
	[NLWIFI_ATTR_HWMODE] = { .type = NLA_U32 },
	[NLWIFI_ATTR_IFTYPE] = { .type = NLA_U32 },
	[NLWIFI_ATTR_CHANNEL] = { .type = NLA_U32 },
	[NLWIFI_ATTR_CHANNEL_TYPE] = { .type = NLA_U32 },
	[NLWIFI_ATTR_FREQ_OFFSET] = { .type = NLA_U32 },
	[NLWIFI_ATTR_TXPWR] = { .type = NLA_U32 },
	[NLWIFI_ATTR_RSSI] = { .type = NLA_U32 },
	[NLWIFI_ATTR_QUALITY] = { .type = NLA_U32 },
	[NLWIFI_ATTR_NOISE] = { .type = NLA_U32 },
	[NLWIFI_ATTR_STANUM] = { .type = NLA_U32 },
	[NLWIFI_ATTR_RTS_THRESHOLD] = { .type = NLA_U32 },
	[NLWIFI_ATTR_COUNTRY] = { .type = NLA_STRING, .len = 2 },
	[NLWIFI_ATTR_MAC] = { .len = ETH_ALEN },
	[NLWIFI_ATTR_BSSID] = { .len = ETH_ALEN },
	[NLWIFI_ATTR_KEY] = { .type = NLA_BINARY, .len = (WLAN_MAX_KEY_LEN*2 + 1) },
	[NLWIFI_ATTR_PPK] = { .type = NLA_BINARY, .len = (WLAN_MAX_KEY_LEN*2 + 1) },
	[NLWIFI_ATTR_SSID] = { .type = NLA_BINARY, .len = (IEEE80211_MAX_SSID_LEN + 1) },
	[NLWIFI_ATTR_AUTH_TYPE] = { .type = NLA_U32 },
	[NLWIFI_ATTR_CIPHER_TYPE] = { .type = NLA_U32 },
	[NLWIFI_ATTR_HIDDEN_SSID] = { .type = NLA_U32 },
	[NLWIFI_ATTR_AP_ISOLATE] = { .type = NLA_U32 },
	[NLWIFI_ATTR_ACL_POLICY] = { .type = NLA_U32 },
	[NLWIFI_ATTR_ACL_MACLIST] = { .type = NLA_NESTED },
	[NLWIFI_ATTR_ADD_MAC] = { .len = ETH_ALEN },
	[NLWIFI_ATTR_DEL_MAC] = { .len = ETH_ALEN },
	[NLWIFI_ATTR_DEAUTH_MAC] = { .len = ETH_ALEN },
	[NLWIFI_ATTR_STA_INFO] = { .type = NLA_BINARY, .len = sizeof(sta_info_t) },
	[NLWIFI_ATTR_AP_INFO] = { .type = NLA_BINARY, .len = sizeof(ap_info_t) },
	[NLWIFI_ATTR_HOSTNAME] = { .type = NLA_STRING, .len = 64 },
	[NLWIFI_ATTR_VENDOR_ID] = { .type = NLA_U16 },
	[NLWIFI_ATTR_DEVICE_ID] = { .type = NLA_U16 },
	[NLWIFI_ATTR_DEVICE_VER] = { .type = NLA_U16 },
	[NLWIFI_ATTR_DEVICE_REV] = { .type = NLA_U16 },
	[NLWIFI_ATTR_FRAME] = { .type = NLA_BINARY, .len = IEEE80211_MAX_DATA_LEN },
	[NLWIFI_ATTR_ACTION] = { .type = NLA_STRING, .len = 32 },
	[NLWIFI_ATTR_DATA] = { .type = NLA_STRING, .len = IEEE80211_MAX_DATA_LEN },
	[NLWIFI_ATTR_VALUE] = { .type = NLA_U32 },
	[NLWIFI_ATTR_TRUE] = { .type = NLA_FLAG },
	[NLWIFI_ATTR_IFACE_RATE] = { .type = NLA_BINARY, .len = sizeof(iface_rate_t) },
	[NLWIFI_ATTR_BCN_REQ_DATA] = { .type = NLA_BINARY, .len = sizeof(bcn_req_info) },
	[NLWIFI_ATTR_NEIGHBOR_RESPONSE] = { .type = NLA_BINARY, .len = IEEE80211_MAX_DATA_LEN },
	[NLWIFI_ATTR_BTM_REQUEST] = { .type = NLA_BINARY, .len = IEEE80211_MAX_DATA_LEN },
};

/* Generic Netlink operations array */
static const struct genl_ops nlwifi_ops[] = {
	{
		.cmd = NLWIFI_CMD_GET_DEVICE,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = nlwifi_get_device,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = NLWIFI_CMD_GET_IFACE,
		.doit = nlwifi_get_interface,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.flags = GENL_ADMIN_PERM,
		.internal_flags = NLWIFI_FLAG_NEED_IFACE,
	},
	{
		.cmd = NLWIFI_CMD_GET_REGION,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = nlwifi_get_region,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = NLWIFI_CMD_TRIGGER_SCAN,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = nlwifi_set_scan,
		.flags = GENL_ADMIN_PERM,
		.internal_flags = NLWIFI_FLAG_NEED_IFACE,
	},
	{
		.cmd = NLWIFI_CMD_GET_STALIST,
		.dumpit = nlwifi_dump_stalist,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.flags = GENL_ADMIN_PERM,
	},
};

/* the netlink family */
static struct genl_family nlwifi_fam __ro_after_init = {
	.name = "nlwifi", /* have users key off the name instead */
	.hdrsize = 0,		/* no private header */
	.version = 1,		/* no particular meaning now */
	.netnsok = true,
	.maxattr = NLWIFI_ATTR_MAX,
	.pre_doit = nlwifi_pre_doit,
	.post_doit = nlwifi_post_doit,
	.policy = nlwifi_policy,
	.ops = nlwifi_ops,
	.n_ops = ARRAY_SIZE(nlwifi_ops),
	.mcgrps = nlwifi_mcgrps,
	.n_mcgrps = ARRAY_SIZE(nlwifi_mcgrps),
	.module = THIS_MODULE,
};

static int nlwifi_prepare_dump(struct netlink_callback *cb)
{
	PRTMP_ADAPTER pAd = g_pAd;
	struct net_device *dev = NULL;
	int bparse = cb->args[0];
	int ret;

	if (!bparse) {
		ret = nlmsg_parse(cb->nlh, GENL_HDRLEN + nlwifi_fam.hdrsize,
					nlwifi_fam.attrbuf, nlwifi_fam.maxattr, nlwifi_policy, NULL);
		if (ret)
			return -1;

		cb->args[0] = 1;
	}

	if (!nlwifi_fam.attrbuf[NLWIFI_ATTR_IFNAME])
		return -1;

	dev = nlwifi_prepare_netdev(pAd, (char *)nla_data(nlwifi_fam.attrbuf[NLWIFI_ATTR_IFNAME]));
	if (!dev)
		return -1;

	return 0;
}

int nlwifi_init(PRTMP_ADAPTER pAd)
{
	int ret;

	ret = genl_register_family(&nlwifi_fam);
	if (ret)
		return ret;

	g_pAd = pAd;

	return 0;

err_out:
	genl_unregister_family(&nlwifi_fam);

	return ret;
}

void nlwifi_exit(void)
{
	genl_unregister_family(&nlwifi_fam);
}


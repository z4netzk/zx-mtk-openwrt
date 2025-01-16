#!/bin/sh
append DRIVERS "mt7981"

. /lib/functions/wtinfo.sh

load_mt7981() {
	ifconfig ra0 up
}

scan_mt7981() {
	return 0
}

disable_mt7981() {
	local device="$1"

	config_get vifs "$device" vifs

	# bring up vifs
	for vif in $vifs; do
		config_get ifname "$vif" ifname
		ifconfig $ifname down
	done

	set_wifi_down "$device"
}

mt7981_handle_apcli() {
	local ifname=$1
	local ssid=$2
	local encryption=$3
	local key=$4

	if [ "$encryption" == "none" ]; then
		iwpriv $ifname set ApCliEnable=0
		iwpriv $ifname set ApCliAuthMode=OPEN
		iwpriv $ifname set ApCliEncrypType=NONE
		iwpriv $ifname set ApCliSsid="$ssid"
		iwpriv $ifname set ApCliAutoConnect=1
		iwpriv $ifname set ApCliEnable=1
	elif [ "$encryption" == "mixed-psk" ]; then
		iwpriv $ifname set ApCliEnable=0
		iwpriv $ifname set ApCliAuthMode=WPAPSKWPA2PSK
		iwpriv $ifname set ApCliEncrypType=TKIPAES
		iwpriv $ifname set ApCliSsid="$ssid"
		iwpriv $ifname set ApCliWPAPSK="$key"
		iwpriv $ifname set ApCliEnable=1
		iwpriv $ifname set ApCliAutoConnect=1
	elif [ "$encryption" == "wpa2pskwpa3psk" ]; then
		iwpriv $ifname set ApCliEnable=0
		iwpriv $ifname set ApCliAuthMode=WPA2PSKWPA3PSK
		iwpriv $ifname set ApCliEncrypType=AES
		iwpriv $ifname set ApCliSsid="$ssid"
		iwpriv $ifname set ApCliWPAPSK="$key"
		iwpriv $ifname set ApCliEnable=1
		iwpriv $ifname set ApCliAutoConnect=1
	fi
}

mt7981_handle_ap() {
	local ifname=$1
	local ssid=$2
	local encryption=$3
	local key=$4
	local hidden=$5

	if [ "$encryption" == "none" ]; then
		iwpriv $ifname set AuthMode=OPEN
		iwpriv $ifname set EncrypType=NONE
		iwpriv $ifname set IEEE8021X=0
		iwpriv $ifname set SSID="$ssid"
		iwpriv $ifname set HideSSID=$hidden
	elif [ "$encryption" == "mixed-psk" ]; then
		iwpriv $ifname set AuthMode=WPAPSKWPA2PSK
		iwpriv $ifname set EncrypType=TKIPAES
		iwpriv $ifname set IEEE8021X=0
		iwpriv $ifname set WPAPSK="$key"
		iwpriv $ifname set SSID="$ssid"
		iwpriv $ifname set HideSSID=$hidden
	elif [ "$encryption" == "wpa2pskwpa3psk" ]; then
		iwpriv $ifname set AuthMode=WPA2PSKWPA3PSK
		iwpriv $ifname set EncrypType=AES
		iwpriv $ifname set IEEE8021X=0
		iwpriv $ifname set WPAPSK="$key"
		iwpriv $ifname set SSID="$ssid"
		iwpriv $ifname set HideSSID=$hidden
	fi
}

aplci_fix()
{
	local vif=$1

	config_get_bool disabled "$vif" disabled 0
	config_get ifname "$vif" ifname
	[ $disabled -eq 0 ] || {
		if [ "$ifname" == "apcli0" -o  "$ifname" == "apclix0" ]; then
			iwpriv $ifname set ApCliEnable=0
		fi

		ifconfig $ifname down
		return
	}

	ifconfig $ifname up
	config_get ssid "$vif" ssid
	config_get encryption "$vif" encryption
	config_get key "$vif" key
	config_get hidden "$vif" hidden 0
	config_get isolate "$vif" isolate 0

	mt7981_handle_apcli "$ifname" "$ssid" "$encryption" "$key"

	local net_cfg bridge
	net_cfg="$(find_net_config "$vif")"
	[ -z "$net_cfg" ] || {
		bridge="$(bridge_interface "$net_cfg")"
		config_set "$vif" bridge "$bridge"
		start_net "$ifname" "$net_cfg"
	}

	set_wifi_up "$vif" "$ifname"
}

apply_mt7981()
{
	local vif=$1

	config_get_bool disabled "$vif" disabled 0
	config_get ifname "$vif" ifname
	[ $disabled -eq 0 ] || {
		if [ "$ifname" == "apcli0" -o  "$ifname" == "apclix0" ]; then
			iwpriv $ifname set ApCliEnable=0
		fi

		ifconfig $ifname down
		return
	}

	ifconfig $ifname up
	config_get ssid "$vif" ssid
	config_get encryption "$vif" encryption
	config_get key "$vif" key
	config_get hidden "$vif" hidden 0
	config_get isolate "$vif" isolate 0

	if [ "$ifname" == "apcli0" -o  "$ifname" == "apclix0" ]; then
		mt7981_handle_apcli "$ifname" "$ssid" "$encryption" "$key"
	else
		mt7981_handle_ap "$ifname" "$ssid" "$encryption" "$key" "$hidden"
		iwpriv "$ifname" set NoForwarding=$isolate
	fi

	local net_cfg bridge
	net_cfg="$(find_net_config "$vif")"
	[ -z "$net_cfg" ] || {
		bridge="$(bridge_interface "$net_cfg")"
		config_set "$vif" bridge "$bridge"
		start_net "$ifname" "$net_cfg"
	}

	set_wifi_up "$vif" "$ifname"
}

set_country() {
	case "$1" in
	"CN")
		iwpriv $2 set CountryString=CHINA
		iwpriv $2 set CountryCode=CN
		if [ "$2" == "ra0" ]; then
			iwpriv $2 set CountryRegion=1
		fi
		if [ "$2" == "rax0" ]; then
			iwpriv $2 set CountryRegionABand=0
		fi
		;;
	"SA")
		iwpriv $2 set CountryString="SAUDI ARABIA"
		if [ "$2" == "ra0" ]; then
			iwpriv $2 set CountryCode=0
		fi
		if [ "$2" == "rax0" ]; then
			iwpriv $2 set CountryRegionABand=14
		fi
		;;
	*)
		iwpriv $2 set CountryString="UNITED STATES"
		iwpriv $2 set CountryCode=US
		if [ "$2" == "ra0" ]; then
			iwpriv $2 set CountryRegion=0
		fi
		if [ "$2" == "rax0" ]; then
			iwpriv $2 set CountryRegionABand=14
		fi
		;;
	esac
}

enable_mt7981() {
	local device="$1"

	config_get hwmode "$device" hwmode g
	config_get htmode "$device" htmode HT20
	config_get channel "$device" channel 0
	config_get power "$device" power 100
	config_get country "$device" country CN

	config_get vifs "$device" vifs

	if [ "$device" == "radio0" ]; then
		ifconfig ra0 up
		set_country $country ra0
		if [ "$hwmode" == "11axg" ]; then
			iwpriv ra0 set WirelessMode=16
		else
			iwpriv ra0 set WirelessMode=9
		fi

		if [ "$channel" == "auto" -o "$channel" == "0" ]; then
			iwpriv ra0 set AutoChannelSel=3
		else
			iwpriv ra0 set Channel=$channel
		fi

		if [ "$htmode" == "HT40" ]; then
			iwpriv ra0 set HtExtCha=0
			iwpriv ra0 set HtBw=1
		elif [ "$htmode" == "HT20" ]; then
			iwpriv ra0 set HtBw=0
		else
			iwpriv ra0 set HtBw=0
		fi

		iwpriv ra0 set TxPower=$power
	elif [ "$device" == "radio1" ]; then
		ifconfig rax0 up
		set_country $country rax0
		if [ "$hwmode" == "11axa" ]; then
			iwpriv rax0 set WirelessMode=17
		else
			iwpriv rax0 set WirelessMode=14
		fi

		if [ "$channel" == "auto" -o "$channel" == "0" ]; then
			iwpriv rax0 set AutoChannelSel=3
		else
			iwpriv rax0 set Channel=$channel
		fi

		if [ "$htmode" == "HT20" ]; then
			iwpriv rax0 set VhtBw=0
			iwpriv rax0 set HtExtCha=0
			iwpriv rax0 set HtBw=0
		elif [ "$htmode" == "HT40" ]; then
			iwpriv rax0 set VhtBw=0
			iwpriv rax0 set HtExtCha=0
			iwpriv rax0 set HtBw=1
		elif [ "$htmode" == "HT80" ]; then
			iwpriv rax0 set VhtBw=1
			iwpriv rax0 set HtExtCha=0
			iwpriv rax0 set HtBw=1
		else
			iwpriv rax0 set HtExtCha=0
			iwpriv rax0 set HtBw=1
			iwpriv rax0 set VhtBw=2
		fi

		iwpriv rax0 set TxPower=$power
	fi

	# bring up vifs
	for vif in $vifs; do
		apply_mt7981 $vif
	done

	if [ "$device" == "radio0" ]; then
		if [ "$htmode" == "HT40" ]; then
			iwpriv ra0 set HtExtCha=0
			iwpriv ra0 set HtBw=1
		elif [ "$htmode" == "HT20" ]; then
			iwpriv ra0 set HtBw=0
		else
			iwpriv ra0 set HtBw=0
		fi

		iwpriv ra0 set TxPower=$power
	elif [ "$device" == "radio1" ]; then
		if [ "$htmode" == "HT80" ]; then
			iwpriv rax0 set HtExtCha=0
			iwpriv rax0 set HtBw=1
			iwpriv rax0 set VhtBw=1
		elif [ "$htmode" == "HT40" ]; then
			iwpriv rax0 set HtExtCha=0
			iwpriv rax0 set HtBw=1
			iwpriv rax0 set VhtBw=0
		elif [ "$htmode" == "HT20" ]; then
			iwpriv rax0 set HtExtCha=0
			iwpriv rax0 set HtBw=0
			iwpriv rax0 set VhtBw=0
		else
			iwpriv rax0 set HtExtCha=0
			iwpriv rax0 set HtBw=1
			iwpriv rax0 set VhtBw=2
		fi

		iwpriv rax0 set TxPower=$power
	fi

	aplci_fix wisp0
	aplci_fix wisp1
	/etc/init.d/smp restart
}

detect_old_mt7981() {
	sleep 1

	config_load wireless
	config_get type "radio0" type
	[ -n "$type" ] && return

	local wt_mac=$(wtinfo_get_mac)
	local wt_wan_mac=$(wtinfo_get_wan_mac)
	local suffix=$(echo $wt_mac | sed 's/://g')
	local mac_suffix=${suffix:8:4}
	local country=$(wtinfo_get_country)

	cat <<EOF
config wifi-device radio0
	option type mt7981
	option vendor ralink
	option phyname mt7981
	option channel auto
	option country ${country}
	option htmode auto
	option hwmode 11axg
	option def_hwmode 11axg
	option wpa3 1
	option power 100

config wifi-iface wlan0
	option device radio0
	option network lan
	option mode ap
	option ifname ra0
	option ssid 5G_CPE
	option encryption mixed-psk
	option key '123456789'
	option macaddr ${wt_mac}
	option disabled 0
	option web_merge 0

config wifi-iface guest0
	option device radio0
	option network guest
	option mode ap
	option ifname ra1
	option ssid 5G_CPE-guest-${mac_suffix}
	option encryption none
	option disabled 1

config wifi-iface wisp0
	option device radio0
	option network wisp0
	option mode sta
	option ifname apcli0
	option ssid 5G_CPE-wisp-${mac_suffix}
	option encryption none
	option disabled 1

config wifi-device radio1
	option type mt7981
	option vendor ralink
	option phyname mt7981
	option channel auto
	option country ${country}
	option htmode auto
	option hwmode 11axa
	option def_hwmode 11axa
	option wpa3 1
	option power 100

config wifi-iface wlan1
	option device radio1
	option network lan
	option mode ap
	option ifname rax0
	option ssid 5G_CPE_5G
	option encryption mixed-psk
	option key '123456789'
	option macaddr ${wt_wan_mac}
	option disabled 0

config wifi-iface guest1
	option device radio1
	option network guest
	option mode ap
	option ifname rax1
	option ssid 5G_CPE-guest-${mac_suffix}
	option encryption none
	option disabled 1

config wifi-iface wisp1
	option device radio1
	option network wisp1
	option mode sta
	option ifname apclix0
	option ssid 5G_CPE-wisp-${mac_suffix}
	option encryption none
	option disabled 1

EOF
}
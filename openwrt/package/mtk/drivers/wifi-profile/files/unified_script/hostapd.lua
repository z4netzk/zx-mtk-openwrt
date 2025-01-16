#!/usr/bin/env lua

local var_path = "/var/run/"
local var_hostapd_path = var_path.."hostapd/"
local hostapd_cli = "/usr/sbin/hostapd_cli"

function hostapd_setup_vif(dev, iface, first)
	local file_name = var_hostapd_path.."hostapd-"..iface[".name"]..".conf"
	local file

	file = io.open(file_name, "w+")

	io.output(file)
	if iface.logger_syslog then
		io.write("logger_syslog=", iface.logger_syslog, "\n")
	end
	if iface.logger_syslog_level then
		io.write("logger_syslog_level=", iface.logger_syslog_level, "\n")
	end
	if iface.logger_stdout then
		io.write("logger_stdout=", iface.logger_stdout, "\n")
	end
	if iface.logger_stdout_level then
		io.write("logger_stdout_level=", iface.logger_stdout_level, "\n")
	end

	io.write("interface=", iface[".name"], "\n")
	io.write("ssid=", iface.ssid, "\n")

--	if dev.country ~= nil then
--		io.write("country_code=", dev.country, "\n")
--	end

--	if dev.country_ie ~= nil then
--		io.write("ieee80211d=", dev.country_ie, "\n")
--	end

	if first == 1 then
		if string.lower(dev.channel) == "auto" or
		   dev.channel == nil or
		   dev.channel == "0" then
--			io.write("channel=0\n")
			if dev.band == "2.4G" then
				io.write("channel=6\n")
			elseif dev.band == "5G" then
				io.write("channel=36\n")
			elseif dev.band == "6G" then
				io.write("channel=37\n")
			end
		else
			io.write("channel=", dev.channel, "\n")
		end


--		io.write("bridge=", "br-", iface.network, "\n")  -- need to have network to bridge translate function

		io.write("driver=nl80211\n")


		if dev.band == "2.4G" then
			io.write("hw_mode=g\n")
			io.write("preamble=1\n")
			io.write("ieee80211n=1\n")
			io.write("ieee80211ac=1\n")
			io.write("ieee80211ax=1\n")
			io.write("ieee80211be=1\n")
		elseif dev.band == "5G" then
			io.write("hw_mode=a\n")
			io.write("ieee80211n=1\n")
			io.write("ieee80211ac=1\n")
			io.write("ieee80211ax=1\n")
			io.write("ieee80211be=1\n")
		elseif dev.band == "6G" then
			io.write("hw_mode=a\n")
			io.write("ieee80211ax=1\n")
			io.write("ieee80211be=1\n")
			io.write("he_6ghz_max_mpdu=0\n")
			io.write("he_6ghz_max_ampdu_len_exp=0\n")
			io.write("he_6ghz_rx_ant_pat=0\n")
			io.write("he_6ghz_tx_ant_pat=0\n")
			io.write("op_class=131\n")
		end

		if iface.beacon_int ~= nil then
			if tonumber(iface.beacon_int) >= 15 and tonumber(iface.beacon_int) <= 65535 then
				io.write("beacon_int=", iface.beacon_int, "\n")
			end
		elseif dev.beacon_int ~= nil then
				if tonumber(dev.beacon_int) >= 15 and tonumber(dev.beacon_int) <= 65535 then
			io.write("beacon_int=", dev.beacon_int, "\n")
			end
		end
	end

	if dev.band == "6G" then
		iface.ieee80211w = "2"
	end


	if iface.dtim_period ~= nil then
		if tonumber(iface.dtim_period) >= 1 and tonumber(iface.dtim_period) <= 255 then
			io.write("dtim_period=", iface.dtim_period, "\n")
		end
	elseif dev.dtim_period ~= nil then
		if tonumber(dev.dtim_period) >= 1 and tonumber(dev.dtim_period) <= 255 then
			io.write("dtim_period=", dev.dtim_period, "\n")
		end
	end

	if iface.hidden ~= nil then
		if iface.hidden == "0" then
			io.write("ignore_broadcast_ssid=0\n")
		elseif iface.hidden == "2" then
			io.write("ignore_broadcast_ssid=2\n")
		else
			io.write("ignore_broadcast_ssid=1\n")
		end
	end

	if iface.dot11vmbssid ~= nil then
		io.write("dot11vmbssid=", iface.dot11vmbssid, "\n")
	end

	io.write("macaddr_acl=0\n")

	if iface.encryption == "none" or
		iface.encryption == nil then
		io.write("auth_algs=1\n")
	elseif iface.encryption == "psk2+tkip+ccmp" then
		io.write("auth_algs=1\n")
		io.write("wpa=2\n")
		io.write("wpa_key_mgmt=WPA-PSK\n")
		io.write("wpa_pairwise=TKIP CCMP\n")
	elseif iface.encryption == "psk2+tkip" then
		io.write("auth_algs=1\n")
		io.write("wpa=2\n")
		io.write("wpa_key_mgmt=WPA-PSK\n")
		io.write("wpa_pairwise=TKIP\n")
	elseif iface.encryption == "psk2+ccmp" then
		io.write("auth_algs=1\n")
		io.write("wpa=2\n")
		if iface.pmf_sha256 == "1" and iface.ieee80211w == "1" then
			io.write("wpa_key_mgmt=WPA-PSK WPA-PSK-SHA256\n")
		elseif iface.ieee80211w == "2" then
			io.write("wpa_key_mgmt=WPA-PSK-SHA256\n")
		else
			io.write("wpa_key_mgmt=WPA-PSK\n")
		end
		io.write("wpa_pairwise=CCMP\n")
	elseif iface.encryption == "psk+tkip+ccmp" then
		io.write("auth_algs=1\n")
		io.write("wpa=1\n")
		io.write("wpa_key_mgmt=WPA-PSK\n")
		io.write("wpa_pairwise=TKIP CCMP\n")
	elseif iface.encryption == "psk+tkip" then
		io.write("auth_algs=1\n")
		io.write("wpa=1\n")
		io.write("wpa_key_mgmt=WPA-PSK\n")
		io.write("wpa_pairwise=TKIP\n")
	elseif iface.encryption == "psk+ccmp" then
		io.write("auth_algs=1\n")
		io.write("wpa=1\n")
		io.write("wpa_key_mgmt=WPA-PSK\n")
		io.write("wpa_pairwise=CCMP\n")
	elseif iface.encryption == "psk-mixed+tkip+ccmp" then
		io.write("auth_algs=1\n")
		io.write("wpa=3\n")
		io.write("wpa_key_mgmt=WPA-PSK\n")
		io.write("wpa_pairwise=TKIP CCMP\n")
	elseif iface.encryption == "psk-mixed+tkip" then
		io.write("auth_algs=1\n")
		io.write("wpa=3\n")
		io.write("wpa_key_mgmt=WPA-PSK\n")
		io.write("wpa_pairwise=TKIP\n")
	elseif iface.encryption == "psk-mixed+ccmp" then
		io.write("auth_algs=1\n")
		io.write("wpa=3\n")
		io.write("wpa_key_mgmt=WPA-PSK\n")
		io.write("wpa_pairwise=CCMP\n")
--[[
	elseif iface.encryption == "wep" or
	       iface.encryption == "wep+open" then
		io.write("auth_algs=1\n")
		io.write("wpa=0\n")
	elseif iface.encryption == "wep+shared" then
		io.write("auth_algs=2\n")
		io.write("wpa=0\n")
	elseif iface.encryption == "wep+auto" then
		io.write("auth_algs=3\n")
		io.write("wpa=0\n")
]]
	elseif iface.encryption == "wpa3" then
		io.write("auth_algs=1\n")
		io.write("wpa=2\n")
		io.write("wpa_key_mgmt=WPA-EAP-SHA256\n")
		io.write("wpa_pairwise=CCMP\n")
		iface.ieee80211w = "2"
	elseif iface.encryption == "wpa3-192" then
		io.write("auth_algs=1\n")
		io.write("wpa=2\n")
		io.write("wpa_key_mgmt=WPA-EAP-SUITE-B-192\n")
		io.write("wpa_pairwise=GCMP-256\n")
		io.write("group_cipher=GCMP-256\n")
		io.write("group_mgmt_cipher=BIP-GMAC-256\n")
		iface.ieee80211w = "2"
	elseif iface.encryption == "wpa3-mixed" then
		io.write("auth_algs=1\n")
		io.write("wpa=2\n")
		io.write("wpa_key_mgmt=WPA-EAP WPA-EAP-SUITE-B-192\n")
		io.write("wpa_pairwise=CCMP\n")
		iface.ieee80211w = "1"
	elseif iface.encryption == "wpa2+tkip+ccmp" then
		io.write("auth_algs=1\n")
		io.write("wpa=2\n")
		io.write("wpa_key_mgmt=WPA-EAP\n")
		io.write("wpa_pairwise=TKIP CCMP\n")
	elseif iface.encryption == "wpa2+ccmp" then
		io.write("auth_algs=1\n")
		io.write("wpa=2\n")
		if iface.pmf_sha256 == "1" and iface.ieee80211w == "1" then
			io.write("wpa_key_mgmt=WPA-EAP WPA-EAP-SHA256\n")
		elseif iface.ieee80211w == "2" then
			io.write("wpa_key_mgmt=WPA-EAP-SHA256\n")
		else
			io.write("wpa_key_mgmt=WPA-EAP\n")
		end
		io.write("wpa_pairwise=CCMP\n")
	elseif iface.encryption == "wpa2+tkip" then
		io.write("auth_algs=1\n")
		io.write("wpa=2\n")
		io.write("wpa_key_mgmt=WPA-EAP\n")
		io.write("wpa_pairwise=TKIP\n")
	elseif iface.encryption == "wpa+tkip+ccmp" then
		io.write("auth_algs=1\n")
		io.write("wpa=1\n")
		io.write("wpa_key_mgmt=WPA-EAP\n")
		io.write("wpa_pairwise=TKIP CCMP\n")
	elseif iface.encryption == "wpa+ccmp" then
		io.write("auth_algs=1\n")
		io.write("wpa=1\n")
		io.write("wpa_key_mgmt=WPA-EAP\n")
		io.write("wpa_pairwise=CCMP\n")
	elseif iface.encryption == "wpa+tkip" then
		io.write("auth_algs=1\n")
		io.write("wpa=1\n")
		io.write("wpa_key_mgmt=WPA-EAP\n")
		io.write("wpa_pairwise=TKIP\n")
	elseif iface.encryption == "wpa-mixed+tkip+ccmp" then
		io.write("auth_algs=1\n")
		io.write("wpa=3\n")
		io.write("wpa_key_mgmt=WPA-EAP\n")
		io.write("wpa_pairwise=TKIP CCMP\n")
	elseif iface.encryption == "wpa-mixed+tkip" then
		io.write("auth_algs=1\n")
		io.write("wpa=3\n")
		io.write("wpa_key_mgmt=WPA-EAP\n")
		io.write("wpa_pairwise=TKIP\n")
	elseif iface.encryption == "wpa-mixed+ccmp" then
		io.write("auth_algs=1\n")
		io.write("wpa=3\n")
		io.write("wpa_key_mgmt=WPA-EAP\n")
		io.write("wpa_pairwise=CCMP\n")
	elseif iface.encryption == "sae" then
		io.write("auth_algs=1\n")
		io.write("wpa=2\n")
		io.write("wpa_key_mgmt=SAE\n")
		io.write("rsn_pairwise=CCMP\n")
		iface.ieee80211w = "2"
	elseif iface.encryption == "sae+ccmp" then
		io.write("auth_algs=1\n")
		io.write("wpa=2\n")
		io.write("wpa_key_mgmt=SAE\n")
		io.write("rsn_pairwise=CCMP\n")
		io.write("group_cipher=CCMP\n")
		iface.ieee80211w = "2"
	elseif iface.encryption == "sae+gcmp" then
		io.write("auth_algs=1\n")
		io.write("wpa=2\n")
		io.write("wpa_key_mgmt=SAE\n")
		io.write("rsn_pairwise=GCMP\n")
		io.write("group_cipher=GCMP\n")
		iface.ieee80211w = "2"
	elseif iface.encryption == "sae+ccmp256" then
		io.write("auth_algs=1\n")
		io.write("wpa=2\n")
		io.write("wpa_key_mgmt=SAE\n")
		io.write("rsn_pairwise=CCMP-256\n")
		io.write("group_cipher=CCMP-256\n")
		iface.ieee80211w = "2"
	elseif iface.encryption == "sae+gcmp256" then
		io.write("auth_algs=1\n")
		io.write("wpa=2\n")
		io.write("wpa_key_mgmt=SAE\n")
		io.write("rsn_pairwise=GCMP-256\n")
		io.write("group_cipher=GCMP-256\n")
		io.write("group_mgmt_cipher=BIP-GMAC-256\n")
		iface.ieee80211w = "2"
	elseif iface.encryption == "sae-mixed" then
		io.write("auth_algs=1\n")
		io.write("wpa=2\n")
		io.write("wpa_key_mgmt=SAE WPA-PSK\n")
		io.write("rsn_pairwise=CCMP\n")
		iface.wps_cred_add_sae="1"
		iface.ieee80211w = "1"
		if iface.sae_require_mfp == nil then
			iface.sae_require_mfp = 1
		end
	elseif iface.encryption == "sae-ext" then
		io.write("auth_algs=1\n")
		io.write("wpa=2\n")
		io.write("wpa_key_mgmt=SAE-EXT-KEY\n")
		io.write("rsn_pairwise=CCMP GCMP-256\n")
		iface.ieee80211w = "2"
	elseif iface.encryption == "owe" then
		io.write("auth_algs=1\n")
		io.write("wpa=2\n")
		io.write("wpa_key_mgmt=OWE\n")
		io.write("wpa_pairwise=CCMP\n")
		iface.ieee80211w = "2"
	elseif iface.encryption == "dpp" then
		io.write("wpa=2\n")
		io.write("wpa_key_mgmt=DPP\n")
		io.write("wpa_pairwise=CCMP\n")
		io.write("rsn_pairwise=CCMP\n")
	elseif iface.encryption == "sae-dpp" then
		io.write("auth_algs=1\n")
		io.write("wpa=2\n")
		io.write("wpa_key_mgmt=SAE DPP\n")
		io.write("wpa_pairwise=CCMP\n")
		io.write("rsn_pairwise=CCMP\n")
	elseif iface.encryption == "psk2-dpp" then
		io.write("auth_algs=1\n")
		io.write("wpa=2\n")
		io.write("wpa_key_mgmt=WPA-PSK DPP\n")
		io.write("wpa_pairwise=CCMP\n")
		io.write("rsn_pairwise=CCMP\n")
	elseif iface.encryption == "psk2-sae-dpp" then
		io.write("auth_algs=1\n")
		io.write("wpa=2\n")
		io.write("wpa_key_mgmt=WPA-PSK SAE DPP\n")
		io.write("wpa_pairwise=CCMP\n")
		io.write("rsn_pairwise=CCMP\n")
	end
	if string.find(iface.encryption, "dpp") ~= nil then
		if iface.dpp_pfs ~= nil and
			tonumber(iface.dpp_pfs) >= 0 and
			tonumber(iface.dpp_pfs) <= 2 then
			io.write("dpp_pfs="..iface.dpp_pfs.."\n")
		end
		if iface.dpp_connector ~= nil then
			io.write("dpp_connector="..iface.dpp_connector.."\n")
		end
		if iface.dpp_connector ~= nil then
			io.write("dpp_csign="..iface.dpp_csign.."\n")
		end
		if iface.dpp_netaccesskey ~= nil then
			io.write("dpp_netaccesskey="..iface.dpp_netaccesskey.."\n")
		end
	end
	if string.find(iface.encryption, "wpa") ~= nil then
		iface.ieee8021x = "1"
	end

	if iface.sae_pwe ~= nil and
		tonumber(iface.sae_pwe) >= 0 and
		tonumber(iface.sae_pwe) <= 2 then
		io.write("sae_pwe="..iface.sae_pwe.."\n")
	else
		io.write("sae_pwe=2\n")
	end

	if iface.sae_require_mfp ~= nil then
		io.write("sae_require_mfp="..iface.sae_require_mfp.."\n")
	end

	io.write("wmm_enabled=1\n")

	if iface.ieee80211w ~= nil and iface.encryption ~= "none" then
		if tonumber(iface.ieee80211w) >= 0 and tonumber(iface.ieee80211w) <= 2 then
			io.write("ieee80211w=", iface.ieee80211w, "\n")
			if tonumber(iface.ieee80211w) >= 1 and tonumber(iface.ieee80211w) <= 2 then
				if iface.beacon_prot ~= nil and iface.beacon_prot == "1" then
					io.write("beacon_prot=1\n")
				end
				if iface.ocv ~= nil and tonumber(iface.ocv) >= 0 and tonumber(iface.ocv) <= 2 then
					io.write("ocv=", iface.ocv, "\n")
				end
			end
		end
	end

--[[
	function prepare_key_wep(key)
		if key == nil or #key == 0 then
			return ""
		end

		local len = #key

		if (len == 10 or len == 26 or len == 32) and key == string.match(key, '%x+') then
			return key
		elseif (len == 5 or len == 13 or len == 16) then
			return "\""..key.."\""
		end

		return ""
	end

	if string.find(iface.encryption, "wep") ~= nil then
		if string.match(iface.key, "^[1-4]$") then
			local i
			for i = 0, 3 do
				local key = iface['key'..tostring(i+1)]
				io.write("wep_key", tostring(i), "=", prepare_key_wep(key), "\n")
			end
			local idx = tonumber(iface.key)
			io.write("wep_default_key=", tostring(idx - 1), "\n")
		else
			io.write("wep_key0=", prepare_key_wep(iface.key), "\n")
			io.write("wep_default_key=0\n")
		end
	end
]]

	if string.find(iface.encryption, "wpa") ~= nil or
	   string.find(iface.encryption, "psk") ~= nil then
		if iface.key ~= nil then
			io.write("wpa_passphrase=", iface.key, "\n")
		end
	elseif string.find(iface.encryption, "sae") ~= nil then
		if iface.sae_password ~= nil then
			io.write("sae_password=", iface.sae_password, "\n")
		elseif iface.key ~= nil then
			io.write("wpa_passphrase=", iface.key, "\n")
		end
	end

	if iface.rekey_interval ~= nil then
		io.write("wpa_group_rekey=", tostring(iface.rekey_interval), "\n")
	end

	if iface.ieee8021x ~= nil and
	   iface.ieee8021x ~= '0' then
		io.write("ieee8021x=", tostring(iface.ieee8021x), "\n")
	end

	if iface.auth_server ~= nil and
	   iface.auth_server ~= '0' then
		io.write("auth_server_addr=", iface.auth_server, "\n")

		if iface.auth_port ~= nil and
		   iface.auth_port ~= '0' then
			io.write("auth_server_port=", iface.auth_port, "\n")
		else
			io.write("auth_server_port=1812\n")
		end

		if iface.auth_secret ~= nil then
			io.write("auth_server_shared_secret=", iface.auth_secret, "\n")
		end
	end

	if iface.acct_server ~= nil and
	   iface.acct_server ~= '0' then
		io.write("acct_server_addr=", iface.acct_server, "\n")

		if iface.acct_port ~= nil and
		   iface.acct_port ~= '0' then
			io.write("acct_server_port=", iface.acct_port, "\n")
		else
			io.write("acct_server_port=1813\n")
		end

		if iface.acct_secret ~= nil then
			io.write("acct_server_shared_secret=", iface.acct_secret, "\n")
		end

		if iface.acct_interim_interval ~= nil then
			io.write("radius_acct_interim_interval=", iface.acct_interim_interval, "\n")
		end
	end

	io.write("ctrl_interface="..var_hostapd_path.."\n")
	io.write("nas_identifier=ap.mtk.com\n")
	io.write("use_driver_iface_addr=1\n")
	io.write("friendly_name=WPS Access Point\n")
	io.write("model_name=MediaTek Wireless Access Point\n")
	io.write("model_number=MT7988\n")
	io.write("serial_number=12345678\n")
	io.write("os_version=80000000\n")

	if string.find(iface.encryption, "psk") ~= nil or
	   string.find(iface.encryption, "none") ~= nil or
	   string.find(iface.encryption, "sae") ~= nil then
		if iface.wps_pin ~= nil then
			io.write("ap_pin=", iface.wps_pin, "\n")
		end
		if iface.ext_registrar ~= nil and iface.ext_registrar == "0" then
			io.write("ap_setup_locked=1\n")
		end

		local wps_config_methods = {"display", "virtual_push_button", "keypad"}
		if iface.wps_label ~= nil then
			if iface.wps_label == "0" then
				for i, config_method in pairs(wps_config_methods) do
					if config_method == "label" then
						table.remove(wps_config_methods, i)
					end
				end
			elseif iface.wps_label == "1" then
				table.insert(wps_config_methods, "label")
			end
		end
		if iface.wps_pushbutton ~= nil then
			if iface.wps_pushbutton == "0" then
				for i, config_method in pairs(wps_config_methods) do
					if config_method == "virtual_push_button" then
						table.remove(wps_config_methods, i)
					end
				end
				for i, config_method in pairs(wps_config_methods) do
					if config_method == "physical_push_button" then
						table.remove(wps_config_methods, i)
					end
			end
			elseif iface.wps_pushbutton == "1" then
				table.insert(wps_config_methods, "physical_push_button")
			end
		end
		if wps_config_methods ~= nil then
			local wps_config_methods_str
			for i, config_method in pairs(wps_config_methods) do
				if wps_config_methods_str then
					wps_config_methods_str = wps_config_methods_str.." "..config_method
				else
					wps_config_methods_str = config_method
				end
    			end
			if wps_config_methods_str then
				io.write("config_methods=", wps_config_methods_str, "\n")
			end
		end
		io.write("eapol_key_index_workaround=0\n")
		io.write("eapol_version=2\n")
		io.write("eap_server=1\n")
		iface.wps_independent = "1"
	elseif string.find(iface.encryption, "wpa") ~= nil then
		io.write("eap_server=0\n")
		iface.wps_independent = nil
		iface.wps_state = nil
	end

	if iface.wps_device_name ~= nil then
		io.write("device_name=", iface.wps_device_name, "\n")
	else
		io.write("device_name=Wireless AP\n")
	end

	if iface.wps_device_type ~= nil then
		io.write("device_type=", iface.wps_device_type, "\n")
	else
		io.write("device_type=6-0050F204-1\n")
	end

	if iface.wps_manufacturer ~= nil then
		io.write("manufacturer=", iface.wps_manufacturer, "\n")
	else
		io.write("manufacturer=MediaTek Inc.\n")
	end

	if dev.band == "2.4G" then
		io.write("wps_rf_bands=b\n")
	elseif dev.band == "5G" then
		io.write("wps_rf_bands=a\n")
	end

	if iface.wps_state ~= nil then
		io.write("wps_state=", iface.wps_state, "\n")
		if iface.wps_state == "1" then
			io.write("upnp_iface=br-", iface.network, "\n")
		end
	else
		io.write("wps_state=0\n")
	end

	if iface.wps_cred_add_sae ~= nil then
		io.write("wps_cred_add_sae=", iface.wps_cred_add_sae, "\n")
	end

	if iface.wps_independent ~= nil then
		io.write("wps_independent=", iface.wps_independent, "\n")
	end

	if iface.dpp_pfs ~= nil then
		io.write("dpp_pfs=", iface.dpp_pfs, "\n")
	end

	if iface.multi_ap ~= nil then
		io.write("multi_ap=", iface.multi_ap, "\n")
	end

	if iface.multi_ap_backhaul_ssid ~= nil then
		io.write("multi_ap_backhaul_ssid=\"", iface.multi_ap_backhaul_ssid, "\"\n")
	end

	if iface.multi_ap_backhaul_wpa_passphrase ~= nil then
		io.write("multi_ap_backhaul_wpa_passphrase=", iface.multi_ap_backhaul_wpa_passphrase, "\n")
	end

	if iface.uuid ~= nil then
		io.write("uuid=", iface.uuid, "\n")
	end

	if iface.interworking ~= nil then
		io.write("interworking=", iface.interworking, "\n")
	end

	if iface.mbo ~= nil and iface.mbo == "1" then
		io.write("mbo=1\n")
	else
		io.write("mbo=0\n")
	end

	if iface.oce ~= nil and tonumber(iface.oce) >= 0 and tonumber(iface.oce) <= 7 then
		io.write("oce=", iface.oce, "\n")
	else
		io.write("oce=0\n")
	end

	io.close()
	if dev.map_mode ~= "0" and dev.map_mode ~= "2" then
		os.execute("sed -i '/wps_state=/d' "..file_name)
		os.execute("sed -i '/wps_cred_add_sae=/d' "..file_name)
		os.execute("sed -i '/wps_independent=/d' "..file_name)
		os.execute("sed -i '/mbo=/d' "..file_name)
		os.execute("sed -i '/dpp_pfs=/d' "..file_name)
		os.execute("sed -i '/interworking=/d' "..file_name)
		os.execute("sed -i '/wpa_key_mgmt=/d' "..file_name)
		os.execute("sed -i '/auth_algs=/d' "..file_name)
		os.execute("sed -i '/wpa=/d' "..file_name)
		os.execute("sed -i '/rsn_pairwise=/d' "..file_name)
		os.execute("sed -i '/wpa_pairwise/d' "..file_name)
		os.execute("sed -i '/wep_default_key/d' "..file_name)
		os.execute("sed -i '/wep_key1=/d' "..file_name)
		os.execute("sed -i '/wpa_passphrase=/d' "..file_name)
		os.execute("sed -i '/sae_password=/d' "..file_name)
		os.execute("sed -i '/ignore_broadcast_ssid=/d' "..file_name)
		os.execute("sed -i '/ieee80211w=/d' "..file_name)
		os.execute("sed -i '/wps_rf_bands=/d' "..file_name)
		os.execute("sed -i '/eapol_key_index_workaround=/d' "..file_name)
		os.execute("sed -i '/eapol_version=/d' "..file_name)
		os.execute("sed -i '/eap_server=/d' "..file_name)
		file = io.open(file_name, "a+")
		io.output(file)
		if iface.multi_ap ~= nil then
			io.write("multi_ap=", iface.multi_ap, "\n")
		end
		if iface.wps_state ~= nil then
			io.write("wps_state=", iface.wps_state, "\n")
		else
			io.write("wps_state=2\n")
		end
		if iface.wps_cred_add_sae ~= nil then
			io.write("wps_cred_add_sae=", iface.wps_cred_add_sae, "\n")
		else
			io.write("wps_cred_add_sae=1\n")
		end
		io.write("wps_independent=0\n")
		if iface.mbo ~= nil then
			io.write("mbo=", iface.mbo, "\n")
		else
			io.write("mbo=1\n")
		end
		if iface.dpp_pfs ~= nil then
			io.write("dpp_pfs=", iface.dpp_pfs, "\n")
		else
			io.write("dpp_pfs=2\n")
		end
		if iface.interworking ~= nil then
			io.write("interworking=", iface.interworking, "\n")
		else
			io.write("interworking=1\n")
		end
		if iface.multi_ap_backhaul_ssid ~= nil then
			io.write("multi_ap_backhaul_ssid=\"", iface.multi_ap_backhaul_ssid, "\"\n")
		end
		if iface.multi_ap_backhaul_wpa_passphrase ~= nil then
			io.write("multi_ap_backhaul_wpa_passphrase=", iface.multi_ap_backhaul_wpa_passphrase, "\n")
		end
		if iface.map_wpa_key_mgmt ~= nil then
			io.write("wpa_key_mgmt=", iface.map_wpa_key_mgmt, "\n")
		else
			io.write("wpa_key_mgmt=SAE\n")
		end
		if iface.map_auth_algs ~= nil then
			io.write("auth_algs=", iface.map_auth_algs, "\n")
		else
			io.write("auth_algs=1\n")
		end
		if iface.map_wpa ~= nil then
			io.write("wpa=", iface.map_wpa, "\n")
		else
			io.write("wpa=2\n")
		end
		if iface.map_rsn_pairwise ~= nil then
			io.write("rsn_pairwise=", iface.map_rsn_pairwise, "\n")
		else
			io.write("rsn_pairwise=CCMP\n")
		end
		if iface.map_wpa_pairwise ~= nil then
			io.write("wpa_pairwise=", iface.map_wpa_pairwise, "\n")
		end
		if iface.map_wep_default_key ~= nil then
			io.write("wep_default_key=", iface.map_wep_default_key, "\n")
		end
		if iface.map_wep_key1 ~= nil then
			io.write("wep_key1=", iface.map_wep_key1, "\n")
		end
		if iface.map_wpa_passphrase ~= nil then
			io.write("wpa_passphrase=", iface.map_wpa_passphrase, "\n")
		end
		if iface.sae_pwe ~= nil then
			io.write("sae_pwe=", iface.sae_pwe, "\n")
		end
		if iface.map_sae_password ~= nil then
			io.write("sae_password=", iface.map_sae_password, "\n")
		end
		if iface.map_ignore_broadcast_ssid ~= nil then
			io.write("ignore_broadcast_ssid=", iface.map_ignore_broadcast_ssid, "\n")
		end
		if dev.band ~= "6G" then
			io.write("ieee80211w=1\n")
		else
			io.write("ieee80211w=2\n")
		end
		if iface.uuid ~= nil then
			io.write("uuid=", iface.uuid, "\n")
		end
		if dev.band ~= "6G" then
			io.write("wps_rf_bands=ag\n")
		end
		io.write("eapol_key_index_workaround=0\n")
		io.write("eapol_version=2\n")
		io.write("eap_server=1\n")
		io.close()
	end
end


function hostapd_enable_vif(phy, iface)
	local file_name = var_hostapd_path.."hostapd-"..iface[".name"]..".conf"
	local action_wps_er_pid = var_path.."action-"..iface[".name"].."-wps-er.pid"
	local action_wps_er_script = "/lib/wifi/hostapd_wps_er_action.lua"

	os.execute(hostapd_cli.." -p "..var_hostapd_path.." -i global raw ADD bss_config="..phy..":"..file_name)
	if iface.wps_state == "1" then
		os.execute("exec 1000>&- && "..hostapd_cli.." -i "..iface[".name"].." -a "..action_wps_er_script.." -B -P "..action_wps_er_pid)
	end
end


function hostapd_disable_vif(iface)
	local file_name = var_hostapd_path.."hostapd-"..iface..".conf"
	local action_wps_er_pid = var_path.."action-"..iface.."-wps-er.pid"

	os.execute("[ -f "..action_wps_er_pid.." ] && kill -SIGTERM `cat "..action_wps_er_pid.."` 2>/dev/null")
	os.execute(hostapd_cli.." -p "..var_hostapd_path.." -i global raw REMOVE "..iface)
	os.remove(action_wps_er_pid)
	os.remove(file_name)
end

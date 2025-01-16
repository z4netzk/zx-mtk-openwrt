#!/usr/bin/env lua

local var_path = "/var/run/"
local var_supplicant_path = var_path.."wpa_supplicant/"
local wpa_cli = "/usr/sbin/wpa_cli"

function supp_setup_vif(dev, iface)
	local file_name = var_supplicant_path.."wpa_supplicant-"..iface[".name"]..".conf"
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
	io.write("ctrl_interface="..var_supplicant_path.."\n")
	io.write("update_config=1\n\n")
	io.write("bss_expiration_scan_count=1\n\n")

	if iface.dpp_config_processing ~= nil then
		io.write("dpp_config_processing="..iface.dpp_config_processing.."\n")
	end

	if iface.wps_cred_processing ~= nil then
		io.write("wps_cred_processing="..iface.wps_cred_processing.."\n")
	end

	if iface.pmf ~= nil then
		io.write("pmf=", iface.pmf, "\n")
	end

	if iface.wps_device_name ~= nil then
		io.write("device_name=", iface.wps_device_name, "\n")
	else
		io.write("device_name=Wireless station\n")
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
	io.write("model_name=MediaTek Wireless Access Point\n")
	io.write("model_number=MT7988\n")
	io.write("serial_number=12345678\n")
	io.write("config_methods=display virtual_push_button keypad physical_push_button\n")

	if iface.wps_cred_processing ~= nil then
		io.write("wps_cred_processing=", iface.wps_cred_processing, "\n")
	end

	if iface.wps_cred_add_sae ~= nil then
		io.write("wps_cred_add_sae=", iface.wps_cred_add_sae, "\n")
	end

	if iface.pmf ~= nil then
		io.write("pmf=", iface.pmf, "\n")
	end

	if iface.sae_pwe ~= nil then
		io.write("sae_pwe=", iface.sae_pwe, "\n")
	else
		io.write("sae_pwe=2\n")
	end

	if dev.map_mode ~= "0" then
		if iface.wps_cred_processing ~= nil then
			io.write("wps_cred_processing=", iface.wps_cred_processing, "\n")
		else
			io.write("wps_cred_processing=1\n")
		end
		if iface.wps_cred_add_sae ~= nil then
			io.write("wps_cred_add_sae=", iface.wps_cred_add_sae, "\n")
		else
			io.write("wps_cred_add_sae=1\n")
		end
		if iface.pmf ~= nil then
			io.write("pmf=", iface.pmf, "\n")
		else
			io.write("pmf=2\n")
		end
		if iface.sae_pwe ~= nil then
			io.write("sae_pwe=", iface.sae_pwe, "\n")
		else
			io.write("sae_pwe=2\n")
		end
	end

	if iface.ssid == nil or iface.ssid == "" then
		io.close()
		return
	end

	if iface.autoscan_interval == nil then
		io.write("autoscan=periodic:30\n")
	elseif iface.autoscan_interval ~= "0" then
		io.write("autoscan=periodic:", iface.autoscan_interval, "\n")
	end

	io.write("network={\n")
	io.write("\tssid=\""..iface.ssid.."\"\n")
	io.write("\tscan_ssid=1\n")

	if iface.encryption == "none" or
		iface.encryption == nil then
		io.write("\tkey_mgmt=NONE\n")
	elseif iface.encryption == "psk+tkip+ccmp" then
		io.write("\tkey_mgmt=WPA-PSK\n")
		io.write("\tproto=WPA\n")
		io.write("\tpairwise=CCMP TKIP\n")
	elseif iface.encryption == "psk+tkip" then
		io.write("\tkey_mgmt=WPA-PSK\n")
		io.write("\tproto=WPA\n")
		io.write("\tpairwise=TKIP\n")
	elseif iface.encryption == "psk+ccmp" then
		io.write("\tkey_mgmt=WPA-PSK\n")
		io.write("\tproto=WPA\n")
		io.write("\tpairwise=CCMP\n")
	elseif iface.encryption == "psk2+tkip+ccmp" then
		io.write("\tkey_mgmt=WPA-PSK\n")
		io.write("\tproto=RSN\n")
		io.write("\tpairwise=CCMP TKIP\n")
	elseif iface.encryption == "psk2+tkip" then
		io.write("\tkey_mgmt=WPA-PSK\n")
		io.write("\tproto=RSN\n")
		io.write("\tpairwise=TKIP\n")
	elseif iface.encryption == "psk2+ccmp" then
		if iface.pmf_sha256 == "1" and iface.ieee80211w == "1" then
			io.write("\tkey_mgmt=WPA-PSK WPA-PSK-SHA256\n")
		elseif iface.pmf_sha256 == "1" and iface.ieee80211w == "2" then
			io.write("\tkey_mgmt=WPA-PSK-SHA256\n")
		else
			io.write("\tkey_mgmt=WPA-PSK\n")
		end
		io.write("\tproto=RSN\n")
		io.write("\tpairwise=CCMP\n")
	elseif iface.encryption == "psk2-mixed+tkip+ccmp" then
		io.write("\tkey_mgmt=WPA-PSK\n")
		io.write("\tproto=WPA RSN\n")
		io.write("\tpairwise=CCMP TKIP\n")
	elseif iface.encryption == "psk2-mixed+tkip" then
		io.write("\tkey_mgmt=WPA-PSK\n")
		io.write("\tproto=WPA RSN\n")
		io.write("\tpairwise=TKIP\n")
	elseif iface.encryption == "psk2-mixed+ccmp" then
		io.write("\tkey_mgmt=WPA-PSK\n")
		io.write("\tproto=WPA RSN\n")
		io.write("\tpairwise=CCMP\n")
	elseif iface.encryption == "psk-mixed+tkip+ccmp" then
		io.write("\tkey_mgmt=WPA-PSK\n")
		io.write("\tproto=RSN\n")
		io.write("\tpairwise=CCMP TKIP\n")
	elseif iface.encryption == "sae" then
		io.write("\tkey_mgmt=SAE\n")
		io.write("\tproto=RSN\n")
		io.write("\tpairwise=CCMP\n")
		io.write("\tgroup=CCMP\n")
		iface.ieee80211w = "2"
	elseif iface.encryption == "sae+ccmp" then
		io.write("\tkey_mgmt=SAE\n")
		io.write("\tproto=RSN\n")
		io.write("\tpairwise=CCMP\n")
		io.write("\tgroup=CCMP\n")
--		io.write("\tgroup_mgmt=BIP-GMAC-256\n")
		iface.ieee80211w = "2"
	elseif iface.encryption == "sae+gcmp" then
		io.write("\tkey_mgmt=SAE\n")
		io.write("\tproto=RSN\n")
		io.write("\tpairwise=GCMP\n")
		io.write("\tgroup=GCMP\n")
--		io.write("\tgroup_mgmt=BIP-GMAC-256\n")
		iface.ieee80211w = "2"
	elseif iface.encryption == "sae+ccmp256" then
		io.write("\tkey_mgmt=SAE\n")
		io.write("\tproto=RSN\n")
		io.write("\tpairwise=CCMP-256\n")
		io.write("\tgroup=CCMP-256\n")
--		io.write("\tgroup_mgmt=BIP-GMAC-256\n")
		iface.ieee80211w = "2"
	elseif iface.encryption == "sae+gcmp256" then
		io.write("\tkey_mgmt=SAE\n")
		io.write("\tproto=RSN\n")
		io.write("\tpairwise=GCMP-256\n")
		io.write("\tgroup=GCMP-256\n")
--		io.write("\tgroup_mgmt=BIP-GMAC-256\n")
		iface.ieee80211w = "2"
	elseif iface.encryption == "sae-mixed" then
		io.write("\tkey_mgmt=WPA-PSK SAE\n")
		io.write("\tproto=RSN\n")
		io.write("\tpairwise=CCMP\n")
		io.write("\tgroup=CCMP\n")
		iface.ieee80211w = "1"
	elseif iface.encryption == "sae-ext" then
		io.write("\tkey_mgmt=SAE-EXT-KEY\n")
		io.write("\tproto=RSN\n")
		io.write("\tpairwise=CCMP GCMP-256\n")
		io.write("\tgroup=CCMP GCMP-256\n")
		iface.ieee80211w = "2"
	elseif iface.encryption == "wpa+tkip" then
		io.write("\tkey_mgmt=WPA-EAP\n")
		io.write("\tproto=WPA\n")
		io.write("\tpairwise=TKIP\n")
	elseif iface.encryption == "wpa+ccmp" then
		io.write("\tkey_mgmt=WPA-EAP\n")
		io.write("\tproto=WPA\n")
		io.write("\tpairwise=CCMP\n")
	elseif iface.encryption == "wpa+tkip+ccmp" then
		io.write("\tkey_mgmt=WPA-EAP\n")
		io.write("\tproto=WPA\n")
		io.write("\tpairwise=TKIP CCMP\n")
	elseif iface.encryption == "wpa2+tkip" then
		io.write("\tkey_mgmt=WPA-EAP\n")
		io.write("\tproto=RSN\n")
		io.write("\tpairwise=TKIP\n")
	elseif iface.encryption == "wpa2+ccmp" then
		if iface.pmf_sha256 == "1" and iface.ieee80211w == "1" then
			io.write("\tkey_mgmt=WPA-EAP WPA-EAP-SHA256\n")
		elseif iface.pmf_sha256 == "1" and iface.ieee80211w == "2" then
			io.write("\tkey_mgmt=WPA-EAP-SHA256\n")
		else
			io.write("\tkey_mgmt=WPA-EAP\n")
		end
		io.write("\tproto=RSN\n")
		io.write("\tpairwise=CCMP\n")
	elseif iface.encryption == "wpa2+tkip+ccmp" then
		io.write("\tkey_mgmt=WPA-EAP\n")
		io.write("\tproto=RSN\n")
		io.write("\tpairwise=TKIP CCMP\n")
	elseif iface.encryption == "wpa-mixed+tkip" then
		io.write("\tkey_mgmt=WPA-EAP\n")
		io.write("\tproto=WPA RSN\n")
		io.write("\tpairwise=TKIP\n")
	elseif iface.encryption == "wpa-mixed+ccmp" then
		io.write("\tkey_mgmt=WPA-EAP\n")
		io.write("\tproto=WPA RSN\n")
		io.write("\tpairwise=CCMP\n")
	elseif iface.encryption == "wpa-mixed+tkip+ccmp" then
		io.write("\tkey_mgmt=WPA-EAP\n")
		io.write("\tproto=WPA RSN\n")
		io.write("\tpairwise=TKIP CCMP\n")
        elseif iface.encryption == "wpa3" then
		io.write("\tkey_mgmt=WPA-EAP-SHA256\n")
		io.write("\tproto=RSN\n")
		io.write("\tpairwise=CCMP\n")
		io.write("\tgroup=CCMP\n")
		iface.ieee80211w = "2"
	elseif iface.encryption == "wpa3-192" then
		io.write("\tkey_mgmt=WPA-EAP-SUITE-B-192\n")
		io.write("\tproto=RSN\n")
		io.write("\tpairwise=GCMP-256\n")
		io.write("\tgroup=GCMP-256\n")
		io.write("\tgroup_mgmt=BIP-GMAC-256\n")
		iface.ieee80211w = "2"
	elseif iface.encryption == "owe" then
		io.write("\tkey_mgmt=OWE\n")
		io.write("\tproto=RSN\n")
	elseif iface.encryption == "dpp" then
		io.write("\tkey_mgmt=DPP\n")
--[[
	elseif iface.encryption == "wep" or
	       iface.encryption == "wep+open" then
		io.write("\tkey_mgmt=NONE\n")
		io.write("\tauth_alg=OPEN\n")
	elseif iface.encryption == "wep+shared" then
		io.write("\tkey_mgmt=NONE\n")
		io.write("\tauth_alg=SHARED\n")
	elseif iface.encryption == "wep+auto" then
		io.write("\tkey_mgmt=NONE\n")
]]
	end

	if iface.ieee80211w ~= nil and iface.encryption ~= "none" then
		if tonumber(iface.ieee80211w) >= 0 and tonumber(iface.ieee80211w) <= 2 then
			io.write("\tieee80211w=", iface.ieee80211w, "\n")
			if tonumber(iface.ieee80211w) >= 1 and tonumber(iface.ieee80211w) <= 2 then
				if iface.beacon_prot ~= nil and iface.beacon_prot == "1" then
					io.write("\tbeacon_prot=1\n")
				end
				if iface.ocv ~= nil and iface.ocv == "1" then
					io.write("\tocv=1\n")
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
		for i = 0, 3 do
			key = iface['key'..tostring(i + 1)]
			if key and key ~= '' then
				io.write("\twep_key", tostring(i), "=", prepare_key_wep(key), "\n")
			end
		end
		if iface.key == nil then
			io.write("\twep_tx_keyidx=0\n")
		else
			local idx = tonumber(iface.key)
			idx = idx - 1
			io.write("\twep_tx_keyidx=", tostring(idx), "\n")
		end
	end
]]

	if string.find(iface.encryption, "wpa") ~= nil then
		if iface.identity ~= nil then
			io.write("\tidentity=\"", iface.identity, "\"\n")
		end

		if iface.client_cert ~= nil then
			io.write("\tclient_cert=\"", iface.client_cert, "\"\n")
		end

		if iface.ca_cert ~= nil then
			io.write("\tca_cert=\"", iface.ca_cert, "\"\n")
		end

		if iface.client_cert ~= nil then
			io.write("\tclient_cert=\"", iface.client_cert, "\"\n")
		end

		if iface.priv_key ~= nil then
			io.write("\tprivate_key=\"", iface.private_key, "\"\n")
		end

		if iface.priv_key_pwd ~= nil then
			io.write("\tprivate_key_passwd=\"", iface.private_key_pwd, "\"\n")
		end

		if iface.eap_type ~= nil then
			io.write("\teap=", string.upper(iface.eap_type), "\n")
			if iface.eap_type == "peap" or
			   iface.eap_type == "ttls" then
				if iface.phase1 ~= nil then
					io.write("\tphase1=\"", iface.phase1, "\"\n")
				end

				if iface.auth ~= nil then
					io.write("\tphase2=\"auth=", iface.auth, "\"\n")
				else
					io.write("\tphase2=\"auth=MSCHAPV2\"\n")
				end

				if iface.password ~= nil then
					io.write("\tpassword=\"", iface.password, "\"\n")
				end
			end
		end
	end
	if string.find(iface.encryption, "sae") ~= nil then
		if iface.sae_password then
			io.write("\tsae_password=\""..iface.sae_password.."\"\n")
		end
	end
	if string.find(iface.encryption, "psk") ~= nil or
	   string.find(iface.encryption, "sae") ~= nil then
		if iface.key then
			io.write("\tpsk=\""..iface.key.."\"\n")
		end
	end

	if string.find(iface.encryption, "dpp") ~= nil then
		if iface.dpp_connector then
			io.write("\tdpp_connector=\""..iface.dpp_connector.."\"\n")
		end
		if iface.dpp_netaccesskey then
			io.write("\tdpp_netaccesskey=\""..iface.dpp_netaccesskey.."\"\n")
		end
		if iface.dpp_csign then
			io.write("\tdpp_csign=\""..iface.dpp_csign.."\"\n")
		end
	end

	io.write("}\n")

	io.close()
end

function supp_enable_vif(iface, bridge)
	local file_name = var_supplicant_path.."wpa_supplicant-"..iface..".conf"
	local action_scan_pid = var_path.."/action-"..iface.."-scan.pid"

	os.execute(wpa_cli.." -p "..var_supplicant_path.." -i global interface_add "..iface.." "..file_name.." \"\" \"\" \"\" "..bridge)
	os.execute("exec 1000>&- && "..wpa_cli.." -i "..iface.." -a /lib/wifi/supplicant_scan_action.sh -B -P "..action_scan_pid)
end

function supp_disable_vif(iface)
	local file_name = var_supplicant_path.."wpa_supplicant-"..iface..".conf"
	local action_scan_pid = var_path.."/action-"..iface.."-scan.pid"
	local scan_state = var_path.."scan_state-"..iface

	os.execute("[ -f "..action_scan_pid.." ] && kill -SIGTERM `cat "..action_scan_pid.."` 2>/dev/null")
	os.execute(wpa_cli.." -p "..var_supplicant_path.." -i global interface_remove "..iface)
	os.remove(action_scan_pid)
	os.remove(scan_state)
	os.remove(file_name)
end

RAMFS_COPY_BIN='mkfs.f2fs blkid blockdev fw_printenv fw_setenv dmsetup'
RAMFS_COPY_DATA="/etc/fw_env.config /var/lock/fw_printenv.lock"

# Flash firmware to MTD partition
#
# $(1): path to image
# $(2): (optional) pipe command to extract firmware, e.g. dd bs=n skip=m
nor_do_upgrade() {
	sync
	echo 3 > /proc/sys/vm/drop_caches
	if [ -n "$UPGRADE_BACKUP" ]; then
		get_image "$1" "$2" | dd bs=64k skip=1 conv=sync 2>/dev/null | mtd $MTD_ARGS $MTD_CONFIG_ARGS -j "$UPGRADE_BACKUP" write - "${PART_NAME:-image}"
	else
		get_image "$1" "$2" | dd bs=64k skip=1 conv=sync 2>/dev/null | mtd $MTD_ARGS write - "${PART_NAME:-image}"
	fi
	[ $? -ne 0 ] && exit 1
}

snand_do_upgrade() {
	local mtdname="ubi2"
	rm -rf /tmp/snand-ubi.bin
	dd if=$1 of=/tmp/snand-ubi.bin bs=64k skip=1

	mtdpart=$(grep "\"${mtdname}\"" /proc/mtd | awk -F: '{print $1}')

	ubiformat /dev/${mtdpart} -y -f /tmp/snand-ubi.bin

	if [ -n "$UPGRADE_BACKUP" ]; then
		echo "save config" >/dev/console
		mkdir -p /tmp/new_overlay
		ubiattach -p /dev/${mtdpart}
		mount -t ubifs ubi1:rootfs_data /tmp/new_overlay
		cp -rf $UPGRADE_BACKUP /tmp/new_overlay/
		sync
		sync
		wtoem -r
		echo "change part" >/dev/console
		sync
		sync
		umount /tmp/new_overlay
	else
		sync
		echo "change part" >/dev/console
		wtoem -r
		sync
		sync
	fi
}

platform_do_upgrade() {
	local board=$(board_name)

	case "$board" in
	"ZX7986E"*)
		rm -rf /tmp/snand-emmc.bin
		dd if=$1 of=/tmp/snand-emmc.bin bs=64k skip=1 
		rm -rf $1
		mtk_mmc_do_upgrade "/tmp/snand-emmc.bin"
		;;
	*)
		cat /proc/mtd | grep ubi2
		if [ $? -eq 0 ]; then
			snand_do_upgrade "$1"
		else
			nor_do_upgrade "$1"
		fi
		;;
	esac
}

PART_NAME=firmware

platform_check_image() {
	local board=$(board_name)

	[ "$#" -gt 1 ] && return 1

	wtcheck -b "$board" -r -f $1

	[ "$?" -eq 0 ] && return 0

	return 1
}


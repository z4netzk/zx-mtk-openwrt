#!/bin/sh

# adjust usb
irq=$(cat /proc/interrupts | grep xhci-hcd | awk '{print $1}')

irq_real=${irq%:}

echo 2 >/proc/irq/$irq_real/smp_affinity

for dev in /sys/class/net/*; do
	[ -d "$dev" ] || continue

	for q in ${dev}/queues/rx-*; do
		echo 3 >$q/rps_cpus
	done
done

echo 2000 > /proc/sys/net/core/netdev_max_backlog
echo 1 >/sys/class/net/wwan0/queues/rx-0/rps_cpus
echo 2 >/sys/class/net/wwan0_1/queues/rx-0/rps_cpus


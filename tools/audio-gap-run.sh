#!/bin/bash
# Copyright 2026, haiku-odroid-c2 port authors.
# Distributed under the terms of the MIT License.
#
# audio-gap-run.sh <name> <seconds> <device> [load command]: plays audiotest's
# tone on a USB audio output of the test board (media services stopped first) for
# that long while the load command runs on the board, then prints dwc2's
# report of the streams (split gaps, part timing, the first gaps) (docs/milestones.md
# M82). Example load: reads of the USB stick,
#   'end=$(($(date +%s)+178)); while [ $(date +%s) -lt $end ]; do
#    dd if=/dev/disk/usb/0/0/raw of=/dev/null bs=1048576 count=64; done'
S="sshpass -p haiku ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=QUIET -o ConnectTimeout=8 baron@10.42.0.2"
name=$1; secs=$2; device=$3; load=${4:-sleep $((secs + 1))}
$S "mark=\$(wc -l < /var/log/syslog)
	/boot/home/audiotest $device $secs 750 48000 > /dev/null 2>&1 &
	$load
	wait
	sleep 2
	tail -n +\$((mark + 1)) /var/log/syslog | grep -E 'dwc2: (isochronous|  )' \
		| sed 's/^KERN: dwc2: //'" | sed "s/^/$name: /"

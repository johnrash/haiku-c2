#!/bin/bash
# Copyright 2026, haiku-odroid-c2 port authors.
# Distributed under the terms of the MIT License.
#
# Software reboots of the test board (10.42.0.2), checking its USB devices
# after each (milestone M21's keyboard dropout came after software reboots,
# never after relay power cycles). After each boot reaches the desktop and
# 30 s more:
#   - which of the given devices (<vid:pid>) listusb shows;
#   - the keyboards usb_hid published (/dev/input/keyboard/usb);
#   - the boot's serial output: CRC errors and other USB errors (not the
#     "NON COMPLIANT descriptor length" every boot has);
#   - the three busiest threads (top, % of all four cores).
# Stops at a panic, KDL, or a board that doesn't come back.
# Usage: usb-reboot-test.sh <count> <label> <vid:pid>...
#   -> logs/usb-reboot-<label>.txt
set -u
count=$1
label=$2
shift 2
root=$(cd "$(dirname "$0")/.." && pwd)
out=$root/logs/usb-reboot-$label.txt
raw=$root/run/console.raw
on() {
	sshpass -p haiku ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
		-o LogLevel=ERROR -o ConnectTimeout=15 baron@10.42.0.2 "$@"
}

{
echo "usb-reboot-test: $count software reboots ($label), devices $*, $(date '+%Y-%m-%d %H:%M')"
for i in $(seq 1 "$count"); do
	start=$(stat -c %s "$raw")
	on 'sync; (sleep 2; shutdown -r) > /dev/null 2>&1 &'
	result=$("$root/tools/c2wait" 'c2-system: desktop up|PANIC|kdebug>' 300 --now 2>&1 \
		| grep -a -o 'c2-system: desktop up\|PANIC.*\|kdebug>' | tail -1)
	if [ "$result" != "c2-system: desktop up" ]; then
		echo "boot $i: STOPPED: ${result:-no desktop within 300 s}"
		break
	fi
	sleep 30
	usb=$(on listusb 2>&1)
	present=""
	for id in "$@"; do
		if echo "$usb" | grep -q "^$id "; then
			present="$present $id"
		else
			present="$present $id:MISSING"
		fi
	done
	keyboards=$(on 'ls /dev/input/keyboard/usb 2>/dev/null | wc -l' | tr -d ' ')
	serial=$(tail -c +$((start + 1)) "$raw" | tr -d '\r')
	crc=$(echo "$serial" | grep -a -c -i "crc")
	errors=$(echo "$serial" | grep -a -i "usb error\|usb_hid.*error\|transfer.*fail" \
		| grep -a -v -c "NON COMPLIANT")
	busiest=$(on 'top -d -i 2 -n 1 2>&1' | awk 'NR > 2 && NF >= 7 \
		{ printf "%s %s%% | ", $NF, $5 }' | cut -d'|' -f1-3)
	echo "boot $i: devices:$present; keyboards $keyboards; CRC lines $crc," \
		"other USB errors $errors; busiest: $busiest"
	if [ "$crc" -gt 0 ] || [ "$errors" -gt 0 ]; then
		echo "$serial" | grep -a -i "crc\|usb error\|usb_hid.*error\|transfer.*fail" \
			| grep -a -v "NON COMPLIANT" | head -5 | sed 's/^/    /'
	fi
done
echo "usb-reboot-test: done $(date '+%H:%M')"
} 2>&1 | tee "$out"

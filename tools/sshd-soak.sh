#!/bin/bash
# Copyright 2026, haiku-odroid-c2 port authors.
# Distributed under the terms of the MIT License.
#
# sshd-soak.sh <hours> <log>: the conditions of the sshd failures on the test board
# (milestone M82): a password login every 5 s, and every 10 minutes
# MediaPlayer plays a track to its end through the USB adapter. Each login
# that fails is logged with its time and sshpass's status (5: password
# refused, 255: ssh error); the board's syslog and a core file on its
# Desktop (debug_server setting) say more. Stops early when
# <log>.stop exists.
hours=$1 log=$2
ssh_c2() {
	sshpass -p haiku ssh -o StrictHostKeyChecking=no \
		-o UserKnownHostsFile=/dev/null -o LogLevel=QUIET -o ConnectTimeout=8 \
		-o PubkeyAuthentication=no -o PreferredAuthentications=password \
		baron@10.42.0.2 "$@"
}
end=$(($(date +%s) + hours * 3600))
next_track=0 logins=0 failures=0
tracks=("03 - Ray Parker Jr - Ghostbusters (Ghostbusters).mp3"
	"07 - Giorgio Moroder - The Chase (Midnight Express).mp3"
	"19 - Echo And The Bunnymen - People Are Strange (The Lost Boys).mp3")
n=0
echo "$(date '+%F %T') start, $hours h" >> "$log"
while [ "$(date +%s)" -lt $end ] && [ ! -e "$log.stop" ]; do
	if [ "$(date +%s)" -ge $next_track ]; then
		ssh_c2 'hey MediaPlayer quit > /dev/null 2>&1'
		f=${tracks[$((n % 3))]}
		n=$((n + 1))
		(ssh_c2 "/boot/system/apps/MediaPlayer \"/boot/home/audio-tests/$f\" > /dev/null 2>&1" &)
		next_track=$(($(date +%s) + 600))
	fi
	ssh_c2 true
	s=$?
	logins=$((logins + 1))
	if [ $s -ne 0 ]; then
		failures=$((failures + 1))
		echo "$(date '+%F %T') login $logins failed: status $s" >> "$log"
		# a crash costs the laptop a 90 s penalty
		[ $s -eq 255 ] && sleep 30
	fi
	sleep 5
done
ssh_c2 'hey MediaPlayer quit > /dev/null 2>&1'
echo "$(date '+%F %T') end: $logins logins, $failures failed" >> "$log"

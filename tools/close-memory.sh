#!/bin/bash
# Copyright 2026, haiku-odroid-c2 port authors.
# Distributed under the terms of the MIT License.
#
# close-memory.sh <seconds> <log>: the test board's tcpchurn server closes each
# connection the ordinary way (fin) while the laptop's client keeps sending;
# used memory every 30 s over one shared ssh connection, then 150 s after the
# load for the time wait. Stops early above 1500 MB used (milestone M82 (docs/milestones.md)).
cd "$(dirname "$0")/.."
T=$1; R=$2; B=10.42.0.2; P=$((5700 + RANDOM % 200))
CM="-o ControlMaster=auto -o ControlPath=/tmp/cm-close-%r@%h -o ControlPersist=900"
O="-o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=QUIET -o ConnectTimeout=8 $CM"
S="sshpass -p haiku ssh $O baron@$B"
used() { timeout 15 $S 'sysinfo -mem' | grep -o "used/max *[0-9]*" | awk '{printf "%d", $2/1048576}'; }
$S -fN 2>/dev/null
echo "$(date +%T) $(timeout 10 $S 'uname -v | cut -c1-16; ls /boot/home/config/non-packaged/add-ons/kernel/network/protocols 2>/dev/null' | tr '\n' ' ')" > $R
echo "$(date +%T) used $(used) MB before" >> $R
timeout 10 $S "nohup /boot/home/tcpchurn server $P 4 20000 $T fin > /boot/home/churn-server.out 2>&1 &"
sleep 2
timeout $((T + 10)) run/tcpchurn-linux client $B $P 4 $T > /tmp/close-client.out 2>&1 & C=$!
start=$(date +%s)
while kill -0 $C 2>/dev/null; do
  sleep 30; u=$(used)
  echo "$(date +%T) $(( $(date +%s) - start )) s: used $u MB" >> $R
  if [ -n "$u" ] && [ "$u" -gt 1500 ]; then echo "$(date +%T) over 1500 MB, stopping" >> $R
    kill $C; timeout 10 $S 'kill $(ps | grep -a "tcpchurn server" | grep -v grep | awk "{print \$(NF-3)}")'; break; fi
done
wait $C 2>/dev/null
echo "$(date +%T) load ended: $(tail -1 /tmp/close-client.out | cut -c1-80)" >> $R
sleep 150; echo "$(date +%T) 150 s later: used $(used) MB" >> $R
timeout 10 $S 'tail -2 /boot/home/churn-server.out; grep -a -c -i panic /var/log/syslog' >> $R
$S -O exit 2>/dev/null

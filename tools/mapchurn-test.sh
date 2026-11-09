#!/bin/bash
# Copyright 2026, haiku-odroid-c2 port authors.
# Distributed under the terms of the MIT License.
#
# mapchurn-test.sh <seconds> <threads> <log>: runs tools/probes/mapchurn on
# The test board and reads the wired pages (KDL page_stats through c2console)
# before, while mapchurn holds its address space after the load, and after
# it exits (milestone M82 (docs/milestones.md): page tables freed on unmap).
cd "$(dirname "$0")/.."
T=$1; N=$2; R=$3; B=10.42.0.2; HOLD=60
SER=$(ls -t logs/m1-*.log | head -1)
O="-o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=QUIET -o ConnectTimeout=8"
S="sshpass -p haiku ssh $O baron@$B"
wired() { A=$(stat -c %s $SER); (timeout 30 $S 'kernel_debugger pages' > /dev/null 2>&1 &); sleep 3
  python3 tools/c2send page_stats; sleep 3; python3 tools/c2send co; sleep 2
  tail -c +$((A+1)) $SER | sed 's/^\[[0-9:.]*\] //' | grep -a -m1 '^wired: ' | awk '{print $2}'; }
sshpass -p haiku scp $O run/mapchurn baron@$B:/boot/home/mapchurn
echo "$(date +%T) $(timeout 10 $S 'uname -v | cut -c1-16')" > $R
w0=$(wired); echo "$(date +%T) wired before: $w0" >> $R
timeout 10 $S "nohup /boot/home/mapchurn $T $N $HOLD > /boot/home/mapchurn.out 2>&1 &"
sleep $((T + 10))
w1=$(wired); echo "$(date +%T) wired while mapchurn holds: $w1 ($(( (w1 - w0) * 4 )) KB more)" >> $R
sleep $((HOLD + 5))
w2=$(wired); echo "$(date +%T) wired after it exits: $w2 ($(( (w2 - w0) * 4 )) KB more)" >> $R
timeout 10 $S 'cat /boot/home/mapchurn.out; grep -a -c -i panic /var/log/syslog' >> $R
cat $R

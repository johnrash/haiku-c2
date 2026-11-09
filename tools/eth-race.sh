#!/bin/bash
# Copyright 2026, haiku-odroid-c2 port authors.
# Distributed under the terms of the MIT License.
#
# eth-race.sh <rounds> <label>: several senders on the test board's Ethernet at
# once, three TCP streams and pings both ways, and how many times the driver
# logged a stalled transmit. Needs run/tcpblast-linux and the board's tcpblast
# (tools/probes/tcpblast.c). Appends to logs/eth-race.log.
cd "$(dirname "$0")/.."
LOG=logs/eth-race.log
SER=$(ls -t logs/m1-*.log | head -1)
O="-o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR -o ConnectTimeout=8"
S="sshpass -p haiku ssh $O baron@10.42.0.2"
txb() { timeout 8 $S "ifconfig /dev/net/meson_dwmac/0 | grep -a Transmit | sed 's/.*errors, //; s/ bytes.*//'"; }
echo "$(date +%T) === $2: $(timeout 8 $S 'uname -v | cut -c1-14; cat /boot/home/config/settings/kernel/drivers/meson_dwmac 2>/dev/null' | tr '\n' ' ')" | tee -a $LOG
total=0; dead=0; start=$(date +%s); first=$(txb)
for r in $(seq 1 $1); do
  before=$(grep -a -c "transmit suspended" $SER)
  p=$((12000 + RANDOM % 4000)); pids=""
  for i in 1 2 3; do
    timeout 30 run/tcpblast-linux recv $((p+i)) > /dev/null 2>&1 & pids="$pids $!"
  done
  sleep 1
  for i in 1 2 3; do
    (timeout 28 $S "timeout 20 /boot/home/tcpblast send 10.42.0.1 $((p+i)) 100000 64 1024 > /dev/null 2>&1" > /dev/null 2>&1 &)
  done
  ping -i 0.01 -c 1500 -q 10.42.0.2 > /dev/null 2>&1 & pids="$pids $!"
  (timeout 26 $S "ping -c 1200 -i 0.01 10.42.0.1 > /dev/null 2>&1" > /dev/null 2>&1 &)
  sleep 24
  kill $pids 2>/dev/null; wait $pids 2>/dev/null
  sleep 2
  after=$(grep -a -c "transmit suspended" $SER)
  n=$((after-before)); total=$((total+n))
  if ping -c 3 -i 0.3 -W 1 10.42.0.2 > /dev/null 2>&1; then a=alive; else a=DEAD; dead=$((dead+1)); fi
  echo "$(date +%T) round $r: stalls $n, link $a" | tee -a $LOG
  [ $a = DEAD ] && break
  sleep 1
done
last=$(txb)
echo "$(date +%T) $2: $r rounds, $total stalls, $dead dead, $(( (${last:-$first} - ${first:-0}) / 1000000 )) MB sent in $(( $(date +%s) - start )) s" | tee -a $LOG

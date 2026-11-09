#!/bin/bash
# Copyright 2026, haiku-odroid-c2 port authors.
# Distributed under the terms of the MIT License.
#
# c2-soak.sh <minutes> <name> [eth]: both of the test board's networks at full load,
# wifi-soak.sh and eth-race.sh together ("eth": only Ethernet), and a record of what else went on, in
# logs/soak-<name>/: wifi.log and eth.log from those scripts; laptop.log, each
# change in what the laptop sees of the network, to tell an access point going
# away from a fault on the board; board.log, the board's memory, teams and network
# counters every ten minutes; summary.txt at the end, from soak-summary.sh.
cd "$(dirname "$0")/.."
D=logs/soak-$2; mkdir -p $D
SER=$(ls -t logs/m1-*.log | head -1)
echo "$SER $(( $(stat -c %s $SER) + 1 ))" > $D/start
O="-o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR -o ConnectTimeout=8"
S="sshpass -p haiku ssh $O baron@10.42.0.2"
touch $D/running

watch_laptop() {
  local last="" lost=0 gw ap now
  while [ -e $D/running ]; do
    if ping -c 1 -W 1 "${GATEWAY:-192.168.1.1}" > /dev/null 2>&1; then lost=0; else lost=$((lost+1)); fi
    # Three in a row: one lost to a busy radio means nothing.
    gw=up; [ $lost -ge 3 ] && gw=LOST
    ap=$(iw dev "${WIFI_IF:-wlan0}" link | awk '/Connected/{print $3}')
    now="router $gw, laptop on ${ap:-nothing}, cable $(cat /sys/class/net/${C2_HOST_IF:-eth1}/carrier 2>/dev/null)"
    [ "$now" != "$last" ] && echo "$(date +%T) $now" >> $D/laptop.log
    last=$now
    sleep 2
  done
}

watch_board() {
  while [ -e $D/running ]; do
    { echo "=== $(date +%T)"
      timeout 20 $S 'sysinfo -mem | head -2; echo "teams $(ps | wc -l)"; for i in meson_dwmac realtekwifi; do ifconfig /dev/net/$i/0 | grep -a -E "Network|Receive|Transmit"; done' 2>&1 || echo "no answer"
    } >> $D/board.log
    sleep 600
  done
}

echo "$(date +%T) === $2, $1 minutes" | tee -a $D/laptop.log $D/board.log
watch_laptop & WL=$!
watch_board & WB=$!
W=""; : > $D/wifi.log
if [ "$3" != eth ]; then tools/wifi-soak.sh $1 > $D/wifi.log 2>&1 & W=$!; fi
tools/eth-race.sh $(( $1 * 2 )) "soak $2" > $D/eth.log 2>&1 & E=$!
wait $W $E
rm -f $D/running
kill $WL $WB 2>/dev/null
tools/soak-summary.sh $2 | tee $D/summary.txt

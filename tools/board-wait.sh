#!/bin/bash
# Copyright 2026, haiku-odroid-c2 port authors.
# Distributed under the terms of the MIT License.
#
# board-wait.sh [cycle]: optionally power cycles the test board (after a sync),
# then waits for it to answer and for Wi-Fi to get an address. Prints the time,
# the access point and the address.
cd "$(dirname "$0")/.."
O="-o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR -o ConnectTimeout=8"
S="sshpass -p haiku ssh $O baron@10.42.0.2"
if [ "$1" = cycle ]; then
  timeout 8 $S 'sync; sync' > /dev/null 2>&1
  python3 tools/power.py cycle > /dev/null 2>&1
  sleep 35
fi
for i in $(seq 1 30); do
  r=$(timeout 8 $S 'ifconfig /dev/net/realtekwifi/0 2>/dev/null | grep -a -E "Network|inet addr" | sed "s/.*Network: //; s/.*addr: //; s/,.*Address: / /; s/, .*//" | tr "\n" " "' 2>/dev/null)
  case "$r" in *"${SSID:?set SSID to the network the board joins}"*.*.*.*) echo "$(date +%T) joined: $r"; exit 0;; esac
  sleep 5
done
echo "$(date +%T) no Wi-Fi address: $r"
timeout 8 $S 'echo but ssh answers' 2>&1 | cut -c1-70
exit 1

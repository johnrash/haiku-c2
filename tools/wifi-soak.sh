#!/bin/bash
# Copyright 2026, haiku-odroid-c2 port authors.
# Distributed under the terms of the MIT License.
#
# wifi-soak.sh <minutes>: the test board's (or $BOARD's) Wi-Fi at full load against the laptop,
# upload then download, 20 s each, with a ping and the access point per round.
# Needs run/tcpblast-linux and, on the board, tcpblast, rate2.sh and killblast.sh
# (tools/probes). The cleanup spares streams to 10.42.0.1, so eth-race.sh can run alongside.
cd "$(dirname "$0")/.."
O="-o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR -o ConnectTimeout=8"
S="sshpass -p haiku ssh $O baron@${BOARD:-10.42.0.2}"
L=$(ip -4 -br addr show "${WIFI_IF:-wlan0}" | awk '{print $3}' | sed 's/\/.*//')
R=run/wifi-soak-receiver.$$
end=$(( $(date +%s) + $1 * 60 )); p=$((5600 + RANDOM % 300)); n=0
while [ $(date +%s) -lt $end ]; do
  n=$((n+1)); p=$((p+1))
  IP=$(timeout 10 $S 'ifconfig /dev/net/realtekwifi/0 | grep -a "inet addr" | sed "s/.*addr: //; s/,.*//"')
  ap=$(timeout 10 $S 'ifconfig /dev/net/realtekwifi/0 | grep -a Network | sed "s/.*Address: //; s/,.*//"')
  timeout 26 run/tcpblast-linux recv $p > /dev/null 2>&1 & LP=$!
  sleep 1
  u=$(timeout 30 $S "sh /boot/home/rate2.sh up $L $p 1024 20")
  kill $LP 2>/dev/null; wait $LP 2>/dev/null
  p=$((p+1))
  # The sender tries to connect once: it waits for the board to say it's
  # listening, and a round that can't start says so instead of counting 0.
  note=""; : > $R
  (timeout 30 $S "timeout 26 /boot/home/tcpblast recv $p 1024 2>&1" > $R 2>&1 &)
  for i in $(seq 1 40); do grep -q listening $R && break; sleep 0.25; done
  grep -q listening $R || note=" (the receiver didn't start)"
  timeout 23 run/tcpblast-linux send $IP $p 100000 > /dev/null 2> $R.send & LP=$!
  sleep 1
  kill -0 $LP 2>/dev/null || note="$note (the sender stopped: $(cut -c1-50 $R.send | head -1))"
  d=$(timeout 30 $S 'sh /boot/home/rate2.sh down 20')
  kill $LP 2>/dev/null; wait $LP 2>/dev/null
  sleep 3
  timeout 8 $S 'sh /boot/home/killblast.sh 10.42.0.1' > /dev/null
  png=$(timeout 15 $S 'ping -c 20 -i 0.1 ${GATEWAY:-192.168.1.1} 2>&1 | tail -2 | tr "\n" " " | sed "s/ packets transmitted, / tx /; s/ packets received, / rx /; s/---[^-]*--- //; s/round-trip min.avg.max.stddev = //"')
  echo "round $n [$ap]: up $u, down $d, ping $png$note"
  case "$png" in *" 0 rx "*) echo "    (no link at the end of round $n, $(date +%T))";; esac
done
rm -f $R $R.send
echo "=== done, $1 min"

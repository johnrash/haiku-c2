#!/bin/bash
# Copyright 2026, haiku-odroid-c2 port authors.
# Distributed under the terms of the MIT License.
#
# soak-summary.sh <name>: the counts from a c2-soak.sh run, so far or at its end.
cd "$(dirname "$0")/.."
D=logs/soak-$1
read SER FROM < $D/start
median() { sort -n | awk '{a[NR]=$1} END {if (NR) print a[int((NR+1)/2)]; else print "-"}'; }

echo "=== serial log, $SER"
for w in "transmit suspended" "transmit restarted" "restarting" "beacon miss" "fatal bus" "PANIC" "Kernel Debugging" "U-Boot 20"; do
  printf "%-20s %s\n" "$w" "$(tail -c +$FROM $SER | grep -a -c "$w")"
done

echo "=== Ethernet"
echo "rounds $(grep -a -c " round " $D/eth.log), stalls $(sed -n 's/.* round [0-9]*: stalls \([0-9]*\),.*/\1/p' $D/eth.log | awk '{s+=$1} END {print s+0}'), dead $(grep -a -c "link DEAD" $D/eth.log)"
grep -a " MB sent " $D/eth.log | tail -1

echo "=== Wi-Fi"
echo "rounds $(grep -a -c "^round " $D/wifi.log), no link at the end of $(grep -a -c "no link" $D/wifi.log)"
for ap in $(sed -n 's/^round [0-9]* \[\([0-9a-f:]*\)\].*/\1/p' $D/wifi.log | sort -u); do
  f=$(grep -a -F "[$ap]" $D/wifi.log)
  echo "$ap: $(echo "$f" | wc -l) rounds, median up $(echo "$f" | sed -n 's/.* up \([0-9]*\) kB.*/\1/p' | median) kB/s, down $(echo "$f" | sed -n 's/.* down \([0-9]*\) kB.*/\1/p' | median) kB/s"
done
echo "rounds with no access point recorded: $(grep -a -c "^round [0-9]* \[\]" $D/wifi.log)"
echo "rounds under 5 MB/s either way: $(awk '/^round /{u=d=0; for(i=1;i<NF;i++){if($i=="up")u=$(i+1); if($i=="down")d=$(i+1)} if(u<5000||d<5000)n++} END {print n+0}' $D/wifi.log)"
echo "pings lost: $(sed -n 's/.* \([0-9]*\) tx \([0-9]*\) rx .*/\1 \2/p' $D/wifi.log | awk '{t+=$1; l+=$1-$2; if($1!=$2)r++} END {print l+0 " of " t+0 ", in " r+0 " rounds"}')"

echo "=== Laptop, changes seen"
grep -a -c "" $D/laptop.log
tail -n 12 $D/laptop.log

echo "=== Board, first and latest"
grep -a -E "bytes free|^teams" $D/board.log | head -2
grep -a -E "bytes free|^teams" $D/board.log | tail -2
grep -a -c "no answer" $D/board.log | sed 's/^/no answer: /'

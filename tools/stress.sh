#!/bin/sh
# M5 stress run: network transfers with checksums, a flood ping and an eMMC
# write/read loop on the C2 at the same time, for $1 minutes (default 30).
# Logs a line per round; stops at the first mismatch or lost connection.
cd "$(dirname "$0")/.."
minutes=${1:-30}
S=$(mktemp -d)
trap 'rm -rf "$S"' EXIT
end=$(( $(date +%s) + minutes * 60 ))
# work files on the C2: /Data if the image has it, else the home folder
W=$(tools/c2sh '[ -d /Data ] && echo /Data || echo /boot/home' 30) || exit 1
mark=$(stat -c %s run/console.raw)
head -c 52428800 /dev/urandom > $S/stress50.bin
want=$(sha256sum $S/stress50.bin | cut -d' ' -f1)

# On the C2: write 64 MiB of random data, sync, check its hash, until stopped.
cat > $S/disk-loop.sh <<'X'
while [ ! -e /boot/home/disk-loop.stop ]; do
	dd if=/dev/urandom of=/boot/home/loop.bin bs=1M count=64 2>/dev/null
	a=$(sha256sum < /boot/home/loop.bin); sync
	b=$(sha256sum < /boot/home/loop.bin)
	[ "$a" = "$b" ] && echo ok >> /boot/home/disk-loop.log || echo MISMATCH >> /boot/home/disk-loop.log
done
rm -f /boot/home/loop.bin /boot/home/disk-loop.stop
X
sed -i "s|/boot/home|$W|g" $S/disk-loop.sh
tools/c2put $S/disk-loop.sh $W/disk-loop.sh > /dev/null 2>&1 || { echo "cannot copy disk loop"; exit 1; }
tools/c2sh "rm -f $W/disk-loop.log $W/disk-loop.stop; (sh $W/disk-loop.sh > /dev/null 2>&1 &); echo disk loop started" 30 || exit 1

sudo ping -f -q 10.42.0.2 > $S/flood.txt 2>&1 &
flood=$!
round=0
while [ $(date +%s) -lt $end ]; do
	round=$((round + 1))
	t0=$(date +%s)
	tools/c2put $S/stress50.bin $W/stress50.bin > /dev/null 2>&1 || { echo "round $round: put failed"; break; }
	rm -f $S/stress-back.bin
	tools/c2get $W/stress50.bin $S/stress-back.bin > /dev/null 2>&1 || { echo "round $round: get failed"; break; }
	got=$(sha256sum $S/stress-back.bin | cut -d' ' -f1)
	remote=$(tools/c2sh "sha256sum $W/stress50.bin" 60 | cut -d' ' -f1)
	disk=$(tools/c2sh "grep -c ok $W/disk-loop.log; grep -c MISMATCH $W/disk-loop.log" 30 | tr '\n' ' ')
	ok=yes; [ "$got" = "$want" ] && [ "$remote" = "$want" ] || ok=NO
	echo "round $round: $(( $(date +%s) - t0 )) s, 50 MB round trip match: $ok, disk loop ok/mismatch: $disk"
	[ $ok = yes ] || break
done
sudo kill -INT $flood 2>/dev/null; sleep 1
echo "flood ping: $(grep -a -E 'packets transmitted' $S/flood.txt)"
tools/c2sh "touch $W/disk-loop.stop; rm -f $W/stress50.bin $W/disk-loop.sh $W/disk-loop.log" 30
tail -c +$((mark + 1)) run/console.raw | tr -d '\r\000' > logs/m5-stress.log
echo "serial: panics $(grep -a -c PANIC logs/m5-stress.log), kdebug $(grep -a -c 'kdebug> ' logs/m5-stress.log)"

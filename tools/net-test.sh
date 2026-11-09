#!/bin/bash
# Copyright 2026, haiku-odroid-c2 port authors.
# Distributed under the terms of the MIT License.
#
# Network tests for a C2 on the LAN, to compare its network link before and
# after a change (a new bridge, a cable): logs/net-<host>-<label>.txt.
#   - ping: the laptop to the board; the board to <peer> and the gateway;
#   - TCP from memory to memory (tools/probes/tcpblast.c), both ways, with
#     the laptop (over its Wi-Fi) and with <peer> (a board wired to the LAN);
#   - SSH, both ways, with the laptop: to /dev/null and from /dev/zero (the
#     network and the cipher), and scp of a 100 MB random file to the
#     board's disk and back (md5 compared);
# Everything it puts on the boards is removed at the end.
# Usage: net-test.sh <host> <peer> <label>
# Environment: GATEWAY for the ping.
set -u
host=$1
peer=$2
label=$3
root=$(cd "$(dirname "$0")/.." && pwd)
out=$root/logs/net-$host-$label.txt
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
laptop=$(ip route get "$host" | sed -n 's/.* src \([0-9.]*\).*/\1/p')
ssh_opts=(-o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null
	-o LogLevel=ERROR -o ConnectTimeout=15)
on() { local h=$1; shift; sshpass -p haiku ssh "${ssh_opts[@]}" "baron@$h" "$@"; }
put() { sshpass -p haiku scp -q "${ssh_opts[@]}" "$2" "baron@$1:$3"; }
seconds() { date +%s.%N; }
rate() { python3 -c "import sys; print('%.2f MB/s (%.1f s)' % ($1 / 1e6 / ($3 - $2), $3 - $2))"; }

# tcpblast for the laptop; the board's from run/
cc -O2 -o "$work/tcpblast" "$root/tools/probes/tcpblast.c" || exit 1
dd if=/dev/urandom of="$work/random" bs=1M count=100 status=none
size=$(stat -c %s "$work/random")
sum=$(md5sum < "$work/random" | cut -d' ' -f1)

{
echo "net-test: $host ($label), peer $peer, laptop $laptop, $(date '+%Y-%m-%d %H:%M')"
echo "--- the board"
on "$host" 'uname -v; ifconfig /dev/net/meson_dwmac/0 | grep -i "media\|inet addr\|errors\|dropped"'
echo "--- the laptop's Wi-Fi"
iw dev "$(ip route get "$host" | sed -n 's/.* dev \([^ ]*\).*/\1/p')" link 2>/dev/null \
	| grep -i "freq\|signal\|tx bitrate"

echo "--- ping"
echo "laptop -> $host: $(ping -q -c 20 -i 0.2 "$host" | tail -2 | tr '\n' ' ')"
for target in "$peer" ${GATEWAY:-}; do
	echo "$host -> $target: $(on "$host" "ping -c 10 $target" | tail -2 | tr '\n' ' ')"
done

echo "--- TCP, memory to memory (tcpblast, 50 MB)"
for h in "$host" "$peer"; do
	put "$h" "$root/run/tcpblast" /boot/home/haiku-c2-tcpblast
done
blast_to_board() # <receiving board> <sender: laptop or a board> <label>
{
	# a time limit: a receiver nobody reaches would keep the port
	on "$1" '(timeout 60 ~/haiku-c2-tcpblast recv 5001 > /tmp/haiku-c2-recv.txt 2>&1 &)'
	sleep 1
	if [ "$2" = laptop ]; then
		"$work/tcpblast" send "$1" 5001 50 > /dev/null 2>&1
	else
		on "$2" "~/haiku-c2-tcpblast send $1 5001 50" > /dev/null 2>&1
	fi
	sleep 1
	echo "$3: $(on "$1" 'cat /tmp/haiku-c2-recv.txt; rm -f /tmp/haiku-c2-recv.txt')"
}
blast_to_board "$host" laptop "laptop -> $host"
timeout 60 "$work/tcpblast" recv 5002 > "$work/recv.txt" 2>&1 &
sleep 1
on "$host" "~/haiku-c2-tcpblast send $laptop 5002 50" > /dev/null 2>&1
wait
echo "$host -> laptop: $(cat "$work/recv.txt")"
blast_to_board "$host" "$peer" "$peer -> $host"
blast_to_board "$peer" "$host" "$host -> $peer"
for h in "$host" "$peer"; do on "$h" 'rm -f ~/haiku-c2-tcpblast'; done

echo "--- SSH with the laptop ($(sshpass -p haiku ssh -v -o StrictHostKeyChecking=no \
	-o UserKnownHostsFile=/dev/null "baron@$host" true 2>&1 \
	| sed -n 's/.*kex: server->client cipher: \([^ ]*\).*/\1/p'))"
start=$(seconds)
sshpass -p haiku ssh "${ssh_opts[@]}" "baron@$host" 'cat > /dev/null' < "$work/random"
echo "laptop -> $host, to /dev/null: $(rate "$size" "$start" "$(seconds)")"
start=$(seconds)
on "$host" 'dd if=/dev/zero bs=1048576 count=100 2> /dev/null' > /dev/null
echo "$host -> laptop, from /dev/zero: $(rate "$size" "$start" "$(seconds)")"
start=$(seconds)
put "$host" "$work/random" /boot/home/haiku-c2-nettest-100M.bin
echo "laptop -> $host, scp to its disk: $(rate "$size" "$start" "$(seconds)")"
start=$(seconds)
sshpass -p haiku scp -q "${ssh_opts[@]}" "baron@$host:/boot/home/haiku-c2-nettest-100M.bin" \
	"$work/back"
echo "$host -> laptop, scp from its disk: $(rate "$size" "$start" "$(seconds)")"
[ "$(md5sum < "$work/back" | cut -d' ' -f1)" = "$sum" ] && echo "md5: the same" \
	|| echo "md5: DIFFERENT"

on "$host" "rm -f ~/haiku-c2-nettest-100M.bin"
} 2>&1 | tee "$out"
echo "net-test: $out"

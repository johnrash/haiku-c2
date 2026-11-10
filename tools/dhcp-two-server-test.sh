#!/bin/bash
# Copyright 2026, haiku-odroid-c2 port authors.
# Distributed under the terms of the MIT License.
#
# dhcp-two-server-test.sh <outdir> <scenario>...: runs each scenario of
# tools/dhcp-two-server-sim.py against the test board (milestone M82). Stops the
# crossover's dnsmasq, restarts the board's DHCP client for each scenario
# and saves the simulator's log with the board's DHCP syslog lines; then
# starts dnsmasq again and has the board take its usual lease from it.
# Needs sudo for the simulator.
set -u
cd "$(dirname "$0")/.."
out=$1
shift
mkdir -p "$out"
DEV=/dev/net/meson_dwmac/0
ssh_c2() {
	sshpass -p haiku ssh -o StrictHostKeyChecking=no \
		-o UserKnownHostsFile=/dev/null -o LogLevel=QUIET \
		-o ConnectTimeout=8 baron@10.42.0.2 "$@"
}

pid=$(cat run/dnsmasq.pid)
kill "$pid"
while kill -0 "$pid" 2>/dev/null; do sleep 0.2; done
echo "dnsmasq ($pid) stopped"

for scenario in "$@"; do
	log=$out/$scenario.txt
	: > "$log"
	ssh_c2 "uname -v" >> "$log"
	mark=$(ssh_c2 "wc -l < /var/log/syslog")
	sudo python3 tools/dhcp-two-server-sim.py "$scenario" 12 "$log" &
	sim=$!
	sleep 1
	ssh_c2 "ifconfig $DEV auto-config"
	wait "$sim"
	echo "--- board syslog" >> "$log"
	ssh_c2 "tail -n +$((mark + 1)) /var/log/syslog | grep -E 'DHCP|dhcp'" \
		>> "$log"
	echo "=== $scenario"
	cat "$log"
done

sudo dnsmasq -C tools/dnsmasq.conf
sleep 1
ssh_c2 "ifconfig $DEV auto-config"
sleep 3
echo "dnsmasq back ($(cat run/dnsmasq.pid)); board lease:"
ssh_c2 "tail -n 20 /var/log/syslog | grep -E 'server:|lease time:|status'"

#!/bin/bash
# Copyright 2026, haiku-odroid-c2 port authors.
# Distributed under the terms of the MIT License.
#
# dhcp-moved-test.sh <log>: a stuck lease (seen on a C2 on Wi-Fi), on the test board's cable
# (milestone M82). dnsmasq stops; tools/dhcp-bridge-sim.py leases the board
# its 10.42.0.2 for 60 s; then tools/dhcp-two-server-sim.py "moved" takes
# over as a router that NAKs .2 and offers 10.42.0.3, sent to that address
# unless the board asks for broadcasts. A board that keeps .2 configured
# drops those offers. Afterwards dnsmasq runs again and the board, at .2 or
# .3, takes its usual lease. Needs sudo for the simulators.
set -u
cd "$(dirname "$0")/.."
log=$1
: > "$log"
DEV=/dev/net/meson_dwmac/0
ssh_c2() {
	sshpass -p haiku ssh -o StrictHostKeyChecking=no \
		-o UserKnownHostsFile=/dev/null -o LogLevel=QUIET \
		-o ConnectTimeout=8 baron@$1 "${@:2}"
}

pid=$(cat run/dnsmasq.pid)
kill "$pid"
while kill -0 "$pid" 2>/dev/null; do sleep 0.2; done
ssh_c2 10.42.0.2 "uname -v" >> "$log"
mark=$(ssh_c2 10.42.0.2 "wc -l < /var/log/syslog")
echo "--- bridge, 60 s leases" >> "$log"
sudo python3 tools/dhcp-bridge-sim.py normal 15 "$log" &
sleep 1
ssh_c2 10.42.0.2 "ifconfig $DEV auto-config"
wait
echo "--- the bridge is gone; a router that moves the board to .3" >> "$log"
sudo python3 tools/dhcp-two-server-sim.py moved 100 "$log"
where=10.42.0.2
ping -c 1 -W 2 10.42.0.3 > /dev/null 2>&1 && where=10.42.0.3
echo "--- board at $where; its syslog" >> "$log"
ssh_c2 $where "ifconfig $DEV | grep 'inet addr'; tail -n +$((mark + 1)) /var/log/syslog | grep -E 'DHCP' | grep -vE 'subnet|broadcast:|gateway|nameserver|renewal time|rebinding time|domain name'" >> "$log"
sudo dnsmasq -C tools/dnsmasq.conf
sleep 1
ssh_c2 $where "ifconfig $DEV auto-config"
sleep 6
echo "--- dnsmasq back: $(ssh_c2 10.42.0.2 "ifconfig $DEV | grep 'inet addr'")" >> "$log"
cat "$log"

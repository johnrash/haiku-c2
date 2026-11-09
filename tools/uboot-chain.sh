#!/bin/sh
# Test a U-Boot build without writing anything: power-cycle into the SD
# U-Boot, load <file> (raw u-boot.bin, in tftp/) to its load address
# 0x01000000, jump to it and stop its autoboot. Prints its banner lines.
# Usage: uboot-chain.sh <file-in-tftp>
# AUTOBOOT=1: let the new U-Boot run its boot command instead (test it with
# c2wait on the console).
tools=$(cd "$(dirname "$0")" && pwd)
raw="$tools/../run/console.raw"
"$tools/uboot-break.sh" > /dev/null || exit 1
for c in "setenv ipaddr 10.42.0.2; setenv netmask 255.255.255.0; setenv serverip 10.42.0.1" \
		"tftpboot 0x20000000 $1" 'cp.b 0x20000000 0x01000000 ${filesize}'; do
	"$tools/c2send" "$c"
	"$tools/c2wait" '\n=> $' 30 > /dev/null || { echo "uboot-chain: '$c' failed" >&2; exit 1; }
done
"$tools/c2send" "go 0x01000000"
[ "${AUTOBOOT:-0}" = 1 ] && exit 0
i=0
while [ $i -lt 300 ]; do
	"$tools/c2send" -n " "
	sleep 0.1
	if tail -c 64 "$raw" | tr -d '\r\n' | grep -q '=> *$'; then
		"$tools/c2send" -k ctrl-c
		python3 -I - "$raw" <<'PY'
import sys
raw = open(sys.argv[1], "rb").read()
t = raw[raw.rfind(b"go 0x01000000"):].decode("utf-8", "replace").replace("\r", "")
print("\n".join(l for l in t.split("\n") if "U-Boot 20" in l or "HDMI" in l))
PY
		exit 0
	fi
	i=$((i + 1))
done
echo "uboot-chain: no prompt from the new U-Boot" >&2
exit 1

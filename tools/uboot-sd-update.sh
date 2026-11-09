#!/bin/sh
# Write a signed U-Boot (u-boot.bin.sd.bin, in tftp/) to the C2's microSD
# from a running U-Boot prompt: boot code bytes 0-439 of sector 0 (the
# partition table stays) and sectors 1-2048, then read back and compare.
# Usage: uboot-sd-update.sh <file-in-tftp>     (U-Boot prompt must be up)
tools=$(cd "$(dirname "$0")" && pwd)
run() {
	"$tools/c2send" "$1"
	out=$("$tools/c2wait" '\n=> $' 60 | tr -d '\r')
	echo "$out" | grep -E 'Bytes transferred|blocks|Total|differ|rror|Capacity'
}
run "setenv ipaddr 10.42.0.2; setenv netmask 255.255.255.0; setenv serverip 10.42.0.1"
run "tftpboot 0x20000000 $1"
run "mmc dev 0"
run "mmc info"
run "mmc read 0x21000000 0 1"
run "cp.b 0x20000000 0x21000000 0x1b8"
run "mmc write 0x21000000 0 1"
run "mmc write 0x20000200 1 0x800"
run "mw.b 0x22000000 0xa5 0x100200"
run "mmc read 0x22000000 0 0x801"
a=$(run "cmp.b 0x22000000 0x21000000 0x200")
b=$(run "cmp.b 0x22000200 0x20000200 0x100000")
echo "$a"; echo "$b"
if echo "$a" | grep -q '512 byte(s) were the same' && echo "$b" | grep -q '1048576 byte(s) were the same'; then
	echo "uboot-sd-update: verified"
else
	echo "uboot-sd-update: VERIFY FAILED - do not power off before fixing" >&2
	exit 1
fi

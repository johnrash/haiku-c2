#!/bin/sh
# Turn a U-Boot command file from boot-scripts/ into tftp/c2.scr, the script
# the C2's U-Boot fetches and runs on every boot.
# Usage: c2scr.sh boot-scripts/<name>.txt
set -e
root=$(cd "$(dirname "$0")/.." && pwd)
mkimage -A arm64 -O linux -T script -C none -n "c2 $(basename "$1" .txt)" \
	-d "$1" "$root/tftp/c2.scr" > /dev/null
echo "c2scr: tftp/c2.scr <- $1"

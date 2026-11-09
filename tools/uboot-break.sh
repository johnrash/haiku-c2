#!/bin/sh
# Power-cycle the C2 and stop U-Boot's autoboot by sending a space every
# 100 ms until the "=>" prompt shows. Needs c2console running.
tools=$(cd "$(dirname "$0")" && pwd)
raw="$tools/../run/console.raw"
"$tools/power.py" cycle
i=0
while [ $i -lt 300 ]; do
	"$tools/c2send" -n " "
	sleep 0.1
	if tail -c 64 "$raw" 2>/dev/null | tr -d '\r\n' | grep -q '=> *$'; then
		"$tools/c2send" -k ctrl-c	# drop the typed spaces, fresh prompt
		echo "uboot-break: at the U-Boot prompt"
		exit 0
	fi
	i=$((i + 1))
done
echo "uboot-break: no U-Boot prompt after 30 s" >&2
exit 1

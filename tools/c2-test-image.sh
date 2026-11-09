#!/bin/sh
# Build the Haiku image, add the guest diagnostics script, flash it to the
# C2's eMMC over TFTP and boot it. Waits for the diagnostics script's first
# dump (or a KDL prompt) and saves this boot's serial output.
# Usage: c2-test-image.sh <name>   -> logs/<name>.log (serial, this boot)
# NOFLASH=1: no build or flash, just power-cycle and boot what is on the eMMC.
# NOBUILD=1: flash the image jam built last, without building.
# Test images get the diagnostics script, the remote shell (c2sh/c2put/c2get)
# and KDL when there is no desktop 120 s after boot (KDL_ON_HANG=0: not that).
# CLEAN=1: an image for everyday use instead, without any of these
# (REMOTE_SHELL=1 adds the remote shell); it waits for "desktop up".
# DIAG_OPTS: more options for image-add-diag.sh (--writetest, --kdl).
# PACKAGES: .hpkg files to add to the image's system packages.
# GROW=1: the system partition grown to 988 MiB, up to the data partition.
# PROFILE: the jam build profile (default c2-mmc: Haiku's regular image
# without HaikuPorts packages, configs/haiku-UserBuildConfig; minimum-mmc).
set -e
root=$(cd "$(dirname "$0")/.." && pwd)
name="$1"
if [ "${CLEAN:-0}" = 1 ]; then
	opts="--no-diag"
	[ "${REMOTE_SHELL:-0}" = 1 ] && opts="$opts --remote-shell"
	waitFor='c2-system: desktop up|kdebug> $'
else
	opts="--remote-shell"
	[ "${KDL_ON_HANG:-1}" = 0 ] || opts="$opts --kdl-on-hang"
	waitFor='diag early end|kdebug> $'
fi
if [ "${NOFLASH:-0}" = 1 ]; then
	"$root/tools/c2scr.sh" "$root/boot-scripts/boot-emmc.txt" > /dev/null
else
if [ "${NOBUILD:-0}" != 1 ]; then
(cd "$root/generated.arm64" && jam -q -j14 @${PROFILE:-c2-mmc}) > "$root/logs/$name-build.log" 2>&1 \
	|| { echo "c2-test-image: build failed, see logs/$name-build.log"; exit 1; }
fi
cp "$root/generated.arm64/haiku-mmc.image" "$root/run/c2-test.img"
# The build's fat_shell sometimes leaves EFI/BOOT/BOOTAA64.EFI empty: put the
# loader in with mtools and check it byte for byte.
loader="$root/generated.arm64/objects/haiku/arm64/release/system/boot/efi/haiku_loader.efi"
mcopy -o -i "$root/run/c2-test.img@@4M" "$loader" ::/EFI/BOOT/BOOTAA64.EFI
mcopy -i "$root/run/c2-test.img@@4M" ::/EFI/BOOT/BOOTAA64.EFI "$root/run/esp-loader.check"
cmp -s "$loader" "$root/run/esp-loader.check" \
	|| { echo "c2-test-image: loader in the ESP does not match"; exit 1; }
for package in $PACKAGES; do
	opts="$opts --package=$package"
done
"$root/tools/image-add-diag.sh" $opts ${DIAG_OPTS} "$root/run/c2-test.img" > /dev/null
# GROW=1: the system partition grown up to the data partition (tools/c2-grow.sh)
if [ "${GROW:-0}" = 1 ]; then
	"$root/tools/c2-grow.sh" "$root/run/c2-test.img" --size 1G --apply > /dev/null \
		|| { echo "c2-test-image: growing the system partition failed"; exit 1; }
fi
gzip -1 -c "$root/run/c2-test.img" > "$root/tftp/haiku-mmc.image.gz"
"$root/tools/c2scr.sh" "$root/boot-scripts/m1-flash-boot.txt" > /dev/null
fi
mark=$(stat -c %s "$root/run/console.raw")
"$root/tools/c2send" -n ""
"$root/tools/power.py" cycle > /dev/null
"$root/tools/c2wait" 'c2: BOOTEFI' 300 --now > /dev/null || { echo "c2-test-image: no bootefi"; exit 1; }
# Later reboots must boot what is on the eMMC, not flash it again.
"$root/tools/c2scr.sh" "$root/boot-scripts/boot-emmc.txt" > /dev/null
start=$(date +%s)
"$root/tools/c2wait" "${WAIT_FOR:-$waitFor}" "${WAIT_SECONDS:-400}" --now > /dev/null || true
echo "c2-test-image: waited $(( $(date +%s) - start )) s after bootefi"
sleep "${EXTRA_SECONDS:-0}"
tail -c +$((mark + 1)) "$root/run/console.raw" | tr -d '\r\000' \
	| sed 's/\x1b\[[0-9;]*m//g' > "$root/logs/$name.log"
echo "c2-test-image: serial saved to logs/$name.log ($(wc -l < "$root/logs/$name.log") lines)"

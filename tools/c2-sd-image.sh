#!/bin/sh
# A self-contained microSD image for an ODROID-C2: U-Boot (configs/
# u-boot-c2-sd.config: boots Haiku from the device it started from, else
# the other one) and the Haiku image, which runs from the card
# ("sd_writable" for meson_gx_mmc; U-Boot's first 4 MiB stay protected).
# No diagnostics and no data partition; extra options for image-add-diag.sh
# in DIAG_OPTS, packages in PACKAGES.
# TARGET=emmc: the same for an eMMC module, with the data partition (/Data,
# 1 GiB to the end of a 116.6 GiB module); a microSD stays read-only.
# UBOOT=<name>: tftp/<name>.sd.bin instead of tftp/u-boot-sd.sd.bin.
# The jam profile is $PROFILE (default c2-mmc, see c2-test-image.sh).
# Writing it: dd if=<image> of=/dev/<card> bs=4M conv=fsync (or Etcher).
# Usage: c2-sd-image.sh <name>   -> images/<name>.img and images/<name>.img.xz
set -e
root=$(cd "$(dirname "$0")/.." && pwd)
name="$1"
out="$root/images"
mkdir -p "$out"
uboot="$root/tftp/${UBOOT:-u-boot-sd}.sd.bin"
if [ ! -e "$uboot" ] && [ -z "$UBOOT" ]; then
	FRAGMENT=configs/u-boot-c2-sd.config "$root/tools/uboot-build.sh" u-boot-sd > /dev/null
fi

(cd "$root/generated.arm64" && jam -q -j14 @${PROFILE:-c2-mmc}) > "$root/logs/$name-build.log" 2>&1 \
	|| { echo "c2-sd-image: build failed, see logs/$name-build.log"; exit 1; }
image="$out/$name.img"
cp "$root/generated.arm64/haiku-mmc.image" "$image"
# the loader by mtools (fat_shell sometimes leaves it empty), checked
loader="$root/generated.arm64/objects/haiku/arm64/release/system/boot/efi/haiku_loader.efi"
mcopy -o -i "$image@@4M" "$loader" ::/EFI/BOOT/BOOTAA64.EFI
mcopy -i "$image@@4M" ::/EFI/BOOT/BOOTAA64.EFI "$root/run/esp-loader.check"
cmp -s "$loader" "$root/run/esp-loader.check" \
	|| { echo "c2-sd-image: loader in the ESP does not match"; exit 1; }

if [ "${TARGET:-sd}" = emmc ]; then
	opts="--no-diag"
else
	opts="--no-diag --no-data-partition --sd-writable"
fi
for package in $PACKAGES; do
	opts="$opts --package=$package"
done
"$root/tools/image-add-diag.sh" $opts $DIAG_OPTS "$image" > /dev/null

# U-Boot as tools/uboot-sd-update.sh writes it: the boot code bytes 0-439 of
# sector 0 (the partition table stays) and sectors 1-2048. Haiku's first
# partition starts at 4 MiB.
dd if="$uboot" of="$image" bs=440 count=1 conv=notrunc status=none
dd if="$uboot" of="$image" bs=512 skip=1 seek=1 count=2048 conv=notrunc status=none
first=$(dd if="$image" bs=1 skip=454 count=4 status=none | od -A n -t u4 | tr -d ' ')
[ "$first" -ge 8192 ] || { echo "c2-sd-image: a partition starts inside U-Boot"; exit 1; }

rm -f "$image.xz"
xz -T0 -k -6 "$image"
echo "c2-sd-image: $image ($(du -h "$image" | cut -f1)), $image.xz ($(du -h "$image.xz" | cut -f1))"

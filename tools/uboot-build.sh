#!/bin/sh
# Build U-Boot for the C2: odroid-c2_defconfig plus configs/u-boot-c2.config,
# signed with LibreELEC's amlogic-boot-fip. The u-boot/ tree must carry
# patches/u-boot/*.patch.
# Usage: uboot-build.sh <name>  ->  tftp/<name>.bin (raw, for uboot-chain.sh)
#                                   tftp/<name>.sd.bin (for uboot-sd-update.sh)
# FRAGMENT=configs/<file>: another config fragment (default u-boot-c2.config)
set -e
root=$(cd "$(dirname "$0")/.." && pwd)
name="$1"
log="$root/logs/$name-build.log"
fragment="$root/${FRAGMENT:-configs/u-boot-c2.config}"
cd "$root/u-boot"
for p in "$root"/patches/u-boot/*.patch; do
	git apply --check -R "$p" 2>/dev/null \
		|| { echo "uboot-build: $(basename "$p") is not applied in u-boot/"; exit 1; }
done
make -s odroid-c2_defconfig
scripts/kconfig/merge_config.sh -m -O . .config "$fragment" > "$log" 2>&1
make -s olddefconfig
grep -q "$(grep '^CONFIG_BOOTCOMMAND' "$fragment" | cut -c1-60)" .config \
	|| { echo "uboot-build: the config fragment was not applied"; exit 1; }
make -j14 CROSS_COMPILE=aarch64-linux-gnu- >> "$log" 2>&1
out="$root/run/fip-$name"
rm -rf "$out"; mkdir -p "$out"
(cd "$root/amlogic-boot-fip" && ./build-fip.sh odroid-c2 "$root/u-boot/u-boot.bin" "$out") >> "$log" 2>&1
cp "$root/u-boot/u-boot.bin" "$root/tftp/$name.bin"
cp "$out/u-boot.bin.sd.bin" "$root/tftp/$name.sd.bin"
echo "uboot-build: tftp/$name.bin, tftp/$name.sd.bin"

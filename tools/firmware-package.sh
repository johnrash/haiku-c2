#!/bin/sh
# The video decoder's firmware (milestone M26 (docs/milestones.md)) as a package of its own, outside
# the fork: firmware/meson_vdec_firmware-<version>-any.hpkg, with the files of
# firmware/meson/vdec/ in data/firmware/meson/vdec/ (where the meson_decoder
# media plugin reads them) and Amlogic's license. tools/c2-repo.py puts it in
# the repository; on a board: pkgman install meson_vdec_firmware.
# The version is linux-firmware's, where the files come from.
set -e
cd "$(dirname "$0")/.."
VERSION=20260319-3
PACKAGE=generated.arm64/objects/linux/x86_64/release/tools/package/package
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
mkdir -p "$work/data/firmware/meson/vdec" "$work/data/licenses"
cp firmware/meson/vdec/*.bin "$work/data/firmware/meson/vdec/"
cp firmware/meson/vdec/LICENSE.amlogic_vdec \
	"$work/data/licenses/Amlogic VDEC firmware"
cat > "$work/.PackageInfo" <<INFO
name			meson_vdec_firmware
version			$VERSION
architecture	any
summary			"Firmware for the Amlogic S905's video decoder"
description		"The firmware the ODROID-C2's video decoder runs: MPEG-1/2, \
H.264, HEVC and MJPEG for the S905 (GXBB), from linux-firmware. Haiku's meson_decoder media \
plugin decodes H.264 and HEVC with it, and the webcam add-on MJPEG; without it, FFmpeg \
and libjpeg-turbo decode."
packager		"haiku-odroid-c2 port"
vendor			"haiku-odroid-c2 port"
licenses		{ "Amlogic VDEC firmware" }
copyrights		{ "Amlogic, Inc." }
provides		{ meson_vdec_firmware = $VERSION }
requires		{ }
INFO
rm -f firmware/meson_vdec_firmware-*.hpkg
out=firmware/meson_vdec_firmware-$VERSION-any.hpkg
"$PACKAGE" create -q -C "$work" -i "$work/.PackageInfo" "$out"
"$PACKAGE" list "$out" | tail -8

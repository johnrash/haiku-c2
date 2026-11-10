#!/bin/sh
# Copyright 2026, haiku-odroid-c2 port authors.
# Distributed under the terms of the MIT License.
#
# The RTL8822B Wi-Fi chip's firmware (milestone M64) as a package of its own,
# outside the fork: firmware/realtek_rtw88_firmware-<version>-any.hpkg, with
# firmware/realtek/rtw88/rtw8822b_fw.bin in data/firmware/realtekwifi/
# (where the realtekwifi driver's RTL8822B support reads it) and Realtek's
# licence. The file is linux-firmware's (Ubuntu's linux-firmware-realtek
# 20260319.git217ca6e4 on this laptop, decompressed), and so is the version.
# On a board: c2-update installs it with the rest of firmware/.
set -e
cd "$(dirname "$0")/.."
VERSION=20260319-1
PACKAGE=generated.arm64/objects/linux/x86_64/release/tools/package/package
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
mkdir -p "$work/data/firmware/realtekwifi" "$work/data/licenses"
cp firmware/realtek/rtw88/rtw8822b_fw.bin "$work/data/firmware/realtekwifi/"
cp firmware/realtek/rtw88/LICENCE.rtlwifi_firmware.txt \
	"$work/data/licenses/Realtek rtlwifi firmware"
cat > "$work/.PackageInfo" <<INFO
name			realtek_rtw88_firmware
version			$VERSION
architecture	any
summary			"Firmware for Realtek RTL8822B Wi-Fi chips"
description		"The firmware the RTL8822B family of Wi-Fi chips runs (USB \
adapters sold as RTL8812BU or RTL8822BU, AC1200 and AC1300), from linux-firmware. \
The realtekwifi driver loads it."
packager		"haiku-odroid-c2 port"
vendor			"haiku-odroid-c2 port"
licenses		{ "Realtek rtlwifi firmware" }
copyrights		{ "2010, Realtek Semiconductor Corporation" }
provides		{ realtek_rtw88_firmware = $VERSION }
requires		{ }
INFO
rm -f firmware/realtek_rtw88_firmware-*.hpkg
out=firmware/realtek_rtw88_firmware-$VERSION-any.hpkg
"$PACKAGE" create -q -C "$work" -i "$work/.PackageInfo" "$out"
"$PACKAGE" list "$out" | tail -8

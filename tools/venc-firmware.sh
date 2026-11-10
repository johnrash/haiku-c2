#!/bin/sh
# Copyright 2026, haiku-odroid-c2 port authors.
# Distributed under the terms of the MIT License.
#
# The S905 H.264 encoder's microcode (milestone M48) as a package of its own,
# for one's own boards only: firmware/meson_venc_firmware-<version>-any.hpkg, with
# h264_enc_mc_gx.bin in data/firmware/meson/venc/ (where the meson_encoder
# media plugin and tools/probes/venctest read it) and Amlogic's notice.
#
# The microcode exists only in Amlogic's vendor kernel, as a hex array
# (Hardkernel's linux, odroidc2-3.14.y, arch/ucode/h264_enc/h264_enc_gx.h)
# that Amlogic labels GPL v2 or later, with no source. It is kept in no
# repository: this script downloads that file (about 26 KB, pinned to the
# commit docs/m48-venc-facts.md was read from), checks it, and writes the
# words as they sit in memory (little-endian, 8,280 bytes) to
# firmware/meson/venc/ (ignored by git).
#   tools/venc-firmware.sh        (then: tools/c2-repo.py, or pkgman on a board)
set -e
cd "$(dirname "$0")/.."
COMMIT=c3e4c730feb1750940971cae9ca1da3ca50f1d56
SOURCE=drivers/amlogic/amports/arch/ucode/h264_enc/h264_enc_gx.h
URL=https://raw.githubusercontent.com/hardkernel/linux/$COMMIT/$SOURCE
SOURCE_MD5=06dcbd9785fe94f71e6b2876bb655ed2
IMAGE_SHA256=778e4b7b71f2fb85fe0ede08a93835a94c7d8fa9f665c17cdbc9dbcaa0d41687
VERSION=20180303-1
PACKAGE=generated.arm64/objects/linux/x86_64/release/tools/package/package
OUT=firmware/meson/venc

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

echo "venc-firmware: downloading $URL"
curl -sSf -o "$work/h264_enc_gx.h" "$URL"
echo "venc-firmware: $(stat -c %s "$work/h264_enc_gx.h") bytes"
echo "$SOURCE_MD5  $work/h264_enc_gx.h" | md5sum -c --quiet

mkdir -p "$OUT"
python3 - "$work/h264_enc_gx.h" "$OUT/h264_enc_mc_gx.bin" <<'EOF'
import re
import struct
import sys
text = open(sys.argv[1]).read()
array = text[text.index("MicroCode[]"):]
array = array[array.index("{") + 1:array.index("}")]
words = [int(word, 16) for word in re.findall(r"0x[0-9a-fA-F]+", array)]
with open(sys.argv[2], "wb") as f:
	f.write(b"".join(struct.pack("<I", word) for word in words))
print("venc-firmware: %d words" % len(words))
EOF
echo "$IMAGE_SHA256  $OUT/h264_enc_mc_gx.bin" | sha256sum -c --quiet

# Amlogic's notice, as the source file has it
sed -n '1,/^\*\//p' "$work/h264_enc_gx.h" > "$OUT/NOTICE.amlogic_venc"
cat >> "$OUT/NOTICE.amlogic_venc" <<NOTICE

h264_enc_mc_gx.bin is the array MicroCode[] of the file above, converted to
bytes as they sit in memory (32-bit little-endian words), from
$URL
No source for this microcode has been published.
NOTICE

mkdir -p "$work/package/data/firmware/meson/venc" "$work/package/data/licenses"
cp "$OUT/h264_enc_mc_gx.bin" "$work/package/data/firmware/meson/venc/"
cp "$OUT/NOTICE.amlogic_venc" "$work/package/data/firmware/meson/venc/"
cat > "$work/package/.PackageInfo" <<INFO
name			meson_venc_firmware
version			$VERSION
architecture	any
summary			"Microcode for the Amlogic S905's H.264 encoder"
description		"The microcode the ODROID-C2's H.264 encoder (HCODEC) runs, \
from Amlogic's vendor kernel (h264_enc_mc_gx). Haiku's meson_encoder media \
plugin encodes H.264 in hardware with it. For the port's own boards only: \
Amlogic labels it GPL v2 or later and has published no source for it."
packager		"haiku-odroid-c2 port"
vendor			"haiku-odroid-c2 port"
licenses		{ "GNU GPL v2" }
copyrights		{ "2015 Amlogic, Inc." }
provides		{ meson_venc_firmware = $VERSION }
INFO
"$PACKAGE" create -C "$work/package" \
	"firmware/meson_venc_firmware-$VERSION-any.hpkg"
echo "venc-firmware: firmware/meson_venc_firmware-$VERSION-any.hpkg"

#!/bin/bash
# Copyright 2026, haiku-odroid-c2 port authors.
# Distributed under the terms of the MIT License.
#
# haiku-setup.sh: the source trees this port builds from, next to tools/:
#   haiku/       upstream Haiku at the port's base, a12d4cd1a5 (hrev60299+6),
#                with haiku-patches/ applied on the branch odroid-c2 (git am:
#                the port's commits, their authors and messages kept). Full
#                history and tags: the build's version comes from them
#                ("hrev60299+348" at patch 0342).
#   buildtools/  Haiku's cross-toolchain sources at 33d46bf5c3 (binutils
#                2.46.1, gcc 13.3.0), with
#                patches/binutils/ld-2.46.1-aarch64-843419-rescan.patch.
#   u-boot/      U-Boot v2026.10 with patches/u-boot/.
#   amlogic-boot-fip/  LibreELEC's boot blobs for the C2, at b6600f2857.
# Then, for the cross tools (see docs/BUILDING.md):
#   mkdir generated.arm64 && cd generated.arm64 &&
#   ../haiku/configure --cross-tools-source ../buildtools \
#       --build-cross-tools arm64 -j$(nproc)
# Downloads Haiku's and buildtools' full histories from GitHub (over 1 GB).
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
HAIKU_URL=${HAIKU_URL:-https://github.com/haiku/haiku.git}
HAIKU_BASE=a12d4cd1a5bd457078cb3a53ce265a364c5528b7
BUILDTOOLS_URL=${BUILDTOOLS_URL:-https://github.com/haiku/buildtools.git}
BUILDTOOLS_BASE=33d46bf5c300bd34cd07c596431e4545c754ac03
UBOOT_URL=${UBOOT_URL:-https://github.com/u-boot/u-boot.git}
UBOOT_TAG=v2026.10
FIP_URL=${FIP_URL:-https://github.com/LibreELEC/amlogic-boot-fip.git}
FIP_BASE=b6600f2857fdf81d11f18ea7a2e44aa989e7bcc4

cd "$ROOT"

if [ ! -d haiku ]; then
	git clone "$HAIKU_URL" haiku
	git -C haiku checkout -b odroid-c2 "$HAIKU_BASE"
	# the patches keep their authors and dates; the committer is whoever
	# runs this
	git -C haiku am --whitespace=nowarn "$ROOT"/haiku-patches/*.patch
	echo "haiku-setup: haiku/ on odroid-c2, $(git -C haiku describe --tags)"
else
	echo "haiku-setup: haiku/ is there already, left as it is"
fi

if [ ! -d buildtools ]; then
	git clone "$BUILDTOOLS_URL" buildtools
	git -C buildtools checkout -q "$BUILDTOOLS_BASE"
	git -C buildtools apply \
		"$ROOT/patches/binutils/ld-2.46.1-aarch64-843419-rescan.patch"
	echo "haiku-setup: buildtools/ at ${BUILDTOOLS_BASE:0:10}, ld patched"
fi

if [ ! -d u-boot ]; then
	git clone --branch "$UBOOT_TAG" --depth 1 "$UBOOT_URL" u-boot
	for patch in "$ROOT"/patches/u-boot/*.patch; do
		git -C u-boot apply "$patch"
	done
	echo "haiku-setup: u-boot/ at $UBOOT_TAG, patched"
fi

if [ ! -d amlogic-boot-fip ]; then
	git clone "$FIP_URL" amlogic-boot-fip
	git -C amlogic-boot-fip checkout -q "$FIP_BASE"
	echo "haiku-setup: amlogic-boot-fip/ at ${FIP_BASE:0:10}"
fi

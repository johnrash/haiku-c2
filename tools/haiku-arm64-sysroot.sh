#!/bin/sh
# Copyright 2026, haiku-odroid-c2 port authors.
# Distributed under the terms of the MIT License.
#
# A Haiku arm64 sysroot in the installed layout, for ports whose build systems
# want --sysroot= (Rust, Firefox's bindgen): the packages a Haiku system with
# its development files would have, extracted into <dir>/boot/system/, where
# the cross gcc's built-in search paths look (<sysroot>/boot/system/develop/
# {headers/*,lib}, buildtools gcc/config/haiku.h). After haiku-pi's
# scripts/haiku-sysroot.sh (https://github.com/haiku-pi/haiku-rpi, MIT).
#   haiku.hpkg, haiku_devel.hpkg      generated.arm64's (our fork's)
#   gcc_syslibs(_devel), zlib(_devel)  the bootstrap packages the image has
# deps/sysroot (tools/haiku-arm64-env.sh) is a different thing: libraries only,
# headers through -I.
# Usage: tools/haiku-arm64-sysroot.sh [<dir>]   (default deps/sysroot-full;
# the directory is wiped first)
set -e
root=$(cd "$(dirname "$0")/.." && pwd)
gen="$root/generated.arm64"
out="${1:-$root/deps/sysroot-full}"
pkgtool="$gen/objects/linux/x86_64/release/tools/package/package"
packages="$gen/objects/haiku/arm64/packaging/packages/haiku.hpkg
$gen/objects/haiku/arm64/packaging/packages/haiku_devel.hpkg"
for p in gcc_syslibs gcc_syslibs_devel zlib zlib_devel; do
	f=$(ls "$gen"/download/"$p"-[0-9]*-arm64.hpkg 2>/dev/null | sort -V | tail -n 1)
	[ -n "$f" ] || { echo "haiku-arm64-sysroot: no $p arm64 package" >&2; exit 1; }
	packages="$packages
$f"
done

rm -rf "$out"
mkdir -p "$out/boot/system"
echo "$packages" | while read -r p; do
	LD_LIBRARY_PATH="$gen/objects/linux/lib" "$pkgtool" extract \
		-C "$out/boot/system" "$p"
	echo "haiku-arm64-sysroot: $(basename "$p")"
done
ls "$out/boot/system/develop/headers" "$out/boot/system/develop/lib" | head -40

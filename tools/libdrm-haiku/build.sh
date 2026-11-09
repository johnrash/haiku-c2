#!/bin/bash
# Copyright 2026, haiku-odroid-c2 port authors.
# Distributed under the terms of the MIT License.
#
# Builds the libdrm stand-in for Haiku arm64 (docs/m43-mesa-lima-haiku.md
# §3.5): a static, position-independent libdrm.a with the libdrm functions
# Mesa's lima driver calls, for the meson_mali driver. Static, so that the
# libraries that only might use it (libGL, OSMesa, Software Pipe) get no new
# runtime dependency; its symbols are hidden, so that the add-on linked with
# it doesn't export them. tools/mesa-build.sh runs it and writes libdrm.pc
# (libdrm.pc.in) for Mesa's meson.
# Usage: build.sh <Mesa source> <output folder>   -> <output folder>/libdrm.a
#   the Mesa source has the drm-uapi headers the stand-in uses
set -e
. "$(dirname "$0")/../haiku-arm64-env.sh"
HERE=$(cd "$(dirname "$0")" && pwd)
MESA=$1
OUT=$2
if [ ! -e "$MESA/include/drm-uapi/lima_drm.h" ] || [ -z "$OUT" ]; then
	echo "usage: $0 <Mesa source> <output folder>" >&2
	exit 1
fi
mkdir -p "$OUT"

# include/ first: its <sys/ioccom.h> must win over Haiku's BSD one
$CC -std=gnu11 -O2 -fPIC -fvisibility=hidden -Wall -Wextra -Werror \
	-I"$HERE/include" -I"$MESA/include" \
	-I"$HAIKU_SRC/headers/private/drivers" $HAIKU_CPPFLAGS \
	-c "$HERE/src/xf86drm.c" -o "$OUT/xf86drm.o"
rm -f "$OUT/libdrm.a"
"$AR" rcs "$OUT/libdrm.a" "$OUT/xf86drm.o"
echo "libdrm-haiku: $OUT/libdrm.a"

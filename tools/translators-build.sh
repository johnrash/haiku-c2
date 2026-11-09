#!/bin/bash
# Haiku's JPEG and PNG translators for arm64, built outside jam: the build
# has no jpeg and libpng build features (no arm64 packages), so the regular
# image leaves them out, and ShowImage and other apps could not open JPEG or
# PNG files. Built from the Haiku tree like jam's Translator rule does
# (executable add-ons), with libjpeg-turbo and libpng from tools/deps-build.sh
# linked statically, and packaged as haiku_translators_c2. (The JPEG
# translator needs haiku's arm64 __swap_float fix for its bitmap headers.)
# Usage: translators-build.sh   -> deps/translators/haiku_translators_c2-1-1-arm64.hpkg
set -e
. "$(dirname "$0")/haiku-arm64-env.sh"
T=$HAIKU_SRC/src/add-ons/translators
UTILS=$HAIKU_GEN/objects/haiku/arm64/release/add-ons/translators/shared/libtranslatorsutils.a
OUT=$HAIKU_PROJECT/deps/translators
BUILD=$HAIKU_PROJECT/deps/build/translators
LOG=$HAIKU_PROJECT/logs/translators-build.log
haiku_rm_tree "$BUILD"
mkdir -p "$BUILD" "$OUT"
: > "$LOG"
export PATH="$HAIKU_PROJECT/tools/cross:$PATH"

# translator <name> <dir> <libraries> <sources...>
translator()
{
	local name=$1 dir=$2 libs=$3 objects="" source
	shift 3
	for source in "$@"; do
		c++ -O2 -c -I"$T/shared" -I"$T/raw" -I"$HAIKU_PREFIX/include" \
			-I"$HAIKU_SRC/headers/private/shared" "$dir/$source" \
			-o "$BUILD/$name-${source%.cpp}.o" >> "$LOG" 2>&1
		objects="$objects $BUILD/$name-${source%.cpp}.o"
	done
	c++ -o "$BUILD/$name" $objects -Xlinker -soname=$name "$UTILS" $libs \
		-lbe -ltranslation -llocalestub >> "$LOG" 2>&1
	# strip first: it would drop the resources xres appends
	"$STRIP" "$BUILD/$name"
	(cd "$dir" && "$HAIKU_HOST_TOOLS/rc/rc" -o "$BUILD/$name.rsrc" "$name.rdef") >> "$LOG" 2>&1
	"$HAIKU_HOST_TOOLS/xres" -o "$BUILD/$name" "$BUILD/$name.rsrc"
	echo "translators-build: $name"
}

translator JPEGTranslator "$T/jpeg" "$HAIKU_PREFIX/lib/libjpeg.a" \
	be_jdatadst.cpp be_jdatasrc.cpp be_jerror.cpp exif_parser.cpp JPEGTranslator.cpp
translator PNGTranslator "$T/png" "$HAIKU_PREFIX/lib/libpng16.a -lz" \
	PNGMain.cpp PNGTranslator.cpp PNGView.cpp

P=$OUT/pkg
haiku_rm_tree "$P"
mkdir -p "$P/add-ons/Translators"
cp "$BUILD/JPEGTranslator" "$BUILD/PNGTranslator" "$P/add-ons/Translators/"
cat > "$P/.PackageInfo" <<EOF
name			haiku_translators_c2
version			1-1
architecture	arm64
summary			"Haiku's JPEG and PNG translators"
description		"JPEGTranslator and PNGTranslator from the Haiku source tree, built for arm64 (ODROID-C2 port) with libjpeg-turbo 3.2.1 and libpng 1.6.59 linked statically."
packager		"haiku-odroid-c2 port"
vendor			"Haiku, Inc."
copyrights		"2001-2026 Haiku, Inc. and contributors"
licenses		"MIT"
provides {
	haiku_translators_c2 = 1
}
requires {
	haiku >= r1~beta6
	lib:libz
}
EOF
HPKG=$OUT/haiku_translators_c2-1-1-arm64.hpkg
rm -f "$HPKG"
(cd "$P" && package create -q "$HPKG")
echo "translators-build: $HPKG ($(stat -c %s "$HPKG") bytes)"

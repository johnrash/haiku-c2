#!/bin/bash
# The arm64 bootstrap ICU package (icu74-74.1_bootstrap-1) has a stub
# libicudata.so and the real data in data/icu/74.1/icudt74l.dat, where ICU
# never looks: its built-in data directory is the bootstrap build's
# /packages/icu74_bootstrap-74.1-1/.self/data/icu/74.1, which does not exist.
# Every ICU formatter then gives empty strings (About: "Time running:").
# This repackages it with a libicudata.so.74.1 that has the data built in, as
# ICU's own "dll" packaging does (the icudt74_dat symbol over the .dat file's
# bytes), and without the separate .dat. Same package name and version, so
# image-add-diag.sh --icu-data puts it in place of the original.
# Its libraries are also patched for Cortex-A53 erratum 843419
# (tools/a53-843419.py): the bootstrap build was linked without the
# workaround, and the sequence in uhash_openSize (libicuuc 0x11bff8) made
# hash tables pick up garbage resize ratios on the C2, so about 1 in 40
# programs crashed in setlocale() (milestone M7 (docs/milestones.md)).
# Usage: icu-data-build.sh       -> deps/icu/icu74-74.1_bootstrap-1-arm64.hpkg
set -e
. "$(dirname "$0")/haiku-arm64-env.sh"
NAME=icu74-74.1_bootstrap-1-arm64
ORIG=$HAIKU_GEN/download/$NAME.hpkg
OUT=$HAIKU_PROJECT/deps/icu
PKG=$OUT/pkg
PACKAGE=$HAIKU_HOST_TOOLS/package/package

haiku_rm_tree "$PKG"
mkdir -p "$PKG"
"$PACKAGE" extract -C "$PKG" "$ORIG"
# the libraries rebuilt with the erratum 835769 workaround, if there are any
# (tools/bootstrap-rebuild.sh icu74)
for f in "$HAIKU_PROJECT"/deps/bootstrap/rebuilt/icu74/lib/*; do
	[ -f "$f" ] || continue
	cp "$f" "$PKG/lib/$(basename "$f")"
	haiku_mimeset "$PKG/lib/$(basename "$f")"
done
find "$PKG/lib" "$PKG/bin" -type f -print0 \
	| xargs -0 python3 "$HAIKU_PROJECT/tools/a53-843419.py" --fix \
	|| { echo "icu-data-build: erratum 843419 sequences left"; exit 1; }
DATA=$PKG/data/icu/74.1/icudt74l.dat
[ -s "$DATA" ] || { echo "icu-data-build: no icudt74l.dat in $ORIG"; exit 1; }

cat > "$OUT/icudata.S" <<EOF
	.section .rodata
	.balign 16
	.globl icudt74_dat
	.type icudt74_dat, %object
icudt74_dat:
	.incbin "$DATA"
	.size icudt74_dat, . - icudt74_dat
	.section .note.GNU-stack, "", %progbits
EOF
LIB=$PKG/lib/libicudata.so.74.1
rm -f "$LIB"
$CC -shared -Wl,-soname=libicudata.so.74 -o "$LIB" "$OUT/icudata.S"
rm -f "$DATA"
haiku_mimeset "$LIB"

HPKG=$OUT/$NAME.hpkg
rm -f "$HPKG"
(cd "$PKG" && "$PACKAGE" create -q "$HPKG")
echo "icu-data-build: $HPKG ($(stat -c %s "$HPKG") bytes)"

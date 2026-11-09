#!/bin/bash
# Copyright 2026, haiku-odroid-c2 port authors.
# Distributed under the terms of the MIT License.
#
# Cross-compile wpa_supplicant for Haiku arm64 as HaikuPorts' recipe builds
# it (wpa_supplicant-2.11.haiku.1: Haiku's fork at $REV, with the recipe's
# two patches, all in deps/src, and ours from patches/wpa_supplicant) and
# package it as deps/wpa_supplicant/wpa_supplicant-2.11.haiku.1-2-arm64.hpkg. net_server
# starts it by its signature to join WPA networks (milestone M64 (docs/milestones.md)).
# OpenSSL is linked statically from deps/prefix (deps-build.sh), as OpenSSH
# is. No catalogs: linkcatkeys isn't among the build's host tools, so its
# dialog is in English only.
# Usage: wpa-supplicant-build.sh
set -e
. "$(dirname "$0")/haiku-arm64-env.sh"
VERSION=2.11.haiku.1
REVISION=6
REV=bdf3144ad606b226aca6f8fae103e671e4655138
SRC=$HAIKU_PROJECT/deps/build/wpa_supplicant-$REV
OUT=$HAIKU_PROJECT/deps/wpa_supplicant
LOG=$HAIKU_PROJECT/logs/wpa-supplicant-build.log

rm -rf "$SRC"
mkdir -p "$(dirname "$SRC")" "$OUT"
tar xzf "$HAIKU_PROJECT/deps/src/wpa_supplicant-$REV.tar.gz" \
	-C "$(dirname "$SRC")"
(cd "$SRC" \
	&& patch -p1 -s < "$HAIKU_PROJECT/deps/src/wpa_supplicant-2.11-security.patchset" \
	&& patch -p1 -s < "$HAIKU_PROJECT/deps/src/wpa_supplicant-extend-scanresults.patch" \
	&& patch -p1 -s < "$HAIKU_PROJECT/patches/wpa_supplicant/10-access-point-choice.patch")

# The fork's .config, with its develop/headers/private paths pointing into
# the Haiku tree (haiku_devel copies them from there)
sed -e "s|/system/develop/headers/private/libs/compat/|$HAIKU_SRC/src/libs/compat/|" \
	-e "s|/system/develop/headers/private/net|$HAIKU_SRC/headers/private/net|" \
	-e "s|^LDO = g++|LDO = $CXX|" \
	"$SRC/wpa_supplicant/.config" > "$SRC/wpa_supplicant/.config.cross"
mv "$SRC/wpa_supplicant/.config.cross" "$SRC/wpa_supplicant/.config"

# rc and xres from the build's host tools; mimeset is done below
TOOLS=$SRC/host-tools
mkdir -p "$TOOLS"
ln -sf "$HAIKU_HOST_TOOLS/rc/rc" "$HAIKU_HOST_TOOLS/xres" "$TOOLS/"
printf '#!/bin/sh\nexit 0\n' > "$TOOLS/mimeset"
chmod +x "$TOOLS/mimeset"

# C and C++ go through $(CC) with the same flags, so the C library headers
# come after libstdc++'s (-idirafter) for both. CFLAGS and LDFLAGS from the
# environment: on make's command line they'd hide the Makefile's additions.
PATH="$TOOLS:$PATH" CFLAGS="-MMD -O2 -Wall -fPIC $HAIKU_CXX_INCLUDES" \
	LDFLAGS="$LDFLAGS" make -C "$SRC/wpa_supplicant" -j14 wpa_supplicant \
	CC="$CC" LDO="$CXX" EXTRALIBS="-lcrypto $HAIKU_CXX_LIBS" \
	V=1 > "$LOG" 2>&1 || { tail -30 "$LOG"; exit 1; }

PKG=$OUT/pkg
haiku_rm_tree "$PKG"
mkdir -p "$PKG/bin" "$PKG/documentation/packages/wpa_supplicant"
cp "$SRC/wpa_supplicant/wpa_supplicant" "$PKG/bin/"
# strip drops the resources (the signature net_server launches it by), so
# they go on again afterwards
"$STRIP" "$PKG/bin/wpa_supplicant"
"$HAIKU_HOST_TOOLS/xres" -o "$PKG/bin/wpa_supplicant" \
	"$SRC/wpa_supplicant/wpa_gui-haiku/wpa_supplicant.rsrc"
haiku_mimeset "$PKG/bin/wpa_supplicant"
cp "$SRC/COPYING" "$SRC/README" "$PKG/documentation/packages/wpa_supplicant/"
cat > "$PKG/.PackageInfo" <<INFO
name			wpa_supplicant
version			${VERSION}-${REVISION}
architecture	arm64
summary			"A WPA supplicant with support for WPA and WPA2"
description		"wpa_supplicant $VERSION (Haiku's fork, as HaikuPorts builds it), cross-compiled for Haiku arm64 (ODROID-C2) with OpenSSL linked statically. net_server starts it to join WPA networks."
packager		"haiku-odroid-c2 port"
vendor			"Jouni Malinen"
copyrights		"2003-2024 Jouni Malinen <j@w1.fi> and contributors"
licenses {
	"BSD (3-clause)"
}
urls			"https://w1.fi/wpa_supplicant/"
provides {
	wpa_supplicant = ${VERSION} compat >= 2
	cmd:wpa_supplicant = ${VERSION} compat >= 2
}
requires {
	haiku
}
INFO
rm -f "$OUT"/wpa_supplicant-${VERSION}-*-arm64.hpkg
(cd "$PKG" && "$HAIKU_HOST_TOOLS/package/package" create -q \
	"$OUT/wpa_supplicant-${VERSION}-${REVISION}-arm64.hpkg")
ls -l "$OUT/wpa_supplicant-${VERSION}-${REVISION}-arm64.hpkg"

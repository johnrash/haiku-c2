#!/bin/bash
# Cross-compile OpenSSH portable (deps/src/openssh-<version>.tar.gz) for Haiku
# arm64 and package it as openssh-<version>-1-arm64.hpkg. OpenSSL is linked
# statically from deps/prefix (see deps-build.sh), zlib comes from the image.
# The package declares the "sshd" privilege separation user; the
# configuration and host keys are made on first boot by
# tools/guest/c2-system.sh (marker c2-ssh).
# Usage: openssh-build.sh        -> deps/openssh/openssh-*-arm64.hpkg
set -e
. "$(dirname "$0")/haiku-arm64-env.sh"
VERSION=${VERSION:-10.6p1}
SRC=$HAIKU_PROJECT/deps/build/openssh-$VERSION
OUT=$HAIKU_PROJECT/deps/openssh

if [ ! -e "$SRC/Makefile" ]; then
	mkdir -p "$(dirname "$SRC")"
	tar xzf "$HAIKU_PROJECT/deps/src/openssh-$VERSION.tar.gz" -C "$(dirname "$SRC")"
	(cd "$SRC" && ./configure --host=aarch64-unknown-haiku --prefix=/boot/system \
		--bindir=/boot/system/bin --sbindir=/boot/system/bin \
		--libexecdir=/boot/system/lib/openssh \
		--sysconfdir=/boot/system/settings/ssh --datadir=/boot/system/data \
		--mandir=/boot/system/documentation/man \
		--with-privsep-path=/boot/system/var/empty \
		--with-pid-dir=/boot/system/var/run --with-ssl-dir="$HAIKU_PREFIX" \
		--with-zlib="$(ls -d $HAIKU_GEN/build_packages/zlib-*_bootstrap-*-arm64/develop)" \
		--disable-strip --without-security-key-builtin CFLAGS="-O2 -fPIC") \
		> "$HAIKU_PROJECT/logs/openssh-configure.log" 2>&1
fi
make -C "$SRC" -j14 > "$HAIKU_PROJECT/logs/openssh-make.log" 2>&1

PKG=$OUT/pkg
haiku_rm_tree "$PKG"
mkdir -p "$PKG/bin" "$PKG/lib/openssh" "$PKG/documentation/packages/openssh"
for f in ssh scp sftp ssh-add ssh-agent ssh-keygen ssh-keyscan sshd; do
	cp "$SRC/$f" "$PKG/bin/"
done
for f in sshd-session sshd-auth sftp-server ssh-keysign ssh-pkcs11-helper \
		ssh-sk-helper; do
	cp "$SRC/$f" "$PKG/lib/openssh/"
done
"$STRIP" "$PKG"/bin/* "$PKG"/lib/openssh/*
haiku_mimeset "$PKG"/bin/* "$PKG"/lib/openssh/*
cp "$SRC/LICENCE" "$SRC/README" "$PKG/documentation/packages/openssh/"
cat > "$PKG/.PackageInfo" <<INFO
name			openssh
version			${VERSION}-1
architecture	arm64
summary			"OpenSSH secure shell client and server"
description		"OpenSSH $VERSION, cross-compiled for Haiku arm64 (ODROID-C2) with OpenSSL linked statically."
packager		"haiku-odroid-c2 port"
vendor			"OpenBSD project"
copyrights		"1995-2026 Tatu Ylonen, The OpenBSD project and contributors"
licenses {
	"BSD (2-clause)"
	"BSD (3-clause)"
	"ISC"
	"MIT"
}

provides {
	openssh = ${VERSION}
	cmd:scp
	cmd:sftp
	cmd:ssh
	cmd:ssh_add
	cmd:ssh_agent
	cmd:ssh_keygen
	cmd:ssh_keyscan
	cmd:sshd
}

requires {
	haiku >= r1~beta6
	lib:libz >= 1
}

groups {
	sshd
}

users {
	sshd real-name "sshd privilege separation" home "/boot/system/var/empty" shell "/bin/true" groups sshd
}
INFO
HPKG=$OUT/openssh-${VERSION}-1-arm64.hpkg
rm -f "$HPKG"
(cd "$PKG" && "$HAIKU_HOST_TOOLS/package/package" create -q "$HPKG")
echo "openssh-build: $HPKG ($(stat -c %s "$HPKG") bytes)"

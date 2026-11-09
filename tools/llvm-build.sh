#!/bin/bash
# Cross-compile LLVM 12.0.1 (deps/src/llvm12: the release tarball and
# HaikuPorts' llvm-12.0.1.patchset, the version Haiku's Mesa 22.0.5 uses) for
# Haiku arm64: libLLVM-12.so with the AArch64 back end, for Mesa's llvmpipe.
# No clang, no tools, no tests. First an llvm-tblgen for this machine, then
# the cross build with the ports sysroot (tools/ports-build.sh). GCC 13 needs
# <cstdint> in a few LLVM 12 headers that don't include it: it is
# force-included (CMAKE_CXX_FLAGS replaces the toolchain file's initial flags,
# so the Cortex-A53 errata flags go in again with it).
# Usage: llvm-build.sh  -> deps/llvm/llvm12_libs-12.0.1-1-arm64.hpkg and
#                          deps/build/llvm12-stage (headers and library for
#                          building Mesa)
set -e
. "$(dirname "$0")/haiku-arm64-env.sh"
VERSION=12.0.1
SRCS=$HAIKU_PROJECT/deps/src/llvm12
WORK=$HAIKU_PROJECT/deps/build
SRC=$WORK/llvm-$VERSION.src
HOST=$WORK/llvm12-host
CROSS=$WORK/llvm12-cross
STAGE=$WORK/llvm12-stage
OUT=$HAIKU_PROJECT/deps/llvm
LOG=$HAIKU_PROJECT/logs/llvm-build.log
TOOLCHAIN=$WORK/ports/haiku-arm64.cmake
mkdir -p "$OUT"
: > "$LOG"

[ -e "$TOOLCHAIN" ] || { echo "run tools/ports-build.sh first (sysroot)"; exit 1; }
echo "7d9a8405f557cefc5a21bf5672af73903b64749d9bc3a50322239f56f34ffddf  $SRCS/llvm-$VERSION.src.tar.xz" \
	| sha256sum -c >> "$LOG"

if [ ! -e "$SRC/.patched" ]; then
	rm -rf "$SRC"
	tar xJf "$SRCS/llvm-$VERSION.src.tar.xz" -C "$WORK"
	(cd "$SRC" && patch -p1 < "$SRCS/llvm-$VERSION.patchset") >> "$LOG" 2>&1
	touch "$SRC/.patched"
fi

COMMON="-G Ninja -DCMAKE_BUILD_TYPE=Release -DLLVM_TARGETS_TO_BUILD=AArch64
	-DLLVM_INCLUDE_TESTS=OFF -DLLVM_INCLUDE_EXAMPLES=OFF
	-DLLVM_INCLUDE_BENCHMARKS=OFF -DLLVM_ENABLE_TERMINFO=OFF
	-DLLVM_ENABLE_ZLIB=OFF -DLLVM_ENABLE_LIBXML2=OFF
	-DCMAKE_POLICY_VERSION_MINIMUM=3.5"

if [ ! -x "$HOST/bin/llvm-tblgen" ]; then
	rm -rf "$HOST" && mkdir -p "$HOST"
	(cd "$HOST" && cmake $COMMON "$SRC" -DCMAKE_CXX_FLAGS="-include cstdint" \
		&& ninja llvm-tblgen) >> "$LOG" 2>&1
fi

rm -rf "$STAGE" && mkdir -p "$CROSS"
[ -e "$CROSS/build.ninja" ] || (cd "$CROSS" && cmake $COMMON "$SRC" \
	-DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" \
	-DLLVM_TABLEGEN="$HOST/bin/llvm-tblgen" \
	-DLLVM_DEFAULT_TARGET_TRIPLE=aarch64-unknown-haiku \
	-DLLVM_HOST_TRIPLE=aarch64-unknown-haiku \
	-DLLVM_BUILD_LLVM_DYLIB=ON -DLLVM_LINK_LLVM_DYLIB=ON \
	-DLLVM_ENABLE_RTTI=ON -DLLVM_INCLUDE_UTILS=OFF -DLLVM_BUILD_TOOLS=OFF \
	-DLLVM_ENABLE_LIBEDIT=OFF -DLLVM_ENABLE_BINDINGS=OFF \
	-DCMAKE_CXX_FLAGS="$HAIKU_ERRATA_FLAGS -include cstdint" \
	-DCMAKE_INSTALL_PREFIX=/boot/system) >> "$LOG" 2>&1
(cd "$CROSS" && ninja LLVM && DESTDIR="$STAGE" ninja install) >> "$LOG" 2>&1

lib=$STAGE/boot/system/lib/libLLVM-12.so
[ -e "$lib" ] || { echo "no $lib"; exit 1; }
python3 "$HAIKU_PROJECT/tools/a53-843419.py" "$lib"
python3 "$HAIKU_PROJECT/tools/a53-835769.py" "$lib"

# the runtime package, as HaikuPorts' llvm12_libs
P=$OUT/pkg
haiku_rm_tree "$P"
mkdir -p "$P/lib" "$P/data/licenses"
cp "$lib" "$P/lib/"
"$STRIP" "$P/lib/libLLVM-12.so"
cp "$SRC/LICENSE.TXT" "$P/data/licenses/Apache v2 with LLVM Exception"
cat > "$P/.PackageInfo" <<INFO
name			llvm12_libs
version			$VERSION-1
architecture	arm64
summary			"LLVM 12 shared library"
description		"libLLVM-12.so with the AArch64 back end, cross-compiled for Haiku arm64 (ODROID-C2 port) for Mesa's llvmpipe."
packager		"haiku-odroid-c2 port"
vendor			"The LLVM project"
copyrights		"2003-2021 University of Illinois at Urbana-Champaign"
licenses		"Apache v2 with LLVM Exception"
provides {
	llvm12_libs = $VERSION
	lib:libLLVM_12 = $VERSION
}
requires {
	haiku >= r1~beta6
	lib:libgcc_s
	lib:libstdc++
}
INFO
rm -f "$OUT"/llvm12_libs-*.hpkg
(cd "$P" && "$HAIKU_HOST_TOOLS/package/package" create -q \
	"$OUT/llvm12_libs-$VERSION-1-arm64.hpkg")
ls -la "$OUT"/*.hpkg

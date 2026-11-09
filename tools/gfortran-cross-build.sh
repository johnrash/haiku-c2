#!/bin/bash
# Copyright 2026, haiku-odroid-c2 port authors.
# Distributed under the terms of the MIT License.
#
# gfortran-cross-build.sh: a second arm64 cross GCC 13.3, from the same
# buildtools sources as Haiku's own cross tools, with Fortran
# (OpenBLAS's LAPACK and SciPy need it; milestone M83 (docs/milestones.md)). Haiku's cross tools are
# built with --disable-shared and C/C++ only. This one also builds the target
# libraries shared, for libgfortran.so.5 (packaged by tools/ports-build.sh as
# gcc_fortran_syslibs); its libstdc++ and libgcc_s are not used, the boards
# keep gcc_syslibs'. The Cortex-A53 erratum workarounds are on by default, and
# the target libraries are built with them (libgfortran had 212 sequences
# without). Installs to deps/cross-fortran; the binutils are Haiku's.
set -e
. "$(dirname "$0")/haiku-arm64-env.sh"
# GCC itself is built with the host compiler
unset CC CXX AR RANLIB CFLAGS CXXFLAGS CPPFLAGS LDFLAGS PKG_CONFIG_PATH PKG_CONFIG_LIBDIR PKG_CONFIG_SYSROOT_DIR
PREFIX=$HAIKU_PROJECT/deps/cross-fortran
BUILD=$HAIKU_PROJECT/deps/build/gcc-fortran
TARGET=aarch64-unknown-haiku
# the ports sysroot (tools/ports-build.sh): Haiku's headers and libraries
SYSROOT=$HAIKU_PROJECT/deps/build/ports/sysroot
JOBS=${JOBS:-8}

rm -rf "$BUILD"
mkdir -p "$BUILD" "$PREFIX/bin" "$PREFIX/$TARGET/bin"
# the target binutils where GCC looks for them
for t in as ld ld.bfd ar nm ranlib strip objcopy objdump readelf; do
	ln -sf "$HAIKU_TOOLS/$TARGET-$t" "$PREFIX/bin/$TARGET-$t"
	ln -sf "$HAIKU_TOOLS/$TARGET-$t" "$PREFIX/$TARGET/bin/$t"
done
cd "$BUILD"
"$HAIKU_PROJECT/buildtools/gcc/configure" --prefix="$PREFIX" \
	--target=$TARGET --disable-nls --enable-shared --with-system-zlib \
	--enable-languages=c,c++,fortran --enable-lto --enable-frame-pointer \
	--enable-__cxa-atexit --enable-threads=posix \
	--with-default-libstdcxx-abi=gcc4-compatible --with-sysroot="$SYSROOT" \
	--with-as="$HAIKU_TOOLS/$TARGET-as" --with-ld="$HAIKU_TOOLS/$TARGET-ld" \
	--disable-maintainer-mode --disable-libgomp --disable-libatomic \
	--disable-multilib --disable-libsanitizer --disable-libitm \
	--disable-libvtv --disable-libssp \
	--enable-fix-cortex-a53-835769 --enable-fix-cortex-a53-843419 \
	CFLAGS_FOR_TARGET="-O2 -g $HAIKU_ERRATA_FLAGS" \
	CXXFLAGS_FOR_TARGET="-O2 -g $HAIKU_ERRATA_FLAGS" \
	FCFLAGS_FOR_TARGET="-O2 -g $HAIKU_ERRATA_FLAGS" \
	LDFLAGS_FOR_TARGET="$HAIKU_ERRATA_FLAGS"
make -j"$JOBS"
make install
echo "gfortran-cross-build: $("$PREFIX/bin/$TARGET-gfortran" --version | head -1)"
ls -la "$PREFIX/$TARGET/lib/"libgfortran.so* 2>/dev/null \
	|| find "$PREFIX" -name 'libgfortran.so*'

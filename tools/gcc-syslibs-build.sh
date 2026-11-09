#!/bin/bash
# Rebuild gcc_syslibs (libstdc++, libsupc++, libgcc_s, libgomp, libatomic,
# libssp) for Haiku arm64 from the buildtools gcc (the cross compiler's
# source) with the Cortex-A53 erratum workarounds, which the bootstrap
# package was built without (milestone M8 (docs/milestones.md)). A gcc configured like
# build/scripts/build_cross_tools_gcc4, but with shared target libraries,
# libgomp and libatomic. The libraries go to deps/bootstrap/rebuilt/gcc_syslibs
# for tools/bootstrap-fix.sh, after a check that they export every symbol the
# bootstrap ones do.
# Usage: gcc-syslibs-build.sh
set -e
. "$(dirname "$0")/haiku-arm64-env.sh"
GCC=$HAIKU_PROJECT/buildtools/gcc
OBJ=$HAIKU_PROJECT/deps/build/gcc-syslibs
INSTALL=$OBJ/install
SYSROOT=$OBJ/sysroot
LOG=$HAIKU_PROJECT/logs/gcc-syslibs-build.log
BOOTSTRAP=$(ls -d $HAIKU_PROJECT/deps/bootstrap/pkg/gcc_syslibs-*)
REBUILT=$HAIKU_PROJECT/deps/bootstrap/rebuilt/gcc_syslibs
: > "$LOG"

# sysroot as the cross-tools script makes it: Haiku's headers, plus the
# libraries and start files target libraries link against
rm -rf "$OBJ"
mkdir -p "$OBJ/build" "$SYSROOT/boot/system/develop/headers" "$INSTALL"
for d in config os posix; do
	cp -r "$HAIKU_SRC/headers/$d" "$SYSROOT/boot/system/develop/headers/"
done
cp -rL "$HAIKU_SYSROOT/boot/system/develop/lib" "$SYSROOT/boot/system/develop/"
rm -f "$SYSROOT"/boot/system/develop/lib/lib{stdc++,supc++,gcc_s}*

export PATH="$HAIKU_TOOLS:$PATH"
# gcc builds its compiler with the host's; target flags come separately
unset CC CXX CPPFLAGS LDFLAGS AR RANLIB STRIP
targetFlags="-O2 -g $HAIKU_ERRATA_FLAGS"
cd "$OBJ/build"
"$GCC/configure" --prefix="$INSTALL" --target=aarch64-unknown-haiku \
	--disable-nls --enable-shared --with-system-zlib \
	--enable-languages=c,c++ --enable-frame-pointer \
	--enable-__cxa-atexit --enable-threads=posix \
	--with-default-libstdcxx-abi=gcc4-compatible \
	--with-sysroot="$SYSROOT" --disable-maintainer-mode \
	--disable-multilib --enable-libgomp --enable-libatomic \
	--with-build-time-tools="$HAIKU_TOOLS" \
	CFLAGS_FOR_TARGET="$targetFlags" CXXFLAGS_FOR_TARGET="$targetFlags" \
	LDFLAGS_FOR_TARGET="$HAIKU_ERRATA_FLAGS" >> "$LOG" 2>&1
make -j14 all-gcc >> "$LOG" 2>&1
make -j14 all-target-libgcc all-target-libstdc++-v3 all-target-libgomp \
	all-target-libatomic all-target-libssp >> "$LOG" 2>&1
make install-target-libgcc install-target-libstdc++-v3 install-target-libgomp \
	install-target-libatomic install-target-libssp >> "$LOG" 2>&1

# compare with the bootstrap libraries, then put them in place
lib=$INSTALL/aarch64-unknown-haiku/lib
rm -rf "$REBUILT"
mkdir -p "$REBUILT/lib"
for l in libstdc++.so.6.0.32 libgcc_s.so.1 libgomp.so.1.0.0 libatomic.so.1.2.0 \
		libssp.so.0.0.0; do
	[ -e "$lib/$l" ] || { echo "gcc-syslibs-build: no $l built"; exit 1; }
	missing=$(LC_ALL=C comm -23 \
		<($HAIKU_TOOLS/aarch64-unknown-haiku-nm -D --defined-only "$BOOTSTRAP/lib/$l" | awk '{print $3}' | LC_ALL=C sort -u) \
		<($HAIKU_TOOLS/aarch64-unknown-haiku-nm -D --defined-only "$lib/$l" | awk '{print $3}' | LC_ALL=C sort -u) | wc -l)
	[ "$missing" = 0 ] || { echo "gcc-syslibs-build: $l lacks $missing symbols"; exit 1; }
	cp "$lib/$l" "$REBUILT/lib/"
	$HAIKU_TOOLS/aarch64-unknown-haiku-strip --strip-debug "$REBUILT/lib/$l"
	echo "gcc-syslibs-build: $l"
done

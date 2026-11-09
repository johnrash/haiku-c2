# Cross-compiling for Haiku arm64 against this build tree. Source it:
#   . tools/haiku-arm64-env.sh
# Sets HAIKU_SYSROOT (deps/sysroot: Haiku's develop/lib layout, linked to the
# build's libraries and start files), HAIKU_PREFIX (deps/prefix: libraries
# built for the target), CC/CXX/AR/RANLIB/STRIP and CPPFLAGS/LDFLAGS.
HAIKU_PROJECT=$(cd "$(dirname "${BASH_SOURCE:-$0}")/.." && pwd)
HAIKU_GEN=$HAIKU_PROJECT/generated.arm64
HAIKU_SRC=$HAIKU_PROJECT/haiku
HAIKU_SYSROOT=$HAIKU_PROJECT/deps/sysroot
HAIKU_PREFIX=$HAIKU_PROJECT/deps/prefix
HAIKU_TOOLS=$HAIKU_GEN/cross-tools-arm64/bin
HAIKU_HOST_TOOLS=$HAIKU_GEN/objects/linux/x86_64/release/tools
export HAIKU_PROJECT HAIKU_GEN HAIKU_SRC HAIKU_SYSROOT HAIKU_PREFIX HAIKU_TOOLS HAIKU_HOST_TOOLS

lib=$HAIKU_SYSROOT/boot/system/develop/lib
if [ ! -e "$lib/libcolumnlistview.a" ]; then
	mkdir -p "$lib"
	r=$HAIKU_GEN/objects/haiku/arm64/release
	syslibs=$(ls -d $HAIKU_GEN/build_packages/gcc_syslibs-*/lib)
	zlib=$(ls -d $HAIKU_GEN/build_packages/zlib-*_bootstrap-*-arm64/develop/lib)
	for f in $r/system/glue/arch/arm64/crti.o $r/system/glue/arch/arm64/crtn.o \
			$r/system/glue/start_dyn.o $r/system/glue/init_term_dyn.o \
			$r/system/libroot/libroot.so $r/kits/libbe.so \
			$r/system/libnetwork/libnetwork.so \
			$r/kits/network/libnetapi/no-ssl/libbnetapi.so \
			$r/kits/shared/libshared.a $r/kits/translation/libtranslation.so \
			$r/kits/tracker/libtracker.so $r/kits/game/libgame.so \
			$r/kits/media/libmedia.so $r/kits/interface/libcolumnlistview.a \
			$r/kits/locale/liblocalestub.a $r/kits/textencoding/libtextencoding.so \
			$r/kits/mail/libmail.so $r/kits/device/libdevice.so \
			$r/libs/gnu/libgnu.so \
			$(find $r -name libbsd.so -type f | head -1) \
			$syslibs/libstdc++.so $syslibs/libsupc++.so $syslibs/libgcc_s.so \
			$syslibs/libgcc_s.so.1 \
			$zlib/libz.so; do
		ln -sf "$f" "$lib/"
	done
	# what kernel add-ons link against
	ln -sf "$r/system/kernel/kernel_arm64" "$lib/_KERNEL_"
fi

h=$HAIKU_SRC/headers
# Every directory of the public API, then the C library headers in the
# order Haiku's own gcc searches them (buildtools gcc/config/haiku.h): the
# BSD compatibility headers first, which enable _DEFAULT_SOURCE.
HAIKU_API_INCLUDES="-I$h"
for d in $(find "$h/os" -maxdepth 2 -type d -not -path "*/arch/*" | sort) $h/os/arch/arm64; do
	HAIKU_API_INCLUDES="$HAIKU_API_INCLUDES -I$d"
done
HAIKU_API_INCLUDES="$HAIKU_API_INCLUDES \
-I$(ls -d $HAIKU_GEN/build_packages/zlib-*_bootstrap-*-arm64/develop/headers) \
-I$HAIKU_PREFIX/include"
HAIKU_LIBC_DIRS="$h/compatibility/bsd $h/glibc $h/compatibility/gnu $h/posix"
HAIKU_CPPFLAGS="$HAIKU_API_INCLUDES"
HAIKU_CXX_INCLUDES="$HAIKU_API_INCLUDES"
for d in $HAIKU_LIBC_DIRS; do
	HAIKU_CPPFLAGS="$HAIKU_CPPFLAGS -I$d"
	# after libstdc++'s headers, which #include_next them
	HAIKU_CXX_INCLUDES="$HAIKU_CXX_INCLUDES -idirafter $d"
done
export HAIKU_CXX_INCLUDES
export HAIKU_CPPFLAGS

# Cortex-A53 errata 835769 and 843419: the C2's A53 r0p4 cores have both,
# and the cross compiler leaves the workarounds off unless asked (843419 made
# ICU's hash tables pick up garbage; see milestone M7 (docs/milestones.md)). Passed to every
# compile and link through CC and CXX.
export HAIKU_ERRATA_FLAGS="-mfix-cortex-a53-835769 -mfix-cortex-a53-843419"
export CC="$HAIKU_TOOLS/aarch64-unknown-haiku-gcc --sysroot=$HAIKU_SYSROOT $HAIKU_ERRATA_FLAGS"
export CXX="$HAIKU_TOOLS/aarch64-unknown-haiku-g++ --sysroot=$HAIKU_SYSROOT $HAIKU_ERRATA_FLAGS"
export AR=$HAIKU_TOOLS/aarch64-unknown-haiku-ar
export RANLIB=$HAIKU_TOOLS/aarch64-unknown-haiku-ranlib
export STRIP=$HAIKU_TOOLS/aarch64-unknown-haiku-strip
export CPPFLAGS="$HAIKU_CPPFLAGS"
export LDFLAGS="-L$HAIKU_PREFIX/lib"
# C++ programs: link libgcc_s.so.1 as Haiku's own build does (its libstdc++
# takes the unwinder from it); without it no C++ exception is ever caught
export HAIKU_CXX_LIBS="-lgcc_s"

# Haiku file attributes (file types, application icons) on this host live in
# generated.arm64/attributes, one directory per inode (the build's generic
# attribute emulation). A plain rm leaves them behind for the next file that
# gets the inode, so package directories are removed with rm_attrs, and new
# files get their emulated attributes cleared before anything is set.
HAIKU_ATTRIBUTES=$HAIKU_GEN/attributes
export LD_LIBRARY_PATH="${LD_LIBRARY_PATH:+$LD_LIBRARY_PATH:}$HAIKU_GEN/objects/linux/lib"

# haiku_rm_tree <dir>: remove a directory tree and its emulated attributes
haiku_rm_tree()
{
	[ -e "$1" ] && "$HAIKU_HOST_TOOLS/rm_attrs" -rf "$1"
	true
}

# haiku_mimeset <file>...: clear stale emulated attributes, then set the file
# type and, for applications, the signature and icon from the resources, as
# Haiku's build does (jam rule MimeSet)
haiku_mimeset()
{
	local f
	for f in "$@"; do
		rm -rf "$HAIKU_ATTRIBUTES/$(stat -c %i "$f")"
		"$HAIKU_HOST_TOOLS/mimeset" -f --mimedb \
			"$HAIKU_GEN/objects/common/data/mime_db/mime_db" "$f"
	done
}

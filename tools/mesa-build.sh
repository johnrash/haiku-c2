#!/bin/bash
# Cross-compile Mesa 22.0.5 (deps/src, the version of Haiku's x86_64 Mesa
# package, with HaikuPorts' mesa-22.0.5.patchset) for Haiku arm64: the Haiku
# OpenGL kit (libGL.so with BGLView), OSMesa and the "Software Pipe" renderer
# add-on: with llvmpipe when tools/llvm-build.sh has built LLVM 12 (softpipe
# stays available: GALLIUM_DRIVER=softpipe), else softpipe only. No Vulkan.
# Also lima for the Mali-450 (docs/m43-mesa-lima-haiku.md): the "Lima Pipe"
# renderer add-on, with tools/libdrm-haiku (a static libdrm stand-in for the
# meson_mali driver) in place of libdrm, and limatest, the first hardware
# test. Our changes: tools/patches/mesa-$VERSION-*.patch (the display target
# BBitmap ignored Mesa's row stride; lima on Haiku, the haiku-lima target, a
# pipe context for flush_frontbuffer, renderer add-on priorities, Lima Pipe's
# presenter thread).
# Packages as HaikuPorts does: mesa (libraries), mesa_swpipe (the renderer)
# and mesa_devel (headers, OpenGL kit); and mesa_lima (the Lima Pipe add-on).
# Usage: mesa-build.sh  -> deps/mesa/mesa{,_swpipe,_lima,_devel}-22.0.5-<revision>-arm64.hpkg
#   (revision 1 without LLVM, 6 with deps/llvm's)
set -e
. "$(dirname "$0")/haiku-arm64-env.sh"
VERSION=22.0.5
SRC=$HAIKU_PROJECT/deps/build/mesa-$VERSION
BUILD=$HAIKU_PROJECT/deps/build/mesa-build
STAGE=$HAIKU_PROJECT/deps/build/mesa-stage
OUT=$HAIKU_PROJECT/deps/mesa
LOG=$HAIKU_PROJECT/logs/mesa-build.log
DRM=$HAIKU_PROJECT/deps/build/libdrm-haiku
DRM_INCLUDE=$HAIKU_PROJECT/tools/libdrm-haiku/include
mkdir -p "$OUT"
: > "$LOG"

if [ ! -e "$SRC/.patched" ]; then
	rm -rf "$SRC"
	tar xf "$HAIKU_PROJECT/deps/src/mesa-$VERSION.tar.xz" -C "$(dirname "$SRC")"
	(cd "$SRC" && patch -p1 < "$HAIKU_PROJECT/deps/src/mesa-$VERSION.patchset" \
		&& for p in "$HAIKU_PROJECT"/tools/patches/mesa-$VERSION-*.patch; do
			patch -p1 < "$p" || exit 1; done) >> "$LOG" 2>&1
	touch "$SRC/.patched"
fi

# the libdrm stand-in first (it takes the drm-uapi headers from Mesa)
"$HAIKU_PROJECT/tools/libdrm-haiku/build.sh" "$SRC" "$DRM" >> "$LOG" 2>&1

# its Haiku targets name the native private headers folder
sed -i "s|'/boot/system/develop/headers/private'|'$HAIKU_SRC/headers/private'|; \
	s|'/system/develop/headers/private'|'$HAIKU_SRC/headers/private'|" \
	"$SRC/src/hgl/meson.build" "$SRC/src/gallium/targets/haiku-softpipe/meson.build" \
	"$SRC/src/gallium/targets/haiku-lima/meson.build"

# pkg-config files for the bootstrap packages' develop folders
pc=$HAIKU_PROJECT/deps/build/pkgconfig
mkdir -p "$pc"
zlib=$(ls -d $HAIKU_GEN/build_packages/zlib-*_bootstrap-*-arm64/develop)
expat=$(ls -d $HAIKU_GEN/build_packages/expat-*_bootstrap-*-arm64/develop)
printf 'Name: zlib\nDescription: zlib\nVersion: 1.2.13\nLibs: -L%s/lib -lz\nCflags: -I%s/headers\n' \
	"$zlib" "$zlib" > "$pc/zlib.pc"
printf 'Name: expat\nDescription: expat\nVersion: 2.5.0\nLibs: -L%s/lib -lexpat\nCflags: -I%s/headers\n' \
	"$expat" "$expat" > "$pc/expat.pc"
# libdrm: the stand-in, as the version lima's meson check wants
sed -e "s|@libdir@|$DRM|" -e "s|@includedir@|$DRM_INCLUDE|" \
	-e "s|@version@|2.4.109|" "$HAIKU_PROJECT/tools/libdrm-haiku/libdrm.pc.in" \
	> "$pc/libdrm.pc"

# LLVM (tools/llvm-build.sh) through Mesa's "llvm" subproject fallback,
# which Mesa has for builds without llvm-config: a cross build can't run the
# arm64 one
LLVM_STAGE=$HAIKU_PROJECT/deps/build/llvm12-stage/boot/system
LLVM_OPTIONS="-Dllvm=disabled"
if [ -e "$LLVM_STAGE/lib/libLLVM-12.so" ]; then
	mkdir -p "$SRC/subprojects/llvm"
	cat > "$SRC/subprojects/llvm/meson.build" <<LLVM
project('llvm', ['cpp'], version : '12.0.1')
dep_llvm = declare_dependency(
  compile_args : ['-isystem', '$LLVM_STAGE/include'],
  link_args : ['-L$LLVM_STAGE/lib', '-lLLVM-12'],
  version : '12.0.1',
)
has_rtti = true
LLVM
	LLVM_OPTIONS="-Dllvm=enabled -Dshared-llvm=enabled"
fi
# revisions: 1 without LLVM; 2 llvmpipe, 3 llvmpipe and lima, 4 lima's
# growable tile heaps (the libdrm stand-in says 1.1), 5 Lima Pipe's
# presenter thread (up to 1280x1024), 6 at any size, and the libdrm stand-in
# tells meson_mali which buffers the CPU maps
if [ "$LLVM_OPTIONS" = "-Dllvm=disabled" ]; then
	REVISION=1
	RENDERERS="softpipe and lima, no LLVM"
	LLVM_REQUIRES=""
else
	REVISION=6
	RENDERERS="llvmpipe (LLVM 12), softpipe and lima"
	LLVM_REQUIRES="
	lib:libLLVM_12"
fi

# meson cross file: C with the libc headers as -I, C++ with -idirafter
# (haiku-arm64-env.sh), the errata flags in every compile and link. The
# libdrm stand-in's headers come first, for every file, not only for those
# that use libdrm: Mesa's drm.h includes <sys/ioccom.h>, which must be the
# stand-in's (Linux's request encoding), not Haiku's BSD one, and
# mesa/main includes drm-uapi headers without the libdrm dependency.
list() { local a; for a in "$@"; do printf "'%s', " "$a"; done; }
cross=$HAIKU_PROJECT/deps/build/haiku-arm64.meson
cat > "$cross" <<EOF
[binaries]
c = ['$HAIKU_TOOLS/aarch64-unknown-haiku-gcc', '--sysroot=$HAIKU_SYSROOT', $(list $HAIKU_ERRATA_FLAGS)]
cpp = ['$HAIKU_TOOLS/aarch64-unknown-haiku-g++', '--sysroot=$HAIKU_SYSROOT', $(list $HAIKU_ERRATA_FLAGS)]
ar = '$HAIKU_TOOLS/aarch64-unknown-haiku-ar'
strip = '$HAIKU_TOOLS/aarch64-unknown-haiku-strip'
pkg-config = 'pkg-config'

[properties]
pkg_config_libdir = '$pc'

[built-in options]
c_args = [$(list -I$DRM_INCLUDE $HAIKU_CPPFLAGS)]
cpp_args = [$(list -I$DRM_INCLUDE $HAIKU_CXX_INCLUDES)]
c_link_args = ['-lgcc_s']
cpp_link_args = ['-lgcc_s']

[host_machine]
system = 'haiku'
cpu_family = 'aarch64'
cpu = 'cortex-a53'
endian = 'little'
EOF

rm -rf "$BUILD" "$STAGE"
meson setup "$BUILD" "$SRC" --cross-file "$cross" \
	-Dplatforms=haiku -Dgallium-drivers=swrast,lima -Ddri-drivers= \
	-Dvulkan-drivers= $LLVM_OPTIONS -Dosmesa=true -Degl=disabled \
	-Dglx=disabled -Dgbm=disabled -Dzstd=disabled -Dvalgrind=disabled \
	-Dlibunwind=disabled -Dlmsensors=disabled -Dbuild-tests=false \
	--buildtype=release --prefix=/boot/system --libdir=lib \
	--includedir=develop/headers >> "$LOG" 2>&1
ninja -C "$BUILD" >> "$LOG" 2>&1
DESTDIR="$STAGE" ninja -C "$BUILD" install >> "$LOG" 2>&1
# package trees, laid out as HaikuPorts' mesa recipe does
st=$STAGE/boot/system
P=$OUT/pkg
haiku_rm_tree "$P"
mkdir -p "$P/mesa/lib" "$P/mesa/documentation/packages/mesa" \
	"$P/swpipe/add-ons/opengl" "$P/lima/add-ons/opengl" \
	"$P/devel/develop/lib" "$P/devel/develop/headers/os/opengl"
for l in libGL.so.1 libglapi.so.0 libOSMesa.so.8 libGLESv1_CM.so.1 \
		libGLESv2.so.2; do
	real=$(readlink -f "$st/lib/$l")
	cp "$real" "$P/mesa/lib/"
	ln -s "$(basename "$real")" "$P/mesa/lib/$l"
	ln -s "../../lib/$l" "$P/devel/develop/lib/${l%.so.*}.so"
done
"$STRIP" "$P"/mesa/lib/*.so.*.*
cp "$SRC/docs/license.rst" "$P/mesa/documentation/packages/mesa/"
cp "$st/lib/libswpipe.so" "$P/swpipe/add-ons/opengl/Software Pipe"
"$STRIP" "$P/swpipe/add-ons/opengl/Software Pipe"
cp "$st/lib/liblimapipe.so" "$P/lima/add-ons/opengl/Lima Pipe"
"$STRIP" "$P/lima/add-ons/opengl/Lima Pipe"
cp -r "$st/develop/headers/GL" "$st/develop/headers/GLES" \
	"$st/develop/headers/GLES2" "$st/develop/headers/GLES3" \
	"$st/develop/headers/KHR" "$P/devel/develop/headers/"
cp "$SRC/include/HaikuGL/OpenGLKit.h" "$P/devel/develop/headers/os/"
cp "$SRC/include/HaikuGL/GLView.h" "$SRC/include/HaikuGL/GLRenderer.h" \
	"$P/devel/develop/headers/os/opengl/"
for d in GL KHR; do
	ln -s "../../$d" "$P/devel/develop/headers/os/opengl/$d"
done

info()
{
	cat <<INFO
name			$1
version			$VERSION-$REVISION
architecture	arm64
summary			"$2"
description		"$3 Mesa $VERSION cross-compiled for Haiku arm64 (ODROID-C2 port): $RENDERERS, no Vulkan."
packager		"haiku-odroid-c2 port"
vendor			"The Mesa project"
copyrights		"1999-2022 Brian Paul and others"
licenses		"MIT"
provides {
$4
}
requires {
$5
}
INFO
}
info mesa "Multi-platform GL implementation" \
	"The Haiku OpenGL kit (libGL with BGLView), OSMesa and OpenGL ES." \
	"	mesa = $VERSION
	lib:libGL = 1.0.0 compat >= 1
	lib:libOSMesa = 8.0.0 compat >= 8
	lib:libglesv1_cm = 1.1.0
	lib:libglesv2 = 2.0.0
	lib:libglapi = 0.0.0" \
	"	haiku >= r1~beta6
	lib:libz
	lib:libexpat$LLVM_REQUIRES" > "$P/mesa/.PackageInfo"
info mesa_swpipe "The Mesa Gallium software pipe renderer" \
	"The BGLRenderer add-on that renders on the CPU ($RENDERERS)." \
	"	mesa_swpipe = $VERSION" \
	"	mesa == $VERSION-$REVISION$LLVM_REQUIRES" > "$P/swpipe/.PackageInfo"
# Gallium (in every renderer) has llvmpipe's LLVM code with LLVM
info mesa_lima "The Mesa Gallium lima renderer" \
	"The BGLRenderer add-on that renders on the Mali-450 GPU through the meson_mali driver; OpenGL picks it before the software pipe while the GPU is there, unless LIBGL_ALWAYS_SOFTWARE is set." \
	"	mesa_lima = $VERSION" \
	"	mesa == $VERSION-$REVISION$LLVM_REQUIRES" > "$P/lima/.PackageInfo"
info mesa_devel "Mesa development files" \
	"Headers and libraries to build OpenGL applications." \
	"	mesa_devel = $VERSION
	devel:libGL = 1.0.0 compat >= 1
	devel:libOSMesa = 8.0.0 compat >= 8
	devel:libglesv1_cm = 1.1.0
	devel:libglesv2 = 2.0.0
	devel:libglapi = 0.0.0" \
	"	mesa == $VERSION-$REVISION" > "$P/devel/.PackageInfo"
for part in mesa swpipe lima devel; do
	name=mesa; [ $part != mesa ] && name=mesa_$part
	hpkg=$OUT/$name-$VERSION-$REVISION-arm64.hpkg
	rm -f "$hpkg"
	(cd "$P/$part" && PATH="$HAIKU_PROJECT/tools/cross:$PATH" package create -q "$hpkg")
	echo "mesa-build: $hpkg ($(stat -c %s "$hpkg") bytes)"
done

#!/bin/bash
# HaikuPorts ports the arm64 HaikuPorts repository has no builds of (it only
# has bootstrap packages), cross-compiled from the sources in deps/src
# (checksums of the HaikuPorts recipes, which are there too) with those
# recipes' patches and options and the Cortex-A53 errata flags (milestone M8 (docs/milestones.md)):
# - libraries behind jam build features (configs/haiku-UserBuildConfig):
#   libedit (Debugger), glu (GLInfo, libglut, Haiku3d), tiff, libwebp, dav1d
#   and libavif1.0 (the TIFF, WebP and AVIF translators), ffmpeg6 (the media
#   plugin: MediaPlayer, the Media Kit's codecs), libogg, libvorbis and
#   fluidlite (libmidi: Media preferences, MidiPlayer, PatchBay), jasper and
#   libicns (the JPEG 2000 and ICNS translators), libraw (RAW translator);
# - libpcre;
# - freetype 2.14.3, for the bootstrap 2.6.3: the app_server and every
#   application draw text with it, and 2.6.3 can't read CFF2 (variable)
#   fonts such as upstream's Noto Sans CJK (milestone M38 (docs/milestones.md)); with libpng
#   (static) for colour emoji (milestone M56 (docs/milestones.md));
# - zstd: the library, and its sources for the kernel's and the boot
#   loader's decompressor (jam's zstd build feature: packages compressed
#   with zstd, as HaikuPorts' are now; milestone M39 (docs/milestones.md));
# - command line tools: xz_utils, bzip2, gzip, tar, unzip, zip, zstd_bin;
# - libpcap and tcpdump (packet capture on the boards, milestone M82 (docs/milestones.md));
# - capstone (the Debugger's disassembler) and fluidlite, static libraries
#   (only deps/ports/<port>/, no package).
# Each port is installed to deps/ports/<port>/ (its /boot/system tree, with
# develop/ for jam and the ports after it) and packaged as
# deps/ports/<port>-<version>-<revision>-arm64.hpkg (without develop/ and
# the documentation; the revision is $REVISION, 1 if unset); provides come
# from the files, requires from DT_NEEDED.
# Usage: ports-build.sh [port...]      (default: all, in dependency order)
set -e
. "$(dirname "$0")/haiku-arm64-env.sh"
SRCS=$HAIKU_PROJECT/deps/src
WORK=$HAIKU_PROJECT/deps/build/ports
OUT=$HAIKU_PROJECT/deps/ports
ALL="xz_utils bzip2 gzip tar unzip zip libedit glu tiff libwebp dav1d libavif1.0
	ffmpeg6 libogg libvorbis fluidlite jasper libicns libraw libpcre capstone
	freetype zstd zstd_bin"
mkdir -p "$WORK" "$OUT"

# The sysroot as haikuporter's cross builds have it (see bootstrap-rebuild.sh),
# plus Mesa (for GLU); each port built here is added to it.
SYSROOT=$WORK/sysroot
if [ ! -e "$SYSROOT/boot/system/develop/headers/posix/stdio.h" ]; then
	haiku_rm_tree "$SYSROOT"
	mkdir -p "$SYSROOT/boot/system"
	for p in $HAIKU_GEN/objects/haiku/arm64/packaging/packages/haiku.hpkg \
			$HAIKU_GEN/objects/haiku/arm64/packaging/packages/haiku_devel.hpkg \
			$HAIKU_GEN/download/gcc_syslibs-*_bootstrap-*-arm64.hpkg \
			$HAIKU_GEN/download/gcc_syslibs_devel-*_bootstrap-*-arm64.hpkg \
			$HAIKU_GEN/download/zlib-*_bootstrap-*-arm64.hpkg \
			$HAIKU_GEN/download/zlib_devel-*_bootstrap-*-arm64.hpkg \
			$HAIKU_GEN/download/ncurses6-*_bootstrap-*-arm64.hpkg \
			$HAIKU_GEN/download/ncurses6_devel-*_bootstrap-*-arm64.hpkg \
			$HAIKU_PROJECT/deps/mesa/mesa-22.0.5-1-arm64.hpkg \
			$HAIKU_PROJECT/deps/mesa/mesa_devel-22.0.5-1-arm64.hpkg; do
		"$HAIKU_HOST_TOOLS/package/package" extract -C "$SYSROOT/boot/system" "$p"
	done
	# the libraries the haiku package has: requires "haiku", not lib:<name>
	"$HAIKU_HOST_TOOLS/package/package" list -p \
		$HAIKU_GEN/objects/haiku/arm64/packaging/packages/haiku.hpkg \
		| sed -n 's|^lib/\([^/]*\.so[^/]*\)$|\1|p' > "$WORK/haiku-libs"
fi
unset CPPFLAGS LDFLAGS
GCC=$HAIKU_TOOLS/aarch64-unknown-haiku-gcc
GXX=$HAIKU_TOOLS/aarch64-unknown-haiku-g++
export CC="$GCC --sysroot=$SYSROOT $HAIKU_ERRATA_FLAGS"
export CXX="$GXX --sysroot=$SYSROOT $HAIKU_ERRATA_FLAGS"
export AR=$HAIKU_TOOLS/aarch64-unknown-haiku-ar
export RANLIB=$HAIKU_TOOLS/aarch64-unknown-haiku-ranlib
READELF=$HAIKU_TOOLS/aarch64-unknown-haiku-readelf
export PKG_CONFIG_SYSROOT_DIR=$SYSROOT
export PKG_CONFIG_LIBDIR=$SYSROOT/boot/system/develop/lib/pkgconfig
# Makefiles that insist on "cc" (unzip's haiku target)
mkdir -p "$WORK/bin"
printf '#!/bin/sh\nexec %s "$@"\n' "$CC" > "$WORK/bin/cc"
chmod +x "$WORK/bin/cc"

# CMake and Meson cross files for this sysroot
cat > "$WORK/haiku-arm64.cmake" <<EOF
set(CMAKE_SYSTEM_NAME Haiku)
set(CMAKE_SYSTEM_PROCESSOR aarch64)
set(CMAKE_C_COMPILER $GCC)
set(CMAKE_CXX_COMPILER $GXX)
set(CMAKE_SYSROOT $SYSROOT)
set(CMAKE_C_FLAGS_INIT "$HAIKU_ERRATA_FLAGS")
set(CMAKE_CXX_FLAGS_INIT "$HAIKU_ERRATA_FLAGS")
set(CMAKE_FIND_ROOT_PATH $SYSROOT)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
EOF
errata=$(printf "'%s', " $HAIKU_ERRATA_FLAGS)
cat > "$WORK/haiku-arm64.meson" <<EOF
[binaries]
c = ['$GCC', '--sysroot=$SYSROOT', $errata]
cpp = ['$GXX', '--sysroot=$SYSROOT', $errata]
ar = '$AR'
strip = '$STRIP'
pkg-config = 'pkg-config'

[properties]
sys_root = '$SYSROOT'
pkg_config_libdir = '$PKG_CONFIG_LIBDIR'

[host_machine]
system = 'haiku'
cpu_family = 'aarch64'
cpu = 'cortex-a53'
endian = 'little'
EOF

# Haiku's directories, as haikuporter's runConfigure passes them
haikuDirs()
{
	echo --prefix=/boot/system --exec-prefix=/boot/system \
		--bindir=/boot/system/bin --sbindir=/boot/system/bin \
		--libdir=/boot/system/lib --libexecdir=/boot/system/lib \
		--datadir=/boot/system/data --datarootdir=/boot/system/data \
		--sysconfdir=/boot/system/settings --localstatedir=/boot/system/var \
		--includedir=/boot/system/develop/headers \
		--mandir=/boot/system/documentation/man \
		--infodir=/boot/system/documentation/info \
		--docdir=/boot/system/documentation/packages/$1
}
CMAKE_DIRS="-DCMAKE_INSTALL_PREFIX=/boot/system -DCMAKE_INSTALL_LIBDIR=lib
	-DCMAKE_INSTALL_BINDIR=bin -DCMAKE_INSTALL_INCLUDEDIR=develop/headers
	-DCMAKE_INSTALL_DATADIR=data -DCMAKE_INSTALL_MANDIR=documentation/man
	-DCMAKE_INSTALL_DOCDIR=documentation/packages"

# unpack <archive> <dir> [patchset...]; with REUSE set, an unpacked tree
# that has a build directory is kept (an interrupted CMake build continues)
unpack()
{
	local archive=$1 p
	dir=$2
	shift 2
	if [ -n "$REUSE" ] && [ -d "$WORK/$dir/build" ]; then
		rm -rf "$WORK/$dir-stage"
		stage=$WORK/$dir-stage
		system=$stage/boot/system
		return
	fi
	rm -rf "$WORK/$dir" "$WORK/$dir-stage"
	case "$archive" in
		*.zip) (cd "$WORK" && unzip -q "$SRCS/$archive") ;;
		*) tar xf "$SRCS/$archive" -C "$WORK" ;;
	esac
	for p in "$@"; do
		(cd "$WORK/$dir" && patch -p1 -s < "$SRCS/$p")
	done
	stage=$WORK/$dir-stage
	system=$stage/boot/system
}

# develLibs: the development files go to develop/lib, as haikuporter's
# prepareInstalledDevelLib puts them (libX.so links, pkg-config and CMake
# files); static libraries and libtool archives are dropped.
develLibs()
{
	local f
	mkdir -p "$system/develop/lib"
	rm -f "$system"/lib/*.la "$system"/lib/*.a
	for f in "$system"/lib/*.so; do
		[ -L "$f" ] || continue
		ln -sf "../../lib/$(readlink "$f")" "$system/develop/lib/$(basename "$f")"
		rm "$f"
	done
	for f in pkgconfig cmake; do
		[ -d "$system/lib/$f" ] || continue
		mv "$system/lib/$f" "$system/develop/lib/"
	done
	sed -i 's|^libdir=.*|libdir=/boot/system/develop/lib|' \
		"$system"/develop/lib/pkgconfig/*.pc 2>/dev/null || true
}

# fixLibtool (in a configured tree): libtool links C++ through the cross
# compiler's libstdc++.la, whose libstdc++.a Haiku's build renames; it has to
# use the sysroot's libstdc++
fixLibtool()
{
	sed -i -E "s#( -L| )[^ \"]*(/\.\./\.\./\.\./\.\.|${HAIKU_TOOLS%/bin})/aarch64-unknown-haiku/lib([ \"])#\3#g" \
		libtool
}

# develOnly <port>: a static library: deps/ports/<port> and the sysroot only
develOnly()
{
	mkdir -p "$system/develop/lib"
	mv "$system"/lib/*.a "$system/develop/lib/"
	rm -rf "$system"/lib/cmake "$system"/lib/pkgconfig
	haiku_rm_tree "$OUT/$1"
	cp -a "$system" "$OUT/$1"
	cp -a "$system/." "$SYSROOT/boot/system/"
	echo "ports-build: $OUT/$1 ($(ls "$OUT/$1"/develop/lib))"
}

# finish <port> <version> <summary> <description> [extra provides...]: put
# the port into deps/ports/<port> and the sysroot, and package it
finish()
{
	local port=$1 version=$2 summary=$3 description=$4 f name v
	local revision=${REVISION:-1}
	shift 4
	haiku_rm_tree "$OUT/$port"
	cp -a "$system" "$OUT/$port"
	# $NO_SYSROOT: tools that run on the boards (gcc, binutils), which no
	# port here builds against
	[ -n "$NO_SYSROOT" ] || cp -a "$system/." "$SYSROOT/boot/system/"

	local pkg=$OUT/pkg/$port
	haiku_rm_tree "$pkg"
	mkdir -p "$pkg"
	for f in "$system"/*; do
		case "$(basename "$f")" in
			develop) [ -z "$KEEP_DEVELOP" ] || cp -a "$f" "$pkg/" ;;
			documentation) ;;
			*) cp -a "$f" "$pkg/" ;;
		esac
	done
	# a license Haiku does not have goes into the package ($LICENSE_FILE)
	if [ ! -e "$HAIKU_SRC/data/system/data/licenses/$LICENSE" ]; then
		mkdir -p "$pkg/data/licenses"
		cp "$WORK/$dir/$LICENSE_FILE" "$pkg/data/licenses/$LICENSE"
	fi

	# strip, check for errata sequences, collect what the ELF files need
	local elfs=() needed=""
	while IFS= read -r -d '' f; do
		head -c 4 "$f" | grep -q ELF || continue
		"$STRIP" --strip-unneeded "$f"
		elfs+=("$f")
		needed="$needed $($READELF -d "$f" | sed -n 's/.*(NEEDED).*\[\(.*\)\]/\1/p')"
	done < <(find "$pkg" -type f -print0)
	python3 "$HAIKU_PROJECT/tools/a53-843419.py" "${elfs[@]}" \
		|| { echo "ports-build: $port: erratum 843419 sequences"; exit 1; }
	python3 "$HAIKU_PROJECT/tools/a53-835769.py" "${elfs[@]}" \
		|| { echo "ports-build: $port: erratum 835769 sequences"; exit 1; }

	# provides: the libraries (lib:libX = A.B.C compat >= A) and commands
	local provides="	$port = $version" requires="	haiku >= r1~beta6" own=""
	# $ALSO_LICENSE: the license of a library linked in (one Haiku has)
	local licenses="	\"$LICENSE\""
	[ -z "$ALSO_LICENSE" ] || licenses="$licenses
	\"$ALSO_LICENSE\""
	for f in "$pkg"/lib/lib*.so.*; do
		[ -f "$f" ] && [ ! -L "$f" ] || continue
		name=$(basename "$f")
		v=${name#*.so.}
		provides="$provides
	lib:${name%%.so.*} = $v compat >= ${v%%.*}"
		own="$own ${name%%.so.*}"
	done
	# libraries elsewhere (PySide6's next to its modules) are found by
	# RPATH: what needs them needs nothing outside the package
	for f in "${elfs[@]}"; do
		name=$($READELF -d "$f" | sed -n 's/.*(SONAME).*\[\(.*\)\]/\1/p')
		[ -z "$name" ] || own="$own ${name%%.so*}"
	done
	for f in "$pkg"/bin/*; do
		[ -e "$f" ] || continue
		provides="$provides
	cmd:$(basename "$f" | tr - _) = $version"
	done
	for f in "$@"; do
		provides="$provides
	$f"
	done
	for name in $(echo $needed | tr ' ' '\n' | LC_ALL=C sort -u); do
		grep -qxF "$name" "$WORK/haiku-libs" && continue
		case " $own " in *" ${name%%.so*} "*) continue ;; esac
		requires="$requires
	lib:${name%%.so*}"
	done
	# $EXTRA_REQUIRES: what ELF files don't say (a Python module's python3.15)
	for f in $EXTRA_REQUIRES; do
		requires="$requires
	$f"
	done

	cat > "$pkg/.PackageInfo" <<EOF
name			$port
version			$version-$revision
architecture	arm64
summary			"$summary"
description		"$description Cross-built for arm64 (ODROID-C2 port) from the HaikuPorts recipe's source and patches, with the Cortex-A53 errata workarounds."
packager		"haiku-odroid-c2 port"
vendor			"Haiku Project"
copyrights		"See the package's sources"
licenses {
$licenses
}
provides {
$provides
}
requires {
$requires
}
EOF
	local hpkg=$OUT/$port-$version-$revision-arm64.hpkg
	rm -f "$OUT/$port-$version"-*-arm64.hpkg
	(cd "$pkg" && PATH="$HAIKU_PROJECT/tools/cross:$PATH" package create -q "$hpkg")
	echo "ports-build: $hpkg ($(stat -c %s "$hpkg") bytes, ${#elfs[@]} ELF files)"
}

# nativeTree <dir>: an empty build directory, for the tools built from
# buildtools' source rather than a tarball
nativeTree()
{
	dir=$1
	rm -rf "$WORK/$dir" "$WORK/$dir-stage"
	mkdir -p "$WORK/$dir"
	stage=$WORK/$dir-stage
	system=$stage/boot/system
}
# nativeEnv <command>: the environment of a Canadian cross (built here, runs
# on the boards, builds for them): the cross compiler as the host's, with
# the sysroot and the errata flags in CC and CXX themselves (gcc passes the
# host's CXXFLAGS to its build-machine libcpp), this laptop's gcc for build
# tools; little-endian said outright (gcc's configure tests it as C++, which
# its fallback test isn't)
nativeEnv()
{
	env -u CC -u CXX -u AR -u RANLIB -u CPPFLAGS -u LDFLAGS \
		PATH="$HAIKU_TOOLS:$PATH" \
		CC="$GCC --sysroot=$SYSROOT $HAIKU_ERRATA_FLAGS" \
		CXX="$GXX --sysroot=$SYSROOT $HAIKU_ERRATA_FLAGS" \
		CFLAGS="-O2" CXXFLAGS="-O2" LDFLAGS= \
		CFLAGS_FOR_BUILD="-O2" CXXFLAGS_FOR_BUILD="-O2" LDFLAGS_FOR_BUILD= \
		ac_cv_c_bigendian=no "$@"
}

# Python modules (milestone M83 (docs/milestones.md)): built as wheels with the host Python 3.15 in
# deps/host/venv-py315 and installed to lib/python3.15/vendor-packages, with
# their bytecode; packages named as HaikuPorts' (<module>_python315).
PYVENV=$HAIKU_PROJECT/deps/host/venv-py315
PYHOST=$HAIKU_PROJECT/deps/host/python3.15/bin/python3.15
# pyInstallWheel <wheel>: into the stage's vendor-packages (any platform's,
# tools/wheel-install.py), with its bytecode; console scripts to bin/, run by
# python3.15
pyInstallWheel()
{
	local vendor=$system/lib/python3.15/vendor-packages
	mkdir -p "$vendor"
	python3 "$HAIKU_PROJECT/tools/wheel-install.py" "$1" "$vendor" \
		"$system/bin" /bin/python3.15 >> "$log" 2>&1
	# templates that only look like Python (PySide6's Android deploy
	# recipes) are left as they are
	"$PYHOST" -m compileall -q -j0 -x '\.tmpl\.py$' -s "$stage" -p / \
		"$vendor" >> "$log" 2>&1
}
# pyCross: the environment a wheel for the boards is built in. The host
# Python reads the target's sysconfig (a copy whose include and library
# directories point into the sysroot), and Meson gets a cross file with the
# Fortran compiler (tools/gfortran-cross-build.sh) and this Python.
pyCross()
{
	local cross=$WORK/pycross f
	rm -rf "$cross"
	mkdir -p "$cross"
	f=$(ls "$SYSROOT"/boot/system/lib/python3.15/_sysconfigdata__haiku_*.py)
	sed -e "s|'/boot/system/develop|'$SYSROOT/boot/system/develop|g" \
		-e "s|'/boot/system/lib'|'$SYSROOT/boot/system/lib'|g" \
		"$f" > "$cross/$(basename "$f")"
	export _PYTHON_SYSCONFIGDATA_NAME=$(basename "$f" .py)
	export PYTHONPATH=$cross
	export _PYTHON_HOST_PLATFORM=haiku-aarch64
	# cython, meson, ninja and pythran run on this laptop
	export PATH=$PYVENV/bin:$PATH
	local ftools=$HAIKU_PROJECT/deps/cross-fortran/bin
	PYCROSS_MESON=$cross/haiku-arm64-python.meson
	cat > "$PYCROSS_MESON" <<MESON
[binaries]
c = ['$GCC', '--sysroot=$SYSROOT', $errata]
cpp = ['$GXX', '--sysroot=$SYSROOT', $errata]
fortran = ['$ftools/aarch64-unknown-haiku-gfortran', '--sysroot=$SYSROOT']
ar = '$AR'
strip = '$STRIP'
pkg-config = 'pkg-config'
python = '$PYVENV/bin/python'
cython = '$PYVENV/bin/cython'

[properties]
sys_root = '$SYSROOT'
pkg_config_libdir = '$PKG_CONFIG_LIBDIR'
needs_exe_wrapper = true
longdouble_format = 'IEEE_QUAD_LE'

[host_machine]
system = 'haiku'
cpu_family = 'aarch64'
cpu = 'cortex-a53'
endian = 'little'
MESON
}
# pyWheel <dir> [pip options...]: builds the wheel of an unpacked source
# into $WORK/wheels (without build isolation: the venv has the tools)
pyWheel()
{
	local src=$1
	shift
	mkdir -p "$WORK/wheels"
	(cd "$src" && "$PYVENV/bin/python" -m pip wheel -q --no-deps \
		--no-build-isolation -w "$WORK/wheels" "$@" .) >> "$log" 2>&1
}

# pyqtTools: sip and PyQt-builder in the venv (they run on this laptop),
# from deps/src: their versions said outright (they take them from git with
# setuptools_scm otherwise), and PyQt-builder's configuration tests handed
# to $PYQTBUILD_TEST_RUNNER (patches/python, milestone M85 (docs/milestones.md))
pyqtTools()
{
	grep -q PYQTBUILD_TEST_RUNNER \
		"$PYVENV/lib/python3.15/site-packages/pyqtbuild/bindings.py" \
		2> /dev/null && [ "$("$PYVENV/bin/sip-build" -V)" = 6.17.2 ] \
		&& return
	local t=$WORK/host-pytools p
	rm -rf "$t"
	mkdir -p "$t"
	for p in sip-6.17.2 pyqt_builder-1.21.0; do
		tar xzf "$SRCS/$p.tar.gz" -C "$t"
		python3 - "$t/$p/pyproject.toml" "${p##*-}" <<'PY'
import re, sys
path, version = sys.argv[1], sys.argv[2]
text = open(path).read()
text = text.replace('dynamic = ["version"]', 'version = "%s"' % version)
text = re.sub(r', *"setuptools_scm[^"]*"', '', text)
text = re.sub(r'\n\[tool\.setuptools_scm\][^\[]*', '\n', text)
open(path, 'w').write(text)
PY
	done
	(cd "$t/pyqt_builder-1.21.0" && patch -p1 -s \
		< "$HAIKU_PROJECT/patches/python/pyqt-builder-1.21.0-test-runner.patch")
	"$PYVENV/bin/python" -m pip install -q --no-index --no-build-isolation \
		"$t/sip-6.17.2" "$t/pyqt_builder-1.21.0" >> "$log" 2>&1
}

# Qt 6.12 (milestone M83 (docs/milestones.md)): cross-built with the host Qt's tools
# (tools/qt6-build.sh host), HaikuPorts' layout and options.
HOSTQT=$HAIKU_PROJECT/deps/host/qt6.12
QT_DIRS="-DCMAKE_INSTALL_PREFIX=/boot/system -DINSTALL_LIBDIR=lib
	-DINSTALL_BINDIR=lib/Qt6 -DINSTALL_PUBLICBINDIR=bin
	-DINSTALL_PLUGINSDIR=add-ons/Qt6 -DINSTALL_LIBEXECDIR=lib/Qt6
	-DINSTALL_ARCHDATADIR=data/Qt6 -DINSTALL_DATADIR=data/Qt6
	-DINSTALL_INCLUDEDIR=develop/headers/Qt6
	-DINSTALL_MKSPECSDIR=data/Qt6/mkspecs -DINSTALL_QMLDIR=data/Qt6/qml
	-DINSTALL_EXAMPLESDIR=develop/sources/Qt6 -DINSTALL_SHAREDIR=data/Qt6
	-DINSTALL_SYSCONFDIR=settings/xdg
	-DINSTALL_TRANSLATIONSDIR=data/Qt6/translations
	-DINSTALL_DOCDIR=documentation/packages/qt6"
# qtDevel: like develLibs, but Qt's CMake package files stay in lib/cmake:
# they find Qt's prefix from where they are (moved to develop/lib/cmake they
# looked for develop/develop/headers)
qtDevel()
{
	local f
	mkdir -p "$system/develop/lib"
	rm -f "$system"/lib/*.la "$system"/lib/*.a
	for f in "$system"/lib/*.so; do
		[ -L "$f" ] || continue
		ln -sf "../../lib/$(readlink "$f")" "$system/develop/lib/$(basename "$f")"
		rm "$f"
	done
	if [ -d "$system/lib/pkgconfig" ]; then
		mkdir -p "$system/develop/lib/pkgconfig"
		mv "$system"/lib/pkgconfig/* "$system/develop/lib/pkgconfig/"
		rmdir "$system/lib/pkgconfig"
	fi
}
# qtModule <source dir> [cmake options...]: configure, build and install a
# Qt module into the stage, against the Qt already in the sysroot
qtModule()
{
	local src=$1
	shift
	(cd "$src" && cmake -G Ninja -S . -B build \
			-DCMAKE_TOOLCHAIN_FILE="$WORK/haiku-arm64.cmake" \
			-DQT_HOST_PATH="$HOSTQT" -DCMAKE_BUILD_TYPE=Release \
			-DCMAKE_PREFIX_PATH=/boot/system $QT_DIRS \
			-DCMAKE_C_FLAGS="-fPIC -D_DEFAULT_SOURCE -D_GNU_SOURCE $HAIKU_ERRATA_FLAGS" \
			-DCMAKE_CXX_FLAGS="-fPIC -D_DEFAULT_SOURCE -D_GNU_SOURCE $HAIKU_ERRATA_FLAGS" \
			-DQT_BUILD_EXAMPLES=OFF -DQT_BUILD_TESTS=OFF -DBUILD_WITH_PCH=OFF \
			-Wno-dev "$@" \
		&& cmake --build build --parallel 12 \
		&& DESTDIR="$stage" cmake --install build \
		&& rm -rf build) >> "$log" 2>&1
	# (the errata flags go with the flags given here: setting CMAKE_*_FLAGS
	# drops the toolchain file's initial ones, and qtbase came out without
	# the Cortex-A53 workarounds)
	# (precompiled headers off, the build tree gone once installed: with
	# them the host Qt's three modules took 21 GB and filled the disk)
}

# The Python stack's libraries (milestone M83 (docs/milestones.md)), built when named: openssl3,
# libffi, sqlite, ffmpeg8 (libraries only, beside ffmpeg6), openblas.

for port in ${@:-$ALL}; do
	log=$HAIKU_PROJECT/logs/ports-$port.log
	: > "$log"
	echo "ports-build: $port"
	case $port in
	xz_utils)
		unpack xz-5.8.4.tar.gz xz-5.8.4
		(cd "$WORK/$dir" && ./autogen.sh --no-po4a --no-doxygen \
			&& ./configure --host=aarch64-unknown-haiku \
				--build=x86_64-pc-linux-gnu $(haikuDirs xz_utils) \
				--disable-rpath --disable-static --disable-nls --disable-doc \
				CFLAGS="-O2" \
			&& make -j14 && make install DESTDIR="$stage") >> "$log" 2>&1
		develLibs
		LICENSE="Public Domain" finish xz_utils 5.8.4 \
			"Compression utilities for the XZ and LZMA formats" \
			"XZ Utils: xz, unxz, xzcat, lzma and liblzma."
		;;
	bzip2)
		unpack bzip2-1.0.8.tar.gz bzip2-1.0.8 bzip2-1.0.8.patchset
		(cd "$WORK/$dir" \
			&& make -j14 CC="$CC" AR="$AR" RANLIB="$RANLIB" libbz2.a bzip2 \
				bzip2recover \
			&& make -f Makefile-libbz2_so CC="$CC" \
			&& make install PREFIX="$system" BINDIR="$system/bin" \
				LIBDIR="$system/lib" INCLUDEDIR="$system/develop/headers" \
				MANDIR="$system/documentation/man/man1" \
			&& cp -a libbz2.so.1.0.8 libbz2.so.1.0 "$system/lib/" \
			&& ln -sf libbz2.so.1.0.8 "$system/lib/libbz2.so") >> "$log" 2>&1
		develLibs
		LICENSE="bzip2-1.0.6" LICENSE_FILE=LICENSE finish bzip2 1.0.8 \
			"A high-quality block-sorting file compressor" \
			"bzip2, bunzip2, bzcat, bzip2recover and libbz2."
		;;
	gzip)
		unpack gzip-1.14.tar.xz gzip-1.14 gzip-1.14.patchset
		(cd "$WORK/$dir" && AUTOCONF=: AUTOMAKE=: ACLOCAL=: ./configure \
				--host=aarch64-unknown-haiku --build=x86_64-pc-linux-gnu \
				$(haikuDirs gzip) CFLAGS="-O2 -Wno-error" \
			&& make -j14 AUTOCONF=: AUTOMAKE=: ACLOCAL=: \
			&& make install DESTDIR="$stage" AUTOCONF=: AUTOMAKE=: ACLOCAL=:) \
			>> "$log" 2>&1
		LICENSE="GNU GPL v3" finish gzip 1.14 "The GNU zip compressor" \
			"gzip, gunzip, zcat and the z* helper scripts."
		;;
	tar)
		unpack tar-1.35.tar.bz2 tar-1.35 tar-1.35.patchset
		(cd "$WORK/$dir" && FORCE_UNSAFE_CONFIGURE=1 ./configure \
				--host=aarch64-unknown-haiku --build=x86_64-pc-linux-gnu \
				$(haikuDirs tar) --disable-nls --disable-gcc-warnings \
				LDFLAGS="-lnetwork" CFLAGS="-O2 -D_BSD_SOURCE" \
			&& make -j14 YACC=: && make install DESTDIR="$stage" YACC=:) \
			>> "$log" 2>&1
		rm -rf "$system/lib"	# only the remote tape helper rmt
		LICENSE="GNU GPL v3" finish tar 1.35 "The GNU tape archiver" \
			"GNU tar, which also extracts .tar.gz, .tar.bz2 and .tar.xz archives with gzip, bzip2 and xz."
		;;
	unzip)
		unpack unzip610c23.zip unzip610c23 unzip-6.10c23.patchset
		# Haiku's libroot has no iconv (HaikuPorts uses libiconv); locale.h was
		# included for ICONV_MAPPING
		sed -i 's/ -DICONV_MAPPING/ -DHAVE_LOCALE_H/; s/ -liconv//' "$WORK/$dir/unix/Makefile"
		(cd "$WORK/$dir" && PATH="$WORK/bin:$PATH" make -f unix/Makefile haiku \
				LN="ln -s" \
			&& PATH="$WORK/bin:$PATH" make -f unix/Makefile install \
				PREFIX="$system" MANDIR="$system/documentation/man/man1" \
				LN="ln -s") >> "$log" 2>&1
		LICENSE="Info-ZIP" LICENSE_FILE=LICENSE finish unzip 6.10c23 \
			"Extraction utility for zip archives" \
			"Info-ZIP's unzip, funzip, unzipsfx, zipgrep and zipinfo."
		;;
	zip)
		unpack zip30.tar.gz zip30 zip-3.0.patchset
		# its configure cannot run the test programs: Large File Support (which
		# Haiku has) is given here
		(cd "$WORK/$dir" && sh unix/configure "$CC" "-I. -DUNIX -DLARGE_FILE_SUPPORT" \
			&& echo 'LFLAGS2="-lbe -lbz2"' >> flags \
			&& make -f unix/Makefile generic \
			&& make -f unix/Makefile install PREFIX="$system" \
				BINDIR="$system/bin" MANDIR="$system/documentation/man/man1") \
			>> "$log" 2>&1
		LICENSE="Info-ZIP" LICENSE_FILE=LICENSE finish zip 3.0 "Compression utility for zip archives" \
			"Info-ZIP's zip, zipcloak, zipnote and zipsplit."
		;;
	libedit)
		unpack libedit-20260512-3.1.tar.gz libedit-20260512-3.1 \
			libedit-20260512_3.1.patchset
		(cd "$WORK/$dir" && autoreconf --force --install \
			&& ./configure --host=aarch64-unknown-haiku \
				--build=x86_64-pc-linux-gnu $(haikuDirs libedit) \
				--enable-examples=no --enable-widec --disable-static \
				CFLAGS="-O2" \
			&& make -j14 && make install DESTDIR="$stage") >> "$log" 2>&1
		develLibs
		LICENSE="BSD (3-clause)" finish libedit 20260512_3.1 \
			"Command line editor library" \
			"The NetBSD Editline library: line editing and history, with a readline compatibility interface."
		;;
	glu)
		unpack glu-9.0.0.tar.gz glu-9.0.0
		(cd "$WORK/$dir" && ./configure --host=aarch64-unknown-haiku \
				--build=x86_64-pc-linux-gnu $(haikuDirs glu) \
				--disable-static CFLAGS="-O2 -include stddef.h" \
				CXXFLAGS="-O2" \
			&& fixLibtool \
			&& make -j14 && make install DESTDIR="$stage") >> "$log" 2>&1
		develLibs
		# the license is in the sources' headers
		sed -n '/^\/\*/,/\*\//p' "$WORK/$dir/include/GL/glu.h" \
			| sed '/\*\//q' > "$WORK/$dir/LICENSE-SGI"
		LICENSE="SGI-B-2.0" LICENSE_FILE=LICENSE-SGI finish glu 9.0.0 "The OpenGL utility library" \
			"Mesa's GLU: the OpenGL utility library."
		;;
	tiff)
		unpack tiff-4.7.0.tar.gz tiff-4.7.0
		# JPEG compression with libjpeg-turbo from tools/deps-build.sh, linked
		# statically (arm64 has no libjpeg package)
		(cd "$WORK/$dir" && ./configure --host=aarch64-unknown-haiku \
				--build=x86_64-pc-linux-gnu $(haikuDirs tiff) \
				--disable-static --disable-tools --disable-tests \
				--disable-contrib --disable-docs --disable-cxx \
				--disable-libdeflate --disable-jbig --disable-lerc \
				--disable-lzma --disable-zstd --disable-webp \
				--with-jpeg-include-dir="$HAIKU_PREFIX/include" \
				--with-jpeg-lib-dir="$HAIKU_PREFIX/lib" CFLAGS="-O2" \
			&& make -j14 && make install DESTDIR="$stage") >> "$log" 2>&1
		develLibs
		LICENSE="LibTIFF" LICENSE_FILE=LICENSE.md finish tiff 4.7.0 "Library for TIFF images" \
			"LibTIFF, with deflate, LZW, PackBits and JPEG compression."
		;;
	libwebp)
		unpack libwebp-1.6.0.tar.gz libwebp-1.6.0 libwebp-1.6.0.patchset
		(cmake -S "$WORK/$dir" -B "$WORK/$dir/build" \
				-DCMAKE_TOOLCHAIN_FILE="$WORK/haiku-arm64.cmake" \
				-DCMAKE_BUILD_TYPE=Release $CMAKE_DIRS -DBUILD_SHARED_LIBS=ON \
				-DWEBP_BUILD_ANIM_UTILS=OFF -DWEBP_BUILD_CWEBP=OFF \
				-DWEBP_BUILD_DWEBP=OFF -DWEBP_BUILD_GIF2WEBP=OFF \
				-DWEBP_BUILD_IMG2WEBP=OFF -DWEBP_BUILD_VWEBP=OFF \
				-DWEBP_BUILD_WEBPINFO=OFF -DWEBP_BUILD_WEBPMUX=OFF \
				-DWEBP_BUILD_EXTRAS=OFF \
			&& cmake --build "$WORK/$dir/build" -j14 \
			&& DESTDIR="$stage" cmake --install "$WORK/$dir/build") \
			>> "$log" 2>&1
		develLibs
		LICENSE="BSD (3-clause)" finish libwebp 1.6.0 \
			"Library for the WebP image format" \
			"libwebp, libwebpdecoder, libwebpdemux, libwebpmux and libsharpyuv."
		;;
	freetype)
		# the recipe's options, but with libpng linked in statically (the
		# PNG bitmaps of colour emoji fonts; the image has no libpng) and
		# without bzip2 (bzip2-compressed fonts), which the image doesn't
		# have either. HarfBuzz is loaded when it is installed (the
		# recipe's patch).
		unpack freetype-2.14.3.tar.xz freetype-2.14.3 \
			enable-validation-modules.patchset freetype-2.14.3.patchset
		# libpng from tools/deps-build.sh, in a directory of the sysroot
		# that only this build's pkg-config looks in; its symbols stay
		# inside libfreetype
		png=$SYSROOT/boot/system/develop/png-static
		haiku_rm_tree "$png"
		mkdir -p "$png/lib/pkgconfig" "$png/headers"
		cp "$HAIKU_PREFIX/lib/libpng16.a" "$png/lib/"
		cp -a "$HAIKU_PREFIX/include/libpng16/." "$png/headers/"
		cat > "$png/lib/pkgconfig/libpng.pc" <<EOF
prefix=/boot/system/develop/png-static
Name: libpng
Description: Loads and saves PNG files (static)
Version: 1.6.59
Requires.private: zlib
Libs: -L\${prefix}/lib -lpng16 -Wl,--exclude-libs,libpng16.a
Cflags: -I\${prefix}/headers
EOF
		cat > "$WORK/freetype-png.meson" <<EOF
[properties]
pkg_config_libdir = '$png/lib/pkgconfig:$PKG_CONFIG_LIBDIR'
EOF
		(meson setup "$WORK/$dir/build" "$WORK/$dir" \
				--cross-file "$WORK/haiku-arm64.meson" \
				--cross-file "$WORK/freetype-png.meson" --buildtype release \
				--prefix /boot/system --libdir lib --bindir bin \
				--datadir data --includedir develop/headers \
				-Derror_strings=true -Dharfbuzz=dynamic -Dpng=enabled \
				-Dbzip2=disabled -Dbrotli=disabled -Dzlib=system \
				-Dtests=disabled \
			&& ninja -C "$WORK/$dir/build" -j14 \
			&& DESTDIR="$stage" ninja -C "$WORK/$dir/build" install) \
			>> "$log" 2>&1
		develLibs
		# libpng is inside libfreetype: what builds against FreeType
		# doesn't need libpng's pkg-config file
		sed -i -e '/^Requires.private:/s/,* *libpng[^,]*//' \
			-e '/^Requires.private: *$/d' \
			"$system/develop/lib/pkgconfig/freetype2.pc"
		rm -rf "$system/data/aclocal"
			# freetype2.m4: the recipe's devel package
		REVISION=2 LICENSE="FreeType" LICENSE_FILE=docs/FTL.TXT \
			ALSO_LICENSE=LibPNG finish freetype 2.14.3 \
			"A free, high-quality, and portable font engine" \
			"FreeType: the app_server and applications draw text with it."
		;;
	zstd)
		# the library only, as the recipe's base package (its commands are
		# zstd_bin); lib/ also goes to develop/sources, untouched, for
		# packagefs's and the boot loader's decompressor
		unpack zstd-v1.5.7.tar.gz zstd-1.5.7 zstd-1.5.7.patchset
		mkdir -p "$system/develop/sources/zstd-1.5.7"
		cp -a "$WORK/$dir/lib" "$system/develop/sources/zstd-1.5.7/"
		(cd "$WORK/$dir/lib" && make -j14 CC="$CC" AR="$AR" \
				prefix=/boot/system libdir=/boot/system/lib \
				includedir=/boot/system/develop/headers libzstd \
			&& make CC="$CC" AR="$AR" prefix=/boot/system \
				libdir=/boot/system/lib \
				includedir=/boot/system/develop/headers DESTDIR="$stage" \
				install-shared install-includes install-pc) >> "$log" 2>&1
		develLibs
		LICENSE="BSD (2-clause)" LICENSE_FILE=LICENSE finish zstd 1.5.7 \
			"Zstandard, a fast real-time compression algorithm" \
			"The zstd library: packages compressed with zstd, as HaikuPorts' are."
		;;
	zstd_bin)
		# the commands, as the recipe's bin package (zstd, unzstd, zstdcat,
		# zstdmt, zstdgrep, zstdless; not the C++ pzstd), for zstd's own
		# format only: the recipe's gzip, xz and lz4 support needs ports
		# this tree doesn't have. The library is built in (static).
		unpack zstd-v1.5.7.tar.gz zstd-1.5.7 zstd-1.5.7.patchset
		(cd "$WORK/$dir/programs" && make -j14 CC="$CC" AR="$AR" \
				HAVE_ZLIB=0 HAVE_LZMA=0 HAVE_LZ4=0 zstd-release \
			&& make CC="$CC" AR="$AR" HAVE_ZLIB=0 HAVE_LZMA=0 HAVE_LZ4=0 \
				prefix=/boot/system bindir=/boot/system/bin \
				mandir=/boot/system/documentation/man DESTDIR="$stage" \
				install) >> "$log" 2>&1
		LICENSE="BSD (2-clause)" LICENSE_FILE=LICENSE finish zstd_bin 1.5.7 \
			"Zstandard's commands" \
			"zstd, unzstd, zstdcat, zstdmt, zstdgrep and zstdless, for zstd's own format."
		;;
	dav1d)
		unpack dav1d-1.5.4.tar.bz2 dav1d-1.5.4
		(meson setup "$WORK/$dir/build" "$WORK/$dir" \
				--cross-file "$WORK/haiku-arm64.meson" --buildtype release \
				--prefix /boot/system --libdir lib \
				--includedir develop/headers -Denable_tools=false \
				-Denable_tests=false -Denable_examples=false \
			&& ninja -C "$WORK/$dir/build" -j14 \
			&& DESTDIR="$stage" ninja -C "$WORK/$dir/build" install) \
			>> "$log" 2>&1
		develLibs
		LICENSE="BSD (2-clause)" finish dav1d 1.5.4 "An AV1 decoder" \
			"VideoLAN's dav1d AV1 decoding library, with its NEON code."
		;;
	libavif1.0)
		unpack libavif-1.4.2.tar.gz libavif-1.4.2 libavif1.0-1.4.2.patchset
		# decoding with dav1d; no encoder (arm64 has no aom or rav1e)
		(cmake -S "$WORK/$dir" -B "$WORK/$dir/build" \
				-DCMAKE_TOOLCHAIN_FILE="$WORK/haiku-arm64.cmake" \
				-DCMAKE_BUILD_TYPE=Release $CMAKE_DIRS -DBUILD_SHARED_LIBS=ON \
				-DAVIF_CODEC_DAV1D=SYSTEM -DAVIF_CODEC_AOM=OFF \
				-DAVIF_CODEC_RAV1E=OFF -DAVIF_CODEC_SVT=OFF \
				-DAVIF_LIBSHARPYUV=OFF -DAVIF_LIBYUV=OFF -DAVIF_LIBXML2=OFF \
				-DAVIF_BUILD_APPS=OFF -DAVIF_BUILD_TESTS=OFF \
				-DAVIF_BUILD_EXAMPLES=OFF \
			&& cmake --build "$WORK/$dir/build" -j14 \
			&& DESTDIR="$stage" cmake --install "$WORK/$dir/build") \
			>> "$log" 2>&1
		develLibs
		LICENSE="BSD (2-clause)" finish libavif1.0 1.4.2 \
			"Library for the AVIF image format" \
			"libavif, decoding AV1 images with dav1d (no encoder)."
		;;
	ffmpeg6)
		unpack ffmpeg-6.1.6.tar.xz ffmpeg-6.1.6 ffmpeg-6.1.6.patchset
		# FFmpeg's own decoders, dav1d for AV1, libwebp, xz; LGPL v3 (no GPL
		# or non-free parts). Not an autotools configure.
		(cd "$WORK/$dir" && ./configure --enable-cross-compile \
				--target-os=haiku --arch=aarch64 --cpu=cortex-a53 \
				--cc="$GCC" --cxx="$GXX" --ar="$AR" --ranlib="$RANLIB" \
				--nm="$HAIKU_TOOLS/aarch64-unknown-haiku-nm" --strip="$STRIP" \
				--sysroot="$SYSROOT" --pkg-config=pkg-config \
				--extra-cflags="$HAIKU_ERRATA_FLAGS" \
				--extra-ldflags="$HAIKU_ERRATA_FLAGS" \
				--prefix=/boot/system --bindir=/boot/system/bin \
				--datadir=/boot/system/data/ffmpeg6 \
				--incdir=/boot/system/develop/headers \
				--libdir=/boot/system/lib --shlibdir=/boot/system/lib \
				--mandir=/boot/system/documentation/man \
				--disable-debug --disable-stripping --disable-static \
				--enable-shared --enable-version3 --enable-pic --disable-doc \
				--enable-libdav1d --enable-libwebp --enable-lzma \
			&& make -j14 && make install DESTDIR="$stage") >> "$log" 2>&1
		develLibs
		LICENSE="GNU LGPL v3" finish ffmpeg6 6.1.6 \
			"Audio and video codecs and formats" \
			"FFmpeg's libraries (libavcodec, libavformat, libavfilter, libavutil, libswscale, libswresample) and the ffmpeg and ffprobe programs; decoders and encoders built into FFmpeg, AV1 decoding with dav1d, WebP."
		;;
	ffmpeg8)
		# For PyAV (14 and later need FFmpeg 7 or newer), beside ffmpeg6,
		# which the media plugin uses: the libraries only (no programs), its
		# headers, links and pkg-config files under develop/*/ffmpeg8 so
		# that ffmpeg6's stay in the sysroot. Same codecs as ffmpeg6.
		unpack ffmpeg-8.1.2.tar.xz ffmpeg-8.1.2
		(cd "$WORK/$dir" && ./configure --enable-cross-compile \
				--target-os=haiku --arch=aarch64 --cpu=cortex-a53 \
				--cc="$GCC" --cxx="$GXX" --ar="$AR" --ranlib="$RANLIB" \
				--nm="$HAIKU_TOOLS/aarch64-unknown-haiku-nm" --strip="$STRIP" \
				--sysroot="$SYSROOT" --pkg-config=pkg-config \
				--extra-cflags="$HAIKU_ERRATA_FLAGS" \
				--extra-ldflags="$HAIKU_ERRATA_FLAGS" \
				--prefix=/boot/system --datadir=/boot/system/data/ffmpeg8 \
				--incdir=/boot/system/develop/headers/ffmpeg8 \
				--libdir=/boot/system/lib --shlibdir=/boot/system/lib \
				--pkgconfigdir=/boot/system/develop/lib/ffmpeg8/pkgconfig \
				--disable-programs --disable-debug --disable-stripping \
				--disable-static --enable-shared --enable-version3 \
				--enable-pic --disable-doc \
				--enable-libdav1d --enable-libwebp --enable-lzma \
			&& make -j14 && make install DESTDIR="$stage") >> "$log" 2>&1
		mkdir -p "$system/develop/lib/ffmpeg8"
		for f in "$system"/lib/*.so; do
			[ -L "$f" ] || continue
			ln -sf "../../../lib/$(readlink "$f")" \
				"$system/develop/lib/ffmpeg8/$(basename "$f")"
			rm "$f"
		done
		sed -i -e 's|^libdir=.*|libdir=/boot/system/develop/lib/ffmpeg8|' \
			"$system"/develop/lib/ffmpeg8/pkgconfig/*.pc
		LICENSE="GNU LGPL v3" finish ffmpeg8 8.1.2 \
			"Audio and video codec libraries (FFmpeg 8)" \
			"FFmpeg 8's libraries (libavcodec, libavformat, libavfilter, libavdevice, libavutil, libswscale, libswresample) for programs that need FFmpeg 7 or newer, such as PyAV; beside ffmpeg6, which the media plugin uses. FFmpeg's own decoders and encoders, AV1 decoding with dav1d, WebP."
		;;
	openssl3)
		# Its Haiku targets are x86 and x86_64; arm64 is the x86_64 one
		# without assembly, as HaikuPorts builds every other architecture
		unpack openssl-3.5.9.tar.gz openssl-3.5.9 haikuports/openssl3-3.5.9.patchset
		cat > "$WORK/$dir/Configurations/60-haiku-aarch64.conf" <<'EOF'
my %targets = (
    "haiku-aarch64" => {
        inherit_from     => [ "haiku-common" ],
        bn_ops           => "SIXTY_FOUR_BIT_LONG",
    },
);
EOF
		(cd "$WORK/$dir" && ./Configure haiku-aarch64 no-asm shared \
				enable-zlib --prefix=/boot/system --libdir=lib \
				--openssldir=/boot/system/data/ssl \
			&& make -j14 && make install_sw install_ssldirs DESTDIR="$stage") \
			>> "$log" 2>&1
		mkdir -p "$system/develop"
		mv "$system/include" "$system/develop/headers"
		develLibs
		# the CA bundle that X509_CERT_FILE names (HaikuPorts' patchset):
		# ca_root_certificates, architecture any, as on stock Haiku
		REVISION=2 LICENSE="Apache v2" \
			EXTRA_REQUIRES=ca_root_certificates finish openssl3 3.5.9 \
			"Cryptography and TLS library (OpenSSL 3)" \
			"OpenSSL 3.5 (LTS): libssl, libcrypto, their modules and the openssl command."
		;;
	libffi)
		unpack libffi-3.4.6.tar.gz libffi-3.4.6 haikuports/libffi-3.4.6.patchset
		(cd "$WORK/$dir" && ./configure --host=aarch64-unknown-haiku \
				--build=x86_64-pc-linux-gnu $(haikuDirs libffi) \
				--disable-static --disable-docs CFLAGS="-O2" \
			&& make -j14 && make install DESTDIR="$stage") >> "$log" 2>&1
		develLibs
		LICENSE="MIT" LICENSE_FILE=LICENSE finish libffi 3.4.6 \
			"A portable foreign function interface library" \
			"libffi calls functions whose interface is known only at run time (Python's ctypes)."
		;;
	sqlite)
		unpack sqlite-autoconf-3530400.tar.gz sqlite-autoconf-3530400
		(cd "$WORK/$dir" && CPPFLAGS="-DSQLITE_ENABLE_COLUMN_METADATA=1 \
				-DSQLITE_ENABLE_UNLOCK_NOTIFY -DSQLITE_ENABLE_DBSTAT_VTAB \
				-DSQLITE_SECURE_DELETE=1 -DSQLITE_ENABLE_FTS3=1 \
				-DSQLITE_ENABLE_FTS3_PARENTHESIS -DSQLITE_ENABLE_FTS5 \
				-DSQLITE_ENABLE_RTREE" ./configure \
				--host=aarch64-unknown-haiku --build=x86_64-pc-linux-gnu \
				--prefix=/boot/system --libdir=/boot/system/lib \
				--includedir=/boot/system/develop/headers \
				--mandir=/boot/system/documentation/man --soname=legacy \
				--disable-static --disable-readline \
			&& make && make install DESTDIR="$stage") >> "$log" 2>&1
		develLibs
		LICENSE="Public Domain" finish sqlite 3.53.4 \
			"An SQL database engine in a library" \
			"SQLite: libsqlite3 and the sqlite3 shell."
		;;
	python3.15)
		# A host Python of the same version first: a cross build runs it to
		# generate code and compile the standard library's bytecode; with
		# pip, for building the modules (tools/python-modules-build.sh)
		host=$HAIKU_PROJECT/deps/host/python3.15
		if [ ! -x "$host/bin/python3.15" ]; then
			rm -rf "$WORK/python-host"
			mkdir -p "$WORK/python-host"
			tar xf "$SRCS/Python-3.15.2.tar.xz" -C "$WORK/python-host"
			(cd "$WORK/python-host/Python-3.15.2" \
				&& env -u CC -u CXX -u AR -u RANLIB -u PKG_CONFIG_LIBDIR \
					-u PKG_CONFIG_SYSROOT_DIR ./configure --prefix="$host" \
					--with-ensurepip=install \
				&& env -u CC -u CXX -u AR -u RANLIB make -j14 \
				&& env -u CC -u CXX -u AR -u RANLIB make install) >> "$log" 2>&1
		fi
		unpack Python-3.15.2.tar.xz Python-3.15.2 \
			haikuports/python3.15-3.15.2.patchset \
			../../patches/python/python3.15-haiku-cross.patch \
			../../patches/python/python3.15-ctypes-library-path.patch
		# The patchset's configure.ac changes (Haiku's libpython name and
		# soname, export-dynamic, no libm) need configure regenerated; the
		# autoconf-archive macros come with aclocal.m4
		(cd "$WORK/$dir" && autoconf) >> "$log" 2>&1
		cat > "$WORK/$dir/config.site" <<'EOF'
ac_cv_file__dev_ptmx=yes
ac_cv_file__dev_ptc=no
ac_cv_buggy_getaddrinfo=no
EOF
		(cd "$WORK/$dir" && CONFIG_SITE=config.site \
				BASECFLAGS="-pipe -D_BSD_SOURCE" OPT="-Wall -O2" ./configure \
				--host=aarch64-unknown-haiku --build=x86_64-pc-linux-gnu \
				--with-build-python="$host/bin/python3.15" \
				--prefix=/boot/system --bindir=/boot/system/bin \
				--libdir=/boot/system/lib --datarootdir=/boot/system/data \
				--includedir=/boot/system/develop/headers \
				--mandir=/boot/system/documentation/man \
				--enable-shared --with-ensurepip=no --with-readline=editline \
				--with-system-libmpdec=no --without-static-libpython \
				--disable-test-modules \
				--with-tzpath=/boot/system/data/zoneinfo:/boot/system/non-packaged/data/zoneinfo \
			&& make -j14 && make install DESTDIR="$stage") >> "$log" 2>&1
		rm -f "$system/lib/libpython3.so" "$system"/bin/idle3*
		vendor=$system/lib/python3.15/vendor-packages
		mkdir -p "$vendor"
		echo 'This directory contains packaged python modules.' > "$vendor/README"
		# pip, from the wheel ensurepip carries (no download)
		(cd "$vendor" && unzip -q "$WORK/$dir"/Lib/ensurepip/_bundled/pip-*.whl)
		for f in pip3 pip3.15; do
			printf '#!/bin/python3.15\nimport sys\nfrom pip._internal.cli.main import main\nsys.exit(main())\n' \
				> "$system/bin/$f"
			chmod +x "$system/bin/$f"
		done
		develLibs
		LICENSE="Python" LICENSE_FILE=LICENSE finish python3.15 3.15.2 \
			"An interpreted, interactive, object-oriented programming language" \
			"Python 3.15 with its standard library (ssl, sqlite3, ctypes, lzma, bz2, curses, readline through libedit) and pip. Packaged modules go to lib/python3.15/vendor-packages, pip's to non-packaged."
		;;
	gcc_fortran_syslibs)
		# libgfortran.so.5 from tools/gfortran-cross-build.sh, for OpenBLAS
		# and SciPy; it needs only libgcc_s and libroot
		dir=gcc-fortran-syslibs
		rm -rf "$WORK/$dir" "$WORK/$dir-stage"
		mkdir -p "$WORK/$dir"
		stage=$WORK/$dir-stage
		system=$stage/boot/system
		mkdir -p "$system/lib"
		cp -a "$HAIKU_PROJECT"/deps/cross-fortran/aarch64-unknown-haiku/lib/libgfortran.so* \
			"$system/lib/"
		cp "$HAIKU_PROJECT/buildtools/gcc/COPYING.RUNTIME" "$WORK/$dir/"
		develLibs
		LICENSE="GCC Runtime Library Exception" LICENSE_FILE=COPYING.RUNTIME \
			ALSO_LICENSE="GNU GPL v3" finish gcc_fortran_syslibs 13.3.0 \
			"GCC's Fortran runtime library" \
			"libgfortran, the run-time library of programs and libraries written in Fortran (OpenBLAS's LAPACK, SciPy)."
		;;
	openblas)
		# pthreads (no libgomp in the cross tools), 4 threads (the C2's
		# cores), Cortex-A53 kernels; LAPACK from Fortran with the second
		# cross GCC (tools/gfortran-cross-build.sh)
		unpack OpenBLAS-0.3.30.tar.gz OpenBLAS-0.3.30
		ftools=$HAIKU_PROJECT/deps/cross-fortran/bin
		conf=(TARGET=CORTEXA53 BINARY=64 HOSTCC=gcc NO_STATIC=1 NO_AFFINITY=1
			NO_WARMUP=1 USE_THREAD=1 USE_OPENMP=0 NUM_THREADS=4
			"CC=$ftools/aarch64-unknown-haiku-gcc --sysroot=$SYSROOT"
			"FC=$ftools/aarch64-unknown-haiku-gfortran --sysroot=$SYSROOT"
			"AR=$AR" "RANLIB=$RANLIB" "COMMON_OPT=-O2 $HAIKU_ERRATA_FLAGS"
			# its check misses the cross gfortran's runtime: without it
			# libopenblas didn't need libgfortran (_gfortran_etime
			# unresolved); EXTRALIB set here replaces the Makefiles' own
			"EXTRALIB=$HAIKU_ERRATA_FLAGS -L$SYSROOT/boot/system/develop/lib -lgfortran")
		(cd "$WORK/$dir" && env -u CC -u CXX make -j14 "${conf[@]}" libs shared \
			&& env -u CC -u CXX make "${conf[@]}" PREFIX=/boot/system \
				DESTDIR="$stage" OPENBLAS_LIBRARY_DIR=/boot/system/lib \
				OPENBLAS_INCLUDE_DIR=/boot/system/develop/headers install) \
			>> "$log" 2>&1
		sed -i -e 's|^libdir=.*|libdir=/boot/system/develop/lib|' \
			-e 's|^includedir=.*|includedir=/boot/system/develop/headers|' \
			"$system"/lib/pkgconfig/openblas.pc
		develLibs
		LICENSE="BSD (3-clause)" LICENSE_FILE=LICENSE finish openblas 0.3.30 \
			"An optimized BLAS and LAPACK library" \
			"OpenBLAS with LAPACK: linear algebra for NumPy and SciPy, Cortex-A53 kernels, up to 4 threads." \
			"lib:libopenblas = 0.3.30 compat >= 0"
		;;
	numpy_python315)
		# OpenBLAS for BLAS and LAPACK; the baseline SIMD only (NEON): the
		# runtime dispatch can't tell the CPU's features on Haiku
		unpack numpy-2.5.4.tar.gz numpy-2.5.4 haikuports/numpy-2.5.3.patchset
		pyCross
		(cd "$WORK/$dir" && "$PYVENV/bin/python" -m build --wheel \
				--no-isolation --skip-dependency-check --outdir "$WORK/wheels" \
				-Cbuilddir=build-cross \
				-Csetup-args=--cross-file="$PYCROSS_MESON" \
				-Csetup-args=-Dblas=openblas -Csetup-args=-Dlapack=openblas \
				-Csetup-args=-Dallow-noblas=false \
				-Csetup-args=-Dcpu-baseline=min -Csetup-args=-Dcpu-dispatch=none \
				-Ccompile-args=-j14) >> "$log" 2>&1
		unset _PYTHON_SYSCONFIGDATA_NAME PYTHONPATH _PYTHON_HOST_PLATFORM
		pyInstallWheel "$WORK"/wheels/numpy-2.5.4-*.whl
		# for SciPy's build: its pkg-config file with absolute paths (the
		# wheel's is relative to itself, under vendor-packages)
		mkdir -p "$system/develop/lib/pkgconfig"
		cat > "$system/develop/lib/pkgconfig/numpy.pc" <<PC
prefix=/boot/system/lib/python3.15/vendor-packages/numpy/_core
includedir=\${prefix}/include

Name: numpy
Description: NumPy is the fundamental package for scientific computing with Python.
Version: 2.5.4
Cflags: -I\${includedir}
PC
		LICENSE="BSD (3-clause)" LICENSE_FILE=LICENSE.txt \
			EXTRA_REQUIRES="python3.15 openblas" finish numpy_python315 2.5.4 \
			"Arrays and numerical computing for Python" \
			"NumPy: n-dimensional arrays, linear algebra (OpenBLAS), FFTs and random numbers."
		;;
	scipy_python315)
		# Fortran (tools/gfortran-cross-build.sh), OpenBLAS, the boards'
		# NumPy found through its pkg-config file; no Pythran, as HaikuPorts
		unpack scipy-1.18.1.tar.gz scipy-1.18.1
		pyCross
		(cd "$WORK/$dir" && "$PYVENV/bin/python" -m build --wheel \
				--no-isolation --skip-dependency-check --outdir "$WORK/wheels" \
				-Cbuilddir=build-cross \
				-Csetup-args=--cross-file="$PYCROSS_MESON" \
				-Csetup-args=-Dblas=openblas -Csetup-args=-Dlapack=openblas \
				-Csetup-args=-Duse-pythran=false \
				-Ccompile-args=-j10) >> "$log" 2>&1
		unset _PYTHON_SYSCONFIGDATA_NAME PYTHONPATH _PYTHON_HOST_PLATFORM
		pyInstallWheel "$WORK"/wheels/scipy-1.18.1-*.whl
		LICENSE="BSD (3-clause)" LICENSE_FILE=LICENSE.txt \
			EXTRA_REQUIRES="python3.15 numpy_python315 openblas" finish \
			scipy_python315 1.18.1 "Scientific computing for Python" \
			"SciPy: signal processing, optimization, integration, interpolation, statistics, sparse matrices and more, on NumPy and OpenBLAS."
		;;
	av_python315)
		# FFmpeg 8 (ffmpeg8, beside ffmpeg6) through its own pkg-config
		# directory; setuptools compiles with the target's sysconfig
		unpack av-19.0.2.tar.gz av-19.0.2 \
			../../patches/python/av-19.0.2-averror.patch
		pyCross
		(cd "$WORK/$dir" \
			&& PKG_CONFIG_LIBDIR="$SYSROOT/boot/system/develop/lib/ffmpeg8/pkgconfig" \
				"$PYVENV/bin/python" -m build --wheel --no-isolation \
				--skip-dependency-check --outdir "$WORK/wheels") >> "$log" 2>&1
		unset _PYTHON_SYSCONFIGDATA_NAME PYTHONPATH _PYTHON_HOST_PLATFORM
		pyInstallWheel "$WORK"/wheels/av-19.0.2-*.whl
		LICENSE="BSD (3-clause)" LICENSE_FILE=LICENSE.txt \
			EXTRA_REQUIRES="python3.15" finish av_python315 19.0.2 \
			"Pythonic bindings for FFmpeg's libraries" \
			"PyAV: audio and video decoding, encoding and filtering through FFmpeg 8 (ffmpeg8)."
		;;
	qt6_base)
		# ICU for time zones (Haiku has no zoneinfo files), as HaikuPorts
		if [ ! -e "$SYSROOT/boot/system/lib/libicuuc.so.74" ]; then
			for f in $HAIKU_GEN/download/icu74-*_bootstrap-*-arm64.hpkg \
					$HAIKU_GEN/download/icu74_devel-*_bootstrap-*-arm64.hpkg; do
				"$HAIKU_HOST_TOOLS/package/package" extract \
					-C "$SYSROOT/boot/system" "$f"
			done
		fi
		unpack qtbase-everywhere-src-6.12.0.tar.xz qtbase-everywhere-src-6.12.0 \
			../../patches/qt6/qt6_base-6.12.0-haiku.patch
		qtModule "$WORK/$dir" -DQT_QMAKE_TARGET_MKSPEC=haiku-g++ \
			-DCMAKE_SHARED_LINKER_FLAGS=-lbsd \
			-DFEATURE_openssl_linked=ON -DFEATURE_system_sqlite=ON \
			-DFEATURE_glib=OFF -DFEATURE_rpath=OFF -DFEATURE_syslog=ON \
			-DFEATURE_zstd=ON -DFEATURE_icu=ON -DFEATURE_dbus=OFF
		qtDevel
		LICENSE="GNU LGPL v3" LICENSE_FILE=LICENSES/LGPL-3.0-only.txt \
			finish qt6_base 6.12.0 "Qt 6: core, GUI, widgets, networking, SQL" \
			"Qt 6.12's base modules (Core, Gui, Widgets, Network, Sql, Concurrent, Xml, PrintSupport, OpenGL, Test) and tools."
		;;
	qt6_shadertools)
		unpack qtshadertools-everywhere-src-6.12.0.tar.xz \
			qtshadertools-everywhere-src-6.12.0
		qtModule "$WORK/$dir"
		qtDevel
		LICENSE="GNU LGPL v3" LICENSE_FILE=LICENSES/LGPL-3.0-only.txt \
			EXTRA_REQUIRES=qt6_base finish qt6_shadertools 6.12.0 \
			"Qt 6 Shader Tools" \
			"Qt 6.12's shader conditioning (QShaderBaker), which Qt Quick uses."
		;;
	qt6_svg)
		unpack qtsvg-everywhere-src-6.12.0.tar.xz qtsvg-everywhere-src-6.12.0
		qtModule "$WORK/$dir"
		qtDevel
		LICENSE="GNU LGPL v3" LICENSE_FILE=LICENSES/LGPL-3.0-only.txt \
			EXTRA_REQUIRES=qt6_base finish qt6_svg 6.12.0 "Qt 6 SVG" \
			"Qt 6.12's SVG rendering (QtSvg, QtSvgWidgets) and SVG icon and image format plugins."
		;;
	qt6_declarative)
		unpack qtdeclarative-everywhere-src-6.12.0.tar.xz \
			qtdeclarative-everywhere-src-6.12.0 \
			haikuports/qt6_declarative-6.10.3.patchset
		# no VectorImage (SVG as QML): its svgtoqml would be a host tool
		# the host Qt (tools/qt6-build.sh, without QtSvg) doesn't have
		qtModule "$WORK/$dir" -DFEATURE_quick_vectorimage=OFF
		qtDevel
		LICENSE="GNU LGPL v3" LICENSE_FILE=LICENSES/LGPL-3.0-only.txt \
			EXTRA_REQUIRES="qt6_base qt6_shadertools qt6_svg" finish \
			qt6_declarative 6.12.0 "Qt 6 QML and Qt Quick" \
			"Qt 6.12's QML engine, Qt Quick, Qt Quick Controls and the qml tools."
		;;
	qt6_multimedia)
		# FFmpeg 8 for decoding (its own pkg-config directory), audio out
		# through the Haiku backend of patches/qt6 (BSoundPlayer)
		unpack qtmultimedia-everywhere-src-6.12.0.tar.xz \
			qtmultimedia-everywhere-src-6.12.0 \
			../../patches/qt6/qt6_multimedia-6.12.0-haiku.patch
		PKG_CONFIG_LIBDIR="$SYSROOT/boot/system/develop/lib/ffmpeg8/pkgconfig:$PKG_CONFIG_LIBDIR" \
			qtModule "$WORK/$dir" -DFEATURE_ffmpeg=ON -DFEATURE_vulkan=OFF
		qtDevel
		LICENSE="GNU LGPL v3" LICENSE_FILE=LICENSES/LGPL-3.0-only.txt \
			EXTRA_REQUIRES="qt6_base qt6_declarative ffmpeg8" finish \
			qt6_multimedia 6.12.0 "Qt 6 Multimedia" \
			"Qt 6.12's audio and video playback and recording (QtMultimedia), FFmpeg 8 for decoding, audio output through Haiku's system mixer."
		;;
	binutils)
		# The native assembler, linker and tools for the boards, from the
		# cross tools' source (buildtools: 2.46.1 with our erratum 843419
		# rescan), laid out as HaikuPorts': the tools' directory in
		# develop/tools, the commands in bin
		nativeTree binutils-native
		(cd "$WORK/$dir" && nativeEnv \
				"$HAIKU_PROJECT/buildtools/binutils/configure" \
				--build=x86_64-pc-linux-gnu --host=aarch64-unknown-haiku \
				--target=aarch64-unknown-haiku --prefix=/boot/system \
				--exec-prefix=/boot/system/develop/tools \
				--bindir=/boot/system/bin --libdir=/boot/system/develop/lib \
				--includedir=/boot/system/develop/headers/binutils \
				--mandir=/boot/system/documentation/man \
				--disable-nls --disable-werror --disable-shared \
				--disable-gprofng --enable-plugins --enable-64-bit-bfd \
				--enable-ld=default --enable-targets=aarch64-unknown-haiku \
				--with-sysroot=/ --enable-default-execstack=no \
			&& nativeEnv make -j14 MAKEINFO=true all-binutils all-gas all-ld \
			&& nativeEnv make MAKEINFO=true DESTDIR="$stage" \
				install-binutils install-gas install-ld) >> "$log" 2>&1
		rm -rf "$system/develop/lib" "$system/develop/headers" "$system/share"
		# the tools' directory (where gcc looks) links to the commands
		tooldir=$system/develop/tools/aarch64-unknown-haiku/bin
		for f in "$tooldir"/*; do
			[ -e "$system/bin/$(basename "$f")" ] || continue
			rm "$f"
			ln -s "../../../../bin/$(basename "$f")" "$f"
		done
		NO_SYSROOT=1 KEEP_DEVELOP=1 LICENSE="GNU GPL v3" \
			finish binutils 2.46.1 \
			"The GNU binary utilities" \
			"The GNU assembler (as), linker (ld) and tools (ar, nm, objdump, readelf, strip...) for building on the board, with the Cortex-A53 erratum 843419 fix in ld."
		;;
	gcc)
		# The native C and C++ compiler for the boards, GCC 13.3 from the
		# cross tools' source (buildtools), as the system's gcc_syslibs:
		# a Canadian cross (the target libraries by the cross compiler),
		# laid out as HaikuPorts' (develop/tools, commands in bin), with
		# the Cortex-A53 errata workarounds on by default. The shared
		# runtime libraries are gcc_syslibs': links to them only.
		nativeTree gcc-native
		# the checkout left gmp's, mpfr's, mpc's and isl's generated
		# files a few nanoseconds older than their sources, which then
		# want automake: newer again, contents unchanged
		for f in gmp mpfr mpc isl; do
			find "$HAIKU_PROJECT/buildtools/gcc/$f" -name aclocal.m4 \
				-exec touch {} +
			find "$HAIKU_PROJECT/buildtools/gcc/$f" \( -name configure \
				-o -name config.h.in -o -name Makefile.in \) -exec touch {} +
		done
		(cd "$WORK/$dir" && nativeEnv \
				CFLAGS_FOR_TARGET="-O2 $HAIKU_ERRATA_FLAGS" \
				CXXFLAGS_FOR_TARGET="-O2 $HAIKU_ERRATA_FLAGS" \
				LDFLAGS_FOR_TARGET="$HAIKU_ERRATA_FLAGS" \
				"$HAIKU_PROJECT/buildtools/gcc/configure" \
				--build=x86_64-pc-linux-gnu --host=aarch64-unknown-haiku \
				--target=aarch64-unknown-haiku \
				--prefix=/boot/system/develop/tools \
				--libexecdir=/boot/system/develop/tools/lib \
				--mandir=/boot/system/documentation/man \
				--with-build-sysroot="$SYSROOT" \
				--enable-threads=posix --disable-nls --enable-shared \
				--with-gnu-ld --with-gnu-as \
				--enable-version-specific-runtime-libs \
				--enable-languages=c,c++ --enable-lto --enable-frame-pointer \
				--with-pkgversion=haiku-odroid-c2 --enable-__cxa-atexit \
				--enable-checking=release \
				--with-default-libstdcxx-abi=gcc4-compatible \
				--disable-multilib --disable-libsanitizer \
				--disable-libquadmath \
				--enable-fix-cortex-a53-835769 --enable-fix-cortex-a53-843419 \
			&& nativeEnv make -j14 MAKEINFO=true LIMITS_H_TEST=true \
			&& nativeEnv make MAKEINFO=true LIMITS_H_TEST=true \
				DESTDIR="$stage" install) >> "$log" 2>&1
		# (t-haiku looks for the boards' limits.h on this laptop: without
		# LIMITS_H_TEST, gcc's limits.h wouldn't include Haiku's)
		tools=$system/develop/tools
		rm -rf "$tools/share" "$tools/info"
		find "$tools/lib" -name "*.la" -delete
		# gcc_syslibs has the shared runtime libraries (links to them
		# here; gcc's own liblto_plugin.so stays); static libstdc++ isn't
		# offered on Haiku (as HaikuPorts')
		find "$tools/lib" -name "libstdc++.a" -delete
		while IFS= read -r -d '' f; do
			rm "$f"
			ln -s "/boot/system/lib/$(basename "$f")" "$f"
		done < <(find "$tools/lib" \( -type f -o -type l \) \( \
			-name "libstdc++.so*" -o -name "libgcc_s.so*" \
			-o -name "libgomp.so*" -o -name "libatomic.so*" \
			-o -name "libssp.so*" \) -print0)
		mkdir -p "$system/bin"
		ln -sf gcc "$tools/bin/cc"
		for f in c++ cc cpp g++ gcc gcc-ar gcc-nm gcc-ranlib gcov gcov-dump \
				gcov-tool lto-dump; do
			[ -e "$tools/bin/$f" ] && ln -s "../develop/tools/bin/$f" "$system/bin/$f"
		done
		NO_SYSROOT=1 KEEP_DEVELOP=1 LICENSE="GNU GPL v3" \
			EXTRA_REQUIRES="binutils haiku_devel gcc_syslibs" finish gcc 13.3.0 \
			"The GNU C and C++ compiler" \
			"GCC 13.3 for building on the board: gcc, g++, cpp and the LTO tools, the runtime libraries' headers and static parts (the shared ones are gcc_syslibs'), with the Cortex-A53 errata workarounds on by default."
		;;
	make)
		# GNU make with HaikuPorts' patches (Haiku's ar format, dirent)
		unpack make-4.4.1.tar.gz make-4.4.1 haikuports/make-4.4.1.patchset
		(cd "$WORK/$dir" && ./configure \
				--host=aarch64-unknown-haiku --build=x86_64-pc-linux-gnu \
				$(haikuDirs make) --disable-nls --disable-rpath \
				--disable-dependency-tracking --without-guile CFLAGS="-O2" \
			&& make -j14 && make install DESTDIR="$stage") >> "$log" 2>&1
		LICENSE="GNU GPL v3" finish make 4.4.1 "GNU make" \
			"GNU make 4.4.1: builds programs from Makefiles."
		;;
	xxhash)
		# the library (rsync's xxh128 and xxh3 checksums) and xxhsum, with
		# HaikuPorts' patches (its tests only)
		unpack xxHash-0.8.3.tar.gz xxHash-0.8.3 haikuports/xxhash-0.8.3.patchset
		xxdirs="PREFIX=/boot/system LIBDIR=/boot/system/lib
			INCLUDEDIR=/boot/system/develop/headers BINDIR=/boot/system/bin
			MANDIR=/boot/system/documentation/man/man1"
		(cd "$WORK/$dir" && make -j14 CC="$CC" AR="$AR" $xxdirs lib xxhsum \
			&& make CC="$CC" AR="$AR" $xxdirs DESTDIR="$stage" install) \
			>> "$log" 2>&1
		develLibs
		LICENSE="BSD (2-clause)" LICENSE_FILE=LICENSE ALSO_LICENSE="GNU GPL v2" \
			finish xxhash 0.8.3 "An extremely fast non-cryptographic hash" \
			"xxHash: the libxxhash library (XXH32, XXH64, XXH3, XXH128; BSD 2-clause) and the xxhsum command (GPL v2)."
		;;
	lz4)
		# the library and the lz4 command
		unpack lz4-1.10.0.tar.gz lz4-1.10.0
		# (lowercase: its programs' LIBDIR is the library's source)
		lzdirs="prefix=/boot/system libdir=/boot/system/lib
			includedir=/boot/system/develop/headers bindir=/boot/system/bin
			mandir=/boot/system/documentation/man"
		(cd "$WORK/$dir" && make -j14 CC="$CC" AR="$AR" $lzdirs lib lz4 \
			&& make CC="$CC" AR="$AR" $lzdirs DESTDIR="$stage" install) \
			>> "$log" 2>&1
		develLibs
		LICENSE="BSD (2-clause)" LICENSE_FILE=lib/LICENSE ALSO_LICENSE="GNU GPL v2" \
			finish lz4 1.10.0 "Extremely fast compression" \
			"LZ4: the liblz4 library (BSD 2-clause) and the lz4 command (GPL v2)."
		;;
	rsync)
		# rsync with HaikuPorts' Haiku patch carried forward
		# (patches/rsync: libnetwork, Linux-style xattrs for Haiku's
		# attributes, no device numbers), so configure.sh regenerated as
		# its prepare-source does; zstd,
		# lz4, xxHash and OpenSSL from the ports; what configure can't run
		# here as the test board's gcc found it (socketpair, C99 vsnprintf, secure
		# mkstemp...). The tarball's signature checked with its release
		# key (milestone M86 (docs/milestones.md)).
		unpack rsync-3.5.2.tar.gz rsync-3.5.2 \
			../../patches/rsync/rsync-3.5.2-haiku.patch
		(cd "$WORK/$dir" && autoconf -o configure.sh \
			&& autoheader && touch config.h.in \
			&& ./configure --host=aarch64-unknown-haiku \
				--build=x86_64-pc-linux-gnu $(haikuDirs rsync) \
				--enable-xattr-support --disable-iconv --disable-iconv-open \
				--disable-locale --disable-debug --with-included-zlib=no \
				--disable-md2man --disable-idn CFLAGS="-O2" \
				rsync_cv_HAVE_BROKEN_LARGEFILE=no ac_cv_func_utime_null=yes \
				rsync_cv_HAVE_SOCKETPAIR=yes rsync_cv_HAVE_BROKEN_READDIR=no \
				rsync_cv_HAVE_C99_VSNPRINTF=yes \
				rsync_cv_HAVE_SECURE_MKSTEMP=yes \
			&& make -j14 && make install DESTDIR="$stage") >> "$log" 2>&1
		LICENSE="GNU GPL v3" finish rsync 3.5.2 "Fast incremental file transfer" \
			"rsync 3.5.2: copies and syncs files locally or over ssh, sending only the differences, with Haiku's attributes as extended attributes (-X), zstd/lz4/zlib compression and xxHash checksums."
		;;
	pyqt6_sip_python315)
		unpack pyqt6_sip-13.13.0.tar.gz pyqt6_sip-13.13.0
		pyCross
		pyWheel "$WORK/$dir"
		pyInstallWheel "$WORK"/wheels/pyqt6_sip-13.13.0-*.whl
		LICENSE="BSD (2-clause)" EXTRA_REQUIRES=python3.15 finish \
			pyqt6_sip_python315 13.13.0 "The sip module of PyQt6" \
			"PyQt6.sip, the support module PyQt6's bindings run on."
		;;
	pyqt6_python315)
		# PyQt6 by sip-build and qmake: the host Qt's qmake with a qt.conf
		# of our own for the sysroot's Qt (Qt's target_qt.conf gives the
		# sysroot twice; the libraries to link are develop/lib's, as
		# Haiku has them), the cross compilers as qmake settings, Python's
		# headers from the sysroot, and for the configuration tests
		# (which a cross build can't run) the answers the test board gave
		# (patches/python/pyqt6-6.12.0-cfgtests; with
		# PYQT_CFGTEST_RUNNER=tools/pyqt-cfgtest-on-board.sh, asked anew)
		pyqtTools
		unpack pyqt6-6.12.0.tar.gz pyqt6-6.12.0
		cat > "$WORK/pyqt-qt.conf" <<CONF
[Paths]
Prefix=/boot/system
Headers=develop/headers/Qt6
Libraries=develop/lib
LibraryExecutables=lib/Qt6
Binaries=lib/Qt6
Plugins=add-ons/Qt6
QmlImports=data/Qt6/qml
ArchData=data/Qt6
Data=data/Qt6
Translations=data/Qt6/translations
Settings=settings/xdg
HostPrefix=$HOSTQT
HostBinaries=bin
HostLibraries=lib
HostLibraryExecutables=libexec
HostData=$SYSROOT/boot/system/data/Qt6
Sysroot=$SYSROOT
SysrootifyPrefix=true
TargetSpec=haiku-g++
HostSpec=linux-g++
CONF
		printf '#!/bin/sh\nexec "%s/bin/qmake6" -qtconf "%s" "$@"\n' \
			"$HOSTQT" "$WORK/pyqt-qt.conf" > "$WORK/pyqt-qmake"
		cat > "$WORK/pyqt-cfgtest" <<'RUN'
#!/bin/sh
exec cp "$HAIKU_PROJECT/patches/python/pyqt6-6.12.0-cfgtests/$(basename "$1").out" "$2"
RUN
		chmod +x "$WORK/pyqt-qmake" "$WORK/pyqt-cfgtest"
		cat >> "$WORK/$dir/pyproject.toml" <<TOML

[tool.sip.project]
py-include-dir = "$SYSROOT/boot/system/develop/headers/python3.15"
TOML
		pyCross
		cc="--sysroot=$SYSROOT $HAIKU_ERRATA_FLAGS"
		(cd "$WORK/$dir" \
			&& PYQTBUILD_TEST_RUNNER=${PYQT_CFGTEST_RUNNER:-$WORK/pyqt-cfgtest} \
				"$PYVENV/bin/sip-build" --verbose --confirm-license \
				--no-designer-plugin --no-qml-plugin --no-dbus-python \
				--no-make --build-dir build --qmake "$WORK/pyqt-qmake" \
				--target-dir /boot/system/lib/python3.15/vendor-packages \
				--scripts-dir /boot/system/bin --pep484-pyi \
				--qmake-setting "QMAKE_CC = $GCC $cc" \
				--qmake-setting "QMAKE_CXX = $GXX $cc" \
				--qmake-setting "QMAKE_LINK = $GXX $cc" \
				--qmake-setting "QMAKE_LINK_SHLIB = $GXX $cc" \
				--qmake-setting "QMAKE_LINK_C = $GCC $cc" \
				--qmake-setting "QMAKE_LINK_C_SHLIB = $GCC $cc" \
				--qmake-setting "QMAKE_AR = $AR cqs" \
				--qmake-setting "QMAKE_STRIP = $STRIP" \
				--qmake-setting "QMAKE_OBJCOPY = $HAIKU_TOOLS/aarch64-unknown-haiku-objcopy" \
				--qmake-setting "QMAKE_NM = $HAIKU_TOOLS/aarch64-unknown-haiku-nm -P" \
			&& make -C build -j14 \
			&& make -C build install INSTALL_ROOT="$stage") >> "$log" 2>&1
		rm -rf "$WORK/$dir/build"
		# sip names the modules after this laptop's Python
		# (.abi3-x86_64-linux-gnu.so; Haiku's imports .abi3.so), and the
		# scripts start the venv's Python
		vendor=$system/lib/python3.15/vendor-packages
		for f in "$vendor"/PyQt6/*.abi3-*.so; do
			mv "$f" "${f%%.abi3-*}.abi3.so"
		done
		sed -i 's/\.abi3-[A-Za-z0-9_]*-[a-z]*-[a-z]*\.so/.abi3.so/' \
			"$vendor"/pyqt6-6.12.0.dist-info/RECORD
		sed -i "s|$PYVENV/bin/python|/bin/python3.15|" \
			"$system/bin/pyuic6" "$system/bin/pylupdate6"
		"$PYHOST" -m compileall -q -j0 -s "$stage" -p / "$vendor" >> "$log" 2>&1
		LICENSE="GNU GPL v3" \
			EXTRA_REQUIRES="python3.15 pyqt6_sip_python315 qt6_haikuplugins" \
			finish pyqt6_python315 6.12.0 "Qt 6 for Python (PyQt6)" \
			"PyQt6 6.12: Riverbank's Python bindings for the Qt 6.12 modules built for the C2, with pyuic6 and pylupdate6."
		;;
	openal)
		# OpenAL Soft with HaikuPorts' Haiku backend (BSoundPlayer), for
		# QtMultimedia's audio output
		unpack openal-soft-1.21.1.tar.gz openal-soft-1.21.1 \
			haikuports/openal-1.21.1.patchset
		(cd "$WORK/$dir" && cmake -G Ninja -S . -B build \
				-DCMAKE_TOOLCHAIN_FILE="$WORK/haiku-arm64.cmake" \
				-DCMAKE_BUILD_TYPE=Release $CMAKE_DIRS \
				-DALSOFT_UTILS=OFF -DALSOFT_EXAMPLES=OFF -DALSOFT_TESTS=OFF \
				-DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
			&& cmake --build build --parallel 10 \
			&& DESTDIR="$stage" cmake --install build) >> "$log" 2>&1
		develLibs
		LICENSE="GNU LGPL v2" LICENSE_FILE=COPYING finish openal 1.21.1 \
			"A 3D audio library (OpenAL Soft)" \
			"OpenAL Soft with the Haiku backend (BSoundPlayer): audio output for QtMultimedia and games."
		;;
	qt6_haikuplugins)
		# The Haiku platform plugin (Qt can't show a window on Haiku
		# without it), the Haiku style and the HVIF image format, from
		# qt6-haikuplugins with HaikuPorts' build fixes, built with
		# patches/qt6/haikuplugins/CMakeLists.txt against qt6_base
		unpack qt6-haikuplugins-1d255f2f8e58d66c1d3f2fc12cf651e4ca0aa80d.tar.gz \
			qt6-haikuplugins-1d255f2f8e58d66c1d3f2fc12cf651e4ca0aa80d \
			haikuports/qt6_base-6.10.3-source2.patchset \
			../../patches/qt6/haikuplugins-theme-font.patch
		cp "$HAIKU_PROJECT/patches/qt6/haikuplugins/CMakeLists.txt" "$WORK/$dir/"
		(cd "$WORK/$dir" && cmake -G Ninja -S . -B build \
				-DCMAKE_TOOLCHAIN_FILE="$WORK/haiku-arm64.cmake" \
				-DQT_HOST_PATH="$HOSTQT" -DCMAKE_BUILD_TYPE=Release \
				-DCMAKE_PREFIX_PATH=/boot/system \
				-DCMAKE_INSTALL_PREFIX=/boot/system \
				-DCMAKE_CXX_FLAGS="-fPIC -D_DEFAULT_SOURCE -D_GNU_SOURCE $HAIKU_ERRATA_FLAGS" \
			&& cmake --build build --parallel 10 \
			&& DESTDIR="$stage" cmake --install build) >> "$log" 2>&1
		LICENSE="GNU GPL v3" LICENSE_FILE=LICENSE \
			EXTRA_REQUIRES="qt6_base" finish qt6_haikuplugins 1.0.20251228 \
			"Qt 6's Haiku platform plugin, style and HVIF image format" \
			"qt6-haikuplugins by threedeyes (commit 1d255f2f): Qt windows, menus, clipboard, fonts and dialogs on Haiku, the Haiku widget style, and reading HVIF icons."
		;;
	pyside6_python315)
		# shiboken6 and PySide6 by their setup.py's cross build: the host
		# generator against the host Qt and clang 21 (with patchelf from
		# the venv), then the modules for every Qt module in the sysroot;
		# Haiku as a target of shiboken's parser (patches/python)
		unpack pyside-setup-everywhere-src-6.12.0.tar.xz \
			pyside-setup-everywhere-src-6.12.0 \
			../../patches/python/pyside6-6.12.0-haiku.patch
		cat > "$WORK/pyside-haiku.cmake" <<TC
cmake_minimum_required(VERSION 3.22)
include_guard(GLOBAL)
set(CMAKE_SYSTEM_NAME Haiku)
set(CMAKE_SYSTEM_PROCESSOR aarch64)
set(CMAKE_SYSROOT $SYSROOT)
set(CMAKE_C_COMPILER $GCC)
set(CMAKE_CXX_COMPILER $GXX)
set(CMAKE_C_FLAGS_INIT "-fPIC $HAIKU_ERRATA_FLAGS")
set(CMAKE_CXX_FLAGS_INIT "-fPIC $HAIKU_ERRATA_FLAGS")
set(CMAKE_FIND_ROOT_PATH $SYSROOT)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
set(Python_INCLUDE_DIR $SYSROOT/boot/system/develop/headers/python3.15 CACHE PATH "")
set(Python_LIBRARY $SYSROOT/boot/system/develop/lib/libpython3.15.so CACHE FILEPATH "")
TC
		(cd "$WORK/$dir" && env -u CC -u CXX -u AR -u RANLIB \
				PATH="$PYVENV/bin:$PATH" LLVM_INSTALL_DIR=/usr/lib/llvm-21 \
				"$PYVENV/bin/python" setup.py bdist_wheel --parallel=10 \
				--ignore-git --limited-api=yes \
				--cmake-toolchain-file="$WORK/pyside-haiku.cmake" \
				--qt-host-path="$HOSTQT" \
				--qt-target-path="$SYSROOT/boot/system" \
				--python-target-path="$SYSROOT/boot/system" \
				--plat-name=haiku_aarch64 --no-qt-tools --skip-docs) \
			>> "$log" 2>&1
		pyInstallWheel "$WORK/$dir"/dist/shiboken6-6.12.0-*.whl
		pyInstallWheel "$WORK/$dir"/dist/pyside6-6.12.0-*.whl
		# the wheel carries copies of the sysroot's FFmpeg libraries (6 and
		# 8) for QtMultimedia; the ffmpeg packages provide them
		rm -rf "$system/lib/python3.15/vendor-packages/PySide6/Qt/lib"
		# setup.py puts the Qt library directory, the sysroot's on this
		# laptop, into the modules' RPATH; only the $ORIGIN ones stay
		find "$system/lib/python3.15/vendor-packages" -name "*.so*" -type f |
			while read -r f; do
				r=$("$PYVENV/bin/patchelf" --print-rpath "$f" 2>/dev/null) ||
					continue
				case "$r" in
					*"$SYSROOT"*)
						"$PYVENV/bin/patchelf" --set-rpath "$(echo "$r" |
							tr : '\n' | grep -v "^$SYSROOT" | paste -sd:)" "$f"
						;;
				esac
			done
		rm -rf "$WORK/$dir/build"
		LICENSE="GNU LGPL v3" LICENSE_FILE=LICENSES/LGPL-3.0-only.txt \
			EXTRA_REQUIRES="python3.15 qt6_haikuplugins" finish \
			pyside6_python315 6.12.0 "Qt 6 for Python (PySide6)" \
			"PySide6 6.12 and shiboken6: Python bindings for the Qt 6.12 modules built for the C2 (Core, Gui, Widgets, Network, Sql, Svg, Qml, Quick, Multimedia and more)."
		;;
	mutagen_python315)
		unpack mutagen-1.48.1.tar.gz mutagen-1.48.1
		pyWheel "$WORK/$dir"
		pyInstallWheel "$WORK"/wheels/mutagen-1.48.1-*.whl
		LICENSE="GNU GPL v2" EXTRA_REQUIRES=python3.15 finish \
			mutagen_python315 1.48.1 "Audio metadata (tags) for Python" \
			"Mutagen reads and writes the tags of MP3, FLAC, Ogg, MP4/M4A and other audio files, with its mid3v2, mid3cp and mutagen-inspect tools."
		;;
	ifaddr_python315)
		unpack ifaddr-0.2.0.tar.gz ifaddr-0.2.0 \
			../../patches/python/ifaddr-0.2.0-haiku.patch
		pyWheel "$WORK/$dir"
		pyInstallWheel "$WORK"/wheels/ifaddr-0.2.0-*.whl
		LICENSE="MIT" LICENSE_FILE=LICENSE.txt EXTRA_REQUIRES=python3.15 \
			finish ifaddr_python315 0.2.0 \
			"Network interfaces and their addresses, for Python" \
			"ifaddr lists the network interfaces and their IP addresses (zeroconf uses it)."
		;;
	zeroconf_python315)
		# pure Python (its optional Cython speedups are skipped)
		unpack zeroconf-0.154.0.tar.gz zeroconf-0.154.0
		SKIP_CYTHON=1 pyWheel "$WORK/$dir"
		pyInstallWheel "$WORK"/wheels/zeroconf-0.154.0-*.whl
		LICENSE="GNU LGPL v2.1" \
			EXTRA_REQUIRES="python3.15 ifaddr_python315" finish \
			zeroconf_python315 0.154.0 "Multicast DNS service discovery for Python" \
			"python-zeroconf: mDNS/DNS-SD (Bonjour) service discovery and registration, pure Python."
		;;
	libogg)
		unpack libogg-1.3.6.tar.gz libogg-1.3.6 libogg-1.3.6.patchset
		(cd "$WORK/$dir" && autoreconf -fi && ./configure \
				--host=aarch64-unknown-haiku --build=x86_64-pc-linux-gnu \
				$(haikuDirs libogg) --disable-static CFLAGS="-O2" \
			&& make -j14 && make install DESTDIR="$stage") >> "$log" 2>&1
		develLibs
		LICENSE="BSD (3-clause)" finish libogg 1.3.6 \
			"The Ogg container format library" "Xiph.org's libogg."
		;;
	libvorbis)
		unpack libvorbis-1.3.7.tar.gz libvorbis-1.3.7 libvorbis-1.3.7.patchset
		(cd "$WORK/$dir" && autoreconf -fi && ./configure \
				--host=aarch64-unknown-haiku --build=x86_64-pc-linux-gnu \
				$(haikuDirs libvorbis) --disable-static CFLAGS="-O2" \
			&& make -j14 && make install DESTDIR="$stage") >> "$log" 2>&1
		develLibs
		LICENSE="BSD (3-clause)" finish libvorbis 1.3.7 \
			"The Ogg Vorbis audio codec library" \
			"Xiph.org's libvorbis, libvorbisenc and libvorbisfile."
		;;
	fluidlite)
		unpack FluidLite-e64b4b3161cab212fffe7d1d3fb1c05750c363cc.tar.gz \
			FluidLite-e64b4b3161cab212fffe7d1d3fb1c05750c363cc
		# static, as Haiku's libmidi links it (libfluidlite-static.a); SF3
		# sound fonts through libvorbis
		(cmake -S "$WORK/$dir" -B "$WORK/$dir/build" \
				-DCMAKE_TOOLCHAIN_FILE="$WORK/haiku-arm64.cmake" \
				-DCMAKE_BUILD_TYPE=Release $CMAKE_DIRS \
				-DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
				-DCMAKE_POSITION_INDEPENDENT_CODE=ON \
				-DFLUIDLITE_BUILD_SHARED=OFF -DFLUIDLITE_BUILD_STATIC=ON \
				-DENABLE_SF3=YES -DSTB_VORBIS=NO \
			&& cmake --build "$WORK/$dir/build" -j14 \
			&& DESTDIR="$stage" cmake --install "$WORK/$dir/build") \
			>> "$log" 2>&1
		mv "$system/lib/libfluidlite.a" "$system/lib/libfluidlite-static.a"
		develOnly fluidlite
		;;
	jasper)
		unpack jasper-version-2.0.33.tar.gz jasper-version-2.0.33
		(cmake -S "$WORK/$dir" -B "$WORK/$dir/build2" \
				-DCMAKE_TOOLCHAIN_FILE="$WORK/haiku-arm64.cmake" \
				-DCMAKE_BUILD_TYPE=Release $CMAKE_DIRS \
				-DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
				-DJAS_ENABLE_AUTOMATIC_DEPENDENCIES=OFF -DCMAKE_SKIP_RPATH=ON \
				-DJAS_ENABLE_OPENGL=OFF -DJAS_ENABLE_LIBJPEG=OFF \
				-DJAS_ENABLE_DOC=OFF \
			&& cmake --build "$WORK/$dir/build2" -j14 \
			&& DESTDIR="$stage" cmake --install "$WORK/$dir/build2") \
			>> "$log" 2>&1
		rm -rf "$system/documentation"
		develLibs
		LICENSE="JasPer2.0" LICENSE_FILE=LICENSE finish jasper 2.0.33 \
			"The JPEG 2000 image codec library" \
			"JasPer: libjasper and the jasper, imgcmp and imginfo tools."
		;;
	libicns)
		unpack libicns-0.8.1.tar.gz libicns-0.8.1 libicns-0.8.1.patchset \
			support-libopenjp2.patch
		# JPEG 2000 icons through jasper; libpng from tools/deps-build.sh,
		# linked statically
		(cd "$WORK/$dir" && autoreconf -fi && ./configure \
				--host=aarch64-unknown-haiku --build=x86_64-pc-linux-gnu \
				$(haikuDirs libicns) --disable-static CFLAGS="-O2" \
				CPPFLAGS="-I$HAIKU_PREFIX/include" \
				LDFLAGS="-L$HAIKU_PREFIX/lib" LIBS="-lz" \
			&& make -j14 && make install DESTDIR="$stage") >> "$log" 2>&1
		develLibs
		LICENSE="GNU LGPL v2" finish libicns 0.8.1 \
			"Library for Mac OS icon (ICNS) files" \
			"libicns and its icns2png, png2icns and icontainer2icns tools."
		;;
	libraw)
		# 0.20, whose API Haiku's RAW translator uses (0.21 moved shot_select)
		unpack LibRaw-0.20.2.tar.gz LibRaw-0.20.2 libraw-0.20.2.patchset
		(cd "$WORK/$dir" && autoreconf -fi && ./configure \
				--host=aarch64-unknown-haiku --build=x86_64-pc-linux-gnu \
				$(haikuDirs libraw) --disable-static --disable-openmp \
				--disable-jpeg --disable-jasper --disable-lcms \
				--disable-examples CFLAGS="-O2" CXXFLAGS="-O2" \
			&& fixLibtool && make -j14 && make install DESTDIR="$stage") \
			>> "$log" 2>&1
		rm -rf "$system/documentation" "$system/data"
		develLibs
		LICENSE="GNU LGPL v2.1" finish libraw 0.20.2 \
			"Library for camera RAW images" \
			"LibRaw, decoding the RAW files of digital cameras."
		;;
	libpcre)
		# the recipe's options, JIT included (sljit has arm64)
		unpack pcre-8.45.tar.bz2 pcre-8.45 libpcre-8.45.patchset
		(cd "$WORK/$dir" && autoreconf -fi && ./configure \
				--host=aarch64-unknown-haiku --build=x86_64-pc-linux-gnu \
				$(haikuDirs libpcre) --enable-pcre8 --enable-pcre16 \
				--enable-pcre32 --enable-utf --enable-unicode-properties \
				--with-pic --enable-jit=auto --enable-pcregrep-jit \
				--enable-shared --disable-static CFLAGS="-O2" CXXFLAGS="-O2" \
			&& fixLibtool && make -j14 && make install DESTDIR="$stage") \
			>> "$log" 2>&1
		develLibs
		# pcre-config belongs with the headers (the recipe's devel package)
		mkdir -p "$system/develop/bin"
		mv "$system/bin/pcre-config" "$system/develop/bin/"
		LICENSE="PCRE" LICENSE_FILE=LICENCE finish libpcre 8.45 \
			"Perl 5 compatible regular expressions" \
			"PCRE: libpcre (8, 16 and 32-bit), libpcrecpp, libpcreposix, pcregrep and pcretest."
		;;
	capstone)
		unpack capstone-5.0.1.tar.gz capstone-5.0.1 capstone5-5.0.1.patchset
		(cmake -S "$WORK/$dir" -B "$WORK/$dir/build" \
				-DCMAKE_TOOLCHAIN_FILE="$WORK/haiku-arm64.cmake" \
				-DCMAKE_BUILD_TYPE=Release $CMAKE_DIRS \
				-DCMAKE_POSITION_INDEPENDENT_CODE=ON \
				-DCAPSTONE_BUILD_SHARED=OFF -DCAPSTONE_BUILD_STATIC=ON \
				-DCAPSTONE_BUILD_TESTS=OFF -DCAPSTONE_BUILD_CSTOOL=OFF \
				-DCAPSTONE_BUILD_DIET=OFF -DCAPSTONE_ARCHITECTURE_DEFAULT=OFF \
				-DCAPSTONE_ARM64_SUPPORT=ON \
			&& cmake --build "$WORK/$dir/build" -j14 \
			&& DESTDIR="$stage" cmake --install "$WORK/$dir/build") \
			>> "$log" 2>&1
		develOnly capstone
		;;
	libpcap)
		unpack libpcap-1.11.0.tar.xz libpcap-1.11.0
		(cd "$WORK/$dir" && ./configure \
				--host=aarch64-unknown-haiku --build=x86_64-pc-linux-gnu \
				$(haikuDirs libpcap) --disable-static CFLAGS="-O2" \
			&& make -j14 && make install DESTDIR="$stage") >> "$log" 2>&1
		# pcap-config is for building against it (the recipe's devel package)
		mkdir -p "$system/develop/bin"
		mv "$system/bin/pcap-config" "$system/develop/bin/"
		rmdir "$system/bin" 2>/dev/null || true
		develLibs
		LICENSE="BSD (3-clause)" finish libpcap 1.11.0 \
			"A packet capture library" \
			"libpcap, the capture library of tcpdump and others, with its Haiku backend."
		;;
	tcpdump)
		unpack tcpdump-4.99.7.tar.xz tcpdump-4.99.7
		(cd "$WORK/$dir" && ./configure \
				--host=aarch64-unknown-haiku --build=x86_64-pc-linux-gnu \
				$(haikuDirs tcpdump) --without-crypto --without-smi \
				CFLAGS="-O2" \
			&& make -j14 && make install DESTDIR="$stage") >> "$log" 2>&1
		rm -f "$system/bin/tcpdump.4.99.7"
		LICENSE="BSD (3-clause)" finish tcpdump 4.99.7 \
			"A command line packet analyzer" \
			"tcpdump prints and saves the packets on a network interface."
		;;
	*)
		echo "ports-build: unknown port $port"
		exit 1
		;;
	esac
done

#!/bin/bash
# Copyright 2026, haiku-odroid-c2 port authors.
# Distributed under the terms of the MIT License.
#
# qt6-build.sh <stage>...: Qt 6.12.0 for the C2 (milestone M83), for PySide6.
#   host    Qt for this laptop in deps/host/qt6.12: the build tools a cross
#           build runs (moc, rcc, uic, qsb, qmlcachegen, ...), and the QtCore
#           PySide6's binding generator links; no X11, OpenGL or Vulkan.
#   target  the arm64 cross build with HaikuPorts' 6.10.3 patches carried
#           forward and the Haiku platform plugin (qt6-haikuplugins), into
#           deps/ports (tools/ports-build.sh packages it).
# Modules: qtbase, qtshadertools, qtsvg, qtdeclarative, qtmultimedia.
set -e
. "$(dirname "$0")/haiku-arm64-env.sh"
VER=6.12.0
SRCS=$HAIKU_PROJECT/deps/src
WORK=$HAIKU_PROJECT/deps/build/qt6
HOSTQT=$HAIKU_PROJECT/deps/host/qt6.12
JOBS=${JOBS:-8}
LOG=$HAIKU_PROJECT/logs
mkdir -p "$WORK"

hostModule()
{
	local m=$1 src
	shift
	src=$WORK/host/$m-everywhere-src-$VER
	rm -rf "$src"
	mkdir -p "$WORK/host"
	tar xf "$SRCS/$m-everywhere-src-$VER.tar.xz" -C "$WORK/host"
	(cd "$src" && env -u CC -u CXX -u AR -u RANLIB -u PKG_CONFIG_LIBDIR \
			-u PKG_CONFIG_SYSROOT_DIR cmake -G Ninja -S . -B build \
			-DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$HOSTQT" \
			-DCMAKE_PREFIX_PATH="$HOSTQT" -DQT_BUILD_EXAMPLES=OFF \
			-DQT_BUILD_TESTS=OFF "$@" \
		&& env -u CC -u CXX cmake --build build --parallel "$JOBS" \
		&& cmake --install build) >> "$LOG/qt6-host-$m.log" 2>&1
	echo "qt6-build: host $m installed"
}

for stage in "$@"; do
	case $stage in
	host)
		hostModule qtbase -DFEATURE_xcb=OFF -DINPUT_opengl=no \
			-DFEATURE_vulkan=OFF -DFEATURE_dbus=OFF -DFEATURE_glib=OFF \
			-DFEATURE_icu=OFF -DFEATURE_sql=OFF -DFEATURE_printsupport=OFF
		hostModule qtshadertools
		hostModule qtdeclarative
		;;
	*)
		echo "qt6-build: unknown stage $stage"
		exit 1
		;;
	esac
done

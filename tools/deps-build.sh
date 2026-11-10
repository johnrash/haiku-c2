#!/bin/bash
# Cross-compile the static libraries OpenSSH links (milestone M7) into
# deps/prefix, from the source archives in deps/src (downloaded once,
# checksums verified):
#   OpenSSL 3.5.9 (own haiku-aarch64 target, tools/deps/haiku-aarch64.conf),
#   libjpeg-turbo 3.2.1 (NEON), libpng 1.6.59.
# Each is unpacked fresh into deps/build. CC from haiku-arm64-env.sh carries
# the Cortex-A53 errata flags.
# Usage: deps-build.sh            -> deps/prefix/{include,lib}
set -e
. "$(dirname "$0")/haiku-arm64-env.sh"
SRC=$HAIKU_PROJECT/deps/src
BUILD=$HAIKU_PROJECT/deps/build
LOGS=$HAIKU_PROJECT/logs
mkdir -p "$BUILD" "$HAIKU_PREFIX"

# fresh <archive> <dir>: unpack a source archive into deps/build
fresh()
{
	rm -rf "$BUILD/$2"
	tar xf "$SRC/$1" -C "$BUILD"
}

echo "deps-build: OpenSSL"
fresh openssl-3.5.9.tar.gz openssl-3.5.9
sed "s|HAIKU_OS_DEP_H|$HAIKU_PROJECT/tools/deps/haiku-os-dep.h|" \
	"$HAIKU_PROJECT/tools/deps/haiku-aarch64.conf" > "$BUILD/haiku-aarch64.conf"
(cd "$BUILD/openssl-3.5.9" && ./Configure haiku-aarch64 \
	--config="$BUILD/haiku-aarch64.conf" no-shared no-tests no-apps no-docs \
	no-module --prefix="$HAIKU_PREFIX" --libdir=lib $HAIKU_CPPFLAGS \
	-D_DEFAULT_SOURCE -fPIC \
	&& make -j14 && make install_sw) > "$LOGS/deps-openssl.log" 2>&1

echo "deps-build: libjpeg-turbo"
fresh libjpeg-turbo-3.2.1.tar.gz libjpeg-turbo-3.2.1
cat > "$BUILD/haiku-arm64.cmake" <<EOF
set(CMAKE_SYSTEM_NAME Haiku)
set(CMAKE_SYSTEM_PROCESSOR aarch64)
set(CMAKE_C_COMPILER $HAIKU_TOOLS/aarch64-unknown-haiku-gcc)
set(CMAKE_SYSROOT $HAIKU_SYSROOT)
set(CMAKE_C_FLAGS_INIT "$HAIKU_ERRATA_FLAGS $HAIKU_CPPFLAGS -fPIC")
set(CMAKE_FIND_ROOT_PATH $HAIKU_SYSROOT $HAIKU_PREFIX)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
EOF
rm -rf "$BUILD/jpeg-build"
(cmake -S "$BUILD/libjpeg-turbo-3.2.1" -B "$BUILD/jpeg-build" \
	-DCMAKE_TOOLCHAIN_FILE="$BUILD/haiku-arm64.cmake" \
	-DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$HAIKU_PREFIX" \
	-DCMAKE_INSTALL_LIBDIR=lib -DENABLE_SHARED=OFF -DENABLE_STATIC=ON \
	-DWITH_TOOLS=OFF -DWITH_TESTS=OFF \
	&& cmake --build "$BUILD/jpeg-build" -j14 \
	&& cmake --install "$BUILD/jpeg-build") > "$LOGS/deps-jpeg.log" 2>&1

echo "deps-build: libpng"
fresh libpng-1.6.59.tar.xz libpng-1.6.59
(cd "$BUILD/libpng-1.6.59" && ./configure --host=aarch64-unknown-haiku \
	--prefix="$HAIKU_PREFIX" --enable-static --disable-shared --disable-tests \
	--disable-tools CFLAGS="-O2 -fPIC" && make -j14 && make install) \
	> "$LOGS/deps-png.log" 2>&1

ls -la "$HAIKU_PREFIX/lib"/*.a

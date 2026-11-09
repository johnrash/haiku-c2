#!/bin/bash
# Copyright 2026, haiku-odroid-c2 port authors.
# Distributed under the terms of the MIT License.
#
# Firefox 157.0.1 cross-built on this laptop for Haiku arm64 (the ODROID-C2),
# after haiku-pi's scripts/build-firefox.sh (https://github.com/haiku-pi/
# haiku-rpi at 5db456c, MIT): HaikuPorts' native Haiku port (www-client/
# firefox at c17ec5a8, toolkit cairo-haiku) and haiku-pi's arm64 patches, with
# ours (patches/firefox/157.0.1/). C and C++: the Haiku cross gcc 13.3; Rust:
# deps/firefox/rust-out (Rust 1.90.0 with aarch64-unknown-haiku, milestone M78 (docs/milestones.md)).
# Everything for the C2 carries the Cortex-A53 errata workarounds.
#
#   tools/firefox-build.sh STEP...
#
# Steps:
#   sysroot    deps/sysroot-full (tools/haiku-arm64-sysroot.sh) plus our FreeType
#              port's development files (deps/ports/freetype)
#   mesa       our Mesa's development files into the sysroot (BGLView)
#   expat      expat 2.9.0 into the sysroot (fontconfig's XML parser)
#   fontconfig fontconfig 2.18.2 into the sysroot (gfx/thebes' Haiku font
#              list is fontconfig-based; Firefox has no copy of its own)
#   cbindgen   cbindgen 0.29.4 for this laptop (Firefox needs >= 0.29.4)
#   config     deps/firefox/mozconfig
#   configure  ./mach configure
#   start      the build step as a memory-capped user service: use this one
#   build      ./mach build (objdir deps/firefox/obj)
#   stage      deps/firefox/stage/apps/Firefox: dist/Firefox stripped, its big
#              libraries in lib/ with fontconfig and expat, fontconfig's
#              configuration, HaikuPorts' launcher, the resources
#   check      the runtime_loader checks and the A53 errata scanners
#   package    deps/firefox/firefox-157.0.1-<revision>-arm64.hpkg
# The sources are prepared beforehand (milestone M78 (docs/milestones.md)): deps/firefox/src/
# firefox-157.0.1 with HaikuPorts' patchsets, patches/firefox/157.0.1/ and
# patches/rust-libc/.
set -e
root=$(cd "$(dirname "$0")/.." && pwd)
ff="$root/deps/firefox"
src="$ff/src/firefox-157.0.1"
sysroot="$root/deps/sysroot-full"
rustOut="$ff/rust-out"
target=aarch64-unknown-haiku
cc="$rustOut/bin/$target-gcc"
cxx="$rustOut/bin/$target-g++"
crossBin="$root/generated.arm64/cross-tools-arm64/bin"
jobs=12

# autotools ports into the sysroot, in Haiku's layout
configure_haiku() {
	CC="$cc" CXX="$cxx" AR="$crossBin/$target-ar" RANLIB="$crossBin/$target-ranlib" \
	STRIP="$crossBin/$target-strip" \
	PKG_CONFIG_SYSROOT_DIR="$sysroot" \
	PKG_CONFIG_LIBDIR="$sysroot/boot/system/develop/lib/pkgconfig" \
	./configure --host=$target --prefix=/boot/system \
		--includedir=/boot/system/develop/headers \
		--libdir=/boot/system/develop/lib "$@"
}

step_sysroot() {
	"$root/tools/haiku-arm64-sysroot.sh" "$sysroot" > /dev/null
	local ft="$root/deps/ports/freetype"
	cp -a "$ft/develop/headers/freetype2" "$sysroot/boot/system/develop/headers/"
	mkdir -p "$sysroot/boot/system/develop/lib/pkgconfig" "$sysroot/boot/system/lib"
	cp -a "$ft"/lib/libfreetype.so* "$sysroot/boot/system/lib/"
	ln -sf ../../lib/libfreetype.so.6 "$sysroot/boot/system/develop/lib/libfreetype.so"
	cp "$ft/develop/lib/pkgconfig/freetype2.pc" \
		"$sysroot/boot/system/develop/lib/pkgconfig/"
	echo "firefox-build: sysroot with FreeType $(sed -n 's/^Version: //p' \
		"$ft/develop/lib/pkgconfig/freetype2.pc")"
}

# our Mesa's development files: Firefox's Haiku GL context is a BGLView
step_mesa() {
	local m="$root/deps/mesa/pkg" d="$sysroot/boot/system"
	cp -a "$m"/devel/develop/headers/. "$d/develop/headers/"
	cp -a "$m"/mesa/lib/libGL.so* "$m"/mesa/lib/libglapi.so* "$d/lib/"
	ln -sf ../../lib/libGL.so.1 "$d/develop/lib/libGL.so"
	echo "firefox-build: Mesa's development files in the sysroot"
}

step_expat() {
	local w="$ff/build-expat"
	rm -rf "$w" && mkdir -p "$w"
	tar -C "$w" -xf "$ff/dl-expat/expat-2.9.0.tar.bz2"
	cd "$w/expat-2.9.0"
	configure_haiku --disable-static --with-getentropy --without-docbook \
		--without-examples --without-tests > "$w/configure.log"
	make -j$jobs > "$w/make.log"
	make install DESTDIR="$sysroot" > "$w/install.log"
	# libtool's archive names host paths, which mislead the next ports' links
	rm -f "$sysroot/boot/system/develop/lib/libexpat.la"
	echo "firefox-build: expat 2.9.0 in the sysroot"
}

step_fontconfig() {
	local w="$ff/build-fontconfig"
	rm -rf "$w" && mkdir -p "$w"
	tar -C "$w" -xf "$ff/dl-fontconfig/fontconfig-2.18.2.tar.xz"
	cd "$w/fontconfig-2.18.2"
	# the paths as haiku-pi's (fontconfig's configuration ships with Firefox,
	# which points FONTCONFIG_PATH at it); va_copy can't be run-tested
	configure_haiku --disable-static --disable-nls --disable-docs \
		--disable-cache-build \
		--sysconfdir=/boot/system/non-packaged/settings \
		--localstatedir=/boot/system/non-packaged/var \
		--with-default-fonts=/boot/system/data/fonts \
		--with-add-fonts=/boot/system/non-packaged/data/fonts,/boot/home/config/data/fonts,/boot/home/config/non-packaged/data/fonts \
		--with-cache-dir=/boot/home/config/cache/fontconfig \
		ac_cv_va_copy=C99 > "$w/configure.log"
	make -j$jobs > "$w/make.log"
	make install DESTDIR="$sysroot" > "$w/install.log"
	rm -f "$sysroot/boot/system/develop/lib/libfontconfig.la"
	echo "firefox-build: fontconfig 2.18.2 in the sysroot"
}

step_config() {
	local cxxInc="$sysroot/boot/system/develop/headers/c++"
	mkdir -p "$ff/mozbuild-state"
	cat > "$ff/mozconfig" <<MOZCONFIG
# written by tools/firefox-build.sh (after haiku-pi's scripts/build-firefox.sh)
mk_add_options MOZ_OBJDIR=$ff/obj
ac_add_options --enable-project=browser
ac_add_options --host=x86_64-pc-linux-gnu
ac_add_options --target=$target
ac_add_options --disable-bootstrap

# HaikuPorts' mozconfig_native (www-client/firefox/additional-files)
export MOZILLA_OFFICIAL=1
ac_add_options --enable-release
ac_add_options --enable-official-branding
ac_add_options --with-app-name=Firefox
ac_add_options --enable-default-toolkit=cairo-haiku
ac_add_options --enable-audio-backends=haiku
ac_add_options --allow-addon-sideload
ac_add_options --with-unsigned-addon-scopes=app,system
ac_add_options --disable-tests
ac_add_options --disable-updater
ac_add_options --disable-valgrind
ac_add_options --disable-crashreporter
ac_add_options --without-wasm-sandboxed-libraries
mk_add_options MOZ_CRASHREPORTER=0
ac_add_options --enable-skia-pdf
ac_add_options --disable-lto
ac_add_options --enable-jit
ac_add_options --disable-debug
ac_add_options --disable-debug-symbols
ac_add_options --disable-debug-js-modules
# as haiku-pi: no WebRTC, no sandbox (Haiku has none), -O2. The C2's system
# has no libffi, JPEG, WebP or libevent packages: Firefox's own copies (the
# libevent one with patches/firefox/157.0.1/30-haiku-libevent-config.patch).
ac_add_options --disable-webrtc
ac_add_options --disable-sandbox
ac_add_options --enable-optimize=-O2
ac_add_options --disable-strip
ac_add_options --disable-install-strip

ac_add_options --with-libclang-path=/usr/lib/llvm-21/lib
ac_add_options --with-clang-path=/usr/lib/llvm-21/bin/clang++
export BINDGEN_CFLAGS="--sysroot=$sysroot -isystem $cxxInc -isystem $cxxInc/$target"
# gcc doesn't predefine __unix__ for Haiku (clang does): Skia's SkFeatures.h
# would take Haiku for macOS
export CFLAGS="-D__unix__=1 -D__unix=1"
export CXXFLAGS="-D__unix__=1 -D__unix=1"
export LDFLAGS="-Wl,--hash-style=both"
# the A53 errata workarounds and the sysroot come with the wrappers
export CC="$cc"
export CXX="$cxx"
export AR="$crossBin/$target-ar"
export RANLIB="$crossBin/$target-ranlib"
export STRIP="$crossBin/$target-strip"
export OBJCOPY="$crossBin/$target-objcopy"
export HOST_CC=gcc
export HOST_CXX=g++
export RUSTC="$rustOut/build/x86_64-unknown-linux-gnu/stage1/bin/rustc"
export CARGO="$rustOut/build/x86_64-unknown-linux-gnu/stage1/bin/cargo"
export CBINDGEN="$ff/host/bin/cbindgen"
export LLVM_OBJDUMP=/usr/lib/llvm-21/bin/llvm-objdump
export PKG_CONFIG_SYSROOT_DIR="$sysroot"
export PKG_CONFIG_LIBDIR="$sysroot/boot/system/develop/lib/pkgconfig"
export RUST_BACKTRACE=full
MOZCONFIG
	echo "firefox-build: $ff/mozconfig"
}

# mach without network access (the Python packages vendored in the source),
# its state in deps/firefox
mach() {
	. "$rustOut/env.sh"
	cd "$src"
	MOZCONFIG="$ff/mozconfig" MOZBUILD_STATE_PATH="$ff/mozbuild-state" \
	MACH_BUILD_PYTHON_NATIVE_PACKAGE_SOURCE=none MACH_NO_TERMINAL_FOOTER=1 \
		./mach "$@"
}

step_configure() {
	mach configure
}

step_build() {
	mach build -j6
}

# The build as a user service of its own, which is what to run: at 10 jobs it
# ran the whole laptop out of memory and the kernel killed the desktop app
# (milestone M78 (docs/milestones.md)). Over the limit only the build dies, and it goes on if the
# app that started it goes away. To stop it: systemctl --user stop firefox-build
# Resumable: the objdir stays.
step_start() {
	local log="$root/logs/firefox-build-$(date +%Y%m%d-%H%M).log"
	systemd-run --user --unit=firefox-build --collect \
		-p MemoryHigh=17G -p MemoryMax=19G -p MemorySwapMax=0 \
		-p OOMScoreAdjust=900 --setenv=PATH="$PATH" \
		--working-directory="$root" \
		/bin/bash -c "tools/firefox-build.sh build >> '$log' 2>&1; \
			echo \"build exit \$?\" >> '$log'"
	echo "firefox-build: running as firefox-build.service, $log"
}

# an AArch64 ELF file (dist/ also has host tools)
is_arm64_elf() {
	[ "$(head -c 5 "$1" | od -An -tx1 | tr -d ' \n')" = 7f454c4602 ] &&
	[ "$(head -c 20 "$1" | od -An -tx1 -j 18 | tr -d ' \n')" = b700 ]
}

# what Haiku's arm64 runtime_loader can load (as haiku-pi checks): position
# independent, no COPY/TLSDESC/TPREL relocations, no thread-pointer-relative
# TLS (mrs tpidr_el0)
check_elf() {
	local f=$1 bad=0
	"$crossBin/$target-readelf" -h "$f" | grep -q 'Type: *DYN' \
		|| { echo "firefox-build: $f is not DYN"; bad=1; }
	if "$crossBin/$target-readelf" -rW "$f" \
			| grep -m3 -E 'R_AARCH64_(COPY|TLSDESC|TLS_TPREL)'; then
		echo "firefox-build: $f has relocations Haiku's runtime_loader lacks"
		bad=1
	fi
	if "$crossBin/$target-objdump" -d "$f" | grep -m3 'mrs.*tpidr_el0'; then
		echo "firefox-build: $f reads tpidr_el0"
		bad=1
	fi
	return $bad
}

# the libraries the HaikuPorts recipe keeps in the app's lib/ (the
# runtime_loader looks there, %A/lib)
libDirLibs="libgkcodecs.so liblgpllibs.so libmozavcodec.so libmozavutil.so
	libmozinference.so libmozsqlite3.so libxul.so"
version=157.0.1
revision=3
stage="$ff/stage"
app="$stage/apps/Firefox"

step_stage() {
	. "$rustOut/env.sh"
	local hostTools="$root/generated.arm64/objects/linux/x86_64/release/tools"
	local fc="$sysroot/boot/system" f l v1 v2 v3
	# dist/Firefox, with omni.ja: what ./mach package ships
	(cd "$ff/obj" && MOZCONFIG="$ff/mozconfig" \
		MOZBUILD_STATE_PATH="$ff/mozbuild-state" \
		make -C browser/installer stage-package > "$ff/stage-package.log")
	rm -rf "$stage"
	mkdir -p "$app/lib"
	cp -a "$ff/obj/dist/Firefox/." "$app/"
	for l in $libDirLibs; do
		[ -f "$app/$l" ] && mv "$app/$l" "$app/lib/"
	done
	# fontconfig and expat, which the C2's system lacks (its FreeType serves)
	cp -L "$fc/develop/lib/libfontconfig.so.1" "$fc/develop/lib/libexpat.so.1" \
		"$app/lib/"
	while IFS= read -r -d '' f; do
		is_arm64_elf "$f" && "$crossBin/$target-strip" --strip-unneeded "$f"
	done < <(find "$app" -type f -print0)
	# fontconfig's configuration in the app directory, where Firefox points
	# FONTCONFIG_PATH (patches/firefox/157.0.1/20-…); conf.d's links become
	# copies
	mkdir -p "$app/fontconfig/conf.d"
	cp "$fc/non-packaged/settings/fonts/fonts.conf" "$app/fontconfig/"
	for l in "$fc"/non-packaged/settings/fonts/conf.d/*; do
		if [ -L "$l" ]; then
			cp "$fc/share/fontconfig/conf.avail/$(basename "$l")" \
				"$app/fontconfig/conf.d/"
		else
			cp "$l" "$app/fontconfig/conf.d/"
		fi
	done
	# our own modified build reports nothing to Mozilla by default
	cat > "$app/defaults/pref/c2-defaults.js" <<'PREFS'
// haiku-odroid-c2: no data reporting from this private build
pref("datareporting.policy.dataSubmissionEnabled", false);
pref("datareporting.healthreport.uploadEnabled", false);
pref("toolkit.telemetry.enabled", false);
pref("toolkit.telemetry.unified", false);
pref("toolkit.telemetry.archive.enabled", false);
pref("app.shield.optoutstudies.enabled", false);
pref("app.normandy.enabled", false);
PREFS
	# HaikuPorts' launcher, and the resources: signature, version, icon
	"$cxx" -O2 -o "$app/Firefox Browser" \
		"$src/tools/haiku-launcher/launcher.cpp" -lbe
	"$crossBin/$target-strip" "$app/Firefox Browser"
	local add="$ff/haikuports/www-client/firefox/additional-files"
	IFS=. read -r v1 v2 v3 <<< "$version"
	sed -e "s|@APP_SIGNATURE@|application/x-vnd.Mozilla-Firefox|" \
		-e "s|@MAJOR@|$v1|" -e "s|@MIDDLE@|$v2|" -e "s|@MINOR@|$v3|" \
		-e "s|@LONG_INFO@|A fast, private and open-source web browser|" \
		"$add/firefox.rdef.in" > "$ff/firefox.rdef"
	sed -e "s|@MAJOR@|$v1|" -e "s|@MIDDLE@|$v2|" -e "s|@MINOR@|$v3|" \
		-e "s|@LONG_INFO@|A fast, private and open-source web browser|" \
		"$add/firefox_launcher.rdef.in" > "$ff/firefox_launcher.rdef"
	(cd "$add" && "$hostTools/rc/rc" -o "$ff/firefox.rsrc" "$ff/firefox.rdef" \
		&& "$hostTools/rc/rc" -o "$ff/firefox_launcher.rsrc" \
			"$ff/firefox_launcher.rdef")
	"$hostTools/xres" -o "$app/Firefox" "$ff/firefox.rsrc"
	"$hostTools/xres" -o "$app/Firefox Browser" "$ff/firefox_launcher.rsrc"
	du -sh "$app"
}

step_check() {
	local f bad=0 errata=0
	while IFS= read -r -d '' f; do
		is_arm64_elf "$f" || continue
		check_elf "$f" || bad=1
		python3 "$root/tools/a53-843419.py" "$f" > /dev/null \
			|| { echo "firefox-build: 843419 sequences in $f"; errata=1; }
		python3 "$root/tools/a53-835769.py" "$f" || errata=1
	done < <(find "$app" -type f -print0)
	"$crossBin/$target-readelf" -d "$app/lib/libxul.so" | grep NEEDED
	[ $bad = 0 ] && echo "firefox-build: the runtime_loader checks pass"
	[ $errata = 0 ] && echo "firefox-build: no A53 errata sequences"
	return $bad
}

step_package() {
	local pkg="$ff/hpkg"
	local hpkg="$ff/firefox-$version-$revision-arm64.hpkg"
	(
		# Haiku's attributes on this host, and haiku_mimeset
		. "$root/tools/haiku-arm64-env.sh" > /dev/null
		haiku_rm_tree "$pkg"
		rm -rf "$pkg"
		mkdir -p "$pkg/apps" "$pkg/bin" "$pkg/data/deskbar/menu/Applications"
		cp -a "$app" "$pkg/apps/"
		# the type, signature and icon as attributes, from the resources: Deskbar's
		# menu and Tracker read those (a running app shows its resources' icon)
		haiku_mimeset "$pkg/apps/Firefox/Firefox" \
			"$pkg/apps/Firefox/Firefox Browser"
		ln -s ../apps/Firefox/Firefox "$pkg/bin/firefox"
		ln -s "../../../../apps/Firefox/Firefox Browser" \
			"$pkg/data/deskbar/menu/Applications/Firefox"
		cat > "$pkg/.PackageInfo" <<INFO
name			firefox
version			$version-$revision
architecture	arm64
summary			"A fast, private and open-source web browser"
description		"Firefox $version for the ODROID-C2: HaikuPorts' native Haiku port (cairo-haiku) with haiku-pi's arm64 patches and the haiku-odroid-c2 port's, cross-built for the C2's Cortex-A53 (tools/firefox-build.sh). fontconfig and expat come with it, in apps/Firefox/lib."
packager		"haiku-odroid-c2 port"
vendor			"haiku-odroid-c2 port"
licenses		{ "MPL v2.0" }
copyrights		{ "1995-2026 Mozilla Developers and Contributors" }
provides {
	firefox = $version
	app:Firefox = $version
	cmd:firefox = $version
}
requires {
	haiku >= r1~beta6_hrev60299_270
	freetype >= 2.14
	lib:libgcc_s
	lib:libstdc++
}
INFO
		rm -f "$hpkg"
		cd "$pkg"
		"$HAIKU_HOST_TOOLS/package/package" create -q "$hpkg"
	)
	ls -l "$hpkg"
}

for step in "$@"; do
	case "$step" in
		sysroot|mesa|expat|fontconfig|config|configure|build|start|stage|check|package)
			"step_$step" ;;
		*) echo "firefox-build: no step $step" >&2; exit 2 ;;
	esac
done

#!/bin/bash
# Rebuild the binaries of arm64 bootstrap packages that have Cortex-A53
# erratum 835769 sequences (milestone M8), from their sources in deps/src
# (checksums of the haikuports.cross recipes; FreeType's GPG signature), with
# those recipes' patches and configure options and the errata flags. Built
# files that the bootstrap package has at the same path go to
# deps/bootstrap/rebuilt/<package>/, for bootstrap-fix.sh.
# Packages: freetype ncurses6 icu74 bash coreutils findutils gawk grep less
# Usage: bootstrap-rebuild.sh <package>...
set -e
. "$(dirname "$0")/haiku-arm64-env.sh"
SRCS=$HAIKU_PROJECT/deps/src
WORK=$HAIKU_PROJECT/deps/build/bootstrap
REBUILT=$HAIKU_PROJECT/deps/bootstrap/rebuilt
PKGS=$HAIKU_PROJECT/deps/bootstrap/pkg
mkdir -p "$WORK"

# A sysroot as haikuporter's cross builds have it: the haiku and haiku_devel
# packages of this build and the bootstrap gcc_syslibs, zlib and ncurses6 (with
# their _devel packages), so the cross gcc finds Haiku's headers as system
# headers, after a package's own (gnulib's replacement headers).
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
			$HAIKU_GEN/download/ncurses6_devel-*_bootstrap-*-arm64.hpkg; do
		"$HAIKU_HOST_TOOLS/package/package" extract -C "$SYSROOT/boot/system" "$p"
	done
fi
unset CPPFLAGS LDFLAGS
export CC="$HAIKU_TOOLS/aarch64-unknown-haiku-gcc --sysroot=$SYSROOT $HAIKU_ERRATA_FLAGS"
export CXX="$HAIKU_TOOLS/aarch64-unknown-haiku-g++ --sysroot=$SYSROOT $HAIKU_ERRATA_FLAGS"
export AR=$HAIKU_TOOLS/aarch64-unknown-haiku-ar
export RANLIB=$HAIKU_TOOLS/aarch64-unknown-haiku-ranlib
# for builds that link -lc (Haiku's C library is libroot)
mkdir -p "$WORK/libc"
ln -sf "$SYSROOT/boot/system/lib/libroot.so" "$WORK/libc/libc.so"

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

# unpack <archive> <dir> [patch...]
unpack()
{
	rm -rf "$WORK/$2" "$WORK/$2-stage"
	tar xf "$SRCS/$1" -C "$WORK"
	local p
	shift 2
	for p in "$@"; do
		(cd "$WORK/$dir" && patch -p1 < "$SRCS/$p")
	done
}

# newer config.sub/config.guess for tarballs that predate aarch64-haiku
refreshConfig()
{
	local f
	for f in $(find "$1" -name config.sub -o -name config.guess); do
		cp "/usr/share/misc/$(basename "$f")" "$f"
	done
}

# deploy <base> <stage>: built ELF files the bootstrap package has too
deploy()
{
	local base=$1 stage=$2/boot/system pkg count=0 f rel
	pkg=$(ls -d "$PKGS/$base"-*_bootstrap-* 2>/dev/null | head -1)
	# ICU is not in bootstrap-fix.sh's folder: the build's extracted copy
	[ -n "$pkg" ] || pkg=$(ls -d "$HAIKU_GEN/build_packages/$base"-*_bootstrap-*-arm64 | head -1)
	rm -rf "$REBUILT/$base"
	while IFS= read -r -d '' f; do
		rel=${f#$stage/}
		# libtool's names (libX.so.6.6.0) for a package's own (libX.so.6.6)
		[ ! -e "$pkg/$rel" ] && [ -f "$pkg/$rel.0" ] && rel=$rel.0
		[ -f "$pkg/$rel" ] && [ ! -L "$pkg/$rel" ] || continue
		head -c 4 "$f" | grep -q ELF || continue
		case "$rel" in
			lib/*.so*)
				# a library must export everything the original does
				local missing
				missing=$(LC_ALL=C comm -23 \
					<($HAIKU_TOOLS/aarch64-unknown-haiku-nm -D --defined-only "$pkg/$rel" | awk '{print $3}' | LC_ALL=C sort -u) \
					<($HAIKU_TOOLS/aarch64-unknown-haiku-nm -D --defined-only "$f" | awk '{print $3}' | LC_ALL=C sort -u) | wc -l)
				[ "$missing" = 0 ] || { echo "bootstrap-rebuild: $rel lacks $missing symbols"; exit 1; }
				;;
		esac
		mkdir -p "$REBUILT/$base/$(dirname "$rel")"
		cp "$f" "$REBUILT/$base/$rel"
		$HAIKU_TOOLS/aarch64-unknown-haiku-strip "$REBUILT/$base/$rel"
		count=$((count + 1))
	done < <(find "$stage" -type f -print0)
	echo "bootstrap-rebuild: $base: $count files"
}

for package in "$@"; do
	log=$HAIKU_PROJECT/logs/bootstrap-$package.log
	echo "bootstrap-rebuild: $package"
	case $package in
	freetype)
		dir=freetype-2.6.3
		unpack freetype-2.6.3.tar.bz2 $dir
		# its makefiles wrap $(CC) in libtool: CC only for configure
		(cd "$WORK/$dir" && env -u CC ./configure --host=aarch64-unknown-haiku \
			$(haikuDirs freetype) --disable-static --without-zlib \
			--without-bzip2 --without-png --without-harfbuzz \
			CC="$CC" CFLAGS="-O2" && env -u CC make -j14 \
			&& env -u CC make install DESTDIR="$WORK/$dir-stage") > "$log" 2>&1
		deploy freetype "$WORK/$dir-stage"
		;;
	ncurses6)
		dir=ncurses-snapshots-6_6
		unpack ncurses-snapshots-6_6.tar.gz $dir ncurses6-6.6.patchset
		(cd "$WORK/$dir" && ./configure --host=aarch64-unknown-haiku \
			$(haikuDirs ncurses6 | sed 's/--datarootdir=[^ ]*//; s/--docdir=[^ ]*//') \
			--with-shared --without-normal \
			--disable-stripping --disable-relink --disable-root-access \
			--enable-root-environ --disable-setuid-environ \
			--enable-hard-tabs --enable-pc-files --enable-symlinks \
			--enable-termcap --without-ada --without-tests --enable-widec \
			--with-pkg-config-libdir=/boot/system/develop/lib/pkgconfig \
			--with-build-cc=gcc --without-debug CFLAGS="-O2" \
			CXXFLAGS="-O2 -fPIC" LDFLAGS="-L$WORK/libc" \
			&& make -j14 \
			&& $CXX -shared -Wl,-soname,libncurses++w.so.6 \
				-o lib/libncurses++w.so.6.6 -Wl,--whole-archive \
				lib/libncurses++w.a -Wl,--no-whole-archive -Llib -lformw \
				-lmenuw -lpanelw -lncursesw \
			&& make install.libs install.includes DESTDIR="$WORK/$dir-stage" \
			&& cp lib/libncurses++w.so.6.6 "$WORK/$dir-stage/boot/system/lib/" \
			&& for p in progs/clear progs/infocmp progs/tabs progs/tic progs/toe \
				progs/tput progs/tset; do
				cp "$p" "$WORK/$dir-stage/boot/system/bin/" || exit 1; done) \
			> "$log" 2>&1
		# (its patchset installs the programs from libtool's .libs; without
		# libtool the C++ binding is only built static: linked shared here)
		deploy ncurses6 "$WORK/$dir-stage"
		;;
	bash)
		dir=bash-5.3
		unpack bash-5.3.tar.gz $dir bash-5.3.patchset
		(cd "$WORK/$dir" && ./configure --host=aarch64-unknown-haiku \
			--build=x86_64-pc-linux-gnu $(haikuDirs bash) \
			--without-bash-malloc CFLAGS="-O2" \
			&& touch lib/intl/plural.c && make -j14 \
			&& make install DESTDIR="$WORK/$dir-stage") > "$log" 2>&1
		deploy bash "$WORK/$dir-stage"
		;;
	less)
		dir=less-668
		unpack less-668.tar.gz $dir less_bootstrap-668.patch
		(cd "$WORK/$dir" && autoconf && ./configure --host=aarch64-unknown-haiku \
			$(haikuDirs less) --with-editor=nano CFLAGS="-O2" \
			&& make -j14 && make install DESTDIR="$WORK/$dir-stage") > "$log" 2>&1
		deploy less "$WORK/$dir-stage"
		;;
	coreutils)
		dir=coreutils-9.9
		unpack coreutils-9.9.tar.xz $dir coreutils_bootstrap-9.9.patchset
		(cd "$WORK/$dir" && PERL=missing FORCE_UNSAFE_CONFIGURE=1 ./configure \
			--host=aarch64-unknown-haiku --build=x86_64-pc-linux-gnu \
			$(haikuDirs coreutils) --without-included-regex --disable-rpath \
			--with-gnu-ld --enable-no-install-program=df,uptime \
			--enable-install-program=hostname --disable-nls CFLAGS="-w -O2" \
			&& touch doc/*.info && make -j14 \
			&& make install DESTDIR="$WORK/$dir-stage") > "$log" 2>&1
		deploy coreutils "$WORK/$dir-stage"
		;;
	findutils)
		dir=findutils-4.9.0
		unpack findutils-4.9.0.tar.xz $dir
		(cd "$WORK/$dir" && ./configure --host=aarch64-unknown-haiku \
			--build=x86_64-pc-linux-gnu $(haikuDirs findutils) \
			--without-included-regex --disable-nls --disable-rpath \
			--with-gnu-ld CFLAGS="-O2" && make -j14 \
			&& make install DESTDIR="$WORK/$dir-stage") > "$log" 2>&1
		deploy findutils "$WORK/$dir-stage"
		;;
	grep)
		dir=grep-3.11
		unpack grep-3.11.tar.xz $dir grep_bootstrap-3.11.patchset
		(cd "$WORK/$dir" && ./configure --host=aarch64-unknown-haiku \
			--build=x86_64-pc-linux-gnu $(haikuDirs grep) \
			--disable-perl-regexp --disable-nls CFLAGS="-O2" && make -j14 \
			&& make install DESTDIR="$WORK/$dir-stage") > "$log" 2>&1
		deploy grep "$WORK/$dir-stage"
		;;
	gawk)
		dir=gawk-3.1.8
		unpack gawk-3.1.8.tar.bz2 $dir
		refreshConfig "$WORK/$dir"
		(cd "$WORK/$dir" && ./configure --host=aarch64-unknown-haiku \
			--build=x86_64-pc-linux-gnu $(haikuDirs gawk) --disable-nls \
			--disable-dependency-tracking CFLAGS="-O2" && make \
			&& make install DESTDIR="$WORK/$dir-stage") > "$log" 2>&1
		deploy gawk "$WORK/$dir-stage"
		;;
	icu74)
		dir=icu
		unpack icu4c-74_1-src.tgz $dir icu74_bootstrap-74.1.patchset
		# the host build makes the tools the cross build needs
		(mkdir -p "$WORK/icu-host" && cd "$WORK/icu-host" \
			&& env -u CC -u CXX -u AR -u RANLIB ../icu/source/configure \
				--disable-tests --disable-samples --disable-extras \
			&& make -j14) > "$log" 2>&1
		(mkdir -p "$WORK/icu-target" && cd "$WORK/icu-target" \
			&& ../icu/source/configure --host=aarch64-unknown-haiku \
				$(haikuDirs icu74) --with-cross-build="$WORK/icu-host" \
				--disable-tools --disable-tests --disable-samples \
				--disable-extras --with-data-packaging=archive \
				CFLAGS="-O2" CXXFLAGS="-O2" \
			&& mkdir -p lib && make -j14 -C stubdata && make -j14 -C common \
			&& make -j14 -C i18n && make -j14 -C io \
			&& mkdir -p "$WORK/icu-stage/boot/system/lib" \
			&& cp -L lib/libicuuc.so.74.1 lib/libicui18n.so.74.1 lib/libicuio.so.74.1 \
				"$WORK/icu-stage/boot/system/lib/") >> "$log" 2>&1
		deploy icu74 "$WORK/icu-stage"
		;;
	*)
		echo "bootstrap-rebuild: unknown package $package"
		exit 1
		;;
	esac
done

#!/bin/sh
# Copyright 2026, haiku-odroid-c2 port authors.
# Distributed under the terms of the MIT License.
#
# Noto Sans CJK JP for the boards, as a package: deps/fonts/
# noto_sans_cjk_jp_static-<version>-any.hpkg. The app_server draws the
# glyphs a font lacks (Korean, Japanese, Chinese) with "Noto Sans CJK JP"
# (GlyphLayoutEngine.h), and the image has no such font. Upstream's
# noto_sans_cjk_jp package holds variable fonts with CFF2 outlines, which
# the bootstrap FreeType 2.6.3 cannot read (CFF2 came with FreeType 2.8), so
# this one has the static Regular and Bold fonts, with CFF outlines, from
# github.com/notofonts/noto-cjk (main, Sans/OTF/Japanese; downloaded into
# deps/fonts/noto-cjk-static/ if they are not there). tools/c2-repo.py puts
# it in the repository; on a board: pkgman install noto_sans_cjk_jp_static.
set -e
cd "$(dirname "$0")/.."
VERSION=2.004-1
PACKAGE=generated.arm64/objects/linux/x86_64/release/tools/package/package
SOURCE=https://raw.githubusercontent.com/notofonts/noto-cjk/main
src=deps/fonts/noto-cjk-static
mkdir -p "$src"
fetch()
{
	[ -f "$src/$(basename "$1")" ] || curl -sSL -m 600 -o "$src/$(basename "$1")" \
		"$SOURCE/$1"
}
fetch Sans/OTF/Japanese/NotoSansCJKjp-Regular.otf
fetch Sans/OTF/Japanese/NotoSansCJKjp-Bold.otf
fetch Sans/LICENSE
(cd "$src" && sha256sum -c - <<SUMS)
68a3fc98800b2a27b371f2fb79991daf3633bd89309d4ffaa6946fd587f375b5  NotoSansCJKjp-Regular.otf
e53dcb0dcb2922e45d01aae1ebd2f382bb81d4229b18b6b883bd170678af1f76  NotoSansCJKjp-Bold.otf
6a73f9541c2de74158c0e7cf6b0a58ef774f5a780bf191f2d7ec9cc53efe2bf2  LICENSE
SUMS

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
docs=$work/documentation/packages/noto_sans_cjk_jp_static
mkdir -p "$work/data/fonts/otfonts" "$docs"
cp "$src/NotoSansCJKjp-Regular.otf" "$src/NotoSansCJKjp-Bold.otf" \
	"$work/data/fonts/otfonts/"
cp "$src/LICENSE" "$docs/LICENSE"
cat > "$work/.PackageInfo" <<INFO
name			noto_sans_cjk_jp_static
version			$VERSION
architecture	any
summary			"Noto Sans CJK JP, Regular and Bold, for Korean, Japanese and Chinese"
description		"The static Regular and Bold Noto Sans CJK JP fonts (CFF outlines), \
which the system draws Hangul, kana and Han characters with when a font lacks them. \
Upstream Haiku's noto_sans_cjk_jp has variable fonts that need FreeType 2.8 or later."
packager		"haiku-odroid-c2 port"
vendor			"haiku-odroid-c2 port"
licenses		{ "SIL Open Font License v1.1" }
copyrights		{ "2014-2021 Adobe (http://www.adobe.com/)" }
provides		{ noto_sans_cjk_jp_static = 2.004 }
requires		{ }
urls			{ "https://github.com/notofonts/noto-cjk" }
INFO
rm -f deps/fonts/noto_sans_cjk_jp_static-*.hpkg
out=deps/fonts/noto_sans_cjk_jp_static-$VERSION-any.hpkg
"$PACKAGE" create -q -C "$work" -i "$work/.PackageInfo" "$out"
"$PACKAGE" list "$out" | sed -n '/^data/,$p'

#!/bin/sh
# Copyright 2026, haiku-odroid-c2 port authors.
# Distributed under the terms of the MIT License.
#
# Noto Color Emoji for the boards, as a package: deps/fonts/
# noto_emoji_color-<version>-any.hpkg. HaikuPorts' noto_emoji_color
# (downloaded into deps/fonts/downloaded/ once) has two fonts that
# both call themselves "Noto Color Emoji" / "Regular": NotoColorEmoji.ttf,
# whose glyphs are PNG pictures (CBDT), and Noto-COLRv1.ttf, whose glyphs
# are layered vector paints. The app_server keeps one face per family and
# style, and it draws only the first kind (FontEngine.cpp's colour glyphs,
# FreeType with libpng), so this package has only NotoColorEmoji.ttf.
# tools/c2-update.py installs it from run/allpkgs.txt; tools/c2-repo.py
# puts it in the repository.
set -e
cd "$(dirname "$0")/.."
VERSION=2.042-2
PACKAGE=generated.arm64/objects/linux/x86_64/release/tools/package/package
upstream=deps/fonts/downloaded/noto_emoji_color-2.042-1-any.hpkg
echo "c1885cb910f405bf6eb39fcdc6239ff608941aad81c1f73aa7fb7593c6604659  $upstream" \
	| sha256sum -c --quiet -

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
mkdir "$work/upstream" "$work/package"
"$PACKAGE" extract -C "$work/upstream" "$upstream"
mkdir -p "$work/package/data/fonts/ttfonts"
cp "$work/upstream/data/fonts/ttfonts/NotoColorEmoji.ttf" \
	"$work/package/data/fonts/ttfonts/"
cat > "$work/package/.PackageInfo" <<INFO
name			noto_emoji_color
version			$VERSION
architecture	any
summary			"The Noto Color Emoji font (PNG glyphs)"
description		"Noto Color Emoji with its glyphs as PNG pictures (CBDT), which the \
system draws emoji with when a font lacks them. HaikuPorts' noto_emoji_color 2.042-1 \
without Noto-COLRv1.ttf, which has the same family and style names and glyphs the \
system can't draw."
packager		"haiku-odroid-c2 port"
vendor			"haiku-odroid-c2 port"
licenses		{ "SIL Open Font License v1.1" }
copyrights		{ "2015-2022 Google Inc." }
provides		{ noto_emoji_color = 2.042 }
requires		{ }
urls			{ "https://github.com/googlefonts/noto-emoji" }
INFO
rm -f deps/fonts/noto_emoji_color-*.hpkg
out=deps/fonts/noto_emoji_color-$VERSION-any.hpkg
"$PACKAGE" create -q -C "$work/package" -i "$work/package/.PackageInfo" "$out"
"$PACKAGE" list "$out" | sed -n '/^data/,$p'
ls -l "$out"

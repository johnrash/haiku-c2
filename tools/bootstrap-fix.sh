#!/bin/bash
# The arm64 HaikuPorts bootstrap packages (generated.arm64/download) were
# built without the Cortex-A53 erratum workarounds (milestone M7/M8). This makes
# fixed copies under the same names and versions, which image-add-diag.sh
# puts in place of the originals:
# - erratum 843419: every sequence is patched (tools/a53-843419.py --fix);
# - erratum 835769 needs a NOP the binaries do not have room for: files
#   rebuilt from source with the workaround (tools/bootstrap-rebuild.sh) are
#   taken from deps/bootstrap/rebuilt/<package>/<path in the package>.
# ICU is left to tools/icu-data-build.sh, which also adds its data and takes
# the rebuilt ICU libraries from the same place.
# Usage: bootstrap-fix.sh      -> deps/bootstrap/<package>.hpkg
set -e
. "$(dirname "$0")/haiku-arm64-env.sh"
OUT=$HAIKU_PROJECT/deps/bootstrap
REBUILT=$OUT/rebuilt
PACKAGE=$HAIKU_HOST_TOOLS/package/package
mkdir -p "$OUT"

for original in "$HAIKU_GEN"/download/*_bootstrap-*-arm64.hpkg; do
	name=$(basename "$original" .hpkg)
	case "$name" in
		icu74-*|*_devel-*) continue ;;
	esac
	base=${name%%-*}
	pkg=$OUT/pkg/$name
	haiku_rm_tree "$pkg"
	mkdir -p "$pkg"
	"$PACKAGE" extract -C "$pkg" "$original"
	replaced=0
	if [ -d "$REBUILT/$base" ]; then
		while IFS= read -r -d '' f; do
			rel=${f#$REBUILT/$base/}
			[ -e "$pkg/$rel" ] || { echo "bootstrap-fix: $base: no $rel"; exit 1; }
			cp "$f" "$pkg/$rel"
			haiku_mimeset "$pkg/$rel"
			replaced=$((replaced + 1))
		done < <(find "$REBUILT/$base" -type f -print0)
	fi
	patched=$(find "$pkg" -type f -print0 \
		| xargs -0 python3 "$HAIKU_PROJECT/tools/a53-843419.py" --fix \
		| grep -c ' fixed, ' || true)
	find "$pkg" -type f -print0 | xargs -0 python3 "$HAIKU_PROJECT/tools/a53-843419.py" \
		> /dev/null || { echo "bootstrap-fix: $name: 843419 sequences left"; exit 1; }
	hpkg=$OUT/$name.hpkg
	rm -f "$hpkg"
	(cd "$pkg" && "$PACKAGE" create -q "$hpkg")
	echo "bootstrap-fix: $name: $patched files patched (843419), $replaced rebuilt"
done

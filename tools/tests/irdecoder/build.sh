#!/bin/sh
# Builds irdecoder-test on the build host (see irdecoder-test.cpp).
# Copyright 2026, haiku-odroid-c2 port authors. MIT License.
here=$(cd "$(dirname "$0")" && pwd)
haiku=$here/../../../haiku
exec g++ -O2 -Wall -Wextra -std=c++17 -I"$here" \
	-DCAPTURE_DIRECTORY="\"$here\"" \
	-I"$haiku/headers/private/drivers" \
	-I"$haiku/src/add-ons/kernel/drivers/input/meson_ir" \
	"$here/irdecoder-test.cpp" \
	"$haiku/src/add-ons/kernel/drivers/input/meson_ir/IRDecoder.cpp" \
	-o "${1:-$here/irdecoder-test}"

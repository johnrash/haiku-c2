# Patches for other projects

Changes to the software the port builds for the C2, outside Haiku itself.
The build scripts in `tools/` apply them. The port's changes to Haiku are
in [`../haiku-patches`](../haiku-patches) instead.

Where the port wrote a patch, it's offered under the MIT licence
("Copyright 2026, haiku-odroid-c2 port authors."). Patches carried forward
from HaikuPorts or haiku-pi keep the terms their headers give. A program
built with any of them stays under its own licence, given in the Licence
column.

| Path | What | Origin | The patched project's licence |
|---|---|---|---|
| `binutils/ld-2.46.1-aarch64-843419-rescan.patch` | The cross `ld` rescans for Cortex-A53 erratum 843419 after every relayout (applied to `buildtools/` by `tools/haiku-setup.sh`) | the port | GPL-3.0-or-later |
| `u-boot/0001-meson-hdmi-wait-for-slow-sinks.patch` | U-Boot's Meson HDMI waits for slow displays (applied by `tools/haiku-setup.sh`) | the port | GPL-2.0-or-later |
| `wpa_supplicant/10-access-point-choice.patch` | Signal levels on Haiku, so the best access point is chosen | the port | BSD-3-Clause |
| `firefox/157.0.1/10-*`, `20-*`, `21-*`, `22-*` | arm64 Haiku: the JavaScript engine, the browser, WebGL's libGL name, getentropy | [haiku-pi](https://github.com/haiku-pi/haiku-rpi) at 5db456c, applied after HaikuPorts' patchsets | MPL-2.0 |
| `firefox/157.0.1/30-*`, `31-*` | The bundled libevent's configuration for Haiku; linking libxul with libgcc_s | the port | MPL-2.0 (libevent's header: BSD) |
| `rust/1.90.0/0001-*`, `rust-libc/0001-*` | The aarch64-unknown-haiku target, and the libc crate's arm64 module | haiku-pi at 5db456c | MIT OR Apache-2.0 |
| `python/python3.15-*.patch` | Cross builds for Haiku, and ctypes' library path | the port | Python Software Foundation licence |
| `python/av-19.0.2-averror.patch` | PyAV's error codes on Haiku, where errno values are negative | the port | BSD-3-Clause |
| `python/ifaddr-0.2.0-haiku.patch` | ifaddr on Haiku (libnetwork, BSD sockaddr layout) | the port | MIT |
| `python/pyqt-builder-1.21.0-test-runner.patch` | Lets PyQt-builder's configuration tests run on a board instead of the build host | the port | BSD-2-Clause |
| `python/pyqt6-6.12.0-cfgtests/` | PyQt6's configuration test answers, recorded once on a C2 | the port | (data) |
| `python/pyside6-6.12.0-haiku.patch` | PySide6 on Haiku | HaikuPorts' pyside6-6.10.3 patchset, carried forward | LGPL-3.0 / GPL |
| `qt6/qt6_base-6.12.0-haiku.patch` | qtbase on Haiku | HaikuPorts' qt6_base-6.10.3 patchset, carried forward | LGPL-3.0 / GPL |
| `qt6/qt6_multimedia-6.12.0-haiku.patch`, `qt6/multimedia-haiku/` | An audio output for Qt Multimedia on Haiku's mixer, and the FFmpeg plugin enabled | the port | LGPL-3.0 / GPL |
| `qt6/haikuplugins/CMakeLists.txt`, `qt6/haikuplugins-theme-font.patch` | Building Haiku's Qt platform plugins (qt6-haikuplugins) without qmake, and a font deadlock fix | the port | the plugins' own sources' licence |
| `rsync/rsync-3.5.2-haiku.patch` | rsync on Haiku | HaikuPorts' rsync-3.2.7 patchset (Jerome Duval), carried forward | GPL-3.0-or-later |
| `haiku/meson_mali-gp-pp-overlap.patch` | An experiment: overlapping the Mali GPU's GP and PP jobs (M61, measured and not adopted) | the port | MIT (the port's own driver) |
| `../tools/patches/mesa-22.0.5-*.patch` | Mesa 22.0.5's lima driver on Haiku ("Lima Pipe") | the port | MIT |

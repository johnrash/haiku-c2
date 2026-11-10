# Haiku on the ODROID-C2

An unofficial port of [Haiku](https://www.haiku-os.org) (arm64) to
Hardkernel's ODROID-C2 (Amlogic S905, 4× Cortex-A53, Mali-450). It boots to
the desktop and runs on the C2's own hardware: HDMI, eMMC and microSD,
Gigabit Ethernet, USB, hardware video decoding, the Mali GPU, and the
40-pin header.

This repository has everything in one place:

- **The port's changes to Haiku**, as 342 patches
  ([`haiku-patches/`](haiku-patches), indexed by area in
  [`docs/haiku-fixes.md`](docs/haiku-fixes.md)). Many are bug fixes in
  Haiku's common code (network stack, file system cache, USB, media,
  app_server) that any platform can hit.
- The tools that build, flash and test it.
- U-Boot settings and a patch.
- Patches for the third-party software built for the C2.
- Two demo apps.
- The design and research notes.

> **Not Haiku.** This port is not made, endorsed or supported by Haiku,
> Inc. or the Haiku project. "Haiku" and the Haiku logo are trademarks of
> Haiku, Inc. The images are unofficial Haiku builds
> (`HAIKU_DISTRO_COMPATIBILITY` "default"), made without Haiku's
> trademarked artwork, as Haiku asks of builds by others: there's no boot
> logo, and About shows the stock "Walter" logo.

> **AI-assisted.** The port, its Haiku changes and these tools were written
> with Claude, Anthropic's AI model, working for the port's owner. The
> commits say so (`Co-Authored-By: Claude`); one patch, 0075, is Axel
> Dörfler's upstream fix, cherry-picked, with only its added note from the
> port. **Haiku does not accept
> AI-generated contributions**, so please don't submit these patches, or
> code derived from them, to Haiku. If a fix here points at a Haiku bug,
> report the bug upstream with what the patch's message says about the
> cause.

## What works

- **Boot:** U-Boot (mainline v2026.10) loads Haiku's EFI loader from the
  eMMC or a microSD card, and the desktop comes up on all four CPUs. It has
  a serial console and the kernel debugger. Reboot and power-off go through
  PSCI. (M1-M7)
- **Display:** a native HDMI driver, 1920x1080 at 60 Hz, and modes up to
  4K at 30 Hz; a display plugged in after boot gets its own mode. HDMI
  audio. (M6, M13, M29, M53)
- **Storage:** eMMC (8-bit DDR52) and microSD. An image made for keeping
  grows its system volume to fill the disk during its first two boots
  (tested on eMMC modules). The bench's test images, which are reflashed
  for every test, keep the rest of the eMMC as a separate `/Data` volume
  instead. (M4, M4.5, M6, M7, M10, M11, M20, M40, M46, M74)
- **Network:** Gigabit Ethernet with DHCP. Wi-Fi with RTL8812BU (RTL8822B)
  USB adapters. NTP at boot, since the C2 has no battery-backed clock.
  (M5, M6, M64, M80, M81)
- **USB:** keyboards, mice and hubs; flash drives; USB audio adapters;
  UVC webcams such as the Logitech C920, with their microphones. (M5, M12,
  M45, M50)
- **Video:** the S905's hardware decoder for H.264, HEVC and MPEG-1/2 (in
  MediaPlayer) and for the webcams' MJPEG; hardware H.264 encoding for
  camera clips. The encoder needs Amlogic's microcode, which isn't
  included: `tools/venc-firmware.sh` fetches it from Hardkernel's vendor
  kernel and packages it for your own boards. (M26, M31, M48, M51, M60)
- **3D:** OpenGL on the Mali-450 through Mesa's lima driver. (M43, M55)
- **CPU:** clock scaling with the load, and the temperature. (M6, M30)
- **The 40-pin header:** GPIO, I2C, UART, PWM and the ADC, through C++
  classes in the style of the BeBox's Device Kit (UART A not yet tried
  with a device). Hardkernel's PCF8563 RTC shield keeps the time; the
  16x2 LCD + IO shield has a driver, and the Weather Board 2 is read over
  I2C B, both with demo apps (`apps/`); the board's IR receiver takes
  remote controls. (M23, M24, M27, M65, M66, M68, M70, M87)
- **Software for the C2,** built by the port's tools as packages for a
  repository of your own (`tools/c2-repo.py`; no prebuilt packages are
  published): Firefox, Python 3.15 with PySide6 and PyQt6, gcc and make for
  building on the board, rsync, and OpenSSH. (M7, M78, M83-M86)

The fork also builds for x86_64 (M77). [`docs/milestones.md`](docs/milestones.md)
lists every milestone, and [`docs/BUILDING.md`](docs/BUILDING.md) has the
known issues.

## Getting started

You need a Linux PC, an ODROID-C2 with an eMMC module or a microSD card,
and a 3.3 V USB serial adapter on the C2's debug header. A crossover
Ethernet cable and a USB relay in the 5 V line make the test loop
hands-free.

```bash
tools/haiku-setup.sh
```

This clones Haiku, its build tools, U-Boot and the boot blobs, and applies
the patches. Then build the cross tools and an image as
[`docs/BUILDING.md`](docs/BUILDING.md) describes.

**Default login.** An image made with `DIAG_OPTS=--ssh` and the OpenSSH
package (see [`docs/BUILDING.md`](docs/BUILDING.md)) has an SSH login for
user `baron` with the test password `haiku`. Change it (`passwd`) before
the board goes on a network you don't control.

## Layout

| Path | What |
|---|---|
| `haiku-patches/` | The port's changes to Haiku, as `git format-patch` files on top of upstream a12d4cd1a5 |
| `tools/` | Building, flashing, testing, packaging; `haiku-setup.sh` sets up the source trees |
| `configs/`, `boot-scripts/` | Haiku's build configuration, U-Boot configuration fragments, U-Boot scripts |
| `patches/` | Changes to other projects; [`patches/README.md`](patches/README.md) lists where each comes from and its licence. Mesa's are in `tools/patches/` |
| `apps/` | LCD Shield and Weather Board, demo apps for Hardkernel's 16x2 LCD + IO Shield and Weather Board 2 |
| `firmware/` | The video decoder's and the RTL8822B's firmware, from linux-firmware |
| `docs/` | Building, the milestones, the patch index, and design and research notes for the drivers |
| `on.py`, `off.py` | Power through a CH340 USB relay |

## Licences

- **The port's own files** (tools, apps, docs, configs): MIT, "Copyright
  2026, haiku-odroid-c2 port authors."
- **`haiku-patches/`:** the licence of the Haiku code each one changes,
  which is mostly MIT. Two kinds of files come in under their own terms:
  - the RTL8822B tables and headers from Linux's rtw88, under its
    BSD-3-Clause option with Realtek's notice (patches 0256 and 0257);
  - Arm's optimized-routines (MIT OR Apache-2.0 WITH LLVM-exception,
    patch 0041).
- **`patches/` and `tools/patches/`:** see
  [`patches/README.md`](patches/README.md). The port's own patches are MIT;
  those carried forward from [HaikuPorts](https://github.com/haikuports/haikuports)
  or from [haiku-pi](https://github.com/haiku-pi/haiku-rpi) keep the
  terms their headers give.
- **`firmware/`:** the licences next to the files.

If you distribute images or packages built from this:
- **Source:** the GPL parts (gcc, binutils, rsync, PyQt6, U-Boot and
  others) must come with their source or an offer of it.
- **Firefox:** `tools/firefox-build.sh` builds with Mozilla's official
  branding (`--enable-official-branding`), which is for your own use only.
  Mozilla's trademarks don't allow passing it on: build an unbranded one
  for that.

## Thanks

To the Haiku project, whose arm64 port this builds on; Hardkernel, for the
C2 and its documentation; LibreELEC, for amlogic-boot-fip; the Linux and
linux-firmware developers, whose drivers documented much of the hardware;
HaikuPorts, for the recipes and patches this reuses; and haiku-pi, for its
arm64 Firefox and Rust work.

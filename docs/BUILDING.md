# Building and testing

How the port is built and tested: on a Linux PC, with the C2 on a crossover
cable, a serial console and a power relay. The port was built on Ubuntu
26.04 (x86_64).

Two files name this repository's path, `/build/haiku-odroid-c2`:
`tools/dnsmasq.conf` and `tools/c2-repo.service`. Clone it there, or edit
them. The other tools find it themselves.

The full build from a fresh clone hasn't been run end to end. The order
below is taken from what each script needs; if a step fails for want of
something, its script's header says what it expects.

## The host

Packages (Ubuntu names):

```bash
sudo apt install git build-essential nasm bc autoconf automake libtool texinfo flex bison gawk unzip zip wget curl xz-utils zlib1g-dev libzstd-dev xorriso mtools u-boot-tools util-linux device-tree-compiler gcc-aarch64-linux-gnu libssl-dev libgnutls28-dev uuid-dev swig python3-dev python3-setuptools python3-pyelftools python3-serial cmake meson ninja-build dnsmasq sshpass ffmpeg v4l-utils picocom qemu-system-arm qemu-efi-aarch64
```

The source trees (see below) are applied with `git am`, which needs a git
identity:

```bash
git config --global user.name "Your Name"
git config --global user.email you@example.com
```

Folders the tools write to, and access to the serial ports (log out and in
again after `usermod`):

```bash
mkdir -p tftp run logs images
sudo usermod -aG dialout $USER
```

Several scripts run builds with `-j14`, the PC they were written on having
14 cores (`c2-test-image.sh`, `c2-sd-image.sh`, `uboot-build.sh`,
`ports-build.sh`). Change it for yours. Disk space: `generated.arm64/`
takes about 4 GB, and `deps/` grew to 35 GB here with the optional ports
built as well.

## The source trees

```bash
tools/haiku-setup.sh
```

This downloads Haiku's and buildtools' full histories (over 1 GB) and puts
the following next to `tools/` (git ignores them):

| Folder | What |
|---|---|
| `haiku/` | Haiku at a12d4cd1a5 (hrev60299+6), with `haiku-patches/` applied on the branch **`odroid-c2`**. The hrev tags come from Haiku's own server (`HAIKU_TAGS_URL`, review.haiku-os.org), since GitHub's mirror has none, and the build's version comes from them |
| `buildtools/` | Haiku's cross toolchain sources at 33d46bf5c3 (binutils 2.46.1, gcc 13.3.0), with `patches/binutils/` |
| `u-boot/` | Mainline U-Boot v2026.10, with `patches/u-boot/` |
| `amlogic-boot-fip/` | LibreELEC's signing tools and boot blobs for the C2, at b6600f2857 |

`HAIKU_URL`, `BUILDTOOLS_URL`, `UBOOT_URL` and `FIP_URL` choose other
places to clone from. A folder that's already there is left as it is.

Then build Haiku's `jam`:

```bash
cd buildtools/jam && make && sudo ./jam0 install
```

Build output goes to:

| Folder | What |
|---|---|
| `generated.arm64/` | Haiku's build directory: cross tools, objects, `haiku-mmc.image` |
| `deps/` | Libraries and ports built for the C2 (`deps/src`: their sources) |
| `tftp/` | U-Boot builds, `c2.scr` and the image being flashed |
| `images/`, `repo/`, `run/`, `logs/` | SD images, the package repository, running state, logs |

## Building an image

**Source archives.** No script downloads them. Put them in `deps/src`
(LLVM's in `deps/src/llvm12`, HaikuPorts' patchsets and recipes in
`deps/src/haikuports` where a script says so).
[`tools/deps/sources.sha256`](../tools/deps/sources.sha256) lists every
archive the port's builds used, with its checksum. The `.recipe` files are
HaikuPorts' and give each archive's `SOURCE_URI`. Check them with:

```bash
cd deps/src && sha256sum -c ../../tools/deps/sources.sha256
```

Some are only needed for optional ports (Python, Qt, gcc, rsync...).

**The order.** The ports need the fork's `haiku` and `haiku_devel`
packages to build against, and the image needs the ports. So Haiku is
built twice:

```bash
mkdir generated.arm64 && cd generated.arm64
../haiku/configure --cross-tools-source ../buildtools --build-cross-tools arm64 -j$(nproc)
jam -q -j$(nproc) haiku.hpkg haiku_devel.hpkg
cd ..
```
```bash
tools/mesa-build.sh
tools/deps-build.sh
tools/ports-build.sh
tools/llvm-build.sh
tools/mesa-build.sh
```
```bash
ln -s ../configs/haiku-UserBuildConfig generated.arm64/UserBuildConfig
cd generated.arm64 && jam -q -j$(nproc) @c2-mmc
```

- The first `mesa-build.sh` makes revision 1 (softpipe only), which
  `ports-build.sh` builds GLU against. The second, once
  `llvm-build.sh` has built LLVM 12, makes revision 6 with llvmpipe: the
  one the image takes.
- `deps-build.sh` builds static libraries (OpenSSL, libjpeg-turbo, libpng)
  into `deps/prefix`, for OpenSSH and the webcam driver's MJPEG.
- `ports-build.sh` with no arguments builds the ports the image needs. The
  optional ones are built by name (`ports-build.sh python3.15`, ...; the
  list is in the script).
- `configs/haiku-UserBuildConfig` turns on the build features those
  libraries provide (OpenGL, FFmpeg, the translators, the Debugger) and
  adds grep, sed, gawk, less and findutils to the image. The `@minimum`
  profile has none of them.
- **The build is an unofficial Haiku build** (`HAIKU_DISTRO_COMPATIBILITY`
  "default", and no `HAIKU_INCLUDE_TRADEMARKS`), so Haiku's logos and
  Haiku3d are left out, as Haiku's guidelines ask of builds by others. Keep
  it that way if you distribute images.
- Everything built for the C2 carries the Cortex-A53 errata workarounds
  (835769 and 843419). The cross `ld` from `buildtools/` must have the
  843419 rescan patch, which `haiku-setup.sh` applies. `tools/a53-843419.py`
  and `tools/a53-835769.py` check a binary.
- HaikuPorts' arm64 bootstrap packages, which jam downloads into the image,
  were built without those workarounds. `tools/bootstrap-rebuild.sh`,
  `tools/gcc-syslibs-build.sh` and then `tools/bootstrap-fix.sh` make fixed
  copies in `deps/bootstrap/`, and images get them when they're there. ICU
  is fixed by `tools/icu-data-build.sh`, which images run by themselves.

**More packages**, built by their own scripts into `deps/` and added to an
image with `PACKAGES` (below) or to a board from the repository:
`openssh-build.sh` (after `deps-build.sh`), `translators-build.sh` (the
JPEG and PNG translators), `firmware-package.sh` (the video decoder's
firmware) and `wifi-firmware-package.sh` (the Wi-Fi chip's; see
[`firmware/README.md`](../firmware/README.md)), `wpa-supplicant-build.sh`,
`qt6-build.sh`, `firefox-build.sh`, and the apps in `apps/`.

## The bench

| What | Where | How it's used |
|---|---|---|
| Serial console | A USB serial adapter set to 3.3 V, on the C2's debug UART header; its `/dev/serial/by-id/...` path in `C2_CONSOLE` | 115200 8N1: U-Boot, loader and kernel output; KDL input |
| Power | A CH340 serial relay module (`/dev/serial/by-id/usb-1a86_USB_Serial-if00-port0`; another path goes in `tools/power.py`), in the 5 V line, wired COM→NC so that the board is on while the relay is released | `tools/power.py on\|off\|cycle` |
| Network | A crossover cable to a USB NIC on the PC, which has 10.42.0.1/24 | dnsmasq on that link only: TFTP for U-Boot; DHCP and DNS for Haiku, which always gets 10.42.0.2; `ftpd` and `telnetd` run on the C2 |
| HDMI | A USB capture dongle | Screenshots, and keeping HDMI on (below) |
| microSD | U-Boot only | Runs `c2.scr` from the PC if there is one, else boots Haiku from the eMMC |
| eMMC | The test image | Rewritten by every test |

Use the `by-id` paths: the `ttyUSB` numbers swap after replugs.

### Setting it up

1. **The network link:** a static address and no gateway, so the PC's
   other connections are unaffected. With NetworkManager:

   ```bash
   nmcli connection add type ethernet ifname <interface> con-name c2-link ipv4.method manual ipv4.addresses 10.42.0.1/24 ipv4.never-default yes ipv6.method disabled
   ```

   Then edit `tools/dnsmasq.conf`: the interface, the C2's MAC address
   (`dhcp-host`), the user dnsmasq runs as, and the paths.

2. **U-Boot on a microSD card**, written once from the PC (this overwrites
   the start of the card; its partition table is kept):

   ```bash
   tools/uboot-build.sh u-boot-c2
   ```
   ```bash
   sudo dd if=tftp/u-boot-c2.sd.bin of=/dev/sdX conv=fsync,notrunc bs=512 skip=1 seek=1
   ```
   ```bash
   sudo dd if=tftp/u-boot-c2.sd.bin of=/dev/sdX conv=fsync,notrunc bs=1 count=440
   ```

   `configs/u-boot-c2.config` gives it the board's static address and a
   boot command that fetches `c2.scr` from 10.42.0.1 over TFTP, and boots
   the eMMC when there isn't one. It also has the watchdog commands, with
   the watchdog off unless started.

3. **An eMMC module that has a bootloader** (a module sold with an OS on
   it): the C2 starts that before the microSD. Clear the module's first
   4 MiB, with an eMMC reader on the PC or from the module's own U-Boot
   prompt. The first test image rewrites the module anyway:

   ```bash
   sudo dd if=/dev/zero of=/dev/sdX bs=1M count=4 conv=fsync
   ```

4. **The HDMI capture dongle.** It only asserts hot-plug while something
   captures from it, and the C2 only drives HDMI when it sees hot-plug at
   boot, so `tools/hdmi-live.sh` keeps a capture running. It finds the
   dongle by the name "Guermok USB2 Video" or "Guermok USB3 Video" (edit
   it for another), and runs `sudo -n ffmpeg` and `sudo -n kill`, so it
   needs passwordless sudo for those two.

## Everyday use

Start these after the PC boots:

```bash
C2_CONSOLE=/dev/serial/by-id/<your adapter> setsid nohup tools/c2console.py c2 > run/console.stdout 2>&1 < /dev/null &
```
```bash
sudo dnsmasq -C tools/dnsmasq.conf
```
```bash
setsid tools/hdmi-live.sh > run/hdmi-live.log 2>&1 < /dev/null &
```
```bash
sudo setsid tools/sntp-server.py > run/sntp.log 2>&1 < /dev/null &
```

The C2 has no battery-backed clock. dnsmasq points Haiku's default NTP
names at the PC, where `sntp-server.py` answers.

To build an image, flash it to the eMMC and boot it, run the following. It
waits for the guest diagnostics' first dump and saves the serial log as
`logs/<name>.log`:

```bash
tools/c2-test-image.sh my-test
```

To boot what's already on the eMMC, without building or flashing:

```bash
NOFLASH=1 tools/c2-test-image.sh my-boot
```

To flash an image for everyday use, without diagnostics, the remote shell
or KDL on hangs (add `REMOTE_SHELL=1` to keep `c2sh` working):

```bash
CLEAN=1 tools/c2-test-image.sh my-image
```

After a flash, `c2.scr` is switched back to booting the eMMC, so later
reboots don't reflash.

`c2-test-image.sh` also reads:

| Variable | What |
|---|---|
| `NOBUILD=1` | Flash the image jam built last, without building |
| `DIAG_OPTS` | More options for `image-add-diag.sh` (below) |
| `PACKAGES` | `.hpkg` files added to the image's system packages |
| `GROW=1` | The system partition grown to 988 MiB, up to the data partition |
| `PROFILE` | The jam profile (default `c2-mmc`; `minimum-mmc` is Haiku's smallest) |
| `KDL_ON_HANG=0` | No KDL when the desktop isn't up 120 s after boot |
| `WAIT_FOR`, `WAIT_SECONDS` | What to wait for on the console, and how long (default 400 s) |
| `EXTRA_SECONDS` | Keep logging this long after it. The diagnostics' first dump ends with `diag early end`, and the disk and network checks after it are only in the log with this set |

Test images are built with a data partition from 1 GiB to the end of a
116.6 GiB eMMC module. For another size, change `EMMC_SECTORS` in
`tools/image-add-data-partition.py`. The partition survives reflashing:
the image covers only the first few hundred MiB.

## Images to keep

`tools/c2-sd-image.sh <name>` makes a self-contained microSD image, with
U-Boot and Haiku running from the card, in `images/` (`.img` and
`.img.xz`). `TARGET=emmc` makes one for an eMMC module, with the data
partition (see above for its size); its microSD stays read-only. Write it
with `dd if=images/<name>.img of=/dev/sdX bs=4M conv=fsync`, or Etcher.

- The U-Boot is `configs/u-boot-c2-sd.config`'s, built the first time
  (`UBOOT=<name>` takes `tftp/<name>.sd.bin` instead). It boots Haiku from
  the device it started from, else the other one.
- With `DIAG_OPTS=--grow-on-boot` (plus `--no-data-partition` for an
  eMMC module), the system volume grows to fill the card or module during
  the first two boots. The first boot extends the partition and restarts
  the board once the desktop is up. The second grows the file system.
- Without it, an eMMC image keeps the space after 1 GiB as a `/Data`
  volume.
- `PACKAGES` and `DIAG_OPTS` work as for `c2-test-image.sh`.
- `sudo tools/c2-grow.sh <device> --apply` grows an existing card or
  module from the PC, in a USB card reader. Without `--apply` it only
  shows what it would do.
- `sudo tools/c2-sd-revert.sh <device> --apply` undoes the last `pkgman`
  update of a card that no longer boots after it.

## Boards on a LAN

**SSH.** Build OpenSSH (`tools/openssh-build.sh`, after `deps-build.sh`)
and make the image with:

```bash
DIAG_OPTS=--ssh PACKAGES=deps/openssh/openssh-10.6p1-1-arm64.hpkg tools/c2-sd-image.sh my-image
```

`--ssh` needs `deps/ssh/shadow`: `/etc/shadow` from a board that has the
test password, which `tools/c2sh` sets on first use
(`tools/c2get /etc/shadow deps/ssh/shadow`). The login is then user
`baron`, password `haiku`. **Change it with `passwd`** on any board others
can reach.

**The package repository.** `tools/c2-repo.py` builds `repo/` from the
image's packages, the ports and jam's optional packages, which need
building first:

```bash
cd generated.arm64 && jam -q haiku_devel.hpkg haiku_extras.hpkg makefile_engine.hpkg netfs.hpkg userland_fs.hpkg
```

`tools/c2-repo.service` serves it on port 8090 (its header says how to
install it). Its URL defaults to the PC's address towards `C2_LAN`; `--url`
gives another. A board uses it after `pkgman add-repo <url>`, or from the
start with `DIAG_OPTS=--repo=<url>`. A package reaches the boards as an
update only when its version is newer than theirs: for the `haiku`
package, that means a new commit in `haiku/`.

**`tools/c2-update.py --host <address>`** updates a board directly over
SSH (user `baron`, password from `C2_PASSWORD`, through `sshpass`). It
copies every package whose version differs from the board's, older ones
too, and installs them in one `pkgman` transaction; a reboot completes it.
Ports the board doesn't have are left out unless `--new-ports` is given.
`--dry-run` only lists them, and `--reboot` reboots afterwards.

## Tools

| Tool | What it does |
|---|---|
| `tools/power.py on\|off\|cycle` | Relay power control (`on.py` and `off.py` do the same) |
| `tools/c2console.py <tag>` | Owns the serial port, logs to `logs/<tag>-<date>.log` and `run/console.raw`, and reconnects after replugs |
| `tools/c2send "<text>"`, `-n`, `-k ctrl-c` | Types into the console (U-Boot, KDL) |
| `tools/c2wait <regex> <s> [--now]` | Waits for console output |
| `tools/uboot-break.sh` | Power-cycles and stops at the U-Boot prompt |
| `tools/c2scr.sh boot-scripts/<x>.txt` | Chooses what U-Boot does on the next boot (`tftp/c2.scr`) |
| `tools/c2-test-image.sh <name>` | Builds, adds diagnostics, flashes, boots and saves the log |
| `tools/c2-sd-image.sh <name>` | An image to keep, for a microSD or an eMMC module |
| `tools/hdmi-live.sh`, `tools/hdmi-grab.sh <name>` | Keep the capture running; save a frame to `logs/hdmi/` |
| `tools/image-add-diag.sh [options] <image>` | Puts the guest scripts into an image (options below) |
| `tools/uboot-build.sh <name>` | Builds and signs U-Boot into `tftp/<name>.bin` and `.sd.bin` (`FRAGMENT=configs/<file>`: another config fragment) |
| `tools/uboot-chain.sh <file in tftp/>` | Tests a raw U-Boot build from RAM without writing anything; `AUTOBOOT=1` lets it run its boot command |
| `tools/uboot-sd-update.sh <file in tftp/>` | At a U-Boot prompt (`uboot-break.sh` first): rewrites U-Boot on the microSD from a `.sd.bin`, then reads it back and compares |
| `tools/qemu-run.sh <image> <log> [seconds]` | QEMU set up like the C2 (GICv2, 4× A53, U-Boot firmware from `u-boot-qemu/u-boot.bin`, a `qemu_arm64_defconfig` build); `QEMU_MACHINE_OPTS=,virtualization=on` enters at EL2 like the C2 |
| `tools/c2sh "<cmd>"` | Runs a command on Haiku over telnet and returns its exit status. The first use sets the dev password `haiku` for user `baron` |
| `tools/c2put <file> <abs path>`, `tools/c2get <abs path> <file>` | Copies files to or from Haiku over FTP |
| `tools/sntp-server.py` | NTP for the C2 on 10.42.0.1 only |
| `tools/haiku-arm64-env.sh` | Source it to cross-compile for Haiku arm64 against this build |
| `tools/c2-repo.py`, `tools/c2-update.py` | The package repository, and updating boards from it |
| `tools/stress.sh [minutes]` | Network round trips with checksums, a flood ping and an eMMC write and verify loop on the C2, all at once |

`tools/image-add-diag.sh` options:

| Option | What |
|---|---|
| `--remote-shell` | `ftpd` and a `telnetd` loop at boot, for `c2sh`, `c2put` and `c2get` |
| `--kdl-on-hang` | KDL if the desktop isn't up 120 s after boot |
| `--no-diag` | No diagnostics script |
| `--kdl` | The diagnostics script enters KDL after its first dump |
| `--writetest` | The diagnostics script runs a 100 MiB write and verify test |
| `--ssh` | sshd set up on the first boot, with the test password (above) |
| `--package=<hpkg>` | A package added to the system packages |
| `--sd-writable` | The microSD may be written (an image that runs from it) |
| `--serial-kdl-key` | Ctrl-K on the serial console enters KDL, also when userland hangs |
| `--no-data-partition` | No eMMC data partition |
| `--grow-on-boot` | The boot volume grows to the whole device at the first boots |
| `--no-icu-data` | Keep the bootstrap ICU package, whose formatters give empty strings |
| `--first-boot-prompt` | No locale settings: Haiku asks for the language and keymap at the first boot |
| `--repo=<url>` | The port's package repository at `<url>` |

An option it doesn't know isn't an error: it becomes a marker file
`diag-<name>` in the image, which the guest scripts may read. Check the
spelling.

Environment variables the tools read:

| Variable | Read by | Default |
|---|---|---|
| `C2_CONSOLE` | `c2console.py`: the serial adapter's `/dev/serial/by-id` path | a placeholder |
| `C2_PASSWORD` | `c2-update.py` | `haiku` |
| `C2_LAN` | `c2-repo.py`: an address on the LAN boards' network, for the repository's URL | `192.168.1.1` |
| `C2_HOST_IF`, `C2_BOARD_MAC` | The network test tools (`arp-learning-test.py`, `dhcp-*-sim.py`, `c2-soak.sh`); dnsmasq has its own lines | `eth1`, a placeholder |
| `BOARD`, `GATEWAY`, `WIFI_IF` | `wifi-soak.sh` (`GATEWAY` and `WIFI_IF` also `c2-soak.sh`) | `10.42.0.2`, `192.168.1.1`, `wlan0` |
| `GATEWAY` | `net-test.sh` (no default: the gateway test is skipped) | none |
| `SSID` | `board-wait.sh` (required) | none |
| `QEMU_NAME`, `QEMU_MACHINE_OPTS`, `QEMU_EXTRA` | `qemu-run.sh` and the `qemu-*.py` tools | `qemu` |
| `HAIKU_URL`, `BUILDTOOLS_URL`, `UBOOT_URL`, `FIP_URL`, `HAIKU_TAGS_URL` | `haiku-setup.sh` | GitHub, review.haiku-os.org |

The rest (`NOFLASH`, `NOBUILD`, `CLEAN`, `REMOTE_SHELL`, `DIAG_OPTS`,
`PACKAGES`, `GROW`, `PROFILE`, `TARGET`, `UBOOT`, `FRAGMENT`, `AUTOBOOT`,
`KDL_ON_HANG`, `WAIT_FOR`, `WAIT_SECONDS`, `EXTRA_SECONDS`) are described
above and in each script's header.

## The guest scripts

**Guest diagnostics** (`tools/guest/UserBootscript`) are run by Haiku's
launch_daemon in the desktop session. A minute after boot the script
writes to `/dev/dprintf`, so that it shows up in the serial log:

- `ps` and the syslog's tail, ending with `diag early end`;
- for each MMC disk: the partitions, a SHA-256 of the first 4 MiB, and the
  raw read speed;
- `df`, the network, `/dev/graphics` and `screenmode`.

With `--kdl` it then enters the kernel debugger.

**The system job** (`tools/guest/c2-system.sh`) is started by the
launch_daemon at boot, before and independent of the desktop. In every
image it:

- formats the data partition, if the image has one, as BFS "Data" when a
  good read finds no file system on it, and mounts it at `/Data`;
- sets the clock over NTP at any boot that starts with it unset (a year
  before 2000), retrying for a minute;
- with `--grow-on-boot`, grows the boot volume (above);
- with `--ssh`, sets up sshd and its host keys on the first boot;
- prints `c2-system: desktop up` once Tracker runs, or, if there's no
  Tracker after 120 s, `ps` to the serial log.

With `--remote-shell` it also starts `ftpd` and a `telnetd` loop for
`c2sh`, `c2put` and `c2get`. With `--kdl-on-hang`, a missing desktop
enters KDL so that the hang can be inspected.

## Known issues

- On the bench, settings live on the system partition, so reflashing the
  test image resets them: for example `dvfs off` in
  `~/config/settings/kernel/drivers/meson_scpi`, which keeps the CPU at
  the firmware's clock. `/Data` keeps its files.

## Things that cost time

- **Cross-compiled C++ must link `libgcc_s.so.1`**, as Haiku's own build
  does. Without it no exception is ever caught: `terminate` and a debugger
  stop.
- **Package updates never replace the EFI loader** on the board's FAT
  partition. A change to the loader reaches a board only by copying it
  there, on the board:
  - `mkdir -p /fat && mount -t fat /dev/disk/mmc/<n>/0 /fat`, where `<n>`
    is the disk the boot volume is on (see `df /boot`);
  - keep the old one: copy `/fat/EFI/BOOT/BOOTAA64.EFI` to
    `BOOTAA64.OLD` next to it;
  - copy `generated.arm64/objects/haiku/arm64/release/system/boot/efi/haiku_loader.efi`
    to `/fat/EFI/BOOT/BOOTAA64.EFI`;
  - `sync`, unmount, and compare checksums after mounting it again
    read-only, before the next reboot.
- **Hangs without output.**
  - The loader starts all CPUs, and the kernel waits for every one of them
    before printing anything, so a secondary CPU that dies is silent.
  - Debug this in QEMU with `virtualization=on` and `-d int`.
  - In KDL, use `threads`, `mutex <addr>` (shows the holder) and `bt`. On
    arm64 you need `cpu N` first to trace a thread that's running on
    another CPU.
- **Device tree nodes with `status = "disabled"`** are registered anyway.
  Touching an unclocked Amlogic block stalls the bus.
- **`fat_shell` sometimes leaves `EFI/BOOT/BOOTAA64.EFI` empty** in the
  built image: it fails copying a file attribute to FAT.
  `c2-test-image.sh` and `c2-sd-image.sh` put the loader in with mtools
  instead.
- **QEMU's default NIC** (legacy virtio-net-pci) hangs Haiku arm64 while it
  holds the device manager lock, so the desktop never appears.
  `qemu-run.sh` passes `-nic none`.
- **Serial tracing at 115200 baud** slows everything down. Keep per-I/O
  traces off.
- **Stale instructions on the Cortex-A53.**
  - The A53's L1 instruction cache is VIPT with 16 KiB ways. Haiku arm64
    synced it through each page's physical-map alias, which misses lines
    cached under the code's own address.
  - The result was intermittent crashes and hangs, only on hardware: QEMU
    models no caches.
  - **To recognise it:** the registers don't match the code. Compare
    `dw <address>` in KDL with `objdump` of the binary.
  - It's fixed in the fork's `arch_cpu_sync_icache()`.
- **The `@minimum` image has no `grep`, `sed` or `awk`.** A script that pipes
  into `grep` fails without any message on the console.

# Building and testing

How the port is built and tested: on a Linux PC, with the C2 on a crossover
cable, a serial console and a power relay. The tools assume this
repository is at `/build/haiku-odroid-c2` in a few places
(`tools/dnsmasq.conf`, `tools/c2-repo.service`, `tools/video/`). Clone it
there, or edit those.

## The source trees

```bash
tools/haiku-setup.sh
```

This puts the following next to `tools/` (they are ignored by git):

| Folder | What |
|---|---|
| `haiku/` | Upstream Haiku at a12d4cd1a5, with `haiku-patches/` applied on the branch **`odroid-c2`** |
| `buildtools/` | Haiku's cross toolchain sources at 33d46bf5c3 (binutils 2.46.1, gcc 13.3.0), with `patches/binutils/` |
| `u-boot/` | Mainline U-Boot v2026.10, with `patches/u-boot/` |
| `amlogic-boot-fip/` | LibreELEC's signed boot blobs for the C2 |

Build output goes to:

| Folder | What |
|---|---|
| `generated.arm64/` | Haiku's build directory: cross tools, objects, `haiku-mmc.image` |
| `deps/` | Libraries and ports built for the C2, and the sysroot (`tools/haiku-arm64-env.sh`) |
| `tftp/`, `images/`, `repo/`, `run/`, `logs/` | U-Boot builds and scripts, SD images, the package repository, running state, logs |

## The cross tools and an image

```bash
mkdir generated.arm64 && cd generated.arm64
../haiku/configure --cross-tools-source ../buildtools --build-cross-tools arm64 -j$(nproc)
ln -s ../configs/haiku-UserBuildConfig UserBuildConfig
jam -q -j$(nproc) @c2-mmc
```

- `configs/haiku-UserBuildConfig` adds grep, sed, gawk, less and findutils
  to the image. The `@minimum` profile has none of them.
- **The build is an unofficial Haiku build** (`HAIKU_DISTRO_COMPATIBILITY`
  "default", and no `HAIKU_INCLUDE_TRADEMARKS`), so Haiku's logos and
  Haiku3d are left out, as Haiku's guidelines ask of builds by others. Keep
  it that way if you distribute images.
- Everything built for the C2 carries the Cortex-A53 errata workarounds
  (835769 and 843419). The cross `ld` from `buildtools/` must have the
  843419 rescan patch. `tools/a53-843419.py` and `tools/a53-835769.py`
  check a binary.
- `tools/c2-sd-image.sh <name>` makes a self-contained microSD image, with
  U-Boot and Haiku running from the card, in `images/`. `PACKAGES` and
  `DIAG_OPTS` (`--ssh`) work as for `c2-test-image.sh`.
- `tools/uboot-build.sh <name>` builds and signs U-Boot, with the config
  fragment and the patches, into `tftp/<name>.bin` and `.sd.bin`.

## The bench

| What | Where | How it's used |
|---|---|---|
| Serial console | A 3.3 V USB serial adapter on the debug header; its `/dev/serial/by-id/...` path in `C2_CONSOLE` | 115200 8N1: U-Boot, loader and kernel output; KDL input |
| Power | A CH340 serial relay (`/dev/serial/by-id/usb-1a86_USB_Serial-if00-port0`), in the 5 V line, wired COM→NC | `tools/power.py on\|off\|cycle` |
| Network | A crossover cable to a USB NIC on the PC (`C2_HOST_IF`), 10.42.0.1/24 | dnsmasq on that link only: TFTP for U-Boot; DHCP and DNS for Haiku, which always gets 10.42.0.2 (the board's MAC in `tools/dnsmasq.conf`); `ftpd` and `telnetd` run on the C2 |
| HDMI | A USB capture dongle (`/dev/video4` here) | Screenshots |
| microSD | U-Boot: runs `c2.scr` from the PC if there is one, else boots Haiku from the eMMC | |
| eMMC | The test image | Rewritten on every test |

Use the `by-id` paths: the `ttyUSB` numbers swap after replugs.

Edit `tools/dnsmasq.conf` for your setup: the interface, the C2's MAC
address, the user, and the paths.

Environment variables some tools read:

| Variable | What | Default |
|---|---|---|
| `C2_CONSOLE` | The serial adapter's `/dev/serial/by-id` path | a placeholder |
| `C2_HOST_IF` | The PC's interface on the crossover cable | `eth1` |
| `C2_BOARD_MAC` | The test board's MAC address | a placeholder |
| `C2_PASSWORD` | The boards' SSH password | `haiku` |
| `C2_LAN` | An address on the LAN boards' network, for the repository's URL | `192.168.1.1` |
| `GATEWAY`, `WIFI_IF`, `SSID` | For the Wi-Fi and soak tests | `192.168.1.1`, `wlan0`, none |

## Everyday use

Start these after the PC boots:

```bash
setsid nohup tools/c2console.py m1 > run/console.stdout 2>&1 < /dev/null &
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

To build an image, flash it to the eMMC and boot it, run the following. It
waits for the diagnostics dump and saves the serial log as
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

**Boards on a LAN** are updated over SSH from the port's package
repository: `tools/c2-repo.py` builds it, and `tools/c2-update.py --host
<address>` installs what's newer. Ports a board doesn't have are left out
unless `--new-ports` is given.

## Tools

| Tool | What it does |
|---|---|
| `tools/power.py on\|off\|cycle` | Relay power control |
| `tools/c2console.py <tag>` | Owns the serial port, logs to `logs/<tag>-<date>.log` and `run/console.raw`, and reconnects after replugs |
| `tools/c2send "<text>"`, `-n`, `-k ctrl-c` | Types into the console (U-Boot, KDL) |
| `tools/c2wait <regex> <s> [--now]` | Waits for console output |
| `tools/uboot-break.sh` | Power-cycles and stops at the U-Boot prompt |
| `tools/c2scr.sh boot-scripts/<x>.txt` | Chooses what U-Boot does on the next boot (`tftp/c2.scr`) |
| `tools/c2-test-image.sh <name>` | Builds, adds diagnostics, flashes, boots and saves the log |
| `tools/hdmi-live.sh`, `tools/hdmi-grab.sh <name>` | Keep the capture running (the dongle only asserts hot-plug then); save a frame to `logs/hdmi/` |
| `tools/image-add-diag.sh [options] <image>` | Puts the guest scripts into an image: `--remote-shell`, `--kdl-on-hang`, `--no-diag`, `--kdl`, `--writetest`, `--ssh` |
| `tools/uboot-chain.sh <u-boot.bin>` | Tests a U-Boot build from RAM without writing anything; `AUTOBOOT=1` lets it run its boot command |
| `tools/uboot-sd-update.sh <u-boot.bin.sd.bin>` | Rewrites U-Boot on the microSD from the U-Boot prompt, then verifies it |
| `tools/qemu-run.sh <image> <log>` | QEMU set up like the C2 (GICv2, 4× A53, U-Boot firmware); `QEMU_MACHINE_OPTS=,virtualization=on` enters at EL2 like the C2 |
| `tools/c2sh "<cmd>"` | Runs a command on Haiku over telnet and returns its exit status. The first use sets the dev password `haiku` for user `baron` |
| `tools/c2put <file> <abs path>`, `tools/c2get <abs path> <file>` | Copies files to or from Haiku over FTP |
| `tools/sntp-server.py` | NTP for the C2 on 10.42.0.1 only; dnsmasq points Haiku's default NTP names there |
| `tools/haiku-arm64-env.sh` | Source it to cross-compile for Haiku arm64 against this build |
| `tools/ports-build.sh`, `tools/deps-build.sh`, `tools/openssh-build.sh`, ... | Build libraries, ports and apps for arm64 as packages in `deps/` |
| `tools/c2-repo.py`, `tools/c2-update.py` | The package repository, and updating boards from it |
| `tools/stress.sh [minutes]` | Network round trips with checksums, a flood ping and an eMMC write and verify loop on the C2, all at once |

**Guest diagnostics** (`tools/guest/UserBootscript`) are run by Haiku's
launch_daemon in the desktop session. A minute after boot the script writes
the following to `/dev/dprintf`, so that it shows up in the serial log:

- `ps`;
- the syslog's tail;
- for each MMC disk: the partitions, a SHA-256 of the first 4 MiB, and the
  raw read speed;
- `df`, `/dev/graphics` and `screenmode`.

With `--kdl` it then enters the kernel debugger.

**The system job** (`tools/guest/c2-system.sh`) is started by the
launch_daemon at boot, before and independent of the desktop. In every
image it:

- formats the data partition as BFS "Data" if a good read finds no file
  system on it, and mounts it at `/Data`;
- sets the clock over NTP on the first boot;
- prints `c2-system: desktop up` once Tracker runs, or, if there's no
  Tracker after 120 s, `ps` to the serial log.

With `--remote-shell` it also starts `ftpd` and a `telnetd` loop for
`c2sh`, `c2put` and `c2get`. With `--kdl-on-hang`, a missing desktop
enters KDL so that the hang can be inspected.

## Known issues

- Clock control (`dvfs on`) and other settings live on the system
  partition, so reflashing the image resets them. `/Data` keeps its files.

## Things that cost time

- **Cross-compiled C++ must link `libgcc_s.so.1`**, as Haiku's own build
  does. Without it no exception is ever caught: `terminate` and a debugger
  stop.
- **The boot splash comes from the EFI loader** on the board's FAT
  partition, which package updates never replace. After a change to the
  loader:
  - Mount the partition on the board: `mount -t fat /dev/disk/mmc/<n>/0`,
    where `<n>` is the disk the boot volume is on (see `df /boot`).
  - Copy `generated.arm64/objects/haiku/arm64/release/system/boot/efi/haiku_loader.efi`
    to `EFI/BOOT/BOOTAA64.EFI` there.
  - Keep the old file, and compare checksums after remounting read-only.
- **`--ssh` needs `deps/ssh/shadow`:** `/etc/shadow` copied from a board
  where `passwd` set the test password
  (`tools/c2get /etc/shadow deps/ssh/shadow`).
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
  `c2-test-image.sh` puts the loader in with mtools instead.
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

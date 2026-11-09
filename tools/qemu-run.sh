#!/bin/sh
# Boot a Haiku arm64 image in QEMU set up like the ODROID-C2 (GICv2,
# 4x Cortex-A53, 2 GiB), with mainline U-Boot (qemu_arm64_defconfig) as
# firmware so the same U-Boot UEFI path is used. bochs-display gives U-Boot a
# GOP framebuffer, like the C2's HDMI.
# Usage: qemu-run.sh <image> <log> [seconds]
# Env:   QEMU_NAME (monitor socket run/<name>-mon.sock, default qemu)
#        QEMU_MACHINE_OPTS (e.g. ",virtualization=on" to enter at EL2 like
#        the C2's firmware does)
#        QEMU_EXTRA (more options, e.g. "-d int -D run/int.log")
# The serial port is a socket (run/<name>-serial.sock, see qemu-serial.py)
# logged to <log>; a USB keyboard lets qemu-mon.py "sendkey alt-sysrq-d"
# drop the guest into the kernel debugger.
# -nic none: QEMU's default NIC is a legacy virtio-net-pci; Haiku arm64's
# virtio_pci hangs in pci_read_io_8() probing it while holding the device
# manager lock, which blocks app_server (and the desktop) forever.
root=$(cd "$(dirname "$0")/.." && pwd)
name=${QEMU_NAME:-qemu}
exec timeout "${3:-900}" qemu-system-aarch64 \
	-M "virt,gic-version=2${QEMU_MACHINE_OPTS}" -cpu cortex-a53 -smp 4 -m 2048 \
	-bios "$root/u-boot-qemu/u-boot.bin" \
	-drive if=none,file="$1",format=raw,id=hd0 -device virtio-blk-device,drive=hd0 \
	-device bochs-display -device virtio-keyboard-device -device virtio-tablet-device \
	-device qemu-xhci -device usb-kbd \
	-nic none \
	-chardev socket,id=ser0,path="$root/run/$name-serial.sock",server=on,wait=off,logfile="$2" \
	-serial chardev:ser0 -display none ${QEMU_EXTRA} \
	-monitor unix:"$root/run/$name-mon.sock",server,nowait

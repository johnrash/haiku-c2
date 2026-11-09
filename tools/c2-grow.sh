#!/bin/sh
# Grow the Haiku system partition of a microSD or eMMC module in this
# laptop's card reader (or of an image file) to use the free space after
# it, as Raspberry Pi OS does on its first boot: MBR partition 2 (BFS,
# type 0xeb) is extended to the next partition or the end of the device,
# then the file system in it (bfs_shell resizefs, offline: BFS moves the
# inodes and data that are in the way of its larger block bitmap and log).
# Files are kept; with --backup <file> the partition's used part is saved
# first (it can be written back with dd). checkfs runs before and after.
# Usage: sudo tools/c2-grow.sh <device or image> [--apply] [--backup <file>]
#   [--size <bytes>] [--drop-data]
#   --size: an image file is extended to <bytes> first.
#   --drop-data: an eMMC image's data partition (3, type 0xeb at sector
#   2097152, tools/image-add-data-partition.py) is removed first, so the
#   system partition takes the whole device; refused if it holds a BFS
#   volume (the board formats it as /Data on its first boot).
# Without --apply it only shows what it would do (and a dry run of the
# file system resize).
set -e
root=$(cd "$(dirname "$0")/.." && pwd)
bfs="$root/generated.arm64/objects/linux/x86_64/release/tools/bfs_shell/bfs_shell"
device="$1"
shift
apply=0
backup=""
size=""
dropData=0
while [ $# -gt 0 ]; do
	case "$1" in
		--apply) apply=1 ;;
		--backup) backup="$2"; shift ;;
		--size) size=$(numfmt --from=iec "$2"); shift ;;
		--drop-data) dropData=1 ;;
		*) echo "c2-grow: unknown option $1"; exit 1 ;;
	esac
	shift
done

if [ -f "$device" ]; then
	if [ -n "$size" ] && [ $apply = 1 ]; then
		truncate -s "$size" "$device"
	fi
	total=$(( $(stat -c %s "$device") / 512 ))
	[ -n "$size" ] && total=$((size / 512))
elif [ -b "$device" ]; then
	name=$(basename "$device")
	if [ "$(cat /sys/block/$name/removable 2>/dev/null)" != 1 ] \
		&& ! udevadm info -q property -n "$device" | grep -q '^ID_BUS=usb'; then
		echo "c2-grow: $device is neither removable nor on USB, not touching it"
		exit 1
	fi
	if findmnt -rn -o SOURCE | grep -q "^$device"; then
		echo "c2-grow: a partition of $device is mounted on this laptop"
		exit 1
	fi
	total=$(blockdev --getsz "$device")
else
	echo "c2-grow: $device is neither a block device nor an image file"
	exit 1
fi

# partition 2 must be BFS; it may grow up to the next partition's start
eval "$(python3 -I - "$device" "$total" "$dropData" <<'EOF'
import struct, sys
with open(sys.argv[1], "rb") as f:
	mbr = f.read(512)
	total = int(sys.argv[2])
	dropData = sys.argv[3] == "1"
	if dropData:
		_, _, kind, _, dataStart, _ = struct.unpack_from("<B3sB3sII", mbr, 446 + 32)
		if kind != 0xeb or dataStart != 2097152:
			sys.exit("c2-grow: partition 3 is not the data partition")
		# BFS keeps "1SFB" at byte 544 of its partition
		f.seek(dataStart * 512 + 544)
		if f.read(4) == b"1SFB":
			sys.exit("c2-grow: the data partition holds a BFS volume, not dropping it")
if mbr[510:512] != b"\x55\xaa":
	sys.exit("c2-grow: no MBR")
entries = [struct.unpack_from("<B3sB3sII", mbr, 446 + 16 * i) for i in range(4)]
_, _, kind, _, start, length = entries[1]
if kind != 0xeb:
	sys.exit("c2-grow: partition 2 is not BFS")
following = [e[4] for i, e in enumerate(entries)
	if e[2] != 0 and e[4] > start and not (dropData and i == 2)]
end = min(following) if following else total
# whole 2 KiB file system blocks, and whole MiB
newLength = ((end - start) // 2048) * 2048
print("start=%d length=%d newLength=%d end=%d" % (start, length, newLength, end))
EOF
)"
[ -n "$start" ] || exit 1
offset=$((start * 512))
newBytes=$((newLength * 512))
echo "c2-grow: BFS partition at sector $start: $((length / 2048)) MiB -> $((newLength / 2048)) MiB" \
	"(up to sector $end)"
[ "$newLength" -gt "$length" ] || { echo "c2-grow: nothing to grow"; exit 0; }

# the partition's end has to be given: a block device's stat() size is 0
end=$(( (start + length) * 512 ))
shell() {
	printf '%s\n' "$@" "quit" \
		| "$bfs" --start-offset "$offset" --end-offset "$end" "$device" 2>&1 \
		| grep -v '^bfs: mounted\|^fssh:/> *$\|Unmounting FS failed'
}
check() {
	result=$(shell "checkfs")
	echo "$result" | grep -q "0 blocks could be freed" \
		&& echo "$result" | grep -q "0 blocks already set" \
		&& echo "$result" | grep -q "0 blocks not allocated" \
		|| { echo "c2-grow: checkfs $1 found problems:"; echo "$result"; exit 1; }
	echo "c2-grow: checkfs $1: clean"
}

check "before"
if [ $apply = 0 ]; then
	# the partition table is not changed yet: the dry run only checks what
	# would have to move, as far as the old size allows
	shell "resizefs -n $((length * 512))" | grep -i "resiz" || true
	echo "c2-grow: dry run, nothing changed (--apply to do it)"
	exit 0
fi

if [ -n "$backup" ]; then
	dd if="$device" of="$backup" bs=1M skip=$((offset / 1048576)) \
		count=$(( (length * 512 + 1048575) / 1048576 )) status=none
	dd if="$device" of="$backup.mbr" bs=512 count=1 status=none
	echo "c2-grow: partition saved to $backup ($(du -h "$backup" | cut -f1))," \
		"the partition table to $backup.mbr; to restore:" \
		"dd if=$backup of=$device bs=1M seek=$((offset / 1048576)) conv=fsync;" \
		"dd if=$backup.mbr of=$device conv=fsync"
fi

python3 -I - "$device" "$newLength" "$dropData" <<'EOF'
import struct, sys
path, newLength = sys.argv[1], int(sys.argv[2])
with open(path, "r+b") as f:
	mbr = bytearray(f.read(512))
	struct.pack_into("<I", mbr, 446 + 16 + 12, newLength)
	if sys.argv[3] == "1":
		mbr[446 + 32:446 + 48] = bytes(16)
	f.seek(0)
	f.write(mbr)
EOF
sync
end=$(( (start + newLength) * 512 ))
result=$(shell "resizefs $newBytes" "sync")
echo "$result" | grep -q "successfully resized" \
	|| { echo "c2-grow: resizefs failed:"; echo "$result"; exit 1; }
check "after"
shell "info" | grep -E "total size|free size" | sed 's/^/  /'

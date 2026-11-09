#!/bin/sh
# Undo the last pkgman update of a Haiku microSD (tools/c2-sd-image.sh) in
# this laptop's card reader, for a board that no longer boots after it: the
# packages of the newest backup state (system/packages/administrative/
# state_*) go back into system/packages in place of their newer versions,
# and the activation file lists them again. Other files are not touched.
# Without --apply it only shows what it would do.
# Usage: sudo tools/c2-sd-revert.sh <device, e.g. /dev/sdb> [--apply]
#   (an image file instead of the device works too, for testing)
set -e
root=$(cd "$(dirname "$0")/.." && pwd)
bfs="$root/generated.arm64/objects/linux/x86_64/release/tools/bfs_shell/bfs_shell"
device="$1"
apply=0
[ "$2" = "--apply" ] && apply=1

if [ -f "$device" ]; then
	: # an image file (for testing)
elif [ -b "$device" ]; then
	name=$(basename "$device")
	if [ "$(cat /sys/block/$name/removable 2>/dev/null)" != 1 ] \
		&& ! udevadm info -q property -n "$device" | grep -q '^ID_BUS=usb'; then
		echo "c2-sd-revert: $device is neither removable nor on USB, not" \
			"touching it"
		exit 1
	fi
else
	echo "c2-sd-revert: $device is neither a block device nor an image file"
	exit 1
fi

# the BFS partition (MBR type 0xeb)
set -- $(sfdisk -d "$device" \
	| awk -F'[=,]' '/type=eb/ { gsub(/ /, "", $2); gsub(/ /, "", $4); print $2, $4; exit }')
start=$1
[ -n "$start" ] || { echo "c2-sd-revert: no BFS partition on $device"; exit 1; }
offset=$((start * 512))
# the partition's end has to be given: a block device's stat() size is 0
end=$(( (start + $2) * 512 ))

shell() {
	printf '%s\n' "$@" "quit" \
		| "$bfs" --start-offset "$offset" --end-offset "$end" "$device" 2>&1 \
		| grep -v '^bfs: mounted\|^fssh:/> *$'
}
names() {
	# file names from bfs_shell "ls -l" lines
	sed -n 's/^.* \([^ ]*\.hpkg\)$/\1/p'
}

admin=/myfs/system/packages/administrative
state=$(shell "ls $admin" | tr ' ' '\n' | grep '^state_' | sort | tail -1)
[ -n "$state" ] || { echo "c2-sd-revert: no backup state on the card"; exit 1; }
old=$(shell "ls -l $admin/$state" | names)
current=$(shell "ls -l /myfs/system/packages" | names)
[ -n "$old" ] || { echo "c2-sd-revert: $state holds no packages"; exit 1; }

commands=""
work=$(mktemp -d)
shell "cat $admin/activated-packages" | sed 's/^fssh:\/> *//' | grep '\.hpkg$' \
	> "$work/activated"
cp "$work/activated" "$work/activated.new"
echo "c2-sd-revert: BFS at sector $start, newest backup state $state"
for package in $old; do
	base=${package%%-*}
	for newer in $current; do
		[ "${newer%%-*}" = "$base" ] || continue
		echo "  $newer -> $package"
		commands="$commands
rm /myfs/system/packages/$newer"
		sed -i "s/^$newer\$/$package/" "$work/activated.new"
	done
	commands="$commands
mv $admin/$state/$package /myfs/system/packages/$package"
	grep -qx "$package" "$work/activated.new" || echo "$package" >> "$work/activated.new"
done
diff "$work/activated" "$work/activated.new" | sed 's/^/  activation: /' || true

if [ $apply = 1 ]; then
	shell "$commands" \
		"rm $admin/activated-packages" \
		"cp :$work/activated.new $admin/activated-packages" \
		"rm -r $admin/$state" \
		"sync" > /dev/null
	echo "c2-sd-revert: done; now:"
	shell "ls -l /myfs/system/packages" | names | sed 's/^/  /'
else
	echo "c2-sd-revert: dry run, nothing changed (--apply to do it)"
fi
rm -rf "$work"

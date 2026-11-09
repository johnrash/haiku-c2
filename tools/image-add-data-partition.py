#!/usr/bin/env python3
"""Add a third MBR partition to a Haiku MMC image: "Data", from 1 GiB to the
end of the C2's eMMC. The image itself only covers the first few hundred
MiB, so flashing it rewrites the partition table but not the data partition;
files there survive reflashes. Haiku formats it (BFS, "Data") on first use,
see tools/guest/c2-system.sh.

Usage: image-add-data-partition.py <image> [<eMMC sectors>]
"""
import struct
import sys

EMMC_SECTORS = 244629504	# 116.6 GiB eMMC on this board (measured)
DATA_START = 2097152		# 1 GiB: room for the image to grow
TYPE_BEOS = 0xeb


def main():
	if len(sys.argv) < 2:
		print(__doc__.strip(), file=sys.stderr)
		return 1
	path = sys.argv[1]
	total = int(sys.argv[2]) if len(sys.argv) > 2 else EMMC_SECTORS
	with open(path, "r+b") as f:
		mbr = bytearray(f.read(512))
		if mbr[510:512] != b"\x55\xaa":
			print("image-add-data-partition: no MBR signature", file=sys.stderr)
			return 1
		entries = [mbr[446 + 16 * i:462 + 16 * i] for i in range(4)]
		for i, entry in enumerate(entries[:2]):
			start, count = struct.unpack("<II", entry[8:16])
			if entry[4] == 0 or start + count > DATA_START:
				print(f"image-add-data-partition: partition {i + 1} is missing or"
					" reaches past 1 GiB", file=sys.stderr)
				return 1
		if entries[2][4] not in (0, TYPE_BEOS) or entries[3][4] != 0:
			print("image-add-data-partition: entries 3/4 are in use", file=sys.stderr)
			return 1
		# LBA only: CHS fields set to the "beyond 8 GiB" marker
		entry = bytes([0x00, 0xfe, 0xff, 0xff, TYPE_BEOS, 0xfe, 0xff, 0xff]) \
			+ struct.pack("<II", DATA_START, total - DATA_START)
		mbr[478:494] = entry
		f.seek(0)
		f.write(mbr)
	print(f"image-add-data-partition: partition 3 at sector {DATA_START},"
		f" {total - DATA_START} sectors ({(total - DATA_START) / 2 / 1024 / 1024:.1f} GiB)")
	return 0


if __name__ == "__main__":
	sys.exit(main())

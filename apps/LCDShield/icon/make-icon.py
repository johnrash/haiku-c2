#!/usr/bin/env python3
# Copyright 2026, haiku-odroid-c2 port authors.
# Distributed under the terms of the MIT License.
#
# make-icon.py: LCD Shield's icon, as HVIF (Haiku Vector Icon Format, the
# layout Haiku's FlatIconImporter reads): the 16x2 display as an isometric
# slab in blue, its front a darker bezel round the screen with two lines of
# white dot-matrix text, as the app's panel draws it.
#   make-icon.py --rdef icon.rdef     the rdef "vector_icon" resource
#   make-icon.py --hvif icon.hvif     the data alone
import struct
import sys

# --- encoding (src/libs/icon/flat_icon/FlatIconFormat.cpp) ----------------

def coord(value):
	if value == int(value) and -32 <= value <= 95:
		return bytes([int(value) + 32])
	value = max(-128.0, min(192.0, value))
	raw = int((value + 128.0) * 102.0) | 0x8000
	return bytes([raw >> 8, raw & 0xff])


def float24(value):
	if value == 0:
		return b"\0\0\0"
	bits = struct.unpack("<I", struct.pack("<f", value))[0]
	sign = bits >> 31
	exponent = ((bits >> 23) & 0xff) - 127
	mantissa = bits & 0x7fffff
	if exponent >= 32 or exponent < -32:
		return b"\0\0\0"
	short = (sign << 23) | ((exponent + 32) << 17) | (mantissa >> 6)
	return bytes([short >> 16, (short >> 8) & 0xff, short & 0xff])


def matrix(m):
	return b"".join(float24(v) for v in m)


class Icon:
	def __init__(self):
		self.styles = []
		self.paths = []
		self.shapes = []

	def solid(self, color):
		self.styles.append(b"\x01" + bytes(color))
		return len(self.styles) - 1

	def gradient(self, kind, stops, transform=None):
		# kind: 0 linear, 1 circular; gradient space runs -64..64
		data = bytes([2, kind, 2 if transform else 0, len(stops)])
		if transform:
			data += matrix(transform)
		for offset, color in stops:
			data += bytes([offset]) + bytes(color)
		self.styles.append(data)
		return len(self.styles) - 1

	def path(self, points):
		# points: (point, in, out) triples, closed
		data = bytes([2, len(points)])
		for point, pin, pout in points:
			for x, y in (point, pin, pout):
				data += coord(round(x, 2)) + coord(round(y, 2))
		self.paths.append(data)
		return len(self.paths) - 1

	def polygon(self, corners):
		return self.path([(p, p, p) for p in corners])

	def shape(self, style, paths, transform=None):
		data = bytes([10, style, len(paths)]) + bytes(paths)
		if transform:
			data += bytes([2]) + matrix(transform)
		else:
			data += bytes([0])
		self.shapes.append(data)

	def data(self):
		out = b"ncif"
		for items in (self.styles, self.paths, self.shapes):
			out += bytes([len(items)]) + b"".join(items)
		return out


K = 0.5523	# cubic handle length for a quarter circle, per radius


def rounded_rect(x0, y0, x1, y1, r):
	k = K * r
	return [
		((x0 + r, y0), (x0 + r - k, y0), (x0 + r, y0)),
		((x1 - r, y0), (x1 - r, y0), (x1 - r + k, y0)),
		((x1, y0 + r), (x1, y0 + r - k), (x1, y0 + r)),
		((x1, y1 - r), (x1, y1 - r), (x1, y1 - r + k)),
		((x1 - r, y1), (x1 - r + k, y1), (x1 - r, y1)),
		((x0 + r, y1), (x0 + r, y1), (x0 + r - k, y1)),
		((x0, y1 - r), (x0, y1 - r + k), (x0, y1 - r)),
		((x0, y0 + r), (x0, y0 + r), (x0, y0 + r - k)),
	]


def linear(x0, y0, x1, y1):
	"""Gradient matrix: gradient space -64..64 on x onto (x0,y0)-(x1,y1)."""
	import math
	dx, dy = x1 - x0, y1 - y0
	scale = math.hypot(dx, dy) / 128.0
	angle = math.atan2(dy, dx)
	c, s = math.cos(angle) * scale, math.sin(angle) * scale
	return [c, s, -s, c, (x0 + x1) / 2, (y0 + y1) / 2]


def radial(cx, cy, rx, ry):
	return [rx / 64.0, 0, 0, ry / 64.0, cx, cy]


# 3x5 dot-matrix glyphs, rows top to bottom
GLYPHS = {
	"L": ["1..", "1..", "1..", "1..", "111"],
	"C": [".11", "1..", "1..", "1..", ".11"],
	"D": ["11.", "1.1", "1.1", "1.1", "11."],
	"1": [".1.", "11.", ".1.", ".1.", "111"],
	"6": [".11", "1..", "111", "1.1", "111"],
	"x": ["...", "1.1", ".1.", "1.1", "..."],
	"2": ["11.", "..1", ".1.", "1..", "111"],
}


# --- the icon ---------------------------------------------------------------

# the slab: W wide (along EX, to the right and down), H high, D deep (along
# EZ, to the right and up); the front's top left corner at ORIGIN
W, H, D = 46.0, 26.0, 7.0
EX = (0.866, 0.5)
EZ = (0.866, -0.5)
ORIGIN = (5.0, 12.0)


def at(u, v, z=0.0):
	"""The front's point u along, v down, moved z back."""
	return (ORIGIN[0] + u * EX[0] + z * EZ[0],
		ORIGIN[1] + u * EX[1] + v + z * EZ[1])


# the front face's own coordinates (u right, v down) onto the screen
FRONT = [EX[0], EX[1], 0.0, 1.0, ORIGIN[0], ORIGIN[1]]


def lcd_icon():
	icon = Icon()

	# a soft shadow under the slab
	shadow = icon.gradient(1, [(0, (0, 0, 0, 90)), (255, (0, 0, 0, 0))],
		radial(30, 57, 26, 6))
	icon.shape(shadow, [icon.polygon([(4, 52), (56, 52), (56, 63),
		(4, 63)])])

	# the top and the right side, in the bezel's blue, lit from above
	top = icon.gradient(0, [(0, (128, 176, 255, 255)),
		(255, (70, 120, 220, 255))], linear(10, 8, 46, 34))
	icon.shape(top, [icon.polygon([at(0, 0), at(W, 0), at(W, 0, D),
		at(0, 0, D)])])
	side = icon.gradient(0, [(0, (22, 56, 140, 255)),
		(255, (12, 32, 92, 255))], linear(46, 31, 52, 60))
	icon.shape(side, [icon.polygon([at(W, 0), at(W, 0, D), at(W, H, D),
		at(W, H)])])

	# the front: the bezel, then the screen, both drawn flat and slanted
	bezel = icon.gradient(0, [(0, (40, 92, 200, 255)),
		(255, (24, 62, 152, 255))], linear(0, 0, 0, H))
	icon.shape(bezel, [icon.path(rounded_rect(0, 0, W, H, 2.5))], FRONT)
	screen = icon.gradient(0, [(0, (70, 140, 255, 255)),
		(255, (34, 96, 228, 255))], linear(0, 2.5, 0, 23.5))
	icon.shape(screen, [icon.path(rounded_rect(2.5, 2.5, W - 2.5, H - 2.5,
		1))], FRONT)

	# two lines of text: dot-matrix blocks, a run of lit dots a rectangle
	block = 1.8
	paths = []
	for line, (text, top_v) in enumerate((("LCD", 3.4), ("16x2", 13.6))):
		u0 = 9.5
		for index, char in enumerate(text):
			left = u0 + index * block * 4
			for row, bits in enumerate(GLYPHS[char]):
				column = 0
				while column < 3:
					if bits[column] != "1":
						column += 1
						continue
					end = column
					while end < 3 and bits[end] == "1":
						end += 1
					x0 = left + column * block
					y0 = top_v + row * block
					paths.append(icon.polygon([(x0, y0),
						(x0 + (end - column) * block, y0),
						(x0 + (end - column) * block, y0 + block),
						(x0, y0 + block)]))
					column = end
	text = icon.solid((236, 246, 255, 255))
	icon.shape(text, paths, FRONT)

	# the screen's sheen, across its top
	sheen = icon.gradient(0, [(0, (255, 255, 255, 70)),
		(255, (255, 255, 255, 0))], linear(0, 3, 0, 11))
	icon.shape(sheen, [icon.path(rounded_rect(3.6, 3.6, W - 3.6, 11, 1))],
		FRONT)
	return icon.data()


def main():
	data = lcd_icon()
	if len(sys.argv) == 3 and sys.argv[1] == "--hvif":
		with open(sys.argv[2], "wb") as f:
			f.write(data)
		return
	hexdata = data.hex().upper()
	lines = ['\t$"%s"' % hexdata[i:i + 64] for i in range(0, len(hexdata), 64)]
	text = "resource vector_icon {\n" + "\n".join(lines) + "\n};\n"
	if len(sys.argv) == 3 and sys.argv[1] == "--rdef":
		with open(sys.argv[2], "w") as f:
			f.write(text)
		return
	sys.stdout.write(text)


if __name__ == "__main__":
	main()

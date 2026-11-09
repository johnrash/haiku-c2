#!/usr/bin/env python3
# Copyright 2026, haiku-odroid-c2 port authors.
# Distributed under the terms of the MIT License.
#
# remote-control-icon.py: the Remote Control preferences' icon, as HVIF
# (Haiku Vector Icon Format, the layout Haiku's FlatIconImporter reads),
# printed as an rdef "vector_icon" resource. Haiku's icon set has no remote
# control, so this draws one in its style: a dark remote, tilted, with a red
# power button, a direction pad and small buttons, sending amber waves.
#   remote-control-icon.py > icon.rdef      (or --hvif <file> for the data)
import math
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


def rgba(color):
	return bytes(color)


class Icon:
	def __init__(self):
		self.styles = []
		self.paths = []
		self.shapes = []

	def solid(self, color):
		self.styles.append(b"\x01" + rgba(color))
		return len(self.styles) - 1

	def gradient(self, kind, stops, transform=None):
		# kind: 0 linear, 1 circular; gradient space runs -64..64
		data = bytes([2, kind, 2 if transform else 0, len(stops)])
		if transform:
			data += matrix(transform)
		for offset, color in stops:
			data += bytes([offset]) + rgba(color)
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


# --- geometry -------------------------------------------------------------

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


def circle(cx, cy, r):
	k = K * r
	return [
		((cx, cy - r), (cx - k, cy - r), (cx + k, cy - r)),
		((cx + r, cy), (cx + r, cy - k), (cx + r, cy + k)),
		((cx, cy + r), (cx + k, cy + r), (cx - k, cy + r)),
		((cx - r, cy), (cx - r, cy + k), (cx - r, cy - k)),
	]


def arc_band(cx, cy, outer, inner, a0, a1):
	"""A ring sector from angle a0 to a1 (radians, screen coordinates)."""
	span = a1 - a0
	ho = 4.0 / 3.0 * math.tan(span / 4) * outer
	hi = 4.0 / 3.0 * math.tan(span / 4) * inner
	def at(r, a):
		return (cx + r * math.cos(a), cy + r * math.sin(a))
	o0, o1, i1, i0 = at(outer, a0), at(outer, a1), at(inner, a1), at(inner, a0)
	return [
		(o0, o0, (o0[0] - ho * math.sin(a0), o0[1] + ho * math.cos(a0))),
		(o1, (o1[0] + ho * math.sin(a1), o1[1] - ho * math.cos(a1)), o1),
		(i1, i1, (i1[0] + hi * math.sin(a1), i1[1] - hi * math.cos(a1))),
		(i0, (i0[0] - hi * math.sin(a0), i0[1] + hi * math.cos(a0)), i0),
	]


def linear(x0, y0, x1, y1):
	"""Gradient matrix: gradient space -64..64 on x onto (x0,y0)-(x1,y1)."""
	dx, dy = x1 - x0, y1 - y0
	scale = math.hypot(dx, dy) / 128.0
	angle = math.atan2(dy, dx)
	c, s = math.cos(angle) * scale, math.sin(angle) * scale
	return [c, s, -s, c, (x0 + x1) / 2, (y0 + y1) / 2]


def radial(cx, cy, rx, ry):
	return [rx / 64.0, 0, 0, ry / 64.0, cx, cy]


# --- the icon ---------------------------------------------------------------

def remote_control():
	icon = Icon()
	# the remote is drawn upright around (32, 34), then turned 30 degrees
	cx, cy, turn = 32.0, 34.0, math.radians(30)
	c, s = math.cos(turn), math.sin(turn)
	tilt = [c, s, -s, c, cx - cx * c + cy * s, cy - cx * s - cy * c]

	# shadow on the ground
	shadow = icon.gradient(1, [(0, (0, 0, 0, 110)), (255, (0, 0, 0, 0))],
		radial(30, 57.5, 22, 5))
	icon.shape(shadow, [icon.path(rounded_rect(8, 52.5, 52, 62.5, 5))])

	# body: a dark outline, the casing lit from the top left
	outline = icon.solid((24, 24, 30, 255))
	icon.shape(outline, [icon.path(rounded_rect(23, 9.5, 41, 58.5, 6))], tilt)
	casing = icon.gradient(0, [(0, (128, 130, 142, 255)),
		(110, (78, 80, 92, 255)), (255, (40, 41, 50, 255))],
		linear(24, 12, 40, 56))
	icon.shape(casing, [icon.path(rounded_rect(24, 10.5, 40, 57.5, 5))],
		tilt)
	# the top's sheen
	sheen = icon.gradient(0, [(0, (255, 255, 255, 90)),
		(255, (255, 255, 255, 0))], linear(32, 11, 32, 24))
	icon.shape(sheen, [icon.path(rounded_rect(25.2, 11.6, 38.8, 24, 4))],
		tilt)

	# infrared window
	window = icon.gradient(0, [(0, (150, 20, 30, 255)),
		(255, (40, 0, 8, 255))], linear(32, 12.6, 32, 15.2))
	icon.shape(window, [icon.path(rounded_rect(28.6, 12.6, 35.4, 15.2, 1.2))],
		tilt)

	# power button
	power = icon.gradient(1, [(0, (255, 140, 120, 255)),
		(255, (190, 20, 20, 255))], radial(31.2, 19.6, 3.2, 3.2))
	icon.shape(power, [icon.path(circle(32, 20.2, 2.4))], tilt)

	# direction pad and its OK button
	ring = icon.gradient(0, [(0, (170, 200, 240, 255)),
		(255, (40, 80, 150, 255))], linear(27, 25, 37, 37))
	icon.shape(ring, [icon.path(circle(32, 31, 5.6))], tilt)
	ok = icon.gradient(0, [(0, (60, 100, 170, 255)),
		(255, (200, 220, 250, 255))], linear(30, 29, 34, 33))
	icon.shape(ok, [icon.path(circle(32, 31, 2.3))], tilt)

	# two rows of small buttons
	keys = icon.gradient(0, [(0, (235, 236, 240, 255)),
		(255, (150, 152, 162, 255))], linear(32, 40, 32, 52))
	paths = []
	for y in (40.5, 46.5):
		for x in (27.3, 32, 36.7):
			paths.append(icon.path(rounded_rect(x - 1.8, y, x + 1.8,
				y + 2.8, 0.9)))
	icon.shape(keys, paths, tilt)

	# waves from the infrared window, along the remote's turned axis
	ex = cx + (32 - cx) * c - (13.5 - cy) * s
	ey = cy + (32 - cx) * s + (13.5 - cy) * c
	axis = math.atan2(-c, s)
	for radius, alpha in ((5.5, 255), (9.5, 190), (13.5, 120)):
		wave = icon.solid((255, 176, 0, alpha))
		icon.shape(wave, [icon.path(arc_band(ex, ey, radius + 1.1, radius,
			axis - math.radians(38), axis + math.radians(38)))])
	return icon.data()


def main():
	data = remote_control()
	if len(sys.argv) == 3 and sys.argv[1] == "--hvif":
		with open(sys.argv[2], "wb") as f:
			f.write(data)
		return
	hexdata = data.hex().upper()
	lines = ['\t$"%s"' % hexdata[i:i + 64] for i in range(0, len(hexdata), 64)]
	print("resource vector_icon array {")
	print("\n".join(lines))
	print("};")


if __name__ == "__main__":
	main()

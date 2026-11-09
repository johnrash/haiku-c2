#!/usr/bin/env python3
# Copyright 2026, haiku-odroid-c2 port authors.
# Distributed under the terms of the MIT License.
"""Haiku vector icon (HVIF, "flat icon") reading and writing.

Enough of the format for simple icons: solid and gradient styles (gradients
read only), paths with or without curves, path source shapes with an
optional transform or translation and a level-of-detail range. Written from
Haiku's src/libs/icon/flat_icon reader.

    hvif.py svg <icon.hvif|icons.rdef:<resource>> <out.svg>

renders an icon (or a vector_icon resource in an .rdef file) to SVG, with
gradients shown as their first color, for checking what was encoded.
"""
import re
import struct
import sys

MAGIC = b"ncif"

STYLE_SOLID_COLOR = 1
STYLE_GRADIENT = 2
STYLE_SOLID_COLOR_NO_ALPHA = 3
STYLE_SOLID_GRAY = 4
STYLE_SOLID_GRAY_NO_ALPHA = 5

GRADIENT_FLAG_TRANSFORM = 1 << 1
GRADIENT_FLAG_NO_ALPHA = 1 << 2
GRADIENT_FLAG_GRAYS = 1 << 4

PATH_FLAG_CLOSED = 1 << 1
PATH_FLAG_USES_COMMANDS = 1 << 2
PATH_FLAG_NO_CURVES = 1 << 3

SHAPE_TYPE_PATH_SOURCE = 10
SHAPE_FLAG_TRANSFORM = 1 << 1
SHAPE_FLAG_HINTING = 1 << 2
SHAPE_FLAG_LOD_SCALE = 1 << 3
SHAPE_FLAG_HAS_TRANSFORMERS = 1 << 4
SHAPE_FLAG_TRANSLATION = 1 << 5


class Path:
	"""A closed or open path; points are (point, in, out) control triples."""
	def __init__(self, points, closed=True):
		self.points = points
		self.closed = closed


class Shape:
	def __init__(self, style, paths, hinting=False, lod=None):
		self.style = style		# index
		self.paths = paths		# indices
		self.hinting = hinting
		self.lod = lod			# (min, max) scale, or None
		self.matrix = None		# 6 affine values, read only


# #pragma mark - writing


def _coord(value):
	"""One coordinate: a byte for whole numbers in -32..95, else two bytes
	with 1/102 unit steps."""
	if value == int(value) and -32 <= value <= 95:
		return bytes([int(value) + 32])
	scaled = int(round((value + 128.0) * 102.0))
	if not 0 <= scaled <= 0x7fff:
		raise ValueError("coordinate %r out of range" % value)
	return bytes([0x80 | (scaled >> 8), scaled & 0xff])


def quantize(value):
	"""What a coordinate reads back as."""
	if value == int(value) and -32 <= value <= 95:
		return float(value)
	return int(round((value + 128.0) * 102.0)) / 102.0 - 128.0


def encode(colors, paths, shapes):
	"""colors: (r, g, b, a) tuples; paths: Path list; shapes: Shape list."""
	out = bytearray(MAGIC)
	out.append(len(colors))
	for r, g, b, a in colors:
		if a == 255:
			out += bytes([STYLE_SOLID_COLOR_NO_ALPHA, r, g, b])
		else:
			out += bytes([STYLE_SOLID_COLOR, r, g, b, a])
	out.append(len(paths))
	for path in paths:
		if len(path.points) > 255:
			raise ValueError("path with %d points" % len(path.points))
		straight = all(p == i == o for p, i, o in path.points)
		flags = PATH_FLAG_CLOSED if path.closed else 0
		if straight:
			flags |= PATH_FLAG_NO_CURVES
		out += bytes([flags, len(path.points)])
		for point, pointIn, pointOut in path.points:
			coords = point if straight else point + pointIn + pointOut
			for value in coords:
				out += _coord(value)
	out.append(len(shapes))
	for shape in shapes:
		flags = 0
		if shape.hinting:
			flags |= SHAPE_FLAG_HINTING
		if shape.lod is not None:
			flags |= SHAPE_FLAG_LOD_SCALE
		out += bytes([SHAPE_TYPE_PATH_SOURCE, shape.style, len(shape.paths)])
		out += bytes(shape.paths)
		out.append(flags)
		if shape.lod is not None:
			out += bytes([int(round(s * 63.75)) for s in shape.lod])
	return bytes(out)


# #pragma mark - reading


class _Reader:
	def __init__(self, data):
		self.data = data
		self.pos = 0

	def u8(self):
		self.pos += 1
		return self.data[self.pos - 1]

	def take(self, count):
		self.pos += count
		return self.data[self.pos - count:self.pos]

	def coord(self):
		value = self.u8()
		if value & 0x80:
			return ((value & 0x7f) << 8 | self.u8()) / 102.0 - 128.0
		return value - 32.0

	def point(self):
		return (self.coord(), self.coord())

	def float24(self):
		b = self.take(3)
		value = b[0] << 16 | b[1] << 8 | b[2]
		if value == 0:
			return 0.0
		sign = (value & 0x800000) >> 23
		exponent = ((value & 0x7e0000) >> 17) - 32
		mantissa = (value & 0x01ffff) << 6
		bits = sign << 31 | ((exponent + 127) & 0xff) << 23 | mantissa
		return struct.unpack("<f", struct.pack("<I", bits))[0]


def _color(reader, alpha, gray):
	if gray:
		red = reader.u8()
		return (red, red, red, reader.u8() if alpha else 255)
	r, g, b = reader.u8(), reader.u8(), reader.u8()
	return (r, g, b, reader.u8() if alpha else 255)


def decode(data):
	"""Returns (colors, paths, shapes); a gradient gives its first color."""
	reader = _Reader(data)
	if reader.take(4) != MAGIC:
		raise ValueError("not a vector icon")
	colors = []
	for _ in range(reader.u8()):
		kind = reader.u8()
		if kind == STYLE_SOLID_COLOR:
			colors.append(_color(reader, True, False))
		elif kind == STYLE_SOLID_COLOR_NO_ALPHA:
			colors.append(_color(reader, False, False))
		elif kind == STYLE_SOLID_GRAY:
			colors.append(_color(reader, True, True))
		elif kind == STYLE_SOLID_GRAY_NO_ALPHA:
			colors.append(_color(reader, False, True))
		elif kind == STYLE_GRADIENT:
			reader.u8()		# type
			flags = reader.u8()
			stops = reader.u8()
			if flags & GRADIENT_FLAG_TRANSFORM:
				for _ in range(6):
					reader.float24()
			first = None
			for _ in range(stops):
				reader.u8()		# offset
				color = _color(reader, not flags & GRADIENT_FLAG_NO_ALPHA,
					flags & GRADIENT_FLAG_GRAYS)
				first = first or color
			colors.append(first)
		else:
			length = struct.unpack("<H", reader.take(2))[0]
			reader.take(length)
	paths = []
	for _ in range(reader.u8()):
		flags = reader.u8()
		count = reader.u8()
		points = []
		if flags & PATH_FLAG_NO_CURVES:
			for _ in range(count):
				p = reader.point()
				points.append((p, p, p))
		elif flags & PATH_FLAG_USES_COMMANDS:
			commands = reader.take((count + 3) // 4)
			last = (0.0, 0.0)
			for i in range(count):
				command = commands[i // 4] >> (i % 4 * 2) & 3
				if command == 0:
					p = (reader.coord(), last[1])
				elif command == 1:
					p = (last[0], reader.coord())
				elif command == 2:
					p = reader.point()
				if command == 3:
					p = reader.point()
					points.append((p, reader.point(), reader.point()))
				else:
					points.append((p, p, p))
				last = p
		else:
			for _ in range(count):
				points.append((reader.point(), reader.point(),
					reader.point()))
		paths.append(Path(points, bool(flags & PATH_FLAG_CLOSED)))
	shapes = []
	for _ in range(reader.u8()):
		if reader.u8() != SHAPE_TYPE_PATH_SOURCE:
			raise ValueError("unsupported shape type")
		style = reader.u8()
		indices = list(reader.take(reader.u8()))
		flags = reader.u8()
		shape = Shape(style, indices, bool(flags & SHAPE_FLAG_HINTING))
		if flags & SHAPE_FLAG_TRANSFORM:
			shape.matrix = [reader.float24() for _ in range(6)]
		elif flags & SHAPE_FLAG_TRANSLATION:
			x, y = reader.point()
			shape.matrix = [1, 0, 0, 1, x, y]
		if flags & SHAPE_FLAG_LOD_SCALE:
			shape.lod = (reader.u8() / 63.75, reader.u8() / 63.75)
		if flags & SHAPE_FLAG_HAS_TRANSFORMERS:
			raise ValueError("shape transformers are not supported")
		shapes.append(shape)
	return colors, paths, shapes


# #pragma mark - previews


def path_data(path, matrix=None):
	def t(p):
		if matrix is None:
			return p
		a, b, c, d, e, f = matrix
		return (a * p[0] + c * p[1] + e, b * p[0] + d * p[1] + f)

	points = path.points
	out = ["M%.3f,%.3f" % t(points[0][0])]
	count = len(points) if path.closed else len(points) - 1
	for i in range(count):
		start = points[i]
		end = points[(i + 1) % len(points)]
		out.append("C%.3f,%.3f %.3f,%.3f %.3f,%.3f"
			% (t(start[2]) + t(end[1]) + t(end[0])))
	if path.closed:
		out.append("Z")
	return " ".join(out)


def to_svg(colors, paths, shapes, scale=1.0):
	"""The icon on its 64 unit canvas; shapes with a level-of-detail range
	are left out unless scale (pixels per unit) is within it."""
	size = 64 * scale
	out = ['<svg xmlns="http://www.w3.org/2000/svg" width="%g" height="%g" '
		'viewBox="0 0 64 64">' % (size, size)]
	for shape in shapes:
		if shape.lod and not shape.lod[0] <= scale <= shape.lod[1]:
			continue
		r, g, b, a = colors[shape.style]
		data = " ".join(path_data(paths[i], shape.matrix)
			for i in shape.paths)
		out.append('<path fill="#%02x%02x%02x" fill-opacity="%.3f" '
			'fill-rule="nonzero" d="%s"/>' % (r, g, b, a / 255.0, data))
	out.append("</svg>")
	return "\n".join(out)


def read_rdef(path, resource):
	"""The bytes of a vector_icon resource in an .rdef file."""
	text = open(path).read()
	match = re.search(r"resource\(%s\)\s*vector_icon\s*\{(.*?)\};"
		% re.escape(resource), text, re.S)
	if match is None:
		raise ValueError("no vector_icon %s in %s" % (resource, path))
	return bytes.fromhex("".join(re.findall(r'\$"([0-9A-Fa-f]*)"',
		match.group(1))))


def rdef_data(data, indent="\t", width=32):
	"""Icon bytes as .rdef hex lines."""
	text = data.hex().upper()
	return "\n".join(
		'%s$"%s"' % (indent, text[i:i + width * 2])
		for i in range(0, len(text), width * 2))


def main():
	if len(sys.argv) != 4 or sys.argv[1] != "svg":
		print(__doc__.strip().split("\n\n")[1], file=sys.stderr)
		sys.exit(1)
	source = sys.argv[2]
	if ".rdef:" in source:
		data = read_rdef(*source.split(":", 1))
	else:
		data = open(source, "rb").read()
	with open(sys.argv[3], "w") as out:
		out.write(to_svg(*decode(data)))


if __name__ == "__main__":
	main()

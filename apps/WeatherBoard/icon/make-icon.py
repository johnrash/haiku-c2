#!/usr/bin/env python3
# Copyright 2026, haiku-odroid-c2 port authors.
# Distributed under the terms of the MIT License.
#
# make-icon.py [--preview <png> [<size>...]] [--rdef <file>]
# Weather Board's application icon (milestone M66 (docs/milestones.md)): a cloud with five rain
# drops, drawn in thickness as in a reference picture, as a
# Haiku vector icon (HVIF, the "flat icon" format of
# haiku/src/libs/icon/flat_icon, which this writes as its importer reads it).
#
# The outlines were traced over a reference picture (not included, 1254
# pixels square): the cloud's front as smooth curves between the notches of
# its bumps, and the drops as teardrops. The thickness is the front outline
# swept toward the upper right: where the outline faces the light (LIGHT) the
# swept band is light, elsewhere it is in shade, and where the outline faces
# away from the sweep the band is hidden behind the front. Each segment's
# band is a path of its own, a run of them one shape; the fronts are drawn
# last.
#
# --rdef writes the "resource vector_icon" for WeatherBoard.rdef;
# --preview reads the HVIF bytes back (with a reader that follows the
# importer too) and renders them, at the given sizes, side by side.

import math
import struct
import sys

SOURCE = 1254.0			# the picture's size; the icon is 64 units
SCALE = 64.0 / SOURCE


# #pragma mark - geometry (picture pixels)


def lerp(a, b, t):
	return (a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t)


def add(a, b):
	return (a[0] + b[0], a[1] + b[1])


def sub(a, b):
	return (a[0] - b[0], a[1] - b[1])


def mul(a, s):
	return (a[0] * s, a[1] * s)


def smooth_contour(points, corners, tension=1.0 / 6):
	"""Closed Catmull-Rom curve through points as cubic segments
	(p0, c0, c1, p1); at the indices in corners the curve may bend."""
	n = len(points)
	tangents = []
	for i in range(n):
		if i in corners:
			tangents.append(None)
		else:
			tangents.append(sub(points[(i + 1) % n], points[i - 1]))
	segments = []
	for i in range(n):
		p0, p1 = points[i], points[(i + 1) % n]
		t0, t1 = tangents[i], tangents[(i + 1) % n]
		chord = sub(p1, p0)
		c0 = add(p0, mul(t0, tension)) if t0 else add(p0, mul(chord, 0.3))
		c1 = sub(p1, mul(t1, tension)) if t1 else sub(p1, mul(chord, 0.3))
		segments.append((p0, c0, c1, p1))
	return segments


def bezier_point(seg, t):
	p0, c0, c1, p1 = seg
	u = 1 - t
	return (u * u * u * p0[0] + 3 * u * u * t * c0[0] + 3 * u * t * t * c1[0]
			+ t * t * t * p1[0],
		u * u * u * p0[1] + 3 * u * u * t * c0[1] + 3 * u * t * t * c1[1]
			+ t * t * t * p1[1])


def bezier_tangent(seg, t):
	p0, c0, c1, p1 = seg
	u = 1 - t
	return (3 * u * u * (c0[0] - p0[0]) + 6 * u * t * (c1[0] - c0[0])
			+ 3 * t * t * (p1[0] - c1[0]),
		3 * u * u * (c0[1] - p0[1]) + 6 * u * t * (c1[1] - c0[1])
			+ 3 * t * t * (p1[1] - c1[1]))


def split(seg, t):
	p0, c0, c1, p1 = seg
	a, b, c = lerp(p0, c0, t), lerp(c0, c1, t), lerp(c1, p1, t)
	d, e = lerp(a, b, t), lerp(b, c, t)
	f = lerp(d, e, t)
	return (p0, a, d, f), (f, e, c, p1)


def sub_segment(seg, t0, t1):
	if t1 < 1:
		seg = split(seg, t1)[0]
	if t0 > 0:
		seg = split(seg, t0 / t1)[1]
	return seg


def swept_pieces(segments, sweep, steps=64):
	"""The runs of the clockwise outline whose outward normal faces the
	sweep, cut where the normal turns from facing the light (lit) to away
	from it (shade): [(lit, [segments])]."""
	length = math.hypot(*sweep)
	v = (sweep[0] / length, sweep[1] / length)
	length = math.hypot(*LIGHT)
	w = (LIGHT[0] / length, LIGHT[1] / length)

	def state(seg, t):
		tx, ty = bezier_tangent(seg, t)
		norm = math.hypot(tx, ty) or 1
		# clockwise on screen (y down): outward normal is (ty, -tx)
		n = (ty / norm, -tx / norm)
		if n[0] * v[0] + n[1] * v[1] <= 0:
			return None			# faces away: hidden behind the front
		return n[0] * w[0] + n[1] * w[1] > 0

	pieces = []
	for seg in segments:
		previous_t, previous = 0.0, state(seg, 0.0)
		for i in range(1, steps + 1):
			t = i / steps
			s = state(seg, t)
			if s != previous or i == steps:
				# refine the change by bisection
				lo, hi = (i - 1) / steps, t
				if s != previous:
					for _ in range(30):
						mid = (lo + hi) / 2
						if state(seg, mid) == previous:
							lo = mid
						else:
							hi = mid
					end = (lo + hi) / 2
				else:
					end = 1.0
				if previous is not None and end - previous_t > 1e-4:
					piece = sub_segment(seg, previous_t, end)
					if pieces and pieces[-1][0] == previous \
							and pieces[-1][2] is seg_id(seg, previous_t):
						pieces[-1][1].append(piece)
					else:
						pieces.append([previous, [piece], None])
					pieces[-1][2] = seg_id(seg, end)
				previous_t, previous = end, s
	# join runs that continue across segment ends
	joined = []
	for lit, segs, _ in pieces:
		if joined and joined[-1][0] == lit \
				and close(joined[-1][1][-1][3], segs[0][0]):
			joined[-1][1].extend(segs)
		else:
			joined.append([lit, list(segs)])
	if len(joined) > 1 and joined[0][0] == joined[-1][0] \
			and close(joined[-1][1][-1][3], joined[0][1][0][0]):
		joined[0][1] = joined[-1][1] + joined[0][1]
		joined.pop()
	return joined


def seg_id(seg, t):
	return None


def close(a, b):
	return abs(a[0] - b[0]) < 0.01 and abs(a[1] - b[1]) < 0.01


def sweep_path(run, sweep):
	"""The band a run of the outline sweeps: the run, a line along the
	sweep, the run moved back the other way, a line back."""
	back = []
	for p0, c0, c1, p1 in reversed(run):
		back.append((add(p1, sweep), add(c1, sweep), add(c0, sweep),
			add(p0, sweep)))
	return run + [line(run[-1][3], back[0][0])] + back \
		+ [line(back[-1][3], run[0][0])]


def line(a, b):
	return (a, lerp(a, b, 1 / 3.0), lerp(a, b, 2 / 3.0), b)


# the cloud's front, clockwise from its leftmost point; the notches between
# the bumps are corners
CLOUD = [
	(148, 470), (172, 388), (226, 336), (272, 328), (328, 345),
	(338, 272), (388, 203), (460, 178), (532, 186), (602, 218), (660, 272),
	(702, 345), (725, 435),
	(782, 432), (828, 458), (862, 520), (875, 607),
	(932, 620), (972, 660), (996, 712), (1000, 770), (984, 822), (948, 848),
	(895, 860),
	(600, 778), (330, 700), (248, 652), (192, 596), (160, 530),
]
CLOUD_CORNERS = {4, 12, 16, 23}
CLOUD_SWEEP = (135, -100)
LIGHT = (-0.4, -1.0)		# where the light comes from: above

DROP_SWEEP = (36, -24)
DROPS = [		# the round part's centre, its radius, the tilt of the tip
	((322, 878), 63, -8),
	((557, 948), 63, -8),
	((792, 1003), 63, -8),
	((421, 1093), 63, -8),
	((651, 1158), 63, -8),
]


def teardrop(center, radius, tilt):
	"""A teardrop, tip up, clockwise from the tip, as cubic segments."""
	k = 0.5523
	local = [
		((0, -2.0), (0.42, -1.42), (1.0, -0.78), (1, 0)),
		((1, 0), (1, k), (k, 1), (0, 1)),
		((0, 1), (-k, 1), (-1, k), (-1, 0)),
		((-1, 0), (-1.0, -0.78), (-0.42, -1.42), (0, -2.0)),
	]
	a = math.radians(tilt)
	cos, sin = math.cos(a), math.sin(a)

	def place(p):
		x, y = p[0] * radius, p[1] * radius
		return (center[0] + x * cos - y * sin, center[1] + x * sin + y * cos)

	return [tuple(place(p) for p in seg) for seg in local]


# #pragma mark - styles


FRONT = ((131, 160, 193), (107, 144, 184))
LIT = ((178, 201, 229), (162, 189, 221))
SHADE = ((104, 134, 172), (66, 96, 132))
DROP_FRONT = ((122, 160, 201), (104, 149, 193))
DROP_LIT = ((158, 186, 218), (146, 176, 211))
DROP_SHADE = ((92, 125, 164), (66, 100, 138))


def vertical_gradient(colors, top, bottom):
	return ("gradient", colors, (0.0, top), (0.0, bottom))


def build():
	"""[([paths], style)] back to front, in picture pixels. A swept run is
	one shape of one band per segment: a band of a strongly curved run would
	cross itself, which the non-zero fill of Haiku's renderer fills but other
	renderers might not."""
	shapes = []
	cloud = smooth_contour(CLOUD, CLOUD_CORNERS)
	drops = [teardrop(*d) for d in DROPS]

	for lit, run in swept_pieces(cloud, CLOUD_SWEEP):
		style = vertical_gradient(LIT if lit else SHADE, 90, 900)
		shapes.append(([sweep_path([seg], CLOUD_SWEEP) for seg in run], style))
	shapes.append(([cloud], vertical_gradient(FRONT, 180, 860)))

	for d, (center, radius, tilt) in zip(drops, DROPS):
		top, bottom = center[1] - 2.1 * radius, center[1] + radius
		for lit, run in swept_pieces(d, DROP_SWEEP):
			style = vertical_gradient(DROP_LIT if lit else DROP_SHADE, top,
				bottom)
			shapes.append(([sweep_path([seg], DROP_SWEEP) for seg in run],
				style))
		shapes.append(([d], vertical_gradient(DROP_FRONT, top, bottom)))
	return shapes


# #pragma mark - HVIF


def write_coord(value):
	value = max(-128.0, min(192.0, value))
	if int(value * 100.0) == int(value) * 100 and -32.0 <= value <= 95.0:
		return bytes([int(value + 32.0)])
	v = int((value + 128.0) * 102.0) | 0x8000
	return bytes([v >> 8, v & 0xff])


def write_float_24(value):
	if value == 0.0:
		return b"\0\0\0"
	bits = struct.unpack("<I", struct.pack("<f", value))[0]
	sign = bits >> 31
	exponent = ((bits >> 23) & 0xff) - 127
	mantissa = bits & 0x7fffff
	exponent = max(-32, min(31, exponent))
	v = (sign << 23) | ((exponent + 32) << 17) | (mantissa >> 6)
	return bytes([(v >> 16) & 0xff, (v >> 8) & 0xff, v & 0xff])


def hvif(shapes):
	"""Styles, paths and shapes as FlatIconImporter reads them: one style
	per shape, and its paths."""
	units = lambda p: (p[0] * SCALE, p[1] * SCALE)
	out = bytearray(b"ncif")
	out.append(len(shapes))
	for _, style in shapes:
		_, colors, start, end = style
		start, end = units(start), units(end)
		# a linear gradient runs along x from -64 to 64: map that onto
		# the line from start to end
		dx, dy = (end[0] - start[0]) / 128.0, (end[1] - start[1]) / 128.0
		matrix = (dx, dy, -dy, dx, (start[0] + end[0]) / 2,
			(start[1] + end[1]) / 2)
		out += bytes([2, 0, 2 | 4, len(colors)])	# linear, transform,
		for value in matrix:						# no alpha
			out += write_float_24(value)
		for i, color in enumerate(colors):
			out += bytes([int(255 * i / (len(colors) - 1))]) + bytes(color)
	all_paths = [path for paths, _ in shapes for path in paths]
	if len(all_paths) > 255:
		raise SystemExit("make-icon: more than 255 paths")
	out.append(len(all_paths))
	for path in all_paths:
		# closed, with curves: each point with its in and out control
		points = []
		for i, (p0, c0, c1, p1) in enumerate(path):
			previous = path[i - 1]
			points.append((p0, previous[2], c0))
		if len(points) > 255:
			raise SystemExit("make-icon: a path with more than 255 points")
		out += bytes([2, len(points)])
		for point, point_in, point_out in points:
			for p in (point, point_in, point_out):
				x, y = units(p)
				out += write_coord(x) + write_coord(y)
	out.append(len(shapes))
	first = 0
	for i, (paths, _) in enumerate(shapes):
		# path source, style i, its paths, no flags
		out += bytes([10, i, len(paths)])
		out += bytes(range(first, first + len(paths))) + bytes([0])
		first += len(paths)
	return bytes(out)


# #pragma mark - reading it back, preview


def read_hvif(data):
	pos = [0]

	def u8():
		pos[0] += 1
		return data[pos[0] - 1]

	def coord():
		v = u8()
		if v & 128:
			return ((v & 127) << 8 | u8()) / 102.0 - 128.0
		return v - 32.0

	def float24():
		v = u8() << 16 | u8() << 8 | u8()
		if v == 0:
			return 0.0
		sign, exponent, mantissa = v >> 23, ((v >> 17) & 0x3f) - 32, \
			(v & 0x1ffff) << 6
		return struct.unpack("<f", struct.pack("<I", sign << 31
			| (exponent + 127) << 23 | mantissa))[0]

	assert data[:4] == b"ncif"
	pos[0] = 4
	styles = []
	for _ in range(u8()):
		kind = u8()
		assert kind == 2, kind
		gradient_type, flags, count = u8(), u8(), u8()
		matrix = [float24() for _ in range(6)] if flags & 2 else None
		stops = []
		for _ in range(count):
			offset = u8()
			stops.append((offset / 255.0, (u8(), u8(), u8())))
		styles.append((matrix, stops))
	paths = []
	for _ in range(u8()):
		flags, count = u8(), u8()
		points = [tuple(coord() for _ in range(6)) for _ in range(count)]
		paths.append(points)
	shapes = []
	for _ in range(u8()):
		assert u8() == 10
		style, path_count = u8(), u8()
		path_indices = [u8() for _ in range(path_count)]
		flags = u8()
		shapes.append((style, path_indices))
	assert pos[0] == len(data), (pos[0], len(data))
	return styles, paths, shapes


def render(data, size):
	import numpy as np
	from PIL import Image, ImageDraw
	styles, paths, shapes = read_hvif(data)
	factor = 4
	big = size * factor
	scale = big / 64.0
	ys, xs = np.mgrid[0:big, 0:big]
	ux, uy = (xs + 0.5) / scale, (ys + 0.5) / scale
	canvas = np.zeros((big, big, 4), dtype=float)
	for style_index, path_indices in shapes:
		mask_image = Image.new("L", (big, big), 0)
		draw = ImageDraw.Draw(mask_image)
		for index in path_indices:
			points = paths[index]
			polygon = []
			for i in range(len(points)):
				x0, y0, _, _, ox, oy = points[i]
				x1, y1, ix, iy, _, _ = points[(i + 1) % len(points)]
				seg = ((x0, y0), (ox, oy), (ix, iy), (x1, y1))
				for k in range(16):
					px, py = bezier_point(seg, k / 16.0)
					polygon.append((px * scale, py * scale))
			draw.polygon(polygon, fill=255)
		mask = np.array(mask_image, dtype=float) / 255.0
		matrix, stops = styles[style_index]
		sx, shy, shx, sy, tx, ty = matrix
		det = sx * sy - shx * shy
		gx = (sy * (ux - tx) - shx * (uy - ty)) / det
		t = np.clip((gx + 64) / 128.0, 0, 1)
		color = np.zeros((big, big, 3))
		(o0, c0), (o1, c1) = stops[0], stops[-1]
		for channel in range(3):
			color[..., channel] = c0[channel] + (c1[channel] - c0[channel]) * t
		for channel in range(3):
			canvas[..., channel] = canvas[..., channel] * (1 - mask) \
				+ color[..., channel] * mask
		canvas[..., 3] = canvas[..., 3] * (1 - mask) + mask
	image = Image.fromarray(np.uint8(np.clip(canvas, 0, 255) * [1, 1, 1, 255]
		/ [1, 1, 1, 1]), "RGBA") if False else None
	rgba = np.zeros((big, big, 4), dtype=np.uint8)
	rgba[..., :3] = np.clip(canvas[..., :3], 0, 255).astype(np.uint8)
	rgba[..., 3] = (np.clip(canvas[..., 3], 0, 1) * 255).astype(np.uint8)
	return Image.fromarray(rgba, "RGBA").resize((size, size), Image.LANCZOS)


def main():
	data = hvif(build())
	args = sys.argv[1:]
	print("make-icon: %d shapes, %d bytes" % (len(build()), len(data)))
	if "--rdef" in args:
		path = args[args.index("--rdef") + 1]
		lines = ["resource vector_icon {"]
		hexed = data.hex().upper()
		for i in range(0, len(hexed), 64):
			lines.append('\t$"%s"' % hexed[i:i + 64])
		lines.append("};")
		with open(path, "w") as f:
			f.write("\n".join(lines) + "\n")
	if "--preview" in args:
		from PIL import Image
		i = args.index("--preview")
		path = args[i + 1]
		sizes = [int(a) for a in args[i + 2:] if a.isdigit()] or [256, 64, 32, 16]
		images = [render(data, s) for s in sizes]
		width = sum(im.width for im in images) + 10 * (len(images) + 1)
		height = max(im.height for im in images) + 20
		sheet = Image.new("RGBA", (width, height), (255, 255, 255, 255))
		x = 10
		for im in images:
			sheet.alpha_composite(im, (x, 10))
			x += im.width + 10
		sheet.convert("RGB").save(path)


if __name__ == "__main__":
	main()

#!/usr/bin/env python3
# Copyright 2026, haiku-odroid-c2 port authors.
# Distributed under the terms of the MIT License.
#
# neocam-icon.py: NeoCam's icon, as HVIF printed as an rdef "vector_icon"
# resource: a dark webcam on a small stand, its big lens blue glass, a red
# recording light, and green waves for what it sends. The HVIF encoder and
# the shapes come from remote-control-icon.py.
#   neocam-icon.py > icon.rdef      (or --hvif <file> for the data)
import importlib.util
import math
import os
import sys

_spec = importlib.util.spec_from_file_location("remote_control_icon",
	os.path.join(os.path.dirname(os.path.abspath(__file__)),
		"remote-control-icon.py"))
_rc = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_rc)

Icon = _rc.Icon
rounded_rect = _rc.rounded_rect
circle = _rc.circle
arc_band = _rc.arc_band
linear = _rc.linear
radial = _rc.radial


def neocam():
	icon = Icon()

	# shadow on the ground
	shadow = icon.gradient(1, [(0, (0, 0, 0, 110)), (255, (0, 0, 0, 0))],
		radial(30, 58, 22, 4.5))
	icon.shape(shadow, [icon.path(rounded_rect(8, 54, 52, 62, 4))])

	# the stand: a neck and a foot
	metal = icon.gradient(0, [(0, (190, 192, 200, 255)),
		(255, (90, 92, 102, 255))], linear(22, 48, 38, 60))
	icon.shape(metal, [icon.path(rounded_rect(27, 44, 33, 54, 1.5)),
		icon.path(rounded_rect(18, 52, 42, 58, 3))])

	# the body: a dark outline, the casing lit from the top left
	outline = icon.solid((22, 22, 28, 255))
	icon.shape(outline, [icon.path(rounded_rect(5, 13, 55, 47, 13))])
	casing = icon.gradient(0, [(0, (120, 124, 138, 255)),
		(120, (62, 64, 76, 255)), (255, (30, 31, 38, 255))],
		linear(10, 14, 48, 46))
	icon.shape(casing, [icon.path(rounded_rect(6, 14, 54, 46, 12))])
	sheen = icon.gradient(0, [(0, (255, 255, 255, 80)),
		(255, (255, 255, 255, 0))], linear(30, 15, 30, 26))
	icon.shape(sheen, [icon.path(rounded_rect(10, 15.2, 50, 27, 9))])

	# the lens: a silver ring, blue glass, the dark pupil, a highlight
	ring = icon.gradient(0, [(0, (235, 238, 245, 255)),
		(255, (96, 100, 112, 255))], linear(19, 19, 41, 41))
	icon.shape(ring, [icon.path(circle(30, 30, 12.5))])
	rim = icon.solid((18, 20, 28, 255))
	icon.shape(rim, [icon.path(circle(30, 30, 10))])
	glass = icon.gradient(1, [(0, (120, 190, 255, 255)),
		(150, (30, 80, 170, 255)), (255, (10, 24, 70, 255))],
		radial(27.5, 27.5, 10, 10))
	icon.shape(glass, [icon.path(circle(30, 30, 9))])
	pupil = icon.gradient(1, [(0, (40, 50, 80, 255)),
		(255, (4, 6, 14, 255))], radial(30, 30, 4.5, 4.5))
	icon.shape(pupil, [icon.path(circle(30, 30, 4.2))])
	highlight = icon.solid((255, 255, 255, 190))
	icon.shape(highlight, [icon.path(circle(26.4, 26.2, 1.9))])

	# the recording light
	light = icon.gradient(1, [(0, (255, 170, 150, 255)),
		(255, (200, 16, 16, 255))], radial(46.2, 21.2, 2.8, 2.8))
	icon.shape(light, [icon.path(circle(47, 22, 2.5))])

	# green waves up to the right: it sends what it sees
	for radius, alpha in ((5.5, 255), (9.0, 200)):
		wave = icon.solid((60, 200, 90, alpha))
		icon.shape(wave, [icon.path(arc_band(49, 10, radius + 1.3, radius,
			math.radians(-160), math.radians(-110)))])
	return icon.data()


def main():
	data = neocam()
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

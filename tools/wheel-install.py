#!/usr/bin/env python3
# Copyright 2026, haiku-odroid-c2 port authors.
# Distributed under the terms of the MIT License.
#
# wheel-install.py <wheel> <site dir> <bin dir> <interpreter>: installs a
# wheel into a package's tree (milestone M83 (docs/milestones.md)), as pip would but for any
# platform: pip refuses a wheel built for Haiku arm64 on this laptop. The
# wheel's files go to <site dir> (its .data/purelib and platlib too), its
# .data/scripts and the console scripts of entry_points.txt to <bin dir>,
# started by <interpreter> (#!/bin/python3.15 on the boards).
import configparser
import os
import sys
import zipfile


def main():
	wheel, site, bindir, interpreter = sys.argv[1:5]
	scripts = []
	with zipfile.ZipFile(wheel) as z:
		for info in z.infolist():
			name = info.filename
			if name.endswith("/"):
				continue
			parts = name.split("/")
			if parts[0].endswith(".data") and len(parts) > 2:
				kind, rest = parts[1], "/".join(parts[2:])
				if kind in ("purelib", "platlib"):
					target = os.path.join(site, rest)
				elif kind == "scripts":
					target = os.path.join(bindir, rest)
					scripts.append(target)
				else:
					print("wheel-install: skipped %s" % name)
					continue
			else:
				target = os.path.join(site, name)
			os.makedirs(os.path.dirname(target), exist_ok=True)
			with z.open(info) as source, open(target, "wb") as out:
				out.write(source.read())
			mode = (info.external_attr >> 16) & 0o777
			if mode:
				os.chmod(target, mode)
		entryPoints = [n for n in z.namelist()
			if n.endswith(".dist-info/entry_points.txt")]
		consoleScripts = []
		if entryPoints:
			parser = configparser.ConfigParser(interpolation=None)
			parser.optionxform = str
			parser.read_string(z.read(entryPoints[0]).decode())
			if parser.has_section("console_scripts"):
				consoleScripts = parser.items("console_scripts")

	for path in scripts:
		with open(path, "rb") as f:
			data = f.read()
		if data.startswith(b"#!python"):
			data = b"#!" + interpreter.encode() + data[len(b"#!python"):]
			with open(path, "wb") as f:
				f.write(data)
		os.chmod(path, 0o755)
	for name, target in consoleScripts:
		module, _, function = target.partition(":")
		function = function.split("[")[0].strip()
		os.makedirs(bindir, exist_ok=True)
		path = os.path.join(bindir, name)
		with open(path, "w") as f:
			f.write("#!%s\nimport sys\nfrom %s import %s\nsys.exit(%s())\n"
				% (interpreter, module.strip(), function.split(".")[0],
					function))
		os.chmod(path, 0o755)


main()

#!/usr/bin/env python3
# Builds the package repository the C2s update from with pkgman, and that
# HaikuDepot and SoftwareUpdater show: repo/ in the project folder, served
# over HTTP by tools/c2-repo.service. It holds the packages our images are
# made of and jam's optional ones (c2packages.py) under packages/, with
# their canonical file names,
# the repository file "repo", its checksum "repo.sha256", and "repo.info",
# which "pkgman add-repo <url>" reads.
# A package only reaches the boards as an update when its version is newer
# than theirs: for the haiku package, that means a new commit in the fork.
# Usage: c2-repo.py [--url <base URL>]
#   default URL: http://<this host's address towards $C2_LAN (192.168.1.1)>:8090
import argparse
import hashlib
import os
import re
import shutil
import subprocess
import sys

from c2packages import GEN, ROOT, local_packages, package_info

REPO_DIR = os.path.join(ROOT, "repo")
PACKAGE_REPO_TOOL = os.path.join(GEN,
	"objects/linux/x86_64/release/tools/package_repo/package_repo")
PORT = 8090


def default_url():
	route = subprocess.run(["ip", "-4", "route", "get",
		os.environ.get("C2_LAN", "192.168.1.1")],
		capture_output=True, text=True, check=True).stdout
	address = re.search(r"\bsrc (\S+)", route).group(1)
	return "http://%s:%d" % (address, PORT)


def sha256(path):
	digest = hashlib.sha256()
	with open(path, "rb") as f:
		for block in iter(lambda: f.read(1 << 20), b""):
			digest.update(block)
	return digest.hexdigest()


def main():
	parser = argparse.ArgumentParser()
	parser.add_argument("--url", default=None)
	args = parser.parse_args()
	url = args.url or default_url()

	packagesDir = os.path.join(REPO_DIR, "packages")
	os.makedirs(packagesDir, exist_ok=True)
	wanted = {}
	for name, (version, path) in sorted(local_packages(optional=True).items()):
		_, _, architecture = package_info(path)
		wanted["%s-%s-%s.hpkg" % (name, version, architecture)] = path

	for fileName in os.listdir(packagesDir):
		if fileName not in wanted:
			os.remove(os.path.join(packagesDir, fileName))
			print("c2-repo: removed %s" % fileName)
	for fileName, path in sorted(wanted.items()):
		target = os.path.join(packagesDir, fileName)
		# copies, not links: jam may rewrite its outputs in place
		if not os.path.exists(target) or sha256(target) != sha256(path):
			shutil.copyfile(path, target + ".new")
			os.replace(target + ".new", target)
			print("c2-repo: added %s" % fileName)

	# the new repository files replace the old ones only when complete;
	# package_repo writes "repo" next to "repo.info"
	building = os.path.join(REPO_DIR, ".building")
	shutil.rmtree(building, ignore_errors=True)
	os.makedirs(building)
	with open(os.path.join(building, "repo.info"), "w") as f:
		f.write('name C2\n'
			'vendor "haiku-odroid-c2 port"\n'
			'summary "Haiku and the packages built for the ODROID-C2 port"\n'
			'priority 1\n'
			'baseurl %s\n'
			'identifier tag:haiku-odroid-c2,2026:repositories/c2/arm64\n'
			'architecture arm64\n' % url)
	subprocess.run([PACKAGE_REPO_TOOL, "create", "-q",
		os.path.join(building, "repo.info")]
		+ [os.path.join(packagesDir, f) for f in sorted(wanted)], check=True)
	with open(os.path.join(building, "repo.sha256"), "w") as f:
		f.write(sha256(os.path.join(building, "repo")) + "\n")
	for name in ("repo", "repo.sha256", "repo.info"):
		os.replace(os.path.join(building, name), os.path.join(REPO_DIR, name))
	shutil.rmtree(building)

	size = sum(os.path.getsize(os.path.join(packagesDir, f)) for f in wanted)
	print("c2-repo: %d packages, %.0f MiB, at %s" % (len(wanted), size / (1 << 20),
		url))
	return 0


if __name__ == "__main__":
	sys.exit(main())

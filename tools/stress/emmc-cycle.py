#!/usr/bin/env python3
# Copyright 2026, haiku-odroid-c2 port authors.
# Distributed under the terms of the MIT License.
#
# Repeats on the test board what preceded the eMMC corruption found on
# .182 (2026-11-01): package-sized files copied and moved into place as an
# update does, sync, a reboot, small program-sized files copied right
# after the boot, and so on; after every boot all files written so far are
# checked against their SHA-256. A file that doesn't match is fetched and
# its pages identified (pagetag.py).
#
#   emmc-cycle.py <rounds> [<first round>] [exec] [pkgman] [replay182]
#
# With "exec", each round also copies real test programs and runs each one
# right away, while its pages are still waiting to be written (as on .182,
# where the programs run that way were the ones damaged).
# With "pkgman", an update goes as c2-update's does: the big files are
# uploaded into a folder, copied into place (pkgman copies), the upload
# folder deleted (its blocks freed for the next files), and the reboot has
# no sync before it.
# "replay182" repeats .182's afternoon: the same four programs copied and
# run as they were (and nothing else small), a pause, an update as
# c2-update makes it, then "sync; shutdown -r".
import hashlib
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
import pagetag  # noqa: E402

HOST = "10.42.0.2"
SSH = ["sshpass", "-p", "haiku", "ssh", "-o", "StrictHostKeyChecking=no",
	"-o", "UserKnownHostsFile=/dev/null", "-o", "LogLevel=ERROR",
	"-o", "ServerAliveInterval=15", "baron@" + HOST]
SCP = ["sshpass", "-p", "haiku", "scp", "-q", "-o", "StrictHostKeyChecking=no",
	"-o", "UserKnownHostsFile=/dev/null", "-o", "LogLevel=ERROR"]
BOARD_DIR = "/boot/home/stress"
WORK = os.path.join(ROOT, "run", "stress")

# like the haiku, haiku_datatranslators and haiku_loader packages, and like
# the test programs copied after the boot
BIG = [(100, 40715291), (101, 1683659), (102, 414259)]
SMALL = [(0, 300328), (1, 349568), (2, 289264), (3, 287960), (4, 313856)]
# programs and a quick run of each, from run/
PROGRAMS = [
	("gltest", "bench 64 64 1"),
	("listselect", ""),
	("rwlocksignal", ""),
	("medianodes", ""),
	("camview", "320 240 1"),
	("physread", "0xc883443c 1"),
]


def ssh(command, check=True):
	result = subprocess.run(SSH + [command], capture_output=True, text=True)
	if check and result.returncode != 0:
		raise RuntimeError("ssh %r: %s" % (command, result.stderr))
	return result.stdout


def make(round_, file_id, size, manifest, board_path):
	local = os.path.join(WORK, "r%d-f%d.dat" % (round_, file_id))
	pagetag.gen(local, round_, file_id, size)
	with open(local, "rb") as f:
		manifest[board_path] = hashlib.sha256(f.read()).hexdigest()
	return local


def verify(manifest, label):
	if not manifest:
		return True
	listing = ssh("sha256sum " + " ".join(manifest.keys()), check=False)
	got = {}
	for line in listing.splitlines():
		digest, path = line.split(None, 1)
		got[path.strip()] = digest
	good = True
	for path, digest in sorted(manifest.items()):
		if got.get(path) != digest:
			good = False
			print("  %s: %s WRONG" % (label, path), flush=True)
			local = os.path.join(WORK, "bad-" + os.path.basename(path))
			subprocess.run(SCP + ["baron@%s:%s" % (HOST, path), local])
			pagetag.verify(local)
	print("  %s: %d files checked, %s" % (label, len(manifest),
		"all good" if good else "CORRUPTION"), flush=True)
	return good


def reboot(sync=True):
	ssh(("sync; " if sync else "") + "(sleep 2; shutdown -r) > /dev/null 2>&1 &",
		check=False)
	result = subprocess.run([os.path.join(ROOT, "tools", "c2wait"),
		"c2-system: desktop up|PANIC|kdebug>", "300", "--now"],
		capture_output=True, text=True)
	if "desktop up" not in result.stdout:
		print(result.stdout[-2000:])
		raise RuntimeError("the board didn't come back")
	ssh("sleep 3; true")


def main():
	rounds = int(sys.argv[1])
	first = int(sys.argv[2]) if len(sys.argv) > 2 else 1
	run_programs = "exec" in sys.argv[3:]
	like_pkgman = "pkgman" in sys.argv[3:]
	replay = "replay182" in sys.argv[3:]
	global PROGRAMS, SMALL
	if replay:
		run_programs = True
		like_pkgman = True
		SMALL = []
		PROGRAMS = [("gltest", "bench 640 480 5"), ("listselect", ""),
			("rwlocksignal", ""), ("medianodes", "")]
	os.makedirs(WORK, exist_ok=True)
	manifest = {}
	ssh("mkdir -p %s/pkg %s/incoming" % (BOARD_DIR, BOARD_DIR))
	for round_ in range(first, first + rounds):
		print("round %d" % round_, flush=True)
		# small files right after the boot, as test programs were
		small_dir = "%s/small-%d" % (BOARD_DIR, round_)
		ssh("mkdir -p " + small_dir)
		files = [make(round_, fid, size, manifest,
			"%s/r%d-f%d.dat" % (small_dir, round_, fid))
			for fid, size in SMALL]
		if files:
			subprocess.run(SCP + files + ["baron@%s:%s/" % (HOST, small_dir)],
				check=True)
		if run_programs:
			commands = []
			for name, args in PROGRAMS:
				local = os.path.join(ROOT, "run", name)
				target = "%s/%s-%d" % (small_dir, name, round_)
				with open(local, "rb") as f:
					manifest[target] = hashlib.sha256(f.read()).hexdigest()
				subprocess.run(SCP + [local, "baron@%s:%s" % (HOST, target)],
					check=True)
				commands.append("chmod +x %s; %s %s > /dev/null 2>&1"
					% (target, target, args))
			# right after the copies, as on .182
			ssh("; ".join(commands), check=False)
		verify(manifest, "after the small copies")
		if replay:
			ssh("sleep 60; true", check=False)

		# an update: big files copied, the old ones moved aside, the new
		# ones moved into place
		files = [make(round_, fid, size, {}, "") for fid, size in BIG]
		subprocess.run(SCP + files + ["baron@%s:%s/incoming/" % (HOST,
			BOARD_DIR)], check=True)
		old_dir = "%s/old-%d" % (BOARD_DIR, round_)
		if like_pkgman:
			ssh("mkdir -p %s; mv %s/pkg/* %s/ 2>/dev/null; cp %s/incoming/* "
				"%s/pkg/; rm -rf %s/incoming; mkdir -p %s/incoming"
				% (old_dir, BOARD_DIR, old_dir, BOARD_DIR, BOARD_DIR,
					BOARD_DIR, BOARD_DIR), check=False)
		else:
			ssh("mkdir -p %s; mv %s/pkg/* %s/ 2>/dev/null; mv %s/incoming/* "
				"%s/pkg/" % (old_dir, BOARD_DIR, old_dir, BOARD_DIR,
					BOARD_DIR), check=False)
		# the manifest follows the moves
		for path in list(manifest):
			if "/pkg/" in path:
				manifest[path.replace("/pkg/", "/old-%d/" % round_)] \
					= manifest.pop(path)
		for fid, _ in BIG:
			name = "r%d-f%d.dat" % (round_, fid)
			with open(os.path.join(WORK, name), "rb") as f:
				manifest["%s/pkg/%s" % (BOARD_DIR, name)] \
					= hashlib.sha256(f.read()).hexdigest()
			os.remove(os.path.join(WORK, name))
		verify(manifest, "after the update")

		reboot(sync=replay or not like_pkgman)
		if not verify(manifest, "after the reboot"):
			return 1
	return 0


if __name__ == "__main__":
	sys.exit(main())

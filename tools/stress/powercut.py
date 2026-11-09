#!/usr/bin/env python3
# Copyright 2026, haiku-odroid-c2 port authors.
# Distributed under the terms of the MIT License.
#
# Whether data written some time before a power loss survives it (docs/milestones.md,
# the eMMC corruption of 2026-11-01): a few small tagged files, less than
# the page writer's 256-page batch together, are copied to the test board,
# then after <wait> seconds and no sync the relay cuts its power. After the
# boot every file is checked; a wrong one is fetched and its pages named
# (pagetag.py).
#
#   powercut.py <round> [<wait seconds>]
#
# Test board only (10.42.0.2, the relay). U-Boot's c2.scr must only boot
# the eMMC.
import hashlib
import os
import subprocess
import sys
import time

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
BOARD_DIR = "/boot/home/powercut"
WORK = os.path.join(ROOT, "run", "powercut")
# about 180 pages together, program-sized like the files lost on .182
SIZES = [(0, 300328), (1, 160000), (2, 250000), (3, 21000)]


def ssh(command, check=True):
	result = subprocess.run(SSH + [command], capture_output=True, text=True)
	if check and result.returncode != 0:
		raise RuntimeError("ssh %r: %s" % (command, result.stderr))
	return result.stdout


def verify(manifest, label):
	listing = ssh("sha256sum " + " ".join(manifest.keys()), check=False)
	got = {}
	for line in listing.splitlines():
		digest, path = line.split(None, 1)
		got[path.strip()] = digest
	bad = 0
	for path, digest in sorted(manifest.items()):
		if got.get(path) != digest:
			bad += 1
			print("  %s: %s WRONG" % (label, path), flush=True)
			local = os.path.join(WORK, "bad-" + os.path.basename(path))
			subprocess.run(SCP + ["baron@%s:%s" % (HOST, path), local])
			if os.path.exists(local):
				pagetag.verify(local)
	print("  %s: %d files checked, %s" % (label, len(manifest),
		"all good" if bad == 0 else "%d WRONG" % bad), flush=True)
	return bad == 0


def main():
	round_ = int(sys.argv[1])
	wait = int(sys.argv[2]) if len(sys.argv) > 2 else 30
	os.makedirs(WORK, exist_ok=True)
	print(ssh("uname -v").strip(), flush=True)

	directory = "%s/r%d" % (BOARD_DIR, round_)
	ssh("mkdir -p " + directory + "; sync")
	manifest = {}
	files = []
	for file_id, size in SIZES:
		local = os.path.join(WORK, "r%d-f%d.dat" % (round_, file_id))
		pagetag.gen(local, round_, file_id, size)
		with open(local, "rb") as f:
			manifest["%s/r%d-f%d.dat" % (directory, round_, file_id)] \
				= hashlib.sha256(f.read()).hexdigest()
		files.append(local)
	subprocess.run(SCP + files + ["baron@%s:%s/" % (HOST, directory)],
		check=True)
	verify(manifest, "after the copy")

	print("  waiting %d s, then cutting the power (no sync)" % wait,
		flush=True)
	time.sleep(wait)
	power = os.path.join(ROOT, "tools", "power.py")
	subprocess.run([power, "off"], check=True)
	time.sleep(3)
	subprocess.run([power, "on"], check=True)
	result = subprocess.run([os.path.join(ROOT, "tools", "c2wait"),
		"c2-system: desktop up|PANIC|kdebug>", "300", "--now"],
		capture_output=True, text=True)
	if "desktop up" not in result.stdout:
		print(result.stdout[-2000:])
		raise RuntimeError("the board didn't come back")
	ssh("sleep 3; true")
	print("  " + ssh("grep -a -c 'Replay log' /var/log/syslog", check=False)
		.strip() + " log replay(s) in this boot's syslog", flush=True)
	return 0 if verify(manifest, "after the power cut") else 1


if __name__ == "__main__":
	sys.exit(main())

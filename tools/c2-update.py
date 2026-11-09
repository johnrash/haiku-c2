#!/usr/bin/env python3
# Bring a running C2 up to date over SSH with the packages our images are
# made of: jam's system packages (haiku, haiku_datatranslators, haiku_loader),
# the ports (deps/ports), Mesa, and the extra packages in run/allpkgs.txt.
# A package goes over when the board has it in another version, or not at
# all (new image contents, such as the dependencies of a newer haiku
# package). Everything is installed with one "pkgman install" transaction,
# whose previous state stays bootable from the loader's menu
# (/boot/system/packages/administrative/state_*); a reboot completes it.
# Packages with the same version but different contents are only reported:
# pkgman would not see them as updates.
# Ports (deps/ports) the board doesn't have are left out unless --new-ports
# is given: since M83 they include optional software (Python, Qt, gcc,
# rsync...) that a board gets only when chosen.
# Usage: c2-update.py [--host <address>] [--dry-run] [--reboot] [--new-ports]
#   default host: the board on the crossover cable (10.42.0.2); user baron,
#   test password
import argparse
import hashlib
import os
import re
import shlex
import subprocess
import sys

from c2packages import local_packages

SSH_OPTIONS = ["-o", "StrictHostKeyChecking=no", "-o", "UserKnownHostsFile=/dev/null",
	"-o", "LogLevel=ERROR", "-o", "ConnectTimeout=15",
	# a connection that died unnoticed fails within a minute instead of
	# hanging the update (once 29 minutes on a sha256sum)
	"-o", "ServerAliveInterval=15", "-o", "ServerAliveCountMax=4"]
UPLOAD_DIR = "/boot/home/c2-update"


def ssh(host, password, command, check=True):
	return subprocess.run(["sshpass", "-p", password, "ssh", *SSH_OPTIONS,
		"baron@" + host, command], capture_output=True, text=True, check=check).stdout


def remote_packages(host, password):
	"""{name: (version, file name)} of the board's system packages."""
	packages = {}
	for line in ssh(host, password, "ls /boot/system/packages").split():
		match = re.match(r"(.+?)-([^-]+-[^-]+)-(arm64|any)\.hpkg$", line)
		if match:
			packages[match.group(1)] = (match.group(2), line)
	return packages


def sha256(path):
	with open(path, "rb") as f:
		return hashlib.sha256(f.read()).hexdigest()


def main():
	parser = argparse.ArgumentParser()
	parser.add_argument("--host", default="10.42.0.2")
	parser.add_argument("--dry-run", action="store_true")
	parser.add_argument("--reboot", action="store_true")
	parser.add_argument("--new-ports", action="store_true")
	args = parser.parse_args()
	password = os.environ.get("C2_PASSWORD", "haiku")

	local = local_packages()
	remote = remote_packages(args.host, password)

	updates = []
	skipped = []
	portsDir = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..",
		"deps", "ports")
	for name, (version, path) in sorted(local.items()):
		if name not in remote:
			if (not args.new_ports and os.path.dirname(os.path.abspath(path))
					== os.path.abspath(portsDir)):
				skipped.append(name)
				continue
			updates.append((name, None, version, path))
		elif remote[name][0] != version:
			updates.append((name, remote[name][0], version, path))
		else:
			remoteSum = ssh(args.host, password, "sha256sum "
				+ shlex.quote("/boot/system/packages/" + remote[name][1])).split()[0]
			if remoteSum != sha256(path):
				print("c2-update: %s %s differs from the board's, same version: "
					"not an update (bump its version)" % (name, version))

	if skipped:
		print("c2-update: not on the board, left out (--new-ports): %s"
			% " ".join(skipped))
	if not updates:
		print("c2-update: the board is up to date")
		return 0
	size = sum(os.path.getsize(path) for _, _, _, path in updates)
	for name, old, new, _ in updates:
		print("c2-update: %s %s -> %s" % (name, old or "(new)", new))
	free = ssh(args.host, password, "df -k /boot")
	match = re.search(r"Free Blocks:\s+([\d.]+) (KiB|MiB|GiB)", free)
	if match:
		freeBytes = float(match.group(1)) * {"KiB": 1 << 10, "MiB": 1 << 20,
			"GiB": 1 << 30}[match.group(2)]
		# the uploads, their copies in the transaction, and the old versions
		# kept in the backup state
		if freeBytes < 2.2 * size:
			print("c2-update: %.0f MiB free on /boot, about %.0f MiB needed"
				% (freeBytes / (1 << 20), 2.2 * size / (1 << 20)))
			return 1
	if args.dry_run:
		return 0

	ssh(args.host, password, "rm -rf %s && mkdir -p %s" % (UPLOAD_DIR, UPLOAD_DIR))
	files = []
	for name, _, version, path in updates:
		remotePath = "%s/%s-%s-%s" % (UPLOAD_DIR, name, version,
			"any.hpkg" if path.endswith("-any.hpkg") else "arm64.hpkg")
		subprocess.run(["sshpass", "-p", password, "scp", *SSH_OPTIONS, path,
			"baron@%s:%s" % (args.host, remotePath)], check=True)
		remoteSum = ssh(args.host, password, "sha256sum " + shlex.quote(remotePath))
		if remoteSum.split()[0] != sha256(path):
			print("c2-update: %s arrived damaged" % remotePath)
			return 1
		files.append(remotePath)
	output = ssh(args.host, password, "pkgman install -y "
		+ " ".join(shlex.quote(f) for f in files) + " 2>&1; echo status=$?; rm -rf "
		+ UPLOAD_DIR, check=False)
	print(output.strip())
	if "status=0" not in output:
		print("c2-update: pkgman failed, nothing changed")
		return 1
	if args.reboot:
		ssh(args.host, password, "(sleep 2; shutdown -r) > /dev/null 2>&1 &", check=False)
		print("c2-update: rebooting %s" % args.host)
	else:
		print("c2-update: reboot the board to complete the update")
	return 0


if __name__ == "__main__":
	sys.exit(main())

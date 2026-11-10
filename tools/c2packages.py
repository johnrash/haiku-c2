# The packages our C2 images are made of, for c2-update.py and c2-repo.py:
# jam's system packages (haiku, haiku_datatranslators, haiku_loader), the
# ports (deps/ports), Mesa (with the Lima Pipe renderer for the Mali-450),
# and the extra packages in run/allpkgs.txt; for
# the repository also jam's optional packages, which images leave out, and
# our own apps (apps/*: Weather Board, LCD Shield; Firefox), which c2-update.py never
# installs: a board gets them only when someone picks them in HaikuDepot or
# with pkgman.
import glob
import os
import re
import subprocess

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GEN = os.path.join(ROOT, "generated.arm64")
PACKAGE_TOOL = os.path.join(GEN, "objects/linux/x86_64/release/tools/package/package")


def package_info(path):
	"""(name, version, architecture) from a package file's attributes."""
	info = subprocess.run([PACKAGE_TOOL, "list", "-i", path], capture_output=True,
		text=True, check=True).stdout
	name = re.search(r"^\s*name: (\S+)", info, re.M).group(1)
	version = re.search(r"^\s*version: (\S+)", info, re.M).group(1)
	architecture = re.search(r"^\s*architecture: (\S+)", info, re.M).group(1)
	return name, version, architecture


OPTIONAL_PACKAGES = ("haiku_devel", "haiku_extras", "makefile_engine", "netfs",
	"userland_fs")


def local_packages(optional=False):
	"""{name: (version, path)} for every package our images are built from,
	with optional=True also for jam's optional packages."""
	names = ("haiku", "haiku_datatranslators", "haiku_loader")
	if optional:
		names += OPTIONAL_PACKAGES
	paths = [os.path.join(GEN, "objects/haiku/arm64/packaging/packages", p + ".hpkg")
		for p in names]
	paths += glob.glob(os.path.join(ROOT, "deps/ports/*.hpkg"))
	paths += glob.glob(os.path.join(ROOT, "savers/*/*.hpkg"))
	paths += glob.glob(os.path.join(ROOT, "firmware/*.hpkg"))
	# the newest revision of each (tools/mesa-build.sh: 1 without LLVM, 2 and up with)
	for name in ("mesa", "mesa_swpipe", "mesa_lima"):
		revisions = glob.glob(os.path.join(ROOT, "deps/mesa",
			name + "-22.0.5-*-arm64.hpkg"))
		if revisions:
			paths.append(max(revisions, key=lambda p: int(
				re.search(r"-(\d+)-arm64\.hpkg$", p).group(1))))
	paths += glob.glob(os.path.join(ROOT, "deps/llvm/*.hpkg"))
	if optional:
		# the newest build of each app last, so that it wins
		paths += sorted(glob.glob(os.path.join(ROOT, "apps/*/*.hpkg"))
			+ glob.glob(os.path.join(ROOT, "deps/firefox/firefox-*-arm64.hpkg")),
			key=os.path.getmtime)
	# more packages for the boards, one path a line (optional)
	extra = os.path.join(ROOT, "run/allpkgs.txt")
	if os.path.exists(extra):
		with open(extra) as f:
			paths += [os.path.join(ROOT, p) for p in f.read().split()]
	packages = {}
	for path in paths:
		name, version, _ = package_info(path)
		packages[name] = (version, path)
	return packages

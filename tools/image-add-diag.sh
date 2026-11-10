#!/bin/sh
# Put the C2 guest scripts into a Haiku MMC image's BFS partition (36 MiB into
# haiku-mmc.image) and add the eMMC data partition to its partition table.
# Always: tools/guest/c2-system.{sh,job} (data partition, clock, desktop
# check), partition 3 (tools/image-add-data-partition.py), the ICU package
# with its data built in and erratum 843419 patched (tools/icu-data-build.sh)
# and Locale settings for English (US) (configs/locale-settings, written by
# tools/probes/locale-settings.cpp), so the regular image's FirstBootPrompt
# does not hold back the desktop; bootstrap packages fixed for the A53 errata
# (tools/bootstrap-fix.sh) in place of the originals; no Haiku and HaikuPorts
# repositories: the official arm64 "haiku" package would replace ours, which
# has the C2's drivers, as soon as its version passes ours.
# Options:
#   --remote-shell  ftpd/telnetd at boot (tools/c2sh, c2put, c2get)
#   --kdl-on-hang   enter KDL if the desktop is not up 120 s after boot
#   --no-diag       no tools/guest/UserBootscript (clean image)
#   --ssh           first boot: sshd configured with host keys; /etc/shadow
#                   with the test password (deps/ssh/shadow) for user baron
#   --package=<hpkg>  add a package to system/packages (activated at boot)
#   --sd-writable   the microSD may be written (an image that runs from it)
#   --serial-kdl-key  Ctrl-K on the serial console enters KDL (haiku's kernel
#                   setting serial_kdl_key), also when userland hangs
#   --no-data-partition  no eMMC data partition (an image for the microSD)
#   --grow-on-boot  the boot volume grows to the whole disk at the first boots
#                   (tools/guest/c2-grow.cpp); with --no-data-partition
#   --no-icu-data   keep the bootstrap ICU package (stub data: ICU formats
#                   nothing) instead of tools/icu-data-build.sh's
#   --first-boot-prompt  no Locale settings: Haiku asks for the language
#                   and keymap at the first boot
#   --kdl           the diagnostics script enters KDL after its first dump
#   --writetest     the diagnostics script runs the 100 MiB write/verify test
#   --repo=<url>    the port's package repository (tools/c2-repo.py) at <url>,
#                   e.g. http://10.42.0.1:8090 for this board,
#                   http://<build host>:8090 on the LAN
# Usage: image-add-diag.sh [options] <image>
set -e
root=$(cd "$(dirname "$0")/.." && pwd)
bfs="$root/generated.arm64/objects/linux/x86_64/release/tools/bfs_shell/bfs_shell"
diag=1
dataPartition=1
icuData=1
localeSettings=1
commands=""
repoIdentifier="tag:haiku-odroid-c2,2026:repositories/c2/arm64"
while [ "${1#--}" != "$1" ]; do
	case "$1" in
		--no-diag)
			diag=0
			;;
		--no-data-partition)
			dataPartition=0
			;;
		--grow-on-boot)
			# tools/guest/c2-grow.cpp, run by c2-system.sh while the marker
			# c2-grow exists; the image needs a BFS grow limit
			# (HAIKU_IMAGE_GROW_LIMIT) and no data partition
			bash -c ". '$root/tools/haiku-arm64-env.sh' > /dev/null 2>&1; \
				\$CXX -O2 \$HAIKU_CPPFLAGS -o '$root/run/c2-grow' \
				'$root/tools/guest/c2-grow.cpp' -lbe" \
				&& python3 "$root/tools/a53-843419.py" "$root/run/c2-grow" \
				&& python3 "$root/tools/a53-835769.py" "$root/run/c2-grow" \
				|| { echo "image-add-diag: building c2-grow failed"; exit 1; }
			: > "$root/run/c2-grow.marker"
			commands="$commands
mkdir -p /myfs/system/non-packaged/bin
cp :$root/run/c2-grow /myfs/system/non-packaged/bin/c2-grow
cp :$root/run/c2-grow.marker /myfs/system/settings/c2-grow"
			;;
		--sd-writable)
			printf 'sd_writable true\n' > "$root/run/meson_gx_mmc.settings"
			commands="$commands
mkdir -p /myfs/home/config/settings/kernel/drivers
cp :$root/run/meson_gx_mmc.settings /myfs/home/config/settings/kernel/drivers/meson_gx_mmc"
			;;
		--serial-kdl-key)
			printf 'serial_kdl_key true\n' > "$root/run/kernel.settings"
			commands="$commands
mkdir -p /myfs/home/config/settings/kernel/drivers
cp :$root/run/kernel.settings /myfs/home/config/settings/kernel/drivers/kernel"
			;;
		--no-icu-data)
			icuData=0
			;;
		--first-boot-prompt)
			localeSettings=0
			;;
		--package=*)
			commands="$commands
cp :$(realpath "${1#--package=}") /myfs/system/packages/$(basename "${1#--package=}")"
			;;
		--repo=*)
			printf 'cfgversion=2\nbaseurl=%s\nidentifier=%s\nurl=%s\npriority=1\n' \
				"${1#--repo=}" "$repoIdentifier" "$repoIdentifier" \
				> "$root/run/c2-repo.config"
			commands="$commands
mkdir -p /myfs/system/settings/package-repositories
cp :$root/run/c2-repo.config /myfs/system/settings/package-repositories/C2"
			;;
		--ssh)
			commands="$commands
cp :$root/tools/guest/c2-system.job /myfs/system/settings/c2-ssh
cp :$root/deps/ssh/shadow /myfs/system/settings/etc/shadow"
			;;
		--remote-shell|--kdl-on-hang)
			# marker files read by c2-system.sh
			commands="$commands
cp :$root/tools/guest/c2-system.job /myfs/system/settings/c2-${1#--}"
			;;
		*)
			# marker files read by UserBootscript: diag-kdl, diag-writetest
			commands="$commands
cp :$root/tools/guest/UserBootscript /myfs/home/config/settings/boot/diag-${1#--}"
			;;
	esac
	shift
done
# bootstrap packages fixed for the Cortex-A53 errata (tools/bootstrap-fix.sh),
# in place of those the image has
if ls "$root"/deps/bootstrap/*.hpkg > /dev/null 2>&1; then
	inImage=$(printf '%s\n' "ls /myfs/system/packages" "quit" \
		| "$bfs" --start-offset 37748736 "$1" 2>/dev/null)
	for fixed in "$root"/deps/bootstrap/*.hpkg; do
		f=$(basename "$fixed")
		case "$inImage" in
			*" $f"*)
				commands="$commands
rm /myfs/system/packages/$f
cp :$fixed /myfs/system/packages/$f"
				;;
		esac
	done
fi
if [ $icuData = 1 ]; then
	icu=icu74-74.1_bootstrap-1-arm64.hpkg
	[ -e "$root/deps/icu/$icu" ] || "$root/tools/icu-data-build.sh" > /dev/null
	commands="$commands
rm /myfs/system/packages/$icu
cp :$root/deps/icu/$icu /myfs/system/packages/$icu"
fi
inImage=$(printf '%s\n' "ls /myfs/system/settings/package-repositories" "quit" \
	| "$bfs" --start-offset 37748736 "$1" 2>/dev/null)
for repository in Haiku HaikuPorts; do
	case "$inImage" in
		*" $repository"*)
			commands="$commands
rm /myfs/system/settings/package-repositories/$repository
rm /myfs/system/cache/package-repositories/$repository"
			;;
	esac
done
if [ $localeSettings = 1 ]; then
	commands="$commands
mkdir -p /myfs/home/config/settings
cp :$root/configs/locale-settings \"/myfs/home/config/settings/Locale settings\""
fi
diagCommand=""
[ $diag = 1 ] && diagCommand="cp :$root/tools/guest/UserBootscript /myfs/home/config/settings/boot/UserBootscript"
# The hardware clock (the RTC shield, milestone M23) keeps GMT, as Linux expects
# (configs/RTC_time_settings; Haiku's default is local time).
printf '%s\n' "mkdir -p /myfs/home/config/settings/boot" \
	"cp :$root/configs/RTC_time_settings /myfs/home/config/settings/RTC_time_settings" \
	"mkdir -p /myfs/system/settings/launch" \
	"cp :$root/tools/guest/c2-system.sh /myfs/system/settings/c2-system.sh" \
	"cp :$root/tools/guest/c2-system.job /myfs/system/settings/launch/c2-system" \
	"$diagCommand" "$commands" "sync" "ls /myfs/home/config/settings/boot" "quit" \
	| "$bfs" --start-offset 37748736 "$1" 2>&1 | grep -E 'UserBootscript|diag-kdl|rror' \
	| grep -v 'Unmounting FS failed' || true
[ $dataPartition = 1 ] && "$root/tools/image-add-data-partition.py" "$1"
true

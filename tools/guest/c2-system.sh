#!/bin/sh
# System-level helpers for the ODROID-C2 port, run by the launch_daemon at
# boot as a system job (tools/guest/c2-system.job, installed by
# tools/image-add-diag.sh), independent of the desktop session.
# Always:
# - the data partition (partition 3 of the eMMC, added to the image by
#   tools/image-add-data-partition.py): formatted as BFS "Data" if a good read
#   finds no file system on it, then mounted at /Data;
# - the clock (the C2 has no battery-backed one, so every boot starts in
#   1970): synced over NTP while it is still unset, retrying for a minute.
#   Haiku's own update-time job tries once, and only if its settings say so;
#   they can be missing, or zeroed by a power cut (seen on a board), and a
#   sync rewrites them;
# - "desktop up" (or a ps dump if there is no Tracker after 120 s) on the
#   serial debug output.
# - first-boot growing of the boot volume (marker c2-grow): tools/guest/
#   c2-grow.cpp, below.
# Optional, by marker files in /boot/system/settings:
# - c2-remote-shell: ftpd and a telnetd loop (telnetd -debug serves one
#   connection, then exits) for tools/c2sh, c2put and c2get;
# - c2-kdl-on-hang: enter the kernel debugger if there is no desktop;
# - c2-ssh: on first boot, an sshd configuration (password logins, also for
#   baron, whose user ID is 0) and this board's host keys; net_server's ssh
#   service starts sshd at every boot. The image's /etc/shadow holds the
#   test password "haiku".
# Uses only commands of the @minimum image (no grep).

settings=/boot/system/settings
log()
{
	echo "c2-system: $*" > /dev/dprintf
}

if [ -e $settings/c2-remote-shell ]; then
	# test images: the user database as the boot found it
	for f in /etc/passwd /etc/shadow; do
		log "$f: $(ls -l $f 2>&1 | cut -c1-60) | $(head -c 40 $f 2>/dev/null | od -A n -c | tr -s ' ' | head -c 90)"
	done
	/bin/ftpd -D > /dev/dprintf 2>&1 &
	(while true; do /bin/telnetd -debug 23; sleep 1; done) > /dev/dprintf 2>&1 &
fi

# Only our own data partition: MBR entry 3 of type 0xeb (BeOS) starting at
# sector 2097152 (tools/image-add-data-partition.py). Another system's eMMC
# (a second board) is never touched.
data=/dev/disk/mmc/1/2
entry=$(dd if=/dev/disk/mmc/1/raw bs=512 count=1 2>/dev/null \
	| od -A n -t u1 -j 482 -N 8 | tr -s ' ')
case "$entry" in
	" 235 "*" 0 0 32 0")
		;;
	*)
		data=/nonexistent
		;;
esac
if [ -e $data ]; then
	# BFS keeps "1SFB" at byte 544 of its partition. Format only after a
	# complete read without it, never because a read failed.
	block=$(dd if=$data bs=512 skip=1 count=1 2>/dev/null | od -A n -c -j 32 -N 4)
	if [ "$(dd if=$data bs=512 skip=1 count=1 2>/dev/null | wc -c)" -ne 512 ]; then
		log "cannot read the data partition, leaving it alone"
	elif [ "$(echo $block)" != "1 S F B" ]; then
		log "formatting the data partition as BFS \"Data\""
		/bin/mkfs -t bfs -q $data Data > /dev/dprintf 2>&1
	fi
	case "$(/bin/df)" in
		*"$data"*)
			;;
		*)
			/bin/mountvolume Data > /dev/dprintf 2>&1 \
				&& log "data partition mounted at /Data"
			;;
	esac
fi

if [ "$(date +%Y)" -lt 2000 ]; then
	(
		i=0
		while [ $i -lt 12 ] && [ "$(date +%Y)" -lt 2000 ]; do
			sleep 5
			case "$(/system/preferences/Time --update 2>&1)" in
				*successful*)
					log "clock set over NTP: $(date)"
					break
					;;
			esac
			i=$((i + 1))
		done
		[ "$(date +%Y)" -lt 2000 ] \
			&& log "clock still unset: no NTP server answered for a minute"
	) &
fi

# First-boot growing (marker c2-grow, image-add-diag.sh --grow-on-boot): the
# boot partition takes the rest of the disk, then the board restarts, as
# Haiku keeps the size of a mounted partition, and at the next boot the file
# system grows into it (tools/guest/c2-grow.cpp).
grow=/boot/system/non-packaged/bin/c2-grow
if [ -e $settings/c2-grow ] && [ -x $grow ]; then
	result=$($grow)
	status=$?
	log "$result"
	if [ $status = 2 ]; then
		(
			# after the desktop and the first boot's own work
			i=0
			while [ $i -lt 36 ]; do
				case "$(/bin/ps)" in
					*/boot/system/Tracker*)
						break
						;;
				esac
				sleep 5
				i=$((i + 1))
			done
			sleep 30
			/bin/notify --title "System volume" --timeout 15 \
				"System will restart shortly to resize boot volume to utilize entire disk space." \
				> /dev/null 2>&1
			sleep 15
			log "restarting to grow the file system"
			sync
			/bin/shutdown -r
		) &
	else
		rm -f $settings/c2-grow
	fi
fi

ssh=/boot/system/settings/ssh
if [ -e $settings/c2-ssh ] && [ -x /boot/system/bin/sshd ] \
		&& [ ! -e $ssh/ssh_host_ed25519_key ]; then
	mkdir -p $ssh /boot/system/var/empty /boot/system/var/run
	cat > $ssh/sshd_config <<CONFIG
# Written on first boot by c2-system.sh (haiku-odroid-c2 test image).
# baron has user ID 0, so password logins need PermitRootLogin.
PermitRootLogin yes
PasswordAuthentication yes
KbdInteractiveAuthentication no
Subsystem sftp /boot/system/lib/openssh/sftp-server
CONFIG
	/boot/system/bin/ssh-keygen -A > /dev/dprintf 2>&1
	# net_server tried to start sshd before there were keys
	/boot/system/bin/sshd > /dev/dprintf 2>&1 \
		&& log "sshd started on port 22 (user baron, test password)"
fi

i=0
while [ $i -lt 24 ]; do
	case "$(/bin/ps)" in
		*/boot/system/Tracker*)
			log "desktop up"
			exit 0
			;;
	esac
	sleep 5
	i=$((i + 1))
done
{ echo "=== c2-system: no desktop 120 s after boot"; /bin/ps; } > /dev/dprintf 2>&1
if [ -e $settings/c2-kdl-on-hang ]; then
	/bin/kernel_debugger "c2-system: no desktop after 120 s"
fi

#!/bin/sh
# Keep the HDMI capture dongle streaming. The dongle only asserts HDMI
# hot-plug (HPD) while it is being captured from, and the C2 (U-Boot, later
# Haiku) only drives HDMI when it sees HPD at boot. The newest frame is
# written to run/hdmi-live.png once a second; hdmi-grab.sh copies it.
# Restarts the capture if the dongle disappears (replug, hub power-cycle);
# after a USB hiccup it can come back as "Guermok USB2 Video", which works too.
# A capture that stops writing frames (ffmpeg blocks in the driver, seen
# across the C2's reboots; it ignores SIGTERM then) is killed after 15 s
# without a new frame and started again.
# Run in the background: setsid tools/hdmi-live.sh &
root=$(cd "$(dirname "$0")/.." && pwd)
live="$root/run/hdmi-live.png"
echo $$ > "$root/run/hdmi-live.pid"
while true; do
	dev=$(for v in /sys/class/video4linux/video*; do
		if grep -q "Guermok USB[23] Video" "$v/name" 2>/dev/null \
				&& [ "$(cat "$v/index")" = 0 ]; then
			echo "/dev/$(basename "$v")"; fi; done | head -n 1)
	if [ -n "$dev" ]; then
		sudo -n ffmpeg -hide_banner -loglevel error -f v4l2 \
			-video_size 1920x1080 -i "$dev" -vf fps=1 -update 1 \
			-y "$live" &
		sudoPid=$!
		started=$(date +%s)
		# sudo's process is root's: kill -0 can't look at it, /proc can
		while [ -d "/proc/$sudoPid" ]; do
			sleep 5
			now=$(date +%s)
			updated=$(stat -c %Y "$live" 2>/dev/null || echo 0)
			if [ $((now - started)) -gt 15 ] \
					&& [ $((now - updated)) -gt 15 ]; then
				ffmpegPid=$(ps -o pid= --ppid "$sudoPid" | head -n 1)
				echo "$(date +%T) no frame for $((now - updated)) s, killing" \
					"ffmpeg $ffmpegPid"
				[ -n "$ffmpegPid" ] && sudo -n kill -9 $ffmpegPid
				# a stopped ffmpeg stops sudo too, which then can't reap it
				sudo -n kill -CONT "$sudoPid" 2>/dev/null
				break
			fi
		done
		wait "$sudoPid" 2>/dev/null
		echo "$(date +%T) capture from $dev ended, restarting"
	fi
	sleep 2
done

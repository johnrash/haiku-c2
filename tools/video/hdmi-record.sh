#!/bin/sh
# Copyright 2026, haiku-odroid-c2 port authors.
# Distributed under the terms of the MIT License.
# hdmi-record.sh <seconds> <out.mkv>: pauses tools/hdmi-live.sh, records the
# capture stick at 1080p60 (MJPEG as is), then restarts the live loop.
root=/build/haiku-odroid-c2
loop=$(cat $root/run/hdmi-live.pid 2>/dev/null)
[ -n "$loop" ] && kill "$loop" 2>/dev/null
for p in $(pgrep -x ffmpeg); do sudo kill "$p"; done
sleep 1
# the stick, as hdmi-live.sh finds it (its number changes after a replug)
dev=$(for v in /sys/class/video4linux/video*; do
	if grep -q "Guermok USB[23] Video" "$v/name" 2>/dev/null \
			&& [ "$(cat "$v/index")" = 0 ]; then
		echo "/dev/$(basename "$v")"; fi; done | head -n 1)
sudo ffmpeg -hide_banner -loglevel error -f v4l2 -input_format mjpeg \
	-framerate 60 -video_size 1920x1080 -i "${dev:-/dev/video4}" -c:v copy \
	-t "$1" -y "$2"
sudo chown "$(id -u):$(id -g)" "$2"
cd $root && setsid tools/hdmi-live.sh > /dev/null 2>&1 < /dev/null &
echo "recorded $(du -h "$2" | cut -f1)"

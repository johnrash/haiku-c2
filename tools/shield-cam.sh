#!/bin/bash
# Copyright 2026, haiku-odroid-c2 port authors.
# Distributed under the terms of the MIT License.
#
# shield-cam.sh <name> [exposure]: a picture of the test C2's 16x2 LCD + IO
# Shield (milestone M68) through the C920 webcam on this laptop, which the
# user pointed at it: logs/shield/<name>.jpg, turned the right way up (the
# camera sees the shield upside down) and with its contrast stretched.
# Manual exposure (default 50; automatic washes the LCD out). The camera is
# only open to the desktop session, so ffmpeg runs through sudo, as for the
# HDMI capture.
set -e
cd "$(dirname "$0")/.."
name=$1
exposure=${2:-50}
device=$(for d in /sys/class/video4linux/video*; do
	if grep -q "C920" "$d/name" && [ "$(cat "$d/index")" = 0 ]; then
		echo "/dev/$(basename "$d")"; break
	fi
done)
[ -n "$device" ] || { echo "shield-cam: no C920" >&2; exit 1; }
mkdir -p logs/shield
sudo -n v4l2-ctl -d "$device" -c auto_exposure=1 \
	-c exposure_time_absolute="$exposure"
# a name only: root may not write into a file another user made in /tmp
tmp=$(mktemp -u /tmp/shield-cam.XXXXXX.jpg)
sudo -n timeout 30 ffmpeg -hide_banner -loglevel error -f v4l2 \
	-input_format mjpeg -video_size 1920x1080 -i "$device" -frames:v 15 \
	-update 1 -y "$tmp"
sudo -n chown "$(id -un)" "$tmp"
python3 -I - "$tmp" "logs/shield/$name.jpg" <<'PY'
import sys
from PIL import Image, ImageOps
image = Image.open(sys.argv[1]).rotate(180)
ImageOps.autocontrast(image, cutoff=0.5).save(sys.argv[2], quality=90)
PY
sudo -n rm -f "$tmp"
echo "logs/shield/$name.jpg"

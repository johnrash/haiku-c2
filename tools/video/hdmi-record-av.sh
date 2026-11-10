#!/bin/sh
# Copyright 2026, haiku-odroid-c2 port authors.
# Distributed under the terms of the MIT License.
# hdmi-record-av.sh <seconds> <out.mkv>: like hdmi-record.sh, with the
# capture stick's HDMI audio (ALSA card 1) as 16-bit PCM
root=$(cd "$(dirname "$0")/../.." && pwd)
loop=$(cat $root/run/hdmi-live.pid 2>/dev/null)
[ -n "$loop" ] && kill "$loop" 2>/dev/null
for p in $(pgrep -x ffmpeg); do sudo kill "$p"; done
sleep 1
# Both inputs are stamped with this laptop's wall clock and keep their time
# stamps (-copyts): otherwise ffmpeg starts each input at 0 on its own, and
# the offset between picture and sound in the file is arbitrary.
sudo ffmpeg -hide_banner -loglevel error -thread_queue_size 512 \
	-use_wallclock_as_timestamps 1 -f v4l2 \
	-input_format mjpeg -framerate 60 -video_size 1920x1080 -i /dev/video4 \
	-thread_queue_size 512 -use_wallclock_as_timestamps 1 \
	-f alsa -ar 48000 -ac 2 -i hw:1,0 \
	-copyts -c:v copy -c:a pcm_s16le -frames:v $(($1 * 60)) -y "$2"
sudo chown "$(id -u):$(id -g)" "$2"
cd $root && setsid tools/hdmi-live.sh > /dev/null 2>&1 < /dev/null &
echo "recorded $(du -h "$2" | cut -f1)"

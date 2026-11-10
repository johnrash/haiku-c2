#!/bin/sh
# Test streams for the S905 video decoder (milestone M26) and FFmpeg's
# decoding of them, frame by frame, as the decoder writes them (NV12), in
# run/vdec/: <stream> and <stream>.framemd5. tools/probes/vdectest.cpp
# compares its frames with these.
#   t480.m2v    MPEG-2 720x480, 30 frames/s, 90 frames, B-frames
#   t1080.m2v   MPEG-2 1920x1080, 60 frames (1080 is not a multiple of 32)
#   t288.m1v    MPEG-1 352x288, 25 frames/s, 50 frames
set -e
cd "$(dirname "$0")/.."
mkdir -p run/vdec
cd run/vdec
make() {
	ffmpeg -hide_banner -loglevel error -y -f lavfi \
		-i "testsrc2=size=$2:rate=$3" -frames:v "$4" -c:v "$5" -b:v "$6" \
		-g "$7" -bf 2 -f "$5" "$1"
	ffmpeg -hide_banner -loglevel error -y -i "$1" -f framemd5 \
		-pix_fmt nv12 "$1.framemd5"
	echo "$1: $(stat -c %s "$1") bytes, $(grep -vc '^#' "$1.framemd5") frames"
}
make t480.m2v 720x480 30 90 mpeg2video 4M 15
make t1080.m2v 1920x1080 30 60 mpeg2video 12M 15
make t288.m1v 352x288 25 50 mpeg1video 1M 12

# H.264 (Annex B), from libx264:
#   t480.h264    Main 720x480, 90 frames, 2 B-frames, CABAC
#   t360b.h264   Constrained Baseline 640x360 (CAVLC, no B-frames), 60 frames
#   t1080.h264   High 1920x1080 (coded 1088, cropped), 60 frames, B-pyramid,
#                4 reference frames, 8x8 transform
#   t720s.h264   High 1280x720, 60 frames, 4 slices per picture
make264() {
	name=$1 size=$2 rate=$3 frames=$4
	shift 4
	ffmpeg -hide_banner -loglevel error -y -f lavfi \
		-i "testsrc2=size=$size:rate=$rate" -frames:v "$frames" \
		-c:v libx264 -pix_fmt yuv420p "$@" -f h264 "$name"
	ffmpeg -hide_banner -loglevel error -y -i "$name" -f framemd5 \
		-pix_fmt nv12 "$name.framemd5"
	echo "$name: $(stat -c %s "$name") bytes, $(grep -vc '^#' "$name.framemd5") frames"
}
make264 t480.h264 720x480 30 90 -profile:v main -bf 2 -g 30 -b:v 2M
make264 t360b.h264 640x360 30 60 -profile:v baseline -g 30 -b:v 1M
make264 t1080.h264 1920x1080 30 60 -profile:v high -bf 3 -x264-params b-pyramid=normal \
	-refs 4 -g 30 -b:v 8M
make264 t720s.h264 1280x720 30 60 -profile:v high -bf 2 -slices 4 -g 30 -b:v 4M

# HEVC (Annex B), from libx265 (milestone M26 stage 3):
#   h240one.hevc  320x240, one IDR picture
#   h240i.hevc    320x240, 10 intra pictures
#   h1080i.hevc   1920x1080, 30 intra pictures, 20 Mbit/s
#   h480.hevc     720x480, 60 pictures, 3 B-frames (P/B: not decoded yet)
make265() {
	name=$1 size=$2 rate=$3 frames=$4
	shift 4
	ffmpeg -hide_banner -loglevel error -y -f lavfi \
		-i "testsrc2=size=$size:rate=$rate" -frames:v "$frames" \
		-c:v libx265 -pix_fmt yuv420p "$@" -f hevc "$name"
	ffmpeg -hide_banner -loglevel error -y -i "$name" -f framemd5 \
		-pix_fmt nv12 "$name.framemd5"
	echo "$name: $(stat -c %s "$name") bytes, $(grep -vc '^#' "$name.framemd5") frames"
}
make265 h240one.hevc 320x240 25 1 -x265-params log-level=error:keyint=1
make265 h240i.hevc 320x240 25 10 -x265-params log-level=error:keyint=1
make265 h1080i.hevc 1920x1080 30 30 -x265-params log-level=error:keyint=1 \
	-b:v 20M
make265 h480.hevc 720x480 30 60 -x265-params log-level=error:keyint=30:bframes=3 \
	-b:v 2M
make265 h480p.hevc 720x480 30 60 \
	-x265-params log-level=error:keyint=60:bframes=0 -b:v 2M
make265 h720b.hevc 1280x720 30 60 \
	-x265-params log-level=error:keyint=30:bframes=4:b-pyramid=1:ref=4 -b:v 4M
make265 h1080.hevc 1920x1080 30 60 -x265-params log-level=error -b:v 8M
make265 h480s.hevc 720x480 30 60 \
	-x265-params log-level=error:keyint=30:slices=4 -b:v 2M

# Portrait (milestone M41): 1080x1920, as phones film upright
make264 p1080.h264 1080x1920 30 60 -profile:v high -bf 3 \
	-x264-params b-pyramid=normal -refs 4 -g 30 -b:v 8M
make265 p1080.hevc 1080x1920 30 60 -x265-params log-level=error -b:v 8M

# The picture size changes (M41): 30 frames each of 1280x720, 1920x1080,
# 640x360 and 1080x1920, every part from an IDR picture. FFmpeg scales a
# stream to its first size, so the checksums are made part by part
# (tools/vdec-compare.py without a size takes each frame's from them).
#   rchange.h264, rchange.hevc
i=0
for size in 1280x720 1920x1080 640x360 1080x1920; do
	i=$((i + 1))
	ffmpeg -hide_banner -loglevel error -y -f lavfi \
		-i "testsrc2=size=$size:rate=30" -frames:v 30 -c:v libx264 \
		-pix_fmt yuv420p -profile:v high -bf 2 -g 30 -b:v 4M -f h264 rc_$i.h264
	ffmpeg -hide_banner -loglevel error -y -f lavfi \
		-i "testsrc2=size=$size:rate=30" -frames:v 30 -c:v libx265 \
		-pix_fmt yuv420p -x265-params log-level=error:keyint=30:bframes=2 \
		-b:v 4M -f hevc rc_$i.hevc
done
for codec in h264 hevc; do
	cat rc_1.$codec rc_2.$codec rc_3.$codec rc_4.$codec > rchange.$codec
	for i in 1 2 3 4; do
		ffmpeg -hide_banner -loglevel error -i rc_$i.$codec -f framemd5 \
			-pix_fmt nv12 - | grep -v '^#'
	done > rchange.$codec.framemd5
	rm -f rc_1.$codec rc_2.$codec rc_3.$codec rc_4.$codec
	echo "rchange.$codec: $(stat -c %s rchange.$codec) bytes, $(wc -l < rchange.$codec.framemd5) frames"
done

#!/bin/sh
# Save the newest HDMI frame from hdmi-live.sh as logs/hdmi/<name>.png.
# Usage: hdmi-grab.sh <name>
set -e
root=$(cd "$(dirname "$0")/.." && pwd)
live="$root/run/hdmi-live.png"
[ -f "$live" ] || { echo "hdmi-grab: hdmi-live.sh is not running" >&2; exit 1; }
mkdir -p "$root/logs/hdmi"
cp "$live" "$root/logs/hdmi/$1.png"
echo "hdmi-grab: logs/hdmi/$1.png ($(stat -c %y "$live" | cut -c12-19))"

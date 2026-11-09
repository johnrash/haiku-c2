#!/bin/bash
# gui-test.sh [output folder]
#
# Drives the breadcrumb buttons of the running (installed) Tracker with
# real mouse and keyboard events and checks where the window goes. Needs
# the Tracker settings "single window navigation", "show navigator" and
# "show full path in title bar". Screenshots end up in the output folder.
#
# The ODROID-C2 port's copy of tracker-breadcrumbs/tests/gui-test.sh: the
# breadcrumbs are in the system's Tracker there, the boards have no
# compiler (crumbinput is cross-built next to this script), and the long
# path is made for the test.

cd "$(dirname "$0")" || exit 1
OUT=${1:-/tmp/crumbtest}
mkdir -p "$OUT"

SETTINGS="$(finddir B_USER_SETTINGS_DIRECTORY)/Tracker/TrackerSettings"
for setting in SingleWindowBrowse ShowNavigator ShowFullPathInTitleBar; do
	if ! grep -q "^$setting on" "$SETTINGS"; then
		echo "gui-test: Tracker setting $setting must be on" >&2
		exit 2
	fi
done

CI="$(pwd)/crumbinput"
[ -x "$CI" ] || { echo "gui-test: no crumbinput next to this script" >&2; exit 2; }

PASS=0
FAIL=0
check()
{
	if [ "$2" = "$3" ]; then
		echo "ok   $1"
		PASS=$((PASS + 1))
	else
		echo "FAIL $1: expected \"$2\", got \"$3\""
		FAIL=$((FAIL + 1))
	fi
}

hey_result()
{
	sed -n 's/.*"result" ([A-Z_0-9]*) : //p' | head -1 \
		| sed 's/^"\(.*\)"$/\1/; s/ (0x[0-9a-fA-F]*)$//'
}

# The window title is the path and changes with it, so the window is
# found once and addressed by its index.
window_index()
{
	local count=$(hey Tracker count Window | hey_result)
	for ((i = 0; i < count; i++)); do
		if [ "$(hey Tracker get Title of Window $i | hey_result)" = "$1" ]; then
			echo $i
			return
		fi
	done
}

title() { hey Tracker get Title of Window $WINDOW | hey_result; }
location() { hey Tracker get Value of View Location of Window $WINDOW | hey_result; }
selected()
{
	hey Tracker get Selection of Poses of Window $WINDOW | hey_result \
		| sed 's#.*/##'
}

# wait_for <command> <expected>: polls up to 5 s
wait_for()
{
	for i in $(seq 50); do
		[ "$($1)" = "$2" ] && return
		sleep 0.1
	done
}

screenshot_window()
{
	kill $(roster | grep -i screen_blanker | awk '{print $1}') 2>/dev/null
	sleep 0.3
	screenshot -s -f png "$OUT/$1.png" >/dev/null 2>&1
}

team=$(roster | grep '(application/x-vnd.Be-TRAK)' | awk '{print $1}')
check "Tracker runs with the system's library" 1 \
	"$(listimage $team | grep -c '/boot/system/lib/libtracker.so')"

START=/boot/home/config/settings
open $START </dev/null >/dev/null 2>&1
for i in $(seq 50); do
	WINDOW=$(window_index $START)
	[ -n "$WINDOW" ] && break
	sleep 0.1
done
if [ -z "$WINDOW" ]; then
	echo "gui-test: no window for $START" >&2
	exit 1
fi
hey Tracker set Frame of Window $WINDOW to 'BRect(100,100,700,450)' >/dev/null
hey Tracker set Active of Window $WINDOW to 'bool(true)' >/dev/null

check "breadcrumb bar is there" breadcrumbs \
	"$(hey Tracker get InternalName of View breadcrumbs of Window $WINDOW | hey_result)"
check "location shows the folder" $START "$(location)"
screenshot_window start

# pressed look, then dragging off the button and releasing cancels
$CI $START press 2
screenshot_window pressed
$CI $START release 3
sleep 0.5
check "release outside the pressed button does nothing" $START "$(location)"

$CI $START click 2
wait_for location /boot/home
check "click on home/ goes to /boot/home" /boot/home "$(location)"
check "window title follows" /boot/home "$(title)"
wait_for selected config
check "the folder we came from is selected" config "$(selected)"

$CI /boot/home click 2
sleep 0.5
check "click on the current folder does nothing" /boot/home "$(location)"

$CI /boot/home click 0
wait_for location /
check "click on / goes to the root" / "$(location)"
wait_for selected boot
check "boot is selected in the root" boot "$(selected)"

# the breadcrumb jumps are part of the history
$CI / key 1c 2
wait_for location /boot/home
check "Back returns to /boot/home" /boot/home "$(location)"
$CI /boot/home key 1c 2
wait_for location $START
check "Back again returns to the start" $START "$(location)"
$CI $START key 1d 2
wait_for location /boot/home
check "Forward goes to /boot/home again" /boot/home "$(location)"

# Up still selects the folder we came from
$CI /boot/home click 1
wait_for location /boot
check "click on boot/ goes to /boot" /boot "$(location)"
wait_for selected home
check "home is selected in /boot" home "$(selected)"

# a long path in a narrow window: the buttons follow the text when it
# scrolls
LONG="$(finddir B_USER_NONPACKAGED_DIRECTORY)/apps/TrackerBreadcrumbs/lib"
mkdir -p "$LONG"
hey Tracker quit Window $WINDOW >/dev/null 2>&1
sleep 0.5
open "$LONG" </dev/null >/dev/null 2>&1
for i in $(seq 50); do
	WINDOW=$(window_index "$LONG")
	[ -n "$WINDOW" ] && break
	sleep 0.1
done
hey Tracker set Frame of Window $WINDOW to 'BRect(100,100,420,350)' >/dev/null
hey Tracker set Active of Window $WINDOW to 'bool(true)' >/dev/null
sleep 0.5
screenshot_window long-start
$CI "$LONG" key 6c 2
$CI "$LONG" key 04
sleep 0.3
screenshot_window long-end
$CI "$LONG" key 01
sleep 0.3
screenshot_window long-home
$CI "$LONG" click 3
wait_for location /boot/home/config
check "after scrolling back, config/ goes to /boot/home/config" \
	/boot/home/config "$(location)"
wait_for selected non-packaged
check "non-packaged is selected" non-packaged "$(selected)"

hey Tracker quit Window $WINDOW >/dev/null 2>&1

echo "gui-test: $PASS passed, $FAIL failed"
[ $FAIL -eq 0 ]

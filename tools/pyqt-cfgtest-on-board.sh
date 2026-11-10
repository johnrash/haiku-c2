#!/bin/sh
# Copyright 2026, haiku-odroid-c2 port authors.
# Distributed under the terms of the MIT License.
#
# pyqt-cfgtest-on-board.sh <test program> <output file>: PyQt-builder's
# PYQTBUILD_TEST_RUNNER for recording: runs one of PyQt6's configuration
# tests (built for the boards) on the test board and fetches what it wrote; the
# answers are kept in patches/python/pyqt6-6.12.0-cfgtests (milestone M85).
set -e
board=baron@10.42.0.2
opts="-o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=QUIET -o PubkeyAuthentication=no"
name=$(basename "$1")
sshpass -p haiku ssh $opts $board "mkdir -p /boot/home/pyqt-cfgtest"
sshpass -p haiku scp $opts "$1" $board:/boot/home/pyqt-cfgtest/
sshpass -p haiku ssh $opts $board \
	"cd /boot/home/pyqt-cfgtest && ./$name $name.out"
sshpass -p haiku scp $opts $board:/boot/home/pyqt-cfgtest/$name.out "$2"
if [ -n "$PYQT_CFGTEST_RECORD" ]; then
	mkdir -p "$PYQT_CFGTEST_RECORD"
	cp "$2" "$PYQT_CFGTEST_RECORD/$name.out"
fi

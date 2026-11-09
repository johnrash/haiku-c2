#!/bin/sh
# deadconsumer.sh: a camera consumer stopped (SIGSTOP) so that its port
# takes all buffers, then killed; the next consumer must get the camera.
while ps | grep -q "[c]amview"; do sleep 1; done
~/camview 1280 720 60 > /tmp/cv.txt 2>&1 &
sleep 6
t=$(ps | grep "[c]amview 1280 720 60" | awk '{print $(NF-3)}')
echo "consumer team: $t"
[ -n "$t" ] || exit 1
kill -STOP $t
sleep 8
kill -KILL $t
sleep 5
ps | grep -c "[c]amview"
~/camview 1280 720 5 2>&1 | tail -1

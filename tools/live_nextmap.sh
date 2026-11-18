#!/bin/sh
# in a split screen game: end it, go back to the lobby, choose another map (the presses that move from the current map to it
# in the map list are the arguments, e.g. "up up" or "down down down"), start it, wait, and take a picture
cd /home/claude/native
./live_key.sh -w 3 start > /dev/null
./live_key.sh -w 2 down down down a > /dev/null
./live_key.sh -w 2 up a > /dev/null
sleep 28
./live_key.sh -w 6 b > /dev/null
./live_key.sh -w 3 right a > /dev/null
./live_key.sh -w 3 a > /dev/null
./live_key.sh -w 2 "$@" a > /dev/null
sleep 3
./live_key.sh -w 2 left > /dev/null
./live_key.sh -w ${WAIT:-60} a | head -1
grep -n "stopped" /tmp/live.txt | head -2

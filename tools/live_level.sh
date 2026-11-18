#!/bin/sh
# after live_start.sh: go from the title screen into a split screen game on the map the lobby shows
# (the main menu must be remembering SPLIT SCREEN, as it does after one visit)
cd /home/claude/native
for i in 1 2 3 4 5 6 7 8 9 10 11 12; do grep -q "window is open" /tmp/live.txt 2>/dev/null && break; sleep 3; done
sleep 22
./live_key.sh -w 5 start > /dev/null
./live_key.sh -w 5 a > /dev/null
./live_key.sh -w 7 a > /dev/null
./live_key.sh -w 9 a > /dev/null
./live_key.sh -w 40 a | head -1

#!/bin/sh
# after live_start.sh: from the title screen into the campaign's first level (The Heretic: the intro cutscene), profile
# Fortune, through Campaign > Select Level (4 rows up from Metropolis, the level played last); the cutscene is NOT skipped
cd /home/claude/native
for i in 1 2 3 4 5 6 7 8 9 10 11 12; do grep -q "window is open" /tmp/live.txt 2>/dev/null && break; sleep 3; done
sleep 22
./live_key.sh -w 6 start > /dev/null
./live_key.sh -w 7 a > /dev/null
row=$(python3 /home/claude/scripts/menu_row.py /tmp/live.png 2>/dev/null)
while [ "$row" -gt 0 ]; do ./live_key.sh -w 2.5 up > /dev/null; row=$((row-1)); done
./live_key.sh -w 4 a > /dev/null
./live_key.sh -w 3 down > /dev/null
./live_key.sh -w 4 a > /dev/null
n=${1:-4}; while [ "$n" -gt 0 ]; do ./live_key.sh -w 2 up > /dev/null; n=$((n-1)); done
./live_key.sh -w 4 a > /dev/null
./live_key.sh -w 5 a | head -1

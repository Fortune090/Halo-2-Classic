#!/bin/sh
# as live_campaign.sh, for a main menu that remembers SPLIT SCREEN (two rows below CAMPAIGN): profile Fortune,
# Campaign > Select Level, $1 rows up (negative: down) from the level played last, start, skip the cutscene. $2: load wait
cd /home/claude/native
sleep 12
for i in 1 2 3 4 5 6 7 8 9 10 11 12; do grep -q "window is open" /tmp/live.txt 2>/dev/null && break; sleep 3; done
sleep 24
./live_key.sh -w 7 start > /dev/null
./live_key.sh -w 7 a > /dev/null
./live_key.sh -w 2 up > /dev/null
./live_key.sh -w 3 up > /dev/null
./live_key.sh -w 5 a > /dev/null
./live_key.sh -w 3 down > /dev/null
./live_key.sh -w 5 a > /dev/null
n=${1:-0}
while [ "$n" -gt 0 ]; do ./live_key.sh -w 3 up > /dev/null; n=$((n-1)); done
while [ "$n" -lt 0 ]; do ./live_key.sh -w 3 down > /dev/null; n=$((n+1)); done
./live_key.sh -w 4 a > /dev/null
./live_key.sh -w ${2:-75} a > /dev/null
./live_key.sh -w 14 a > /dev/null
./live_key.sh -w 14 a | head -1

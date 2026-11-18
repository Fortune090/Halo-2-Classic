#!/bin/sh
# after live_start.sh: from the title screen into Metropolis with the profile "Fortune" (the profile list opens on it),
# through Campaign > Select Level; then the opening cutscene is skipped. The main menu wraps round and remembers its row,
# so the highlighted row is read from a frame picture first.
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
./live_key.sh -w 4 a > /dev/null
./live_key.sh -w ${1:-75} a > /dev/null
./live_key.sh -w 14 a | head -1

#!/bin/sh
# after live_start.sh: pictures of the menus for the Widescreen Fix. $1: presses of "down" from the main menu's remembered row
# to SPLIT SCREEN (2 from CAMPAIGN, 0 if it remembers SPLIT SCREEN); $2: a tag for the files. Saves /tmp/m<tag>_<name>.png:
# title, mainmenu, profile, lobby, game, score (Back held), pause
cd /home/claude/native
K=run/work2/keys.txt
T=${2:-}
newest() { ls -t run/work2/h2classic_frame_*.bmp 2>/dev/null | head -1; }
grab() { f=$(newest); [ -n "$f" ] && python3 -c "
from PIL import Image
import sys
Image.open(sys.argv[1]).convert('RGB').save(sys.argv[2])" "$f" /tmp/m${T}_$1.png && echo "$1 $f"; }
shot() { before=$(newest); echo shot >> $K; for i in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20; do sleep 0.5; [ "$(newest)" != "$before" ] && break; done; grab $1; }
key() { echo "$1" >> $K; sleep ${2:-1.5}; }
sleep 12
for i in 1 2 3 4 5 6 7 8 9 10 11 12; do grep -q "window is open" /tmp/live.txt 2>/dev/null && break; sleep 3; done
sleep 22
shot title
key start 7; key a 6; shot mainmenu
n=${1:-0}; while [ "$n" -gt 0 ]; do key down 2; n=$((n-1)); done
key a 7; shot profile
key a 7; shot lobby
key a 45; shot game
before=$(newest); printf 'back*150\nshot\n' >> $K; sleep 6; grab score
key start 4; shot pause

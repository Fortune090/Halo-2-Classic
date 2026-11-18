#!/bin/sh
# after live_start.sh with H2N_TESTPADS=2: split screen with a second player on port 2 (profile Halo0002), then a game
cd /home/claude/native
K=run/work2/keys.txt
newest() { ls -t run/work2/h2classic_frame_*.bmp 2>/dev/null | head -1; }
shot() { before=$(newest); echo shot >> $K; for i in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20; do sleep 0.5; [ "$(newest)" != "$before" ] && break; done; f=$(newest); python3 -c "
from PIL import Image
import sys
Image.open(sys.argv[1]).convert('RGB').save(sys.argv[2])" "$f" /tmp/c_$1.png && echo "$1 $f"; }
key() { echo "$1" >> $K; sleep ${2:-1.5}; }
sleep 12
for i in 1 2 3 4 5 6 7 8 9 10 11 12; do grep -q "window is open" /tmp/live.txt 2>/dev/null && break; sleep 3; done
sleep 22
key start 7; key a 6
key a 7
key 2:a 5; key 2:down 3; key 2:a 6; shot 5ready
key a 8; shot 6lobby
key a 50; shot 7game
key forward*60 4; shot 8game2

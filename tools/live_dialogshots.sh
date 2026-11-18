#!/bin/sh
# after live_start.sh: the main menu, then its Sign Out dialog (B) - pictures a second apart (/tmp/d<tag>_NN.png)
cd /home/claude/native
K=run/work2/keys.txt
T=${1:-}
newest() { ls -t run/work2/h2classic_frame_*.bmp 2>/dev/null | head -1; }
shot() { before=$(newest); echo shot >> $K; for i in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20; do sleep 0.5; [ "$(newest)" != "$before" ] && break; done; f=$(newest); python3 -c "
from PIL import Image
import sys
Image.open(sys.argv[1]).convert('RGB').save(sys.argv[2])" "$f" /tmp/d${T}_$1.png && echo "$1 $f"; }
sleep 12
for i in 1 2 3 4 5 6 7 8 9 10 11 12; do grep -q "window is open" /tmp/live.txt 2>/dev/null && break; sleep 3; done
sleep 22
echo start >> $K; sleep 7; echo a >> $K; sleep 6
shot 00
echo b >> $K; sleep 2
for n in 01 02 03 04 05; do shot $n; sleep 1; done

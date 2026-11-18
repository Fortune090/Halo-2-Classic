#!/bin/sh
# after live_start.sh: as live_campaign.sh up to starting the level ($1 rows up from the level played last), then a
# saved frame every $2 seconds (default 1.5) for $3 shots (default 24), each copied to /tmp/load_NN.png
cd /home/claude/native
sleep 12
for i in 1 2 3 4 5 6 7 8 9 10 11 12; do grep -q "window is open" /tmp/live.txt 2>/dev/null && break; sleep 3; done
sleep 24
./live_key.sh -w 7 start > /dev/null
./live_key.sh -w 7 a > /dev/null
./live_key.sh -w 5 a > /dev/null
./live_key.sh -w 3 down > /dev/null
./live_key.sh -w 5 a > /dev/null
n=${1:-0}
while [ "$n" -gt 0 ]; do ./live_key.sh -w 3 up > /dev/null; n=$((n-1)); done
while [ "$n" -lt 0 ]; do ./live_key.sh -w 3 down > /dev/null; n=$((n+1)); done
./live_key.sh -w 4 a > /dev/null
cp /tmp/live.png /tmp/load_pre.png
echo a >> run/work2/keys.txt
i=0
while [ $i -lt ${3:-24} ]; do
  sleep ${2:-1.5}
  echo shot >> run/work2/keys.txt
  sleep 0.6
  f=$(ls -t run/work2/h2classic_frame_*.bmp 2>/dev/null | head -1)
  [ -n "$f" ] && python3 -c "
from PIL import Image
import sys
Image.open(sys.argv[1]).convert('RGB').save(sys.argv[2])" "$f" /tmp/load_$(printf %02d $i).png && echo "$i $f"
  i=$((i+1))
done
tail -2 /tmp/live.txt | cut -c1-160

#!/bin/sh
# usage: live_key.sh [-w seconds] name...   each name is one press (or "shot"); then the newest saved frame becomes /tmp/live.png
cd /home/claude/native/run/${H2N_LIVE_DIR:-work2} || exit 1
wait=1.2
if [ "$1" = -w ]; then wait=$2; shift 2; fi
for k in "$@"; do echo "$k" >> keys.txt; sleep $wait; done
echo shot >> keys.txt; sleep 2.5
f=$(ls -t h2classic_frame_*.bmp 2>/dev/null | head -1)
[ -n "$f" ] && python3 -c "
from PIL import Image
import sys
Image.open(sys.argv[1]).convert('RGB').save('/tmp/live.png')" "$f" && echo "$f"
tail -2 /tmp/live.txt | cut -c1-160

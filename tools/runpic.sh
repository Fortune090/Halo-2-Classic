#!/bin/sh
# run the current build with a window (under Xvfb) against the real game files; frames named in H2N_SHOT are saved
# usage: runpic.sh [seconds] [keep]   extra settings come from the environment (H2N_SHOT, H2N_GFXTRACE, H2N_D3DTRACE ...)
cd /home/claude/native/run/work2 || exit 1
export H2N_NOLAUNCHER=1 WINEDEBUG=-all WINEPREFIX=/home/claude/.wine32
if [ "$2" != keep ]; then rm -f h2classic_stop.bin h2classic_log.txt; [ -d h2native ] && mv h2native /tmp/h2native_old_$$; fi
rm -f h2classic_stop.bin h2classic_frame_*.bmp
for attempt in 1 2 3 4; do
  H2N_SECONDS=${1:-20} timeout 400 xvfb-run -a -s "-screen 0 1600x1200x24" wine ../../out/halo2_native.exe "Z:\\home\\claude\\native\\run\\real" > /tmp/pic.txt 2>&1
  rc=$?
  grep -q "did not start" /tmp/pic.txt || break     # Wine under Xvfb now and then gives no Direct3D: run again
  sleep 2
done
echo "exit=$rc lines=$(wc -l < /tmp/pic.txt) attempt=$attempt"
grep -n "picture:\|\*\*\*" /tmp/pic.txt | cut -c1-260 | head -${PICLINES:-40}
grep -n "^frame" /tmp/pic.txt | tail -3 | cut -c1-260
grep -n "stopped\|===\|FAILED\|no frame" -A3 /tmp/pic.txt | cut -c1-230 | head -16
[ -f h2classic_stop.bin ] && python3 /home/claude/scripts/readdump.py h2classic_stop.bin ../../out/halo2_native.exe ../../out/halo2_native.map | sed -n 5,26p | cut -c1-150
ls h2classic_frame_*.bmp 2>/dev/null | head -20

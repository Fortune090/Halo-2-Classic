#!/bin/sh
# run the current build against the real game files in run/real; print how it ended and, if it stopped, where
# usage: runreal.sh [seconds] [keep]   (keep = do not start from an empty work folder)
cd /home/claude/native/run/work2 || exit 1
export H2N_NOLAUNCHER=1 WINEDEBUG=-all WINEPREFIX=/home/claude/.wine32
[ "$2" = keep ] || { rm -f h2classic_stop.bin h2classic_log.txt; rm -rf h2classic; }
rm -f h2classic_stop.bin
H2N_SECONDS=${1:-60} timeout 400 wine ../../out/halo2_native.exe "Z:\\home\\claude\\native\\run\\real" > /tmp/real.txt 2>&1
echo "exit=$? lines=$(wc -l < /tmp/real.txt)"
grep -n "^\[ *[0-9]*\]" /tmp/real.txt | tail -6 | cut -c1-170
grep -n "^frame" /tmp/real.txt | tail -3 | cut -c1-230
grep -n "stopped\|===\|FAILED\|no frame\|debug text" -A3 /tmp/real.txt | cut -c1-230 | head -24
[ -f h2classic_stop.bin ] && python3 /home/claude/scripts/readdump.py h2classic_stop.bin ../../out/halo2_native.exe ../../out/halo2_native.map | sed -n 5,26p | cut -c1-150

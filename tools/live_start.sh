#!/bin/sh
# start the current build under Xvfb and leave it running, taking presses from run/work2/keys.txt (see live_key.sh)
# extra settings come from the environment; stop it with live_stop.sh
cd /home/claude/native/run/work2 || exit 1
export H2N_NOLAUNCHER=1 WINEDEBUG=-all WINEPREFIX=/home/claude/.wine32 DISPLAY=:77
pgrep -f "Xvfb :77" > /dev/null || { Xvfb :77 -screen 0 1600x1200x24 > /dev/null 2>&1 & sleep 2; }
rm -f h2classic_stop.bin h2classic_frame_*.bmp keys.txt; : > keys.txt
H2N_KEYFILE=keys.txt exec wine ../../out/halo2_native.exe "Z:\\home\\claude\\native\\run\\real" > /tmp/live.txt 2>&1

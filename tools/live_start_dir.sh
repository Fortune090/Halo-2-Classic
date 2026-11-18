#!/bin/sh
# as live_start.sh, but in the folder given as the first argument (under run/), with its own keys.txt
cd /home/claude/native/run/"$1" || exit 1
export H2N_NOLAUNCHER=1 WINEDEBUG=-all WINEPREFIX=/home/claude/.wine32 DISPLAY=:77
pgrep -f "Xvfb :77" > /dev/null || { Xvfb :77 -screen 0 1600x1200x24 > /dev/null 2>&1 & sleep 2; }
rm -f h2classic_stop.bin keys.txt; : > keys.txt
H2N_KEYFILE=keys.txt exec wine ../../out/halo2_native.exe "Z:\\home\\claude\\native\\run\\real" > /tmp/live.txt 2>&1

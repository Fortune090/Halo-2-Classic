#!/bin/sh
# close the window of the running build (so that it writes its closing report), then make sure nothing is left
export H2N_NOLAUNCHER=1 WINEDEBUG=-all WINEPREFIX=/home/claude/.wine32 DISPLAY=:77
cd /home/claude/d3dtest && timeout 20 wine closer.exe > /dev/null 2>&1
sleep 4
pkill -f halo2_native.exe 2>/dev/null
tail -5 /tmp/live.txt | cut -c1-200

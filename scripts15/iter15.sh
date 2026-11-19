#!/bin/sh
# iter15.sh [seconds]: regenerate retail_code.S (gen_code15.py), build the 1.5 exe, run it in work_s10o, print how it ended
cd /home/claude/v15 && timeout 900 python3 gen_code15.py > /tmp/gen15.txt 2>&1 || { tail -3 /tmp/gen15.txt; exit 1; }
cd /home/claude/native15 && timeout 900 ./build15.sh s10p_v15 > out/build.log 2>&1 || { echo "build failed"; tail -5 out/build.log; exit 1; }
cd /home/claude/native/run/work_s10o || exit 1
export H2N_NOLAUNCHER=1 WINEDEBUG=-all WINEPREFIX=/home/claude/.wine32 DISPLAY=:77
pgrep -f "Xvfb :77" > /dev/null || { Xvfb :77 -screen 0 1600x1200x24 > /dev/null 2>&1 & sleep 2; }
rm -f h2classic_stop.bin
H2N_SECONDS=${1:-75} timeout 300 wine /home/claude/native15/out/halo2_native15.exe > /tmp/v15.txt 2>&1
echo "exit=$?"
grep -n "\*\*\* stopped\|cannot continue\|no frame presented for 5 s" h2classic_log.txt | head -2 | cut -c1-260
grep "^frame" h2classic_log.txt | tail -1 | cut -c1-100
[ -f h2classic_stop.bin ] && grep -q "stopped: exception" h2classic_log.txt && python3 /home/claude/scripts/readdump.py h2classic_stop.bin /home/claude/native15/out/halo2_native15.exe /home/claude/native15/out/halo2_native15.map 2>&1 | sed -n 5,14p | cut -c1-150
true

#!/bin/sh
# run15.sh [env assignments...]: run the current 1.5 exe once (60 s) and print how it ended
cd /home/claude/native/run/work_s10o || exit 1
export H2N_NOLAUNCHER=1 WINEDEBUG=-all WINEPREFIX=/home/claude/.wine32 DISPLAY=:77
rm -f h2classic_stop.bin
env "$@" H2N_SECONDS=${SECS:-60} timeout 300 wine /home/claude/native15/out/halo2_native15.exe > /tmp/v15.txt 2>&1
echo "exit=$? $*"
grep -n "\*\*\* stopped\|cannot continue\|no frame presented for 5 s" h2classic_log.txt | head -1 | cut -c1-200
grep "^frame" h2classic_log.txt | tail -1 | cut -c1-60

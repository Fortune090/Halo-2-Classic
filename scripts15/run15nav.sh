#!/bin/sh
# run15nav.sh "<key> <seconds> <key> <seconds> ..." [env...]: run the 1.5 exe and press keys from a key file, each after
# the given wait (wall-clock seconds), with a picture saved ("shot") after each press; prints how it ended
cd /home/claude/native/run/work_s10o || exit 1
export H2N_NOLAUNCHER=1 WINEDEBUG=-all WINEPREFIX=/home/claude/.wine32 DISPLAY=:77
SEQ="$1"; shift
rm -f h2classic_stop.bin keys.txt h2classic_frame_*.bmp; : > keys.txt
( set -- $SEQ; while [ $# -ge 2 ]; do sleep $2; echo $1 >> keys.txt; sleep 3; echo shot >> keys.txt; shift 2; done ) &
env "$@" H2N_KEYFILE=keys.txt H2N_SECONDS=${SECS:-200} timeout 400 wine /home/claude/native15/out/halo2_native15.exe > /tmp/v15k.txt 2>&1
echo "exit=$? $*"
grep -n "\*\*\* stopped\|cannot continue\|Direct3D 9 did not" h2classic_log.txt | head -2 | cut -c1-200
grep "frames presented" h2classic_log.txt | tail -1 | cut -c1-60
grep -n "from the key file" h2classic_log.txt | sed 's/.*controller: //' | tr '\n' ';' | cut -c1-600; echo
ls h2classic_frame_*.bmp 2>/dev/null | tr '\n' ' '

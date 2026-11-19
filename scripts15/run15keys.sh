#!/bin/sh
# run15keys.sh [env...]: run the 1.5 exe, press start / pick the profile / don't sign in / split screen / player 1 A,
# then report how it ended (for finding what breaks the split-screen lobby)
cd /home/claude/native/run/work_s10o || exit 1
export H2N_NOLAUNCHER=1 WINEDEBUG=-all WINEPREFIX=/home/claude/.wine32 DISPLAY=:77
rm -f h2classic_stop.bin keys.txt h2classic_frame_*.bmp; : > keys.txt
( sleep 62; for k in start; do echo $k >> keys.txt; done; sleep 4; echo down >> keys.txt; sleep 2; echo a >> keys.txt; sleep 4; echo a >> keys.txt; sleep 5;
  echo down >> keys.txt; sleep 2; echo down >> keys.txt; sleep 2; echo a >> keys.txt; sleep 5; echo a >> keys.txt; sleep 8; echo shot >> keys.txt ) &
env "$@" H2N_KEYFILE=keys.txt H2N_SECONDS=110 timeout 300 wine /home/claude/native15/out/halo2_native15.exe > /tmp/v15k.txt 2>&1
echo "exit=$? $*"
grep -n "\*\*\* stopped\|cannot continue" h2classic_log.txt | head -1 | cut -c1-200
grep "frames presented" h2classic_log.txt | tail -1 | cut -c1-60

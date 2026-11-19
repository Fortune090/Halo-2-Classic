#!/bin/sh
# aa_menu_run.sh <label> : with the H2N_ settings in the environment, start the current build under Xvfb (live_start.sh),
# go through the title, the profile and the sign-in to the main menu, let the run end by itself (H2N_SECONDS), and keep
# the log as /tmp/aa_<label>.txt
cd /home/claude/native
export H2N_SECONDS=${H2N_SECONDS:-150}
(setsid ./live_start.sh </dev/null >/dev/null 2>&1 &)
for i in $(seq 1 40); do grep -q "window is open" /tmp/live.txt 2>/dev/null && break; sleep 2; done
sleep 25
./live_key.sh -w 12 'start*12' > /dev/null
./live_key.sh -w 20 'a*24' > /dev/null
./live_key.sh -w 20 'a*24' > /dev/null
for i in $(seq 1 120); do grep -q "frames presented" run/work2/h2classic_log.txt 2>/dev/null && break; sleep 3; done
sleep 3
cp run/work2/h2classic_log.txt /tmp/aa_$1.txt
grep -n "views done\|moved by the sub-pixel\|DLSS\|TAA is used\|takes over\|GFXCACHE=2 check\|frames presented" /tmp/aa_$1.txt | cut -c1-220 | tail -12

#!/bin/sh
# nametest.sh <label> NAME=value...: two players in split screen on Warlock (1.5); player 1 kills himself with his two grenades
# and his view then follows player 2, whose name is drawn above the head; every frame of that is saved; then the name's size.
cd /home/claude/v15tools || exit 1
l=$1; shift
timeout 295 python3 nav.py --name $l H2N_UNLOCK=1 H2N_TESTPADS=2 "$@" ready start '?\profile' sleep/2 down a/3 a/4 down down a/4 a/5 2:a/3 2:a/3 2:a/3 right a/3 a/3 down/0.6 down/0.6 down/0.6 down/0.6 down/0.6 down/0.6 down/0.6 down/0.6 down/0.6 down/0.6 a/3 left a/2 '?warlock.map' sleep/10 'lookdown*40' lt/5 lt/0.5 'lookleft*160+shots' sleep/22 2>&1 | tail -3 | cut -c1-200
cd /home/claude/native/run/work_s10o && python3 /home/claude/v15tools/namewidth.py $(ls h2classic_frame_*.bmp | tail -12) | grep -v 'no name' | tail -3

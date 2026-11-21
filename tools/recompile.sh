#!/bin/sh
# recompile the named game objects (after their sources changed), rewrite all objects and rebuild
# usage: recompile.sh <build id> <object stem>...     e.g. recompile.sh fix1 unknown_03bcb0 stubs_lane_w
cd /home/claude/native || exit 1
id=$1; shift
export H2N_NOLAUNCHER=1 WINEDEBUG=-all WINEPREFIX=/home/claude/.wine32 XDK_DIR=/home/claude/xdkw
for f in "$@"; do rm -f obj2/$f.obj; done
: > compile_log.txt
python3 /home/claude/scripts/native_compile.py /home/claude/h2decomp_src/halo2-decompiled-main /home/claude/native/obj2 4 | tail -1
grep -n "error" compile_log.txt | head -20 | cut -c1-240
for f in "$@"; do [ -f obj2/$f.obj ] || { echo "NOT COMPILED: $f"; exit 1; }; done
python3 /home/claude/scripts/add_globals.py && python3 /home/claude/scripts/rewrite.py > out/rewrite.log 2>&1; head -1 out/rewrite.log | cut -c1-200
# rewrite.py makes every obj_r object again: the anti-aliasing view hook (scene_hook.py, s9w) has to go back on each time
python3 /home/claude/scripts/scene_hook.py obj_r/unknown_023540.obj obj_r/unknown_023540.obj | tail -1
python3 /home/claude/scripts/sg_hooks.py obj_r | tail -3
./build2.sh $id > out/build.log 2>&1; echo "build rc=$?"; grep -n "error\|undefined\|duplicate" out/build.log | head -20 | cut -c1-240

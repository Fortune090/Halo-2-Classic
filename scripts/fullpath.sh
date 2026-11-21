#!/bin/sh
# fullpath.sh <pristine decomp with its own build done (tools/check.py)>: the tables of the native build made again after
# the decompilation changed. Before it: the patched tree in /home/claude/h2decomp_src (patch applied, scripts/gen_tu.py
# run there) compiled by native_compile.py into native/obj2. After it: v15/chain15.sh, then native/build3.sh.
# Each step's last lines go to /tmp/fullpath_<step>.log; the script stops at the first step that fails.
P=${1:?the pristine tree}
PREV=${2:-}      # the retail_code.S of the build before (keep_routes.py), when the route list of that build matters
S=/home/claude/scripts; N=/home/claude/native
export RETAIL_XBE=/home/claude/xbe/default.xbe
cd $N || exit 1
ls $N/obj2/*.obj > $N/objects2.rsp
step() { name=$1; shift; echo "== $name"; "$@" > /tmp/fullpath_$name.log 2>&1; rc=$?; tail -2 /tmp/fullpath_$name.log | cut -c1-220; [ $rc = 0 ] || { echo "$name FAILED rc=$rc"; exit 1; }; }
step oracle        python3 $S/oracle.py $P
step undef3        python3 $S/undef3.py
step plan          python3 $S/plan.py
step oracle2       python3 $S/oracle2.py $P
step scan          python3 $S/scan.py
step scan2         python3 $S/scan2.py
step statics1      python3 $S/statics1.py $P
step statics2      python3 $S/statics2.py
step vtables       python3 $S/vtables.py
step retailconv    python3 $S/retailconv.py
step unproven      python3 $S/unproven_routes.py
step pointer       python3 $S/pointer_routes.py
step immediates    python3 $S/code_immediates.py
step add_globals   python3 $S/add_globals.py
step rewrite       python3 $S/rewrite.py
step scene_hook    python3 $S/scene_hook.py obj_r/unknown_023540.obj obj_r/unknown_023540.obj
step sg_hooks      python3 $S/sg_hooks.py obj_r
step gen           python3 $S/gen.py
step fix_resolve   python3 $S/fix_resolve.py
step add_globals2  python3 $S/add_globals.py
step gen_code1     python3 $S/gen_code.py
step keep_routes   python3 $S/keep_routes.py $PREV
step gen_code      python3 $S/gen_code.py
echo "full path done"

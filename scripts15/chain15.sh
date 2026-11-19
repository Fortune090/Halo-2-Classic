#!/bin/sh
# chain15.sh: the whole 1.5 table chain from the current 1.0 build (native/) - then iter15.sh builds and runs
cd /home/claude/v15 || exit 1
step() { echo "== $1"; shift; timeout 1200 "$@" > /tmp/chain15_step.log 2>&1 || { echo "FAILED"; tail -5 /tmp/chain15_step.log; exit 1; }; tail -1 /tmp/chain15_step.log | cut -c1-200; }
step map15 python3 map15.py
step reach15 python3 reach15.py
step boundary15 python3 boundary15.py
step conv15 python3 conv15.py
step adapt15 python3 adapt15.py
step vt15 python3 vt15.py
step syms15 python3 syms15.py
step tables15 python3 tables15.py
step gen_code15 python3 gen_code15.py
step back15 python3 back15.py
step backadapt15 python3 backadapt15.py
step callsite15 python3 callsite15.py
step taintall15 python3 taintall15.py
step gen_code15 python3 gen_code15.py
echo chain done

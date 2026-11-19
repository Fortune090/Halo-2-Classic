#!/bin/sh
# rev.sh <1.0 addr>... : compact review lines (final convention, prototype, one 1.0 call, 1.5 entry)
for a in "$@"; do
  python3 /home/claude/v15tools/final.py $B $a | cut -c1-170
  python3 /home/claude/v15tools/trapinfo.py $a 2>&1 | grep -E "src .*\)$" | head -1 | cut -c1-200
  python3 /home/claude/v15tools/trapinfo.py $a 2>&1 | grep -E "1.0 call" | head -2 | rev | cut -c1-200 | rev
  python3 /home/claude/v15tools/trapinfo.py $a 2>&1 | grep -E "1.5 entry" | cut -c1-260
done

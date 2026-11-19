#!/usr/bin/env python3
# dis.py <10|15> <addr> [n] : n instructions of the retail function at addr, on one ';'-joined line per 6
import sys
sys.path.insert(0, '/home/claude/v15')
from xmap15_lib import r10, r15, md
v, a = sys.argv[1], int(sys.argv[2], 16); n = int(sys.argv[3]) if len(sys.argv) > 3 else 30
rd = r10 if v == '10' else r15
code = rd(a, n * 8); out = []
for i in md.disasm(code, a):
    out.append('%x: %s %s' % (i.address, i.mnemonic, i.op_str))
    if len(out) >= n: break
for i in range(0, len(out), 4): print(' ; '.join(out[i:i+4]))

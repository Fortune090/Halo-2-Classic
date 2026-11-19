#!/usr/bin/env python3
"""callers10.py <1.0 address>...: the functions of the 1.0 XBE that call the given addresses (E8 calls), with their source file."""
import sys, struct, bisect, csv
sys.path.insert(0, '/home/claude/v15')
import xmap15_lib as X
rows = []
for r in csv.reader(open('/home/claude/h2decomp_1118b_pure/halo2-decompiled-main/config/functions.csv', errors='replace')):
    try: rows.append((int(r[0], 16), r))
    except Exception: pass
rows.sort(); ks = [a for a, _ in rows]
tg = {int(a, 16) for a in sys.argv[1:]}
out = {}
for s in X.S10:
    if not (s['fl'] & 4): continue
    try: d = X.r10(s['va'], s['rs'])
    except Exception: continue
    j = d.find(b'\xe8')
    while j >= 0 and j + 5 <= len(d):
        t = (s['va'] + j + 5 + struct.unpack_from('<i', d, j + 1)[0]) & 0xffffffff
        if t in tg:
            k = bisect.bisect(ks, s['va'] + j) - 1
            out.setdefault((t, rows[k][0]), []).append(s['va'] + j)
        j = d.find(b'\xe8', j + 1)
for (t, fa), al in sorted(out.items()):
    r = [x for a, x in rows if a == fa][0]
    print('%x <- %x size %s %s %s  %s' % (t, fa, r[1], r[8][:34] if len(r) > 8 else '', r[9] if len(r) > 9 else '', ' '.join('%x' % a for a in al[:6])))

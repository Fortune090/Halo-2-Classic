"""fmap2.py: look for the twins of fmap.py's 'changed' functions anywhere in 1.5's code (fmap.py searched only within
0x20000 of the running offset, so functions that 1.5's link moved came out 'changed'). A twin has the 1.0 function's
bytes wherever the 1.0 function's own mask (fmap_lib.mask) keeps them. Writes moved15.json: {a10: [a15 candidates]}."""
import json, csv, sys, collections
sys.path.insert(0, '/home/claude/v15')
from fmap_lib import load, text, mask
D10, S10 = load('/home/claude/xbe/default.xbe'); D15, S15 = load('/home/claude/v15/default_v15.xbe')
T10va, T10 = text(D10, S10); T15va, T15 = text(D15, S15)
fm = json.load(open('/home/claude/v15/fmap.json'))
size = {int(r[0], 16): int(r[1]) for r in csv.reader(open('/home/claude/h2decomp_src/halo2-decompiled-main/config/functions.csv'))
        if r and r[0][:1] in '0123456789abcdef' and r[1].isdigit()}
out = {}; st = collections.Counter()
for k, (t, kind) in fm.items():
    if kind != 'changed': continue
    va = int(k, 16); n = size[va]; o = va - T10va; b = T10[o:o + n]; m = mask(b)
    best = (0, 0); s0 = None
    for i in range(n + 1):
        if i < n and m[i]:
            if s0 is None: s0 = i
        else:
            if s0 is not None and i - s0 > best[1] - best[0]: best = (s0, i)
            s0 = None
    lit = b[best[0]:best[1]]
    if len(lit) < 5: st['too short'] += 1; continue
    good = []; p = T15.find(lit)
    while p >= 0:
        s = p - best[0]
        if 0 <= s and s + n <= len(T15) and all((not y) or x == z for x, y, z in zip(b, m, T15[s:s + n])): good.append(s + T15va)
        p = T15.find(lit, p + 1)
    if good: out[k] = good
    st[min(len(good), 2)] += 1
json.dump(out, open('/home/claude/v15/moved15.json', 'w'))
print({str(a): b for a, b in st.items()})

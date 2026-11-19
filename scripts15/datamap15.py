"""datamap15.py: map15's data votes made into a map for any 1.0 data address: an address same code uses maps as the
votes say; one between two voted addresses that moved by the same amount moves by that amount too; otherwise it is
'uncertain' (moved by the amount of the voted address below it, and listed). Library data sections (DSOUND, XONLINE's
XON_RD, BINKDATA, DOLBY, D3D, XPP...) move with their section. Use: from datamap15 import map_data."""
import json, bisect, sys
sys.path.insert(0, '/home/claude/v15')
from fmap_lib import load
M = json.load(open('/home/claude/v15/map15.json'))
_D10, S10 = load('/home/claude/xbe/default.xbe'); _D15, S15 = load('/home/claude/v15/default_v15.xbe')
votes = sorted((int(k, 16), v) for k, v in M['data'].items())
ties = {int(k, 16) for k in M['ties']}
GAME = ('.rdata', '.data', '.data1', '.text')
def sec10(a):
    for i, s in enumerate(S10):
        if s['va'] <= a < s['va'] + max(s['vs'], s['rs']): return i
    return None
bysec = {}
for a, b in votes:
    i = sec10(a)
    if i is not None: bysec.setdefault(i, []).append((a, b))
for i in bysec: bysec[i].sort()
def _rd(D, S, va, n):
    for s in S:
        if s['va'] <= va and va + n <= s['va'] + s['rs']:
            o = s['raw'] + va - s['va']; return D[o:o + n]
    return None
def _mask(b):
    import struct
    m = bytearray(b'\1' * len(b))
    for i in range(0, len(b) - 3):
        v = struct.unpack_from('<I', b, i)[0]
        if 0x10000 <= v < 0x700000: m[i:i + 4] = b'\0\0\0\0'
    return m
NEXT = []      # the start of the next known object after an address (retail_syms.S), set by set_objects()
def set_objects(addrs):
    global NEXT
    NEXT = sorted(set(addrs))
def map_data(a):
    """-> (1.5 address, how): 'vote', 'between', 'section', 'uncertain', or (None, 'outside')"""
    i = sec10(a)
    if i is None: return None, 'outside'
    if S10[i]['name'] not in GAME:
        return a + S15[i]['va'] - S10[i]['va'], 'section'
    L = bysec.get(i, [])
    keys = [x for x, _ in L]
    j = bisect.bisect_right(keys, a) - 1
    if j >= 0 and keys[j] == a: return L[j][1], 'vote'
    lo = L[j] if j >= 0 else None; hi = L[j + 1] if j + 1 < len(L) else None
    if lo and hi and lo[1] - lo[0] == hi[1] - hi[0]: return a + lo[1] - lo[0], 'between'
    # between two votes that moved by different amounts: something was put in between - find the bytes (in data the
    # file holds; values that look like addresses masked, since they move) among the places the votes allow
    if lo and hi:
        p0 = lo[1] + (a - lo[0]); p1 = hi[1] - (hi[0] - a)
        lo_p, hi_p = min(p0, p1) - 64, max(p0, p1) + 64
        n = min(64, hi[0] - a)
        b10 = _rd(_D10, S10, a, n)
        if b10 and n >= 4 and any(b10):
            m = _mask(b10); hits = []
            for p in range((lo_p + 3) & ~3, hi_p + 1, 4):
                b15 = _rd(_D15, S15, p, n)
                if b15 and all((not y) or x == z for x, y, z in zip(b10, m, b15)): hits.append(p)
            if len(hits) == 1: return hits[0], 'bytes'
    # votes inside the object itself (a field of it, used at an offset): all moved by one amount
    if NEXT:
        q = bisect.bisect_right(NEXT, a)
        end = NEXT[q] if q < len(NEXT) else a + 0x1000
        k0 = bisect.bisect_left(keys, a); k1 = bisect.bisect_left(keys, end)
        ds = {L[k][1] - L[k][0] for k in range(k0, k1)}
        if len(ds) == 1: return a + ds.pop(), 'inner'
        if len(ds) > 1: return a + (L[k0][1] - L[k0][0]), 'uncertain'
        if k0 == k1: return a + (lo[1] - lo[0] if lo else 0), 'unused'
    if lo: return a + lo[1] - lo[0], 'uncertain'
    if hi: return a + hi[1] - hi[0], 'uncertain'
    return a + S15[i]['va'] - S10[i]['va'], 'uncertain'
if __name__ == '__main__':
    import re, collections
    st = collections.Counter(); unc = []
    allsym = [int(x, 16) for x in re.findall(r'=\s*0x([0-9a-f]+)', open('/home/claude/native/layer/retail_syms.S').read())]
    set_objects(allsym)
    for l in open('/home/claude/native/layer/retail_syms.S'):
        m = re.match(r'\s*(".*?"|\S+)\s*=\s*0x([0-9a-f]+)', l)
        if not m: continue
        a = int(m.group(2), 16); b, how = map_data(a); st[how] += 1
        if how == 'uncertain': unc.append((m.group(1), a, b))
    print(dict(st))
    for n, a, b in unc[:20]: print(' uncertain', n[:60], hex(a), hex(b))

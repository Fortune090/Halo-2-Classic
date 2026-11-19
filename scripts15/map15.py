"""map15.py: the 1.0 -> 1.5 maps the 1.5 build is made from.
  funcs: every 1.0 function (functions.csv) -> [1.5 address or None, how]: 'same' (fmap.py, or fmap2.py's twin of a
         function 1.5's link moved), 'diff' (a same-by-bytes function whose 1.5 twin calls another function or uses
         another global than the maps say - it is not the same), 'changed' (placed by the calls of same functions or by
         the slots of tables both versions hold), 'guess' (funcs15.py's place between its neighbours), None.
  data:  every address that same functions use -> its 1.5 address (majority of the fields; ties listed).
Writes map15.json {"funcs": {a10: [a15, how]}, "data": {a10: a15}, "diff": {a10: [first differing field]}}."""
import json, sys, collections, struct, csv
sys.path.insert(0, '/home/claude/v15')
from xmap15_lib import F, r10, r15, fields, md
from fmap_lib import mask
fm = json.load(open('/home/claude/v15/fmap.json')); mv = json.load(open('/home/claude/v15/moved15.json'))
size = {int(r[0], 16): int(r[1]) for r in csv.reader(open('/home/claude/h2decomp_src/halo2-decompiled-main/config/functions.csv'))
        if r and r[0][:1] in '0123456789abcdef' and r[1].isdigit()}
starts10 = set(size)
twin = {}                                     # a10 -> a15 for functions with the same bytes
for k, (t, kind) in fm.items():
    if kind == 'same': twin[int(k, 16)] = int(t, 16)
for k, c in mv.items():
    if len(c) == 1: twin[int(k, 16)] = c[0]
dup = {int(k, 16): c for k, c in mv.items() if len(c) > 1}
# the code pointers of the data both versions hold (datalign15.py pairs the tables slot by slot): where the 1.5 slot's
# function has the 1.0 function's whole body, that is its place - it settles which of two identical bodies is which,
# and finds the functions only tables reach
_sl = json.load(open('/home/claude/v15/slots15.json'))['code']
for k, t15 in _sl.items():
    a10 = int(k, 16)
    if a10 not in size or size[a10] <= 0: continue
    b = r10(a10, size[a10]); q = r15(t15, size[a10])
    if b and q and all((not y) or x == z for x, y, z in zip(b, mask(b), q)): twin[a10] = t15
# the libraries' sections (DSOUND, XONLINE, XNET, BINK*, D3D, XPP, WMADEC) moved whole: the twin is at the section's
# offset, or within 64 bytes of it (XNET is 8 bytes shorter in 1.5); and the .text functions fmap.py did not cover
from fmap_lib import load
_D10, _S10 = load('/home/claude/xbe/default.xbe'); _D15, _S15 = load('/home/claude/v15/default_v15.xbe')
def _sec(S, va):
    for i, s in enumerate(S):
        if s['va'] <= va < s['va'] + s['vs']: return i
libn = 0
for a10, n in size.items():
    if a10 in twin or a10 in dup or n <= 0: continue
    i = _sec(_S10, a10)
    if i is None: continue
    b = r10(a10, n)
    if not b: continue
    m = mask(b); d = _S15[i]['va'] - _S10[i]['va'] if _S10[i]['name'] != '.text' else None
    tries = [a10 + d + k for k in sorted(range(-64, 65), key=abs)] if d is not None else []
    for t in tries:
        q = r15(t, n)
        if q and all((not y) or x == z for x, y, z in zip(b, m, q)): twin[a10] = t; libn += 1; break
print('library and other functions placed by their section:', libn)
# (s10r) a twin is the 1.0 function instruction for instruction (strict_lib): fmap_lib.mask hides any three bytes that a
# zero byte follows when the third is 0x01-0x6f, so a function whose only change is a structure offset or a size the
# update made bigger (the menus' per-controller data 0xc70 -> 0xc74, the network session's 0x528 -> 0x550, fields that
# moved by 4) passed as the same and ran as 1.0's compiled source with 1.0's numbers on 1.5's data. Such a function is
# a changed one: 1.5's own code runs.
from strict_lib import strict as _strict
_loose = {a: t for a, t in twin.items() if size.get(a, 0) > 0 and not _strict(a, t, size[a])}
for a in _loose: del twin[a]
# ... and only where the two are the same instructions with other numbers in them is the place known (a short function
# can pass the mask by accident: 1.0's 0x199e6d and a C run-time function)
from strict_lib import same_shape as _shape
_n0 = len(_loose)
_loose_all = dict(_loose)
_loose = {a: t for a, t in _loose.items() if _shape(a, t, size[a])}
print('   of those, the same instructions with other numbers (their 1.5 place is known): %d of %d' % (len(_loose), _n0))
for a in list(dup):
    c = [t for t in dup[a] if _strict(a, t, size[a])]
    if len(c) == 1: twin[a] = c[0]; del dup[a]
    elif c: dup[a] = c
    else: del dup[a]
print('twins by masked bytes that are not the same instruction for instruction (now changed):', len(_loose_all))
def collect(pairs):
    calls = collections.defaultdict(collections.Counter); data = collections.defaultdict(collections.Counter)
    per = {}
    for a10, a15 in pairs.items():
        fl = list(fields(a10, a15, size[a10])); per[a10] = fl
        for off, kind, v10, v15 in fl:
            if kind == 'rel':
                if v10 in starts10: calls[v10][v15] += 1
            elif v10 in starts10: calls[v10][v15] += 1       # a function's address as a number (push offset f)
            else: data[v10][v15] += 1
    return calls, data, per
# (s10q) what the tables themselves say: a 1.0 data address that same code names (a vote) and that starts a run of code
# pointers - a vtable, a function table - pairs slot for slot with its 1.5 address, up to the next named address. Where
# every such slot agrees on a 1.5 function that is the 1.0 function instruction for instruction (strict_lib), that is
# the twin: by bytes alone the scalar deleting destructors of different classes are all alike, and datalign15's
# word pairing follows the order of the tables, which 1.5's link changed (1.0's 0x2325a7 is in three vtables whose 1.5
# places all hold 0x238ecb; the word pairing gave 0x2389d2, another class's - whose destructor then ran on the
# campaign's difficulty screen). A twin two or more slots name wins; one slot wins over a twin that is not strict.
from strict_lib import strict
def table_votes(dm):
    votes = collections.defaultdict(collections.Counter); keys = set(dm)
    for s10, s15 in dm.items():
        if not (0x411000 <= s10 < 0x590000): continue
        for i in range(0, 0x200, 4):
            if i and (s10 + i) in keys: break
            a = r10(s10 + i, 4); b = r15(s15 + i, 4)
            if not a or not b: break
            x = struct.unpack('<I', a)[0]; y = struct.unpack('<I', b)[0]
            if not (0x12000 <= x < 0x411000 and 0x12000 <= y < 0x41b600): break
            if x in size: votes[x][y] += 1
    return votes
_c0, _d0, _p0 = collect(twin)
_tv = table_votes({a: c.most_common(1)[0][0] for a, c in _d0.items()}); tabn = []
for a10, c in _tv.items():
    if len(c) != 1: continue
    (t, n), = c.items()
    if twin.get(a10) == t or not strict(a10, t, size[a10]): continue
    if n >= 2 or a10 not in twin or not strict(a10, twin[a10], size[a10]):
        tabn.append('%x: %s -> %x (%d)' % (a10, ('%x' % twin[a10]) if a10 in twin else None, t, n)); twin[a10] = t; dup.pop(a10, None)
print('twins the tables name (in place of another, or new):', len(tabn)); print('  ' + '\n  '.join(tabn))
calls, data, per = collect(twin)
# the function map: a twin by bytes, unless every same caller agrees on another place that also has the same bytes
func = {}
for a10 in size:
    c = calls.get(a10)
    if a10 in twin:
        func[a10] = [twin[a10], 'same']
        if c:
            top, n = c.most_common(1)[0]
            if top != twin[a10] and n >= 2 and n == sum(c.values()):
                if _strict(a10, top, size[a10]): func[a10] = [top, 'same']
    elif a10 in dup and c and c.most_common(1)[0][0] in dup[a10]:
        func[a10] = [c.most_common(1)[0][0], 'same']
    elif c and (a10 not in _loose or c.most_common(1)[0][1] == sum(c.values())):
        top, n = c.most_common(1)[0]
        func[a10] = [top, 'changed'] if n == sum(c.values()) else [top, 'changed?']
    elif a10 in _loose:
        func[a10] = [_loose[a10], 'changed']      # (s10r) its place is known: the same function with another number in it
    else:
        g = F.get('%x' % a10)
        func[a10] = [int(g[0], 16), 'guess'] if g and g[0] else [None, None]
dmap = {a: c.most_common(1)[0][0] for a, c in data.items()}
ties = {a: dict(c) for a, c in data.items() if len(c) > 1}
# a same-by-bytes function is the same only if every field it has agrees with both maps (a number that is equal in
# both versions and maps elsewhere is a constant, not an address)
diff = {}
for a10, fl in per.items():
    for off, kind, v10, v15 in fl:
        if v10 in starts10: e = func[v10][0]
        else: e = dmap.get(v10)
        if e is not None and v15 != e and not (kind == 'abs' and v10 == v15):
            diff[a10] = [off, kind, v10, v15, e]; break
for a10 in diff:
    if func[a10][1] == 'same': func[a10][1] = 'diff'
json.dump({'funcs': {'%x' % a: v for a, v in func.items()}, 'data': {'%x' % a: b for a, b in dmap.items()},
           'ties': {'%x' % a: v for a, v in ties.items()}, 'diff': {'%x' % a: v for a, v in diff.items()}},
          open('/home/claude/v15/map15.json', 'w'))
st = collections.Counter(v[1] for v in func.values())
print('functions:', dict(st)); print('data addresses:', len(dmap), 'with more than one answer:', len(ties))

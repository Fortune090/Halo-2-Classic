"""vt15.py: where each 1.0 vtable is in 1.5, from the tables' own contents.
A vtable's entries are functions; where a function is the same in 1.5 instruction for instruction (map15.json 'same'),
its 1.5 place is known, and the 1.5 table is the one that holds those places slot for slot. That is evidence of its
own: the votes of same code (map15's data map) are missing for a table only changed code names (the network
session's state classes after s10r's strict map), and the word pairing (datalign15) and the data map follow the order
of the tables, which 1.5's link changed.
Writes vt15.json {1.0 address: [1.5 address, slots that agree, slots known]} for the tables found without doubt;
syms15.py takes these first. Prints what differs from the address the other methods give."""
import re, json, struct, sys, os, bisect, collections
sys.path.insert(0, '/home/claude/v15')
from xmap15_lib import r10, r15
from fmap_lib import load
import datamap15
NAT = '/home/claude/native'
M = json.load(open('/home/claude/v15/map15.json'))
same15 = {int(k, 16): v[0] for k, v in M['funcs'].items() if v and v[0] and v[1] == 'same'}
votes = {int(k, 16): v for k, v in M['data'].items()}
SLOTS = {int(k, 16): v for k, v in json.load(open('/home/claude/v15/slots15.json'))['slots'].items()}
txt = open(NAT + '/layer/retail_syms.S').read()
syms = {m.group(1).strip('"'): int(m.group(2), 16) for m in re.finditer(r'^\s*(".*?"|[^\s=]+)\s*=\s*0x([0-9a-f]+)', txt, re.M)}
tables = {a for n, a in syms.items() if n.startswith('??_7')}
# vtables the rewrite put into the objects themselves (vtables_plan): absolute symbols named ??_7...
for p in [l.strip() for l in open(NAT + '/objects_r.rsp') if l.strip()]:
    d = open(p, 'rb').read()
    if b'??_7' not in d: continue
    symptr, nsym = struct.unpack_from('<II', d, 8); strtab = symptr + nsym * 18; i = 0
    while i < nsym:
        o = symptr + 18 * i
        if struct.unpack_from('<h', d, o + 12)[0] == -1:
            if d[o:o + 4] == b'\0\0\0\0':
                so = struct.unpack_from('<I', d, o + 4)[0]; e = d.index(b'\0', strtab + so); nm = d[strtab + so:e]
            else: nm = d[o:o + 8].rstrip(b'\0')
            if nm.startswith(b'??_7'): tables.add(struct.unpack_from('<I', d, o + 8)[0])
        i += 1 + d[o + 17]
datamap15.set_objects(syms.values())
bounds = sorted(set(syms.values()) | set(votes) | tables)
D15, S15 = load('/home/claude/v15/default_v15.xbe')
rd = [s for s in S15 if s['name'] == '.rdata'][0]
blob = bytes(D15[rd['raw']:rd['raw'] + rd['rs']]); rva = rd['va']
occ = collections.defaultdict(list)
for q in range(0, len(blob) - 3, 4):
    w = struct.unpack_from('<I', blob, q)[0]
    if 0x12000 <= w < 0x41b600: occ[w].append(rva + q)
import csv
from strict_lib import strict, same_shape
size = {int(r[0], 16): int(r[1]) for r in csv.reader(open('/home/claude/h2decomp_src/halo2-decompiled-main/config/functions.csv'))
        if r and r[0][:1] in '0123456789abcdef' and r[1].isdigit()}
out = {}; st = collections.Counter(); notes = []; found = {}
def fits(x, y):
    """2: 1.5's entry is the 1.0 entry's function instruction for instruction (whichever of several alike); 1: the same
    shape with other numbers (a function the update changed); 0: another function"""
    n = size.get(x)
    if not n or not (0x12000 <= y < 0x41b600): return 0
    if strict(x, y, n): return 2
    return 1 if n >= 12 and same_shape(x, y, n) else 0
anyf = {int(k, 16): v[0] for k, v in M['funcs'].items() if v and v[0]}
cs = {}; nslots = {}
for a10 in sorted(tables):
    i = bisect.bisect_right(bounds, a10); nxt = bounds[i] if i < len(bounds) else a10 + 256
    slots10 = []
    for k in range(min(64, (nxt - a10) // 4)):
        w = r10(a10 + 4 * k, 4)
        if not w: break
        x = struct.unpack('<I', w)[0]
        if not (0x12000 <= x < 0x411000): break
        slots10.append((k, x))
    known = [(k, same15[x]) for k, x in slots10 if x in same15]
    # candidates: where the 1.5 place of any of its functions stands in a 1.5 table (a place the map only guesses too:
    # the score below is what counts)
    cands = set()
    for k, x in slots10:
        f = anyf.get(x)
        if f and len(occ.get(f, ())) <= 40:
            for p in occ[f]: cands.add(p - 4 * k)
    L = []
    for T in cands:
        pts = 0; full = 0
        for k, x in slots10:
            w = r15(T + 4 * k, 4)
            q = fits(x, struct.unpack('<I', w)[0]) if w else 0
            pts += q; full += q == 2
        exact = sum(1 for k, f in known if (r15(T + 4 * k, 4) or b'') == struct.pack('<I', f))
        if pts: L.append((pts, exact, full, T))
    if not L: st['its entries are nowhere in 1.5 tables'] += 1; continue
    L.sort(reverse=True); cs[a10] = L; nslots[a10] = len(slots10)
# every table takes the best place no better-fitting table takes (two 1.0 tables are not one 1.5 table; tables of classes
# made from one template hold functions that are alike instruction for instruction, so among places that fit equally the
# one holding the very places the function map gives comes first)
claimed = {}
for a10 in sorted(cs, key=lambda a: (-cs[a][0][0], -cs[a][0][1], a)):
    free = [c for c in cs[a10] if c[3] not in claimed]
    if not free: st['no place left'] += 1; continue
    pts, exact, full, T = free[0]; n = nslots[a10]
    if len(free) > 1 and free[1][:2] == (pts, exact): st['more than one place fits'] += 1; continue
    if not (full >= 2 or pts == 2 * n or (full >= 1 and pts >= 3)): st['too few slots fit'] += 1; continue
    claimed[T] = a10; found[a10] = (T, pts, full, n)
for a10, (T, pts, full, n) in sorted(found.items()):
    out['%x' % a10] = [T, full, n]; st['found'] += 1
    cur = votes.get(a10) or SLOTS.get(a10) or datamap15.map_data(a10)[0]
    if cur != T: notes.append('%x: the table is at %x (%d of %d slots the same function, score %d); the other methods say %x' % (a10, T, full, n, pts, cur))
json.dump(out, open('/home/claude/v15/vt15.json', 'w'), indent=0)
print('\n'.join(notes))
print('vtables:', len(tables), dict(st), '; placed differently by the tables themselves:', len(notes))

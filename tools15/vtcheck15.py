#!/usr/bin/env python3
# vtcheck15.py : every vtable symbol (??_7...) at its 1.5 place: do the 1.5 table's entries equal the 1.0 table's
# functions' own 1.5 places (map15.json)? Lists the tables that do not agree.
import re, json, struct, sys
sys.path.insert(0, '/home/claude/v15')
from xmap15_lib import r10, r15
M = json.load(open('/home/claude/v15/map15.json'))['funcs']
f15 = {int(k, 16): v[0] for k, v in M.items() if v and v[0]}
def syms(p):
    return {m.group(1): int(m.group(2), 16) for m in re.finditer(r'^"([^"]+)" = 0x([0-9a-f]+)', open(p).read(), re.M)}
s10 = syms('/home/claude/native/layer/retail_syms.S'); s15 = syms('/home/claude/native15/layer/retail_syms.S')
bad = 0; n = 0
import bisect
# a table ends where the next table or symbol starts: every vtable is preceded by its RTTI word in neither version, so
# the next symbol's address is the bound; other tables' starts found as data words that code names are bounds too
allsym = sorted(set(s10.values()))
extra = sorted(int(k, 16) for k in json.load(open('/home/claude/v15/map15.json'))['data'])
bounds = sorted(set(allsym) | set(extra))
for name, a10 in sorted(s10.items(), key=lambda q: q[1]):
    if not name.startswith('??_7'): continue
    a15 = s15[name]; n += 1; agree = 0; differ = []
    nxt = bounds[bisect.bisect_right(bounds, a10)] if bisect.bisect_right(bounds, a10) < len(bounds) else a10 + 256
    for k in range(min(64, (nxt - a10) // 4)):
        w10 = r10(a10 + 4 * k, 4); w15 = r15(a15 + 4 * k, 4)
        if not w10 or not w15: break
        x = struct.unpack('<I', w10)[0]; y = struct.unpack('<I', w15)[0]
        if not (0x12000 <= x < 0x411000): break
        t = f15.get(x)
        if t is None: continue
        if t == y: agree += 1
        else: differ.append((k, x, t, y))
    if differ:
        bad += 1; print('%-58s 1.0 %x -> 1.5 %x: %d entries agree, %d differ (slot %d: 1.0 %x is 1.5 %x, table has %x)' % (name[:58], a10, a15, agree, len(differ), *differ[0]))
print(n, 'vtables;', bad, 'do not agree')

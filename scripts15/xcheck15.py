"""xcheck15.py: same-by-mask functions whose 1.5 twin references a different function or global than the map says
(so they are not really the same): writes samediff15.json {a10: [[off, kind, v10, v15, expected], ...]}."""
import sys, json, collections
sys.path.insert(0, '/home/claude/v15')
from xmap15_lib import *
A = json.load(open('/home/claude/v15/addrmap15.json'))
M = {a: b for a, b, n in A['pairs']}
for k, (v15, size, kind) in F.items():
    if v15: M.setdefault(int(k, 16), int(v15, 16))
    if v15: M[int(k, 16)] = int(v15, 16)          # function starts: the function map wins
out = {}
for a10, a15, size in same_functions():
    bad = []
    for off, kind, v10, v15 in fields(a10, a15, size):
        e = M.get(v10)
        if e is not None and v15 != e and not (kind == 'abs' and v10 == v15): bad.append([off, kind, v10, v15, e])
    if bad: out['%x' % a10] = bad
json.dump(out, open('/home/claude/v15/samediff15.json', 'w'))
print('same functions with a different reference:', len(out))
for k, v in list(out.items())[:25]: print(k, [(o, t, hex(a), hex(b), hex(e)) for o, t, a, b, e in v[:3]])

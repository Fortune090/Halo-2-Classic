"""slotvotes15.py: function twins as the tables say them: for every 1.0 data address that same code names (map15.json
'data', a vote) and that starts a run of code pointers (a vtable or a function table), slot i pairs with slot i of its
1.5 address while both hold code pointers and no other named address starts in between. Prints where these votes
disagree with map15.json's 'funcs'. Writes slotvotes15.json {a10: {a15: votes}}."""
import json, struct, sys, collections, csv
sys.path.insert(0, '/home/claude/v15')
from xmap15_lib import r10, r15
m = json.load(open('/home/claude/v15/map15.json'))
dm = {int(k, 16): v for k, v in m['data'].items()}; f = m['funcs']
size = {int(r[0], 16): int(r[1]) for r in csv.reader(open('/home/claude/h2decomp_src/halo2-decompiled-main/config/functions.csv'))
        if r and r[0][:1] in '0123456789abcdef' and r[1].isdigit()}
C10, C15 = (0x12000, 0x411000), (0x12000, 0x41b600)
votes = collections.defaultdict(collections.Counter)
keys = set(dm)
for s10, s15 in dm.items():
    if not (0x411000 <= s10 < 0x590000): continue
    for i in range(0, 0x200, 4):
        if i and (s10 + i) in keys: break
        a = r10(s10 + i, 4); b = r15(s15 + i, 4)
        if not a or not b: break
        x = struct.unpack('<I', a)[0]; y = struct.unpack('<I', b)[0]
        if not (C10[0] <= x < C10[1] and C15[0] <= y < C15[1]): break
        if x in size: votes[x][y] += 1
json.dump({'%x' % a: {'%x' % b: n for b, n in c.items()} for a, c in votes.items()}, open('/home/claude/v15/slotvotes15.json', 'w'))
bad = []; agree = 0; split = 0
for a10, c in votes.items():
    e = f.get('%x' % a10)
    if len(c) > 1: split += 1
    top, n = c.most_common(1)[0]
    if e and e[0] == top: agree += 1
    else: bad.append((a10, e, dict(c)))
print('functions the tables name:', len(votes), 'agree with the map:', agree, 'more than one 1.5 place:', split, 'disagree:', len(bad))
for a10, e, c in sorted(bad)[:int(sys.argv[1]) if len(sys.argv) > 1 else 40]:
    print('  %x: map %s  tables %s' % (a10, (('%x' % e[0]) if e and e[0] else None, e and e[1]), {('%x' % k): v for k, v in c.items()}))

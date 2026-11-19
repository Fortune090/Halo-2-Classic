import sys; sys.path.insert(0, "/home/claude/v15")
from xmap15_lib import *
votes = collections.defaultdict(collections.Counter)
nf = 0
for a10, a15, size in same_functions():
    nf += 1
    for off, kind, v10, v15 in fields(a10, a15, size): votes[v10][v15] += 1
pairs = []; conf = []
for a, c in sorted(votes.items()):
    (b, n), = c.most_common(1)
    if len(c) > 1: conf.append([a, {('%x' % x): y for x, y in c.items()}])
    pairs.append([a, b, n])
json.dump({'pairs': pairs, 'conflicts': conf}, open('/home/claude/v15/addrmap15.json', 'w'))
print('functions', nf, 'addresses', len(pairs), 'conflicts', len(conf))
for a, c in conf[:15]: print(hex(a), c)

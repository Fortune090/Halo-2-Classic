"""boundary15.py -> boundary15.json: the changed functions (map15 / reach15) that our same compiled code calls, with
their callers: each needs a route to 1.5's own function (adapt15.py works out how)."""
import json, csv, collections
M = json.load(open('/home/claude/v15/map15.json'))['funcs']; R = json.load(open('/home/claude/v15/reach15.json'))
rows = {int(r[0], 16): r for r in csv.reader(open('/home/claude/h2decomp_src/halo2-decompiled-main/config/functions.csv')) if r and r[0][:1] in '0123456789abcdef' and r[1].isdigit()}
fm = {int(k, 16): v for k, v in json.load(open('/home/claude/native/funcmap.json')).items()}
comp = {a for a, v in fm.items() if not v['stub']}
same = {int(k, 16) for k, v in M.items() if v[1] == 'same' or R.get(k) == 'same'}
chg = {int(k, 16) for k, v in M.items() if int(k, 16) not in same}
B = collections.defaultdict(set)
for a in comp & same:
    for c in rows[a][7].split():
        c = int(c, 16)
        if c in chg and c in fm: B[c].add(a)
json.dump({'%x' % c: sorted('%x' % a for a in v) for c, v in B.items()}, open('/home/claude/v15/boundary15.json', 'w'))
print('boundary (compiled, changed, called by same compiled code):', len(B))

import csv, sys, collections
rows=list(csv.DictReader(open(sys.argv[1])))
print('rows',len(rows))
by=collections.defaultdict(lambda: collections.Counter())
bs=collections.defaultdict(lambda: collections.Counter())
for r in rows:
    by[r['owner']][r['status'] or '(none)']+=1
    bs[r['owner']][r['status'] or '(none)']+=int(r['size'])
for o in sorted(by, key=lambda o:-sum(bs[o].values())):
    tot=sum(by[o].values()); tb=sum(bs[o].values())
    print(f'{o:22s} funcs {tot:6d} bytes {tb:8d} ', {k:(by[o][k],bs[o][k]) for k in by[o]})
# has source but not matched
src=collections.Counter()
for r in rows:
    if r['owner'] in('game','third:havok'):
        src[(r['owner'],r['status'] or '(none)', bool(r['source']))]+=1
print(src)

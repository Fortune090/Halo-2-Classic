import csv,sys,collections
rows={int(r['va'],16):r for r in csv.DictReader(open(sys.argv[1]))}
ours=[l for l in open(sys.argv[2]) if not l.startswith('#')]
rd=list(csv.DictReader(ours))
print('our D3D names:',len(rd), collections.Counter(r['how'] for r in rd))
c=collections.Counter()
new=[]
for r in rd:
    va=int(r['va'],16); row=rows.get(va)
    if not row: c['va not a function start in decomp csv']+=1; continue
    if row['name']: c['decomp already has a name' + (' (same)' if row['name']==r['name'] else ' (different)')]+=1
    else: c['decomp row unnamed -> ours adds a name']+=1; new.append((r['va'],r['name'],r['how'],r['score'],row['owner']))
print(c)
d3d=[r for r in rows.values() if r['owner']=='xdk:d3d8']
print('decomp xdk:d3d8 rows',len(d3d),'named',sum(1 for r in d3d if r['name']))
for n in new[:12]: print('  ',n)
by=collections.Counter(r['owner'] for r in rows.values() if r['name']); print('named rows by owner',dict(by))

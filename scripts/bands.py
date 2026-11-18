import csv,sys,collections
rows=[r for r in csv.DictReader(open(sys.argv[1])) if r['owner'] in('game','third:havok')]
band=0x10000
tot=collections.Counter(); un=collections.Counter(); unf=collections.Counter()
for r in rows:
    b=int(r['va'],16)//band
    tot[b]+=int(r['size'])
    if r['status']!='matched': un[b]+=int(r['size']); unf[b]+=1
for b in sorted(tot):
    if un[b]: print(f'{b*band:#08x} total {tot[b]:7d}  unmatched {un[b]:7d} ({100*un[b]/tot[b]:4.0f}%) {unf[b]:3d} funcs')

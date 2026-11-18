import csv,sys,collections
rows={r['va']:r for r in csv.DictReader(open(sys.argv[1]))}
sc={r['va']:r for r in csv.DictReader(open(sys.argv[2]))}
for owner in ('game','third:havok'):
    un=[r for r in rows.values() if r['owner']==owner and r['status']!='matched']
    have=[r for r in un if r['va'] in sc]
    print(owner,'unmatched',len(un),'with score row',len(have))
    buckets=collections.Counter(); bb=collections.Counter()
    for r in un:
        s=sc.get(r['va'])
        size=int(r['size'])
        if not s: k='no score row'
        else:
            ours=int(s['ours']); score=int(s['score'])
            if ours==0: k='ours=0 (no body built)'
            else:
                ratio=ours/size
                if ratio<0.5: k='body <50% of retail size'
                elif ratio<0.9 or ratio>1.1: k='body 50-90% / >110% of retail size'
                else:
                    # score relative to size: differing instrs + len diff
                    rel=score/max(1,size/3.5)  # ~instructions
                    if rel<0.1: k='full-size, <10% instrs differ'
                    elif rel<0.3: k='full-size, 10-30% instrs differ'
                    else: k='full-size, >30% instrs differ'
        buckets[k]+=1; bb[k]+=size
    for k in sorted(buckets,key=lambda k:-bb[k]): print(f'   {k:40s} {buckets[k]:5d} funcs {bb[k]:8d} bytes')
    # stale check: matched functions in scores
print('score rows whose function is now matched:',sum(1 for v in sc if rows.get(v,{}).get('status')=='matched'))

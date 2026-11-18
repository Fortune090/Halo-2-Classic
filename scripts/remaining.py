import csv,sys,collections
rows=list(csv.DictReader(open(sys.argv[1])))
un=[r for r in rows if r['owner'] in('game','third:havok') and r['status']!='matched']
print('no source:')
for r in un:
    if not r['source']: print('  ',r['va'],r['size'],r['owner'],r['status'])
print('largest unmatched:')
for r in sorted(un,key=lambda r:-int(r['size']))[:25]: print('  ',r['va'],r['size'],r['owner'],r['status'],r['source'])
# size histogram
import bisect
edges=[100,300,1000,3000,10000,10**9]
h=collections.Counter(); hb=collections.Counter()
for r in un:
    s=int(r['size']); i=bisect.bisect_left(edges,s); h[edges[i]]+=1; hb[edges[i]]+=s
for e in edges: print(f'  <= {e}: {h[e]} funcs, {hb[e]} bytes')
# by source file
bf=collections.Counter(); bfb=collections.Counter()
for r in un: bf[r['source']]+=1; bfb[r['source']]+=int(r['size'])
print('by source file (top 30 by bytes):')
for f,b in bfb.most_common(30): print(f'  {f:55s} {bf[f]:4d} funcs {b:7d} bytes')
print('files with unmatched:',len(bf))

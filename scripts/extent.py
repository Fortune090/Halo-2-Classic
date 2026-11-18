import csv,sys,collections,re,os
rows=list(csv.DictReader(open(sys.argv[1])))
rows.sort(key=lambda r:int(r['va'],16))
# contiguous runs by owner
runs=[]
for r in rows:
    va=int(r['va'],16); end=va+int(r['size'])
    o=r['owner'].split(':')[0] if r['owner'].startswith('xdk') else r['owner']
    o=r['owner']
    if runs and runs[-1][2]==o and va-runs[-1][1]<0x2000: runs[-1][1]=end; runs[-1][3]+=1
    else: runs.append([va,end,o,1])
big=[x for x in runs if x[1]-x[0]>0x3000]
for a,b,o,n in big: print(f'{a:08x}-{b:08x} {o:16s} {n:6d} funcs {b-a:8d} bytes')
print('first',rows[0]['va'],'last end',hex(max(int(r['va'],16)+int(r['size']) for r in rows)))

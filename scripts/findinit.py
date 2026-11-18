import sys,csv,struct
sys.path.insert(0,'/home/claude/scripts')
from xbeimg import XbeImage
x=XbeImage(sys.argv[1])
rows={int(r['va'],16):r for r in csv.DictReader(open(sys.argv[2]))}
print('entry %#x kernel thunk table %#x'%(x.entry,x.kernel_thunk))
rd=[s for s in x.sections if s['name']=='.rdata'][0]
best=None; run=[]; runs=[]
for va in range(rd['va']&~3, rd['va']+rd['rsize']-3, 4):
    v=x.u32(va)
    if v is not None and 0x3762e0<=v<0x379c60 and v in rows:
        run.append((va,v))
    else:
        if len(run)>=3: runs.append(run)
        run=[]
for r in runs: print('run at %#x: %d entries, first %#x last %#x'%(r[0][0],len(r),r[0][1],r[-1][1]))
big=max(runs,key=len)
print('before table:',[hex(x.u32(big[0][0]-4*k)) for k in range(1,5)],'after:',[hex(x.u32(big[-1][0]+4*k)) for k in range(1,5)])
import json; json.dump([v for _,v in big],open('/home/claude/native/init_table.json','w'))
tgt=set(v for _,v in big); allf=[va for va in rows if 0x3762e0<=va<0x379c60]
print('initialiser functions in range',len(allf),'in table',len(tgt),'not in table',len(set(allf)-tgt), [hex(v) for v in sorted(set(allf)-tgt)[:12]])

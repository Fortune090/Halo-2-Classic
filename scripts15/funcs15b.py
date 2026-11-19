"""funcs15b.py: places the changed functions funcs15.py left unplaced: function starts in 1.5 are the targets of direct
calls in its code and the code addresses its data holds; inside each run of changed functions they are taken in order."""
import json, csv, struct, bisect, sys
sys.path.insert(0,'/home/claude/v15')
from fmap_lib import load, text, mask
D15,S15=load('/home/claude/v15/default_v15.xbe'); T15va,T15=text(D15,S15)
out=json.load(open('functions15.json'))
starts=set()
for i in range(len(T15)-5):
    if T15[i]==0xE8:
        t=T15va+i+5+struct.unpack_from('<i',T15,i+1)[0]
        if T15va<=t<T15va+len(T15): starts.add(t)
for s in S15:
    if s['fl'] & 4: continue                       # data sections only
    d=D15[s['raw']:s['raw']+s['rs']]
    for i in range(0,len(d)-3,4):
        v=struct.unpack_from('<I',d,i)[0]
        if T15va<=v<T15va+len(T15) and v%16==0: starts.add(v)
starts=sorted(starts)
rows=sorted((int(r[0],16),int(r[1])) for r in csv.reader(open('/home/claude/h2decomp_src/halo2-decompiled-main/config/functions.csv')) if r and r[0][:1] in '0123456789abcdef' and r[1].isdigit() and ('%x'%int(r[0],16)) in out)
fixed=0; still=0
for idx,(va,size) in enumerate(rows):
    v=out['%x'%va]
    if v[0]: continue
    # the placed neighbours around it
    lo=next(int(out['%x'%rows[k][0]][0],16) for k in range(idx-1,-1,-1) if out['%x'%rows[k][0]][0])
    hi_k=next((k for k in range(idx+1,len(rows)) if out['%x'%rows[k][0]][0]), None)
    hi=int(out['%x'%rows[hi_k][0]][0],16) if hi_k is not None else T15va+len(T15)
    # how many unplaced ones in this gap before this one
    before=sum(1 for k in range(idx-1,-1,-1) if not out['%x'%rows[k][0]][0] and int(rows[k][0])>0 and all(not out['%x'%rows[q][0]][0] for q in range(k,idx)))
    cands=[s for s in starts[bisect.bisect_right(starts,lo):bisect.bisect_left(starts,hi)]]
    if len(cands)>before:
        out['%x'%va]=['%x'%cands[before],None,'call-target']; fixed+=1
    else: still+=1
st=sorted(int(v[0],16) for v in out.values() if v[0])
for k,v in out.items():
    if v[0] and v[1] is None:
        a=int(v[0],16); q=bisect.bisect_right(st,a); v[1]=(st[q]-a) if q<len(st) else None
json.dump(out,open('functions15.json','w'))
print('placed by call targets / data pointers:', fixed, 'still unplaced:', still, 'starts known in 1.5:', len(starts))

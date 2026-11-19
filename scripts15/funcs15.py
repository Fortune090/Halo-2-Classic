"""funcs15.py: a 1.5 address for every 1.0 function (functions15.json). Same functions come from fmap.json; a changed one
lies between its unchanged neighbours: the first of a run starts at the first 16-aligned byte after the previous one's
end that is not padding; later ones where the masked first bytes of the 1.0 function match best."""
import json, csv, struct, sys
sys.path.insert(0, '/home/claude/v15')
from fmap_lib import load, text, mask
D10,S10=load('/home/claude/native/run/real/default.xbe'); D15,S15=load('/home/claude/v15/default_v15.xbe')
T10va,T10=text(D10,S10); T15va,T15=text(D15,S15)
m=json.load(open('/home/claude/v15/fmap.json'))
rows=sorted((int(r[0],16),int(r[1]),r[9].strip()) for r in csv.reader(open('/home/claude/h2decomp_src/halo2-decompiled-main/config/functions.csv')) if r and r[0][:1] in '0123456789abcdef' and r[1].isdigit())
rows=[r for r in rows if '%x'%r[0] in m]
out={}; i=0; n=len(rows); conf={'same':0,'first':0,'matched':0,'guessed':0}
def score(a10, p15, k=24):
    b=T10[a10-T10va:a10-T10va+k]; mm=mask(b); s=0
    for j in range(len(b)):
        if p15-T15va+j>=len(T15): break
        if mm[j] and b[j]==T15[p15-T15va+j]: s+=1
    return s, sum(mm[:len(b)])
while i<n:
    va,size,st=rows[i]
    t,kind=m['%x'%va]
    if kind=='same': out['%x'%va]=[t,size,'same']; conf['same']+=1; i+=1; continue
    j=i
    while j<n and m['%x'%rows[j][0]][1]=='changed': j+=1
    prev_end=int(out['%x'%rows[i-1][0]][0],16)+rows[i-1][1] if i>0 else T15va
    next_start=int(m['%x'%rows[j][0]][0],16) if j<n else T15va+len(T15)
    pos=prev_end
    for k in range(i,j):
        a10,s10,_=rows[k]
        if k==i:
            p=(pos+15)&~15
            while p<next_start and T15[p-T15va] in (0xcc,0x90): p+=16 if T15[p-T15va:p-T15va+16].strip(b'\xcc\x90')==b'' else 1
            out['%x'%a10]=['%x'%p,None,'first']; conf['first']+=1; pos=p+1
        else:
            best=None
            for p in range((pos+15)&~15, next_start, 16):
                s,tot=score(a10,p)
                if tot and s==tot: best=p; break
                if tot and s>=0.75*tot and best is None: best=p
            if best is None: out['%x'%a10]=[None,None,'unplaced']; conf['guessed']+=1
            else: out['%x'%a10]=['%x'%best,None,'matched']; conf['matched']+=1; pos=best+1
    i=j
# sizes for changed ones: up to the next function's start in 1.5 (padding included)
starts=sorted(int(v[0],16) for v in out.values() if v[0])
import bisect
for k,v in out.items():
    if v[0] and v[1] is None:
        a=int(v[0],16); q=bisect.bisect_right(starts,a)
        v[1]=(starts[q]-a) if q<len(starts) else None
json.dump(out,open('/home/claude/v15/functions15.json','w'))
print(conf)

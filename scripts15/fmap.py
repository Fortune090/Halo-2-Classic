"""fmap.py: map every v1.0 function (functions.csv) to its twin in the v1.5 XBE (u5) by masked bytes:
absolute values in 0x10000-0x700000 and rel32 call/jump operands are masked; library sections move by a fixed delta."""
import struct, csv, json, sys, collections
def load(p):
    d=open(p,'rb').read(); base=struct.unpack_from('<I',d,0x104)[0]; ns,sa=struct.unpack_from('<II',d,0x11C)
    secs=[]
    for i in range(ns):
        o=sa-base+56*i; fl,va,vs,raw,rs,na=struct.unpack_from('<6I',d,o)
        secs.append(dict(name=d[na-base:d.index(b'\0',na-base)].decode(),va=va,vs=vs,raw=raw,rs=rs,fl=fl))
    return d,secs
D10,S10=load(sys.argv[1]); D15,S15=load(sys.argv[2])
def text(D,S):
    s=[x for x in S if x['name']=='.text'][0]; return s['va'], D[s['raw']:s['raw']+s['rs']]
T10va,T10=text(D10,S10); T15va,T15=text(D15,S15)
LO,HI=0x10000,0x700000
def mask(b):
    m=bytearray(b'\1'*len(b))
    for i in range(len(b)-3):
        v=struct.unpack_from('<I',b,i)[0]
        if LO<=v<HI: m[i:i+4]=b'\0\0\0\0'
    for i in range(len(b)-4):
        if b[i] in (0xE8,0xE9): m[i+1:i+5]=b'\0\0\0\0'
        if b[i]==0x0F and 0x80<=b[i+1]<=0x8F and i+6<=len(b): m[i+2:i+6]=b'\0\0\0\0'
    return bytes(m)
def same(a,ma,b):
    if len(a)!=len(b): return False
    for i in range(len(a)):
        if ma[i] and a[i]!=b[i]: return False
    return True
rows=[r for r in csv.reader(open(sys.argv[3])) if r and r[0][:1] in '0123456789abcdef' and len(r)>1 and r[1].isdigit()]
funcs=sorted((int(r[0],16),int(r[1]),r[-1].strip(),r[2]) for r in rows)
out={}; delta=0; stats=collections.Counter()
for va,size,status,kind in funcs:
    if not (T10va<=va<T10va+len(T10)) or size<=0: continue
    o=va-T10va; b=T10[o:o+size]; m=mask(b)
    # anchor: longest unmasked run
    best=(0,0); st=None
    for i in range(len(m)+1):
        if i<len(m) and m[i]:
            if st is None: st=i
        else:
            if st is not None and i-st>best[1]-best[0]: best=(st,i)
            st=None
    found=None
    cand=o+delta
    if 0<=cand and cand+size<=len(T15) and same(b,m,T15[cand:cand+size]): found=cand
    elif best[1]-best[0]>=6:
        a=b[best[0]:best[1]]; lo=max(0,o+delta-0x20000); hi=min(len(T15),o+delta+0x20000+size)
        hits=[]; p=T15.find(a,lo,hi)
        while p>=0 and len(hits)<4:
            q=p-best[0]
            if 0<=q and q+size<=len(T15) and same(b,m,T15[q:q+size]): hits.append(q)
            p=T15.find(a,p+1,hi)
        if len(hits)==1: found=hits[0]
        elif len(hits)>1:
            found=min(hits,key=lambda q:abs(q-(o+delta)))
            stats['ambiguous-picked-nearest']+=1
    if found is not None:
        delta=found-o; out['%x'%va]=('%x'%(T15va+found),'same'); stats['same-'+status]+=1
    else:
        out['%x'%va]=(None,'changed'); stats['changed-'+status]+=1
json.dump(out,open(sys.argv[4],'w'))
tot=collections.Counter()
for k,v in stats.items(): tot[k.split('-')[0]]+=v
print(dict(tot)); print(sorted(stats.items(),key=lambda kv:-kv[1])[:20])

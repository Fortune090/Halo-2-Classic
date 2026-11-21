# ncalls.py: for each hook target, the retail call sites (e8 rel32 inside known functions) against the mod's hooked sites
import sys,struct,json,csv,bisect
X=open('/home/claude/xbe/default.xbe','rb').read()
base=struct.unpack_from('<I',X,0x104)[0]; nsec=struct.unpack_from('<I',X,0x11c)[0]; sh=struct.unpack_from('<I',X,0x120)[0]-base
secs=[]
for i in range(nsec):
    fl,va,vs,ra,rs=struct.unpack_from('<5I',X,sh+i*56)
    secs.append((va,vs,ra,rs))
F={}
for r in csv.DictReader(open('/home/claude/h2decomp_src/halo2-decompiled-main/config/functions.csv')): F[int(r['va'],16)]=r
starts=sorted(F)
def owner(va):
    i=bisect.bisect_right(starts,va)-1; s=starts[i]
    return s if va<s+int(F[s]['size'],0) else None
H=json.load(open('hooks.json'))['v1.0']['calls']
want={}
for h,l in H.items():
    for s,t in l: want.setdefault(int(t,16),{})[int(s,16)]=h
found={t:[] for t in want}
for va,vs,ra,rs in secs:
    if va>=0x411000: continue
    d=X[ra:ra+rs]
    i=d.find(b'\xe8')
    while i>=0 and i+5<=len(d):
        t=(va+i+5+struct.unpack_from('<i',d,i+1)[0])&0xffffffff
        if t in found and owner(va+i) is not None: found[t].append(va+i)
        i=d.find(b'\xe8',i+1)
only=set(sys.argv[1].split(',')) if len(sys.argv)>1 else None
for t in sorted(want):
    hs=set(want[t].values())
    if only and not (hs&only): continue
    un=[s for s in found[t] if s not in want[t]]
    print('%x %s: %d calls in retail, %d hooked; not hooked: %s'%(t,'/'.join(sorted(hs)),len(found[t]),len(want[t]),' '.join('%x@%x'%(s,owner(s)) for s in un[:12])))

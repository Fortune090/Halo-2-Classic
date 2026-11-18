import sys,os,json,collections,bisect
root=sys.argv[1]
sys.path.insert(0,os.path.join(root,'tools')); os.chdir(root)
import reduced
from xbe import Xbe, retail_xbe_path
ours=reduced.Image.read(os.path.join(root,'build','halo2.map'),os.path.join(root,'build','halo2.exe'))
retail=Xbe(retail_xbe_path())
o=json.load(open('/home/claude/native/oracle.json')); plan=json.load(open('/home/claude/native/plan.json'))['plan']
lm=ours.linkmap; absf=set(ours.absolute)
byname={}
for s in lm.symbols: byname.setdefault((s.name,s.object,int(s.static)),s)
res=collections.Counter(); bad=[]
seen=set()
for name,obj,static,off,theirs,n in o['raw']:
    p=plan.get(name)
    if not static and not (p and off>=max(p['size'],1)): continue
    s=byname.get((name,obj,static))
    if s is None: continue
    mine=s.va+off
    if mine in seen: continue
    seen.add(mine)
    sec=retail.read(theirs,4)
    # how far: to the next symbol in our image, at most 256 bytes
    nxt=lm.symbol_at(mine)
    end=lm.extent(nxt)[1] if nxt else mine+4
    n_cmp=max(4,min(256,end-mine))
    a=ours.image.read(mine,n_cmp); b=retail.read(theirs,n_cmp)
    if a is None or b is None: res['unreadable']+=1; continue
    diff=0;k=0
    while k<n_cmp:
        if (mine+k) in absf: k+=4; continue
        if a[k]!=b[k]: diff+=1
        k+=1
    if diff==0: res['same as retail']+=1
    else:
        res['differs']+=1; bad.append((hex(theirs),n_cmp,diff,name[:44],off,obj,a[:12].hex(),b[:12].hex()))
print(dict(res))
for b in bad[:40]: print('  ',b)

"""Second pass over the code pointers found in retail data: use what the source itself says about each object."""
import sys,os,json,csv,struct,collections,bisect
sys.path.insert(0,'/home/claude/scripts')
from xbeimg import XbeImage
from coff import Coff
NAT='/home/claude/native'
x=XbeImage('/home/claude/xbe/default.xbe')
rows={int(r['va'],16):r for r in csv.DictReader(open('/home/claude/h2decomp_src/halo2-decompiled-main/config/functions.csv'))}
plan=json.load(open(NAT+'/plan.json'))['plan']
fx=json.load(open(NAT+'/fixups.json'))
funcmap={int(k,16):v for k,v in json.load(open(NAT+'/funcmap.json')).items()}
name2va={v['name']:k for k,v in funcmap.items()}
delta=json.load(open(NAT+'/init_delta.json'))
# relocations inside each redirected symbol, from the native objects
objs=[l.strip() for l in open(NAT+'/objects2.rsp') if l.strip()]
srcrel={}     # retail address of a field -> symbol the source points it at
srcinit=set() # retail addresses the source gives explicit starting bytes
for path in objs:
    c=Coff(path)
    for s in c.sections:
        if s.flags&0x20 or s.data is None: continue
        rel={va:c.by_index[symi].name for va,symi,typ in s.relocs if typ==6}
        for y in s.symbols:
            p=plan.get(y.name) if y.storage==2 else None
            if not p: continue
            for off in range(0,p['size'],1):
                srcinit.add(p['retail']+off)
            for va,tname in rel.items():
                if y.value<=va<y.value+p['size']: srcrel[p['retail']+(va-y.value)]=tname
iv=sorted((p['retail'],p['retail']+max(p['size'],1),n) for n,p in plan.items()); starts=[i[0] for i in iv]
def owner_symbol(a):
    i=bisect.bisect_right(starts,a)-1
    while i>=0 and i>len(iv)-1: i-=1
    for j in range(i,max(-1,i-6),-1):
        if iv[j][0]<=a<iv[j][1]: return iv[j][2]
    return None
mem={}
RANGES=[('.rdata',0x411520,0x461148),('.data',0x461180,0x563b78),('.data1',0x56ad00,0x56ade0)]
for name,lo,hi in RANGES: mem[name]=(lo,bytearray(x.read(lo,hi-lo)))
for a,hexb in delta['runs']:
    for name,(lo,m) in mem.items():
        if lo<=a<lo+len(m): b=bytes.fromhex(hexb); m[a-lo:a-lo+len(b)]=b
def u32(a):
    for name,(lo,m) in mem.items():
        if lo<=a<lo+len(m)-3: return struct.unpack_from('<I',m,a-lo)[0]
    return None
def kind_of_value(v):
    if v is None: return 'none'
    if v==0: return 'zero'
    if v in rows: return 'function'
    if 0x12000<=v<0x411520: return 'midcode'
    if 0x411520<=v<0x586000: return 'data'
    if v<0x10000: return 'small'
    return 'other'
cat=collections.Counter(); out=[]; review=[]; mismatch=[]
for d in fx['fix']:
    a,v=d['at'],d['retail']
    if not d.get('symbol'): cat['library-internal or unknown target: left pointing at the retail address']+=1; continue
    own=owner_symbol(a)
    if a in srcrel:
        t=srcrel[a]; tv=name2va.get(t)
        if tv==v: cat['A: the source has the same pointer here']+=1
        else:
            cat['A: the source has a pointer here, to something else']+=1
            mismatch.append((hex(a),own,'source: '+t[:50],'retail: %#x %s'%(v,d['symbol'][:50])))
        out.append(d); continue
    nb=[kind_of_value(u32(a+4*k)) for k in (-2,-1,1,2)]
    near=[nb[1],nb[2]]
    round_=(v&0xfff)==0
    ptr_near = 'function' in near or 'data' in near
    bad = ('midcode' in near and not ptr_near) or (round_ and not ptr_near and not own)
    if own:
        if a in srcinit: lab='B: inside an object the source initialises, without a pointer here'
        else: lab='B: inside an object the source leaves empty'
    else: lab='C: in data the source does not name'
    if bad:
        cat[lab+' - REJECTED (looks like a number)']+=1; review.append((hex(a),hex(v),own,nb)); continue
    cat[lab]+=1; out.append(d)
for k,n in sorted(cat.items()): print(f'  {n:6d}  {k}')
print('accepted:',len(out),'rejected:',len(review))
for r in review[:30]: print('   rejected',r)
print('source and retail disagree on the target:',len(mismatch))
for m in mismatch[:40]: print('   ',m)
json.dump(dict(fix=out,mismatch=mismatch,review=review),open(NAT+'/fixups2.json','w'))

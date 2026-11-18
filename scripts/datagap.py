"""How much of retail's initial data does the build reproduce? For each global the source names by its retail address,
compare the build's bytes with retail's at that address."""
import sys,os,re,struct,bisect,collections
root,xbe_path=sys.argv[1],sys.argv[2]
sys.path.insert(0,os.path.join(root,'tools'))
from pe import Pe
pe=Pe(os.path.join(root,'build','halo2.exe'))
x=open(xbe_path,'rb').read()
base,hsz,isz,ihsz,ts,cert,nsec,sech=struct.unpack_from('<8I',x,0x104)
xs=[]
for i in range(nsec):
    o=sech-base+i*56
    flags,va,vsz,raw,rsz,nameaddr=struct.unpack_from('<6I',x,o)
    name=x[nameaddr-base:x.index(b'\0',nameaddr-base)].decode('latin1'); xs.append((name,va,vsz,raw,rsz))
def retail(va,n):
    for name,sva,vsz,raw,rsz in xs:
        if sva<=va<sva+vsz:
            off=va-sva; b=x[raw+off:raw+min(off+n,rsz)] if off<rsz else b''
            return b+bytes(n-len(b)),name,off<rsz
    return None,None,False
def ours(va,n):
    for s in pe.sections:
        if s.va<=va<s.va+max(s.vsize,s.rsize):
            off=va-s.va; b=pe.data[s.raw+off:s.raw+min(off+n,s.rsize)] if off<s.rsize else b''
            return b+bytes(n-len(b))
    return None
fix=set(pe.fixups) if hasattr(pe,'fixups') else set()
# map symbols
syms=[]
for line in open(os.path.join(root,'build','halo2.map'),errors='replace'):
    m=re.match(r'\s*([0-9a-f]{4}):([0-9a-f]{8})\s+(\S+)\s+([0-9a-f]{8})\s+(?:f\s+)?(?:i\s+)?(\S+)',line)
    if m: syms.append((int(m.group(4),16),m.group(3),m.group(5)))
syms.sort(); addrs=[s[0] for s in syms]
named=re.compile(r'g_(?:unknown_|global_)?([0-9a-f]{6})(?![0-9a-f])')
img_lo,img_hi=0x10000,0x10000+isz
tot=collections.Counter(); cover={}  # retail va -> state per byte
diffs=[]
for i,(va,name,obj) in enumerate(syms):
    m=named.search(name)
    if not m: continue
    A=int(m.group(1),16)
    if not (0x411520<=A<0x586000): continue
    size=(addrs[i+1]-va) if i+1<len(addrs) else 4
    j=i+1
    while j<len(addrs) and addrs[j]==va: j+=1
    size=addrs[j]-va if j<len(addrs) else 4
    size=min(size,0x40000)
    rb,sec,backed=retail(A,size); ob=ours(va,size)
    if rb is None or ob is None: tot['named global outside retail data']+=1; continue
    same=0;diff=0;ptr=0
    k=0
    while k<size:
        if (va+k) in fix and k+4<=size:
            rv=struct.unpack_from('<I',rb,k)[0]
            ok=img_lo<=rv<img_hi
            for q in range(4): cover[A+k+q]='pointer' if ok else 'differs'
            ptr+=4 if ok else 0; diff+=0 if ok else 4; k+=4; continue
        if ob[k]==rb[k]: cover.setdefault(A+k,'same'); same+=1
        else: cover[A+k]='differs'; diff+=1
        k+=1
    if diff: diffs.append((diff,size,f'{A:#x}',name[:50],obj))
print('globals named by retail address in the build:',sum(1 for _,n,_ in syms if named.search(n)))
for secname in ('.rdata','.data'):
    name,sva,vsz,raw,rsz=[s for s in xs if s[0]==secname][0]
    c=collections.Counter(); nz=collections.Counter()
    for off in range(rsz):
        st=cover.get(sva+off,'not defined by an address-named global')
        c[st]+=1
        if x[raw+off]: nz[st]+=1
    print(f'{secname}: {rsz} initialised bytes in retail ({sum(1 for b in x[raw:raw+rsz] if b)} non-zero)')
    for k,v in c.most_common(): print(f'    {k:42s} {v:7d} bytes ({nz[k]} non-zero)')
diffs.sort(reverse=True)
print('globals with differing bytes:',len(diffs),'bytes',sum(d[0] for d in diffs))
for d in diffs[:15]: print('   ',d)

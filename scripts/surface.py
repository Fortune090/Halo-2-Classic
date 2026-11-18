import csv,sys,collections,struct
rows={int(r['va'],16):r for r in csv.DictReader(open(sys.argv[1]))}
d3dn={}
for l in open(sys.argv[2]):
    if l.startswith('#') or l.startswith('va,'): continue
    p=l.strip().split(','); d3dn[int(p[0],16)]=p[1]
xbe=open(sys.argv[3],'rb').read()
secs=[]
base,hsz,isz,ihsz,ts,cert,nsec,sech=struct.unpack_from('<8I',xbe,0x104)
for i in range(nsec):
    o=sech-base+i*56
    flags,va,vsz,raw,rsz,nameaddr=struct.unpack_from('<6I',xbe,o)
    name=xbe[nameaddr-base:xbe.index(b'\0',nameaddr-base)].decode('latin1')
    secs.append((name,va,vsz,raw,rsz))
def lib(r,va):
    o=r['owner']
    if o in('game','third:havok','eh'): return None
    return o
used=collections.defaultdict(set); callers=collections.defaultdict(set)
for va,r in rows.items():
    if r['owner'] not in('game','third:havok'): continue
    for c in r['calls'].split():
        t=int(c,16); tr=rows.get(t)
        if not tr:
            used['(not a known function)'].add(t); continue
        L=lib(tr,t)
        if L: used[L].add(t); callers[t].add(va)
out=csv.writer(open(sys.argv[4],'w',newline=''))
out.writerow(['library','va','size','name','name_source','game_callers'])
tot=0
for L in sorted(used,key=lambda L:-len(used[L])):
    allf=[r for r in rows.values() if r['owner']==L]
    named=0
    for t in sorted(used[L]):
        r=rows.get(t,{}); nm=r.get('name',''); src='decomp csv' if nm else ''
        if not nm and t in d3dn: nm=d3dn[t]; src='our D3D naming'
        named+=bool(nm)
        out.writerow([L,f'{t:08x}',r.get('size',''),nm,src,len(callers[t])])
    tot+=len(used[L])
    print(f'{L:24s} called directly by game/Havok: {len(used[L]):4d} of {len(allf):4d} functions in that library; named {named}')
print('total library entry points the game code calls:',tot)

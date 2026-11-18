"""For every global the source names by its retail address: does the source give it the starting value retail has?"""
import sys,os,re,struct,collections,json
sys.path.insert(0,'/home/claude/scripts')
from coff import Coff
objdir=sys.argv[1]; xbe=open(sys.argv[2],'rb').read()
base,hsz,isz,ihsz,ts,cert,nsec,sech=struct.unpack_from('<8I',xbe,0x104)
xs=[]
for i in range(nsec):
    o=sech-base+i*56
    flags,va,vsz,raw,rsz,nameaddr=struct.unpack_from('<6I',xbe,o)
    name=xbe[nameaddr-base:xbe.index(b'\0',nameaddr-base)].decode('latin1'); xs.append((name,va,vsz,raw,rsz))
def retail(va,n):
    """(bytes or None, section name, number of file-backed bytes)"""
    for name,sva,vsz,raw,rsz in xs:
        if sva<=va<sva+vsz:
            off=va-sva; backed=max(0,min(n,rsz-off))
            b=xbe[raw+off:raw+off+backed]+bytes(n-backed)
            return b,name,backed
    return None,None,0
named=re.compile(r'g_(?:unknown_|global_)?([0-9a-f]{6})(?![0-9a-f])')
keep=json.load(open('/home/claude/native/keep_manifest.json'))
replaced={os.path.basename(k).replace('_keep.obj','.obj') for k in keep}
objs=sorted(os.path.join(objdir,f) for f in os.listdir(objdir) if f.endswith('.obj') and f not in replaced)+sorted(keep)
seen={}
stats=collections.Counter(); bytes_=collections.Counter(); missing=[]; differ=[]
for path in objs:
    c=Coff(path)
    for s in c.sections:
        if not s.symbols: continue
        if s.flags & 0x20: continue   # code
        syms=sorted([y for y in s.symbols if y.storage in (2,3) and not (y.storage==3 and y.name==s.name)],key=lambda y:y.value)
        size=s.rawsize if s.rawsize else s.vsize
        rel={va:(symi,typ) for va,symi,typ in s.relocs}
        for i,y in enumerate(syms):
            if y.storage!=2: continue
            m=named.search(y.name)
            if not m: continue
            A=int(m.group(1),16)
            if not (0x411520<=A<0x586000): continue
            nxt=min([z.value for z in syms if z.value>y.value]+[size])
            n=nxt-y.value
            if n<=0 or y.name in seen: continue
            seen[y.name]=(os.path.basename(path),A,n)
            rb,secname,backed=retail(A,n)
            if rb is None: stats['retail address not in a data section']+=1; continue
            ob=s.data[y.value:y.value+n] if s.data is not None else None
            r_nonzero=sum(1 for b in rb if b)
            if ob is None:
                if r_nonzero==0: stats['both start as zero']+=1; bytes_['both start as zero']+=n
                else:
                    stats['retail has a starting value, source leaves it zero']+=1; bytes_['retail has a starting value, source leaves it zero']+=r_nonzero
                    missing.append((A,n,r_nonzero,y.name,os.path.basename(path),secname))
                continue
            # ours initialised: compare outside relocations
            bad=0; k=0; ptr=0
            while k<n:
                if (y.value+k) in rel and k+4<=n: ptr+=1; k+=4; continue
                if ob[k]!=rb[k]: bad+=1
                k+=1
            if bad==0: stats['source gives a starting value, same as retail (pointers not compared)']+=1; bytes_['source gives a starting value, same as retail (pointers not compared)']+=n
            else:
                stats['source gives a starting value that differs']+=1; bytes_['source gives a starting value that differs']+=bad
                differ.append((A,n,bad,y.name,os.path.basename(path),secname))
print('address-named globals defined in the objects:',len(seen))
for k,v in stats.most_common(): print(f'  {v:5d} globals {bytes_[k]:8d} bytes  {k}')
missing.sort(key=lambda t:-t[2])
print('largest missing starting values:')
for t in missing[:25]: print('   %#x size %d nonzero %d %s %s %s'%t)
print('differing (first 15 by bytes):')
for t in sorted(differ,key=lambda t:-t[2])[:15]: print('   %#x size %d differing %d %s %s %s'%t)
json.dump({'missing':missing,'differ':differ,'seen':seen},open('/home/claude/native/datacmp.json','w'))

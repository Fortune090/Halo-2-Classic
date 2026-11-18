"""From every function whose bytes match retail, read off which retail address each of our symbols stands for:
the function's address fields sit at the same offsets in both builds, ours naming a symbol and retail's an address."""
import sys,os,json,collections,struct
root=sys.argv[1]
sys.path.insert(0,os.path.join(root,'tools')); os.chdir(root)
import build, check, reduced
from inventory import read_rows
from xbe import Xbe, retail_xbe_path, FUNCTIONS_CSV
ours=reduced.Image.read(os.path.join(root,'build','halo2.map'),os.path.join(root,'build','halo2.exe'))
retail=Xbe(retail_xbe_path())
rows=read_rows(FUNCTIONS_CSV)
marked=build.marked_sources(); stubs=build.stub_sources()
standin_calls=check.StandinCalls(ours.linkmap,ours.image,marked)
report=json.load(open(os.path.join(root,'build','report.json')))
import bisect
absf=ours.absolute; relf=ours.relative
lm=ours.linkmap
secs=ours.image.sections
def sec_of(va):
    for s in secs:
        if s.va<=va<s.va+max(s.vsize,s.rsize): return s
votes=collections.defaultdict(collections.Counter)   # (name, object, static) -> Counter(retail base)
raw=collections.Counter()   # (name, object, static, offset into the symbol, retail address) -> fields
funcs={}       # retail va -> (decorated name, object, static, status)
nofield=0
for m in marked:
    e=report.get(f'{m.retail:08x}')
    sym=check.resolve(lm,m,standin_calls)
    if sym is None: continue
    funcs[m.retail]=(sym.name,sym.object,sym.static,e['status'] if e else '?')
    if not e or e['status']!='matched': continue
    start,end=lm.extent(sym)
    size=int(rows[m.retail]['size'])
    lo=bisect.bisect_left(absf,start); hi=bisect.bisect_left(absf,start+size-3)
    for f in absf[lo:hi]:
        mine=int.from_bytes(ours.image.read(f,4),'little')
        theirs=int.from_bytes(retail.read(m.retail+(f-start),4),'little')
        t=lm.symbol_at(mine)
        if t is None: nofield+=1; continue
        if t.function: continue
        votes[(t.name,t.object,t.static)][theirs-(mine-t.va)]+=1
        raw[(t.name,t.object,int(t.static),mine-t.va,theirs)]+=1
out={}; conflicts=[]
for key,c in votes.items():
    best,n=c.most_common(1)[0]
    if len(c)>1: conflicts.append((key,c.most_common(4)))
    out['|'.join([key[0],key[1],str(int(key[2]))])]=dict(retail=best,votes=n,alternatives=len(c))
json.dump(dict(data=out,functions={f'{k:08x}':v for k,v in funcs.items()},raw=[list(k)+[n] for k,n in raw.items()]),open('/home/claude/native/oracle.json','w'))
print('matched functions used:',sum(1 for v in funcs.values() if v[3]=='matched'),'marked functions resolved:',len(funcs))
print('data symbols with a retail address:',len(out),'conflicting:',len(conflicts),'fields without a symbol:',nofield)
import re
named=re.compile(r'g_(?:unknown_|global_)?([0-9a-f]{6})(?![0-9a-f])')
agree=dis=other=0; kinds=collections.Counter(); ex=collections.defaultdict(list)
for k,v in out.items():
    name,obj,static=k.split('|')
    m=named.search(name)
    if m:
        if int(m.group(1),16)==v['retail']: agree+=1
        else: dis+=1; ex['named but oracle disagrees'].append((name,hex(v['retail']),v['votes']))
    else:
        r=v['retail']
        kind=('string/const' if name.startswith(('??_C@','__real@','$SG','__xmm@','__mask')) else 'vtable' if name.startswith('??_7') else 'static' if static=='1' else 'other external')
        where='rdata' if 0x411520<=r<0x461180 else 'data' if 0x461180<=r<0x563b80 else 'libsection' if r<0x411520 else 'other'
        kinds[(kind,where)]+=1
        if len(ex[(kind,where)])<6: ex[(kind,where)].append((name[:60],obj,hex(r),v['votes']))
print('address-named: agree',agree,'disagree',dis)
for k,v in sorted(kinds.items(),key=lambda t:-t[1]): print('  ',k,v, ex[k][:4])
print(ex['named but oracle disagrees'][:10])
for key,c in conflicts[:12]: print('  conflict',key[0][:50],key[1],[(hex(a),n) for a,n in c])

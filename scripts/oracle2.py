"""Check every address field of every matched function against retail: data fields against the redirect plan,
call fields against the retail address of the function ours names."""
import sys,os,json,collections,bisect
root=sys.argv[1]
sys.path.insert(0,os.path.join(root,'tools')); os.chdir(root)
import build, check, reduced
from inventory import read_rows
from xbe import Xbe, retail_xbe_path, FUNCTIONS_CSV
ours=reduced.Image.read(os.path.join(root,'build','halo2.map'),os.path.join(root,'build','halo2.exe'))
retail=Xbe(retail_xbe_path()); rows=read_rows(FUNCTIONS_CSV)
marked=build.marked_sources(); stubs=build.stub_sources()
standin_calls=check.StandinCalls(ours.linkmap,ours.image,marked)
report=json.load(open(os.path.join(root,'build','report.json')))
plan=json.load(open('/home/claude/native/plan.json'))['plan']
lm=ours.linkmap; absf=ours.absolute; relf=ours.relative
ourfunc={}      # our va -> retail va, for marked functions (and stubs)
resolved={}
for m in marked+stubs:
    try: sym=check.resolve(lm,m,standin_calls)
    except Exception: sym=None
    if sym is None: continue
    ourfunc[sym.va]=m.retail; resolved[m.retail]=sym
libnames={}     # retail address -> library symbol name, from call fields of matched functions
data_all=[]; data_bad=[]; call_bad=[]; call_ok=0; call_lib=collections.Counter(); call_unknown=collections.Counter()
for m in marked:
    e=report.get(f'{m.retail:08x}')
    if not e or e['status']!='matched': continue
    sym=resolved.get(m.retail)
    if sym is None: continue
    start,end=lm.extent(sym); size=int(rows[m.retail]['size'])
    for f in absf[bisect.bisect_left(absf,start):bisect.bisect_left(absf,start+size-3)]:
        mine=int.from_bytes(ours.image.read(f,4),'little'); theirs=int.from_bytes(retail.read(m.retail+(f-start),4),'little')
        t=lm.symbol_at(mine)
        if t is None: continue
        if t.function:
            r=ourfunc.get(t.va)
            if mine==t.va and r is not None and r!=theirs: call_bad.append((f'{m.retail:#x}',m.name,'address of',t.name[:50],hex(r),hex(theirs)))
            elif mine==t.va and r is None: libnames.setdefault(theirs,(t.name,t.object))
            continue
        p=plan.get(t.name)
        if p and not t.static and (mine-t.va)<max(p['size'],1): data_all.append((f'{m.retail:#x}',t.name,mine-t.va,theirs,f-start))
        if p and not t.static and (mine-t.va)<max(p['size'],1) and theirs!=p['retail']+(mine-t.va):
            data_bad.append((f'{m.retail:#x}',m.name,m.path,t.name,mine-t.va,hex(p['retail']+(mine-t.va)),hex(theirs)))
    for f in relf[bisect.bisect_left(relf,start):bisect.bisect_left(relf,start+size-3)]:
        mine=(f+4+int.from_bytes(ours.image.read(f,4),'little',signed=True))&0xffffffff
        theirs=(m.retail+(f-start)+4+int.from_bytes(retail.read(m.retail+(f-start),4),'little',signed=True))&0xffffffff
        t=lm.symbol_at(mine)
        if t is None or mine!=t.va: call_unknown['no symbol at our target']+=1; continue
        r=ourfunc.get(t.va)
        if r is None:
            call_lib[(t.name,t.object)]+=1; 
            prev=libnames.setdefault(theirs,(t.name,t.object))
            if prev[0]!=t.name: call_unknown['library name clash']+=1
        elif r==theirs: call_ok+=1
        else: call_bad.append((f'{m.retail:#x}',m.name,'calls',t.name[:50],hex(r),hex(theirs)))
print('calls between marked functions: consistent',call_ok,'inconsistent',len(call_bad))
for c in call_bad[:20]: print('   ',c)
print('calls to unmarked (library) functions:',sum(call_lib.values()),'distinct names',len(call_lib),'distinct retail addresses',len(libnames),dict(call_unknown))
print('data fields inconsistent with the plan:',len(data_bad))
for d in data_bad: print('   ',d[0],d[1][:34],d[3][:44],'+%d'%d[4],'plan',d[5],'retail',d[6])
funcmap={f'{r:08x}':dict(name=s.name,object=s.object,static=s.static,stub=(r in {m.retail for m in stubs})) for r,s in resolved.items()}
json.dump(funcmap,open('/home/claude/native/funcmap.json','w'))
print('functions with a symbol:',len(funcmap),'of which stubs',sum(1 for v in funcmap.values() if v['stub']),'static',sum(1 for v in funcmap.values() if v['static']))
badf={d[0] for d in data_bad}
json.dump(dict(libnames={f'{k:08x}':v for k,v in libnames.items()},data_bad=data_bad,call_bad=call_bad,data_fields=[d for d in data_all if d[0] in badf]),open('/home/claude/native/oracle2.json','w'))

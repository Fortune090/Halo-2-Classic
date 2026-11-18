"""Static data the oracle could not name: address fields of matched functions that point, in our LTCG image, at data
with no symbol in the link map (file-scope statics), paired with the retail address at the same place.
-> native/statics_fields.json: [function retail va, function's object, our address, retail address]"""
import sys,os,json,collections,bisect
root=sys.argv[1]
sys.path.insert(0,os.path.join(root,'tools')); os.chdir(root)
import build, check, reduced
from inventory import read_rows
from xbe import Xbe, retail_xbe_path, FUNCTIONS_CSV
ours=reduced.Image.read(os.path.join(root,'build','halo2.map'),os.path.join(root,'build','halo2.exe'))
retail=Xbe(retail_xbe_path())
rows=read_rows(FUNCTIONS_CSV)
marked=build.marked_sources()
standin_calls=check.StandinCalls(ours.linkmap,ours.image,marked)
report=json.load(open(os.path.join(root,'build','report.json')))
absf=ours.absolute; lm=ours.linkmap
out=[]
plan=json.load(open('/home/claude/native/plan.json'))
sizes={k:v['size'] for k,v in plan['plan'].items()}
for k,v in plan['statics']: sizes.setdefault(k,v['size'])
for m in marked:
    e=report.get(f'{m.retail:08x}')
    sym=check.resolve(lm,m,standin_calls)
    if sym is None or not e or e['status']!='matched': continue
    start,end=lm.extent(sym)
    size=int(rows[m.retail]['size'])
    lo=bisect.bisect_left(absf,start); hi=bisect.bisect_left(absf,start+size-3)
    for f in absf[lo:hi]:
        mine=int.from_bytes(ours.image.read(f,4),'little')
        theirs=int.from_bytes(retail.read(m.retail+(f-start),4),'little')
        t=lm.symbol_at(mine)
        if t is None: out.append([m.retail,sym.object,mine,theirs,sym.name,None,0]); continue
        if t.function: continue
        size=sizes.get(t.name)
        if size is not None and mine-t.va>=size: out.append([m.retail,sym.object,mine,theirs,sym.name,t.name,mine-t.va])
json.dump(dict(fields=out,sections=[[s.name,s.va,s.vsize] for s in ours.image.sections]),open('/home/claude/native/statics_fields.json','w'))
print(len(out),'fields')

"""Vtables the plan does not place: classes whose constructors are not in matched code. Our compiled copy has the
slot order the source declares, which nothing proves; retail's own table for the class is in the data in place. Find
it by the functions both hold: each function in our table has a retail address (funcmap); the retail table is where
those addresses stand in a row in retail's .rdata.  -> native/vtables_plan.json: object|symbol -> retail address"""
import sys,os,json,struct,collections,glob
sys.path.insert(0,'/home/claude/scripts')
from coff import Coff
from xbeimg import XbeImage
NAT='/home/claude/native'
plan=json.load(open(NAT+'/plan.json'))['plan']
fm=json.load(open(NAT+'/funcmap.json'))
byname=collections.defaultdict(list)
for va,v in fm.items(): byname[v['name']].append(int(va,16))
x=XbeImage('/home/claude/xbe/default.xbe')
RD0,RD1=0x411520,0x461148
rdata=x.read(RD0,RD1-RD0)
where=collections.defaultdict(list)          # retail function address -> places in .rdata that hold it
for off in range(0,len(rdata)-3,4):
    v=struct.unpack_from('<I',rdata,off)[0]
    if 0x12000<=v<0x411000: where[v].append(RD0+off)
# where a retail table can start: an address in .rdata that retail's code names (a constructor storing it)
starts=set()
for s in x.sections:
    if s['va']>=0x411000: continue
    code=x.x[s['raw']:s['raw']+s['rsize']]
    for i in range(len(code)-3):
        if code[i+3]==0 and code[i+2] in (0x41,0x42,0x43,0x44,0x45,0x46):
            v=struct.unpack_from('<I',code,i)[0]
            if RD0<=v<RD1 and v%4==0: starts.add(v)
# functions that many classes share (folded bodies) say little about which table this is
common={r for r,places in where.items() if len(places)>6}
result={}; stats=collections.Counter(); report=[]
for path in sorted(glob.glob(NAT+'/obj2/*.obj')):
    c=Coff(path); base=os.path.basename(path)
    for y in c.symbols:
        if not y.name.startswith('??_7') or y.section<=0 or y.storage!=2: continue
        if y.name in plan: continue
        sec=c.sections[y.section-1]
        slots=[]
        rel={off:c.by_index[symi].name for off,symi,typ in sec.relocs if symi in c.by_index}
        off=y.value
        while off in rel: slots.append(rel[off]); off+=4
        if not slots: stats['empty']+=1; continue
        # votes for where the retail table starts: each of our slots whose function has a retail address
        votes=collections.Counter()
        for i,name in enumerate(slots):
            for r in byname.get(name,[]):
                if r in common: continue
                for place in where.get(r,[]):
                    for shift in (0,-4,4,-8,8):                 # the source may have a slot too few or too many before it
                        votes[place-4*i+shift]+=1 if shift else 3
        cands=[(n,a) for a,n in votes.items() if a in starts]
        if not cands: stats['no retail table found']+=1; report.append((base,y.name,len(slots),'none')); continue
        cands.sort(reverse=True)
        best=cands[0]
        if len(cands)>1 and cands[1][0]==best[0]: stats['ambiguous']+=1; report.append((base,y.name,len(slots),'ambiguous %s'%[hex(a) for n,a in cands[:3]])); continue
        result[base+'|'+y.name]=best[1]; stats['placed']+=1
        # does the slot order agree?
        same=sum(1 for i,name in enumerate(slots) if x.u32(best[1]+4*i) in byname.get(name,[]))
        if same<len([s for s in slots if byname.get(s)]): stats['placed, and the source declares a different slot order']+=1
json.dump(result,open(NAT+'/vtables_plan.json','w'),indent=0,sort_keys=True)
print(dict(stats))
for r in report[:12]: print('  ',r)
print({k:hex(v) for k,v in result.items() if 'c_vertex_shape' in k})

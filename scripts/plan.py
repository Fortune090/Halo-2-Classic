"""Decide, for every data symbol the game objects define, which retail address it stands for (if any)."""
import sys,os,re,json,collections
sys.path.insert(0,'/home/claude/scripts')
from coff import Coff
NAT='/home/claude/native'
objs=[l.strip() for l in open(NAT+'/objects2.rsp') if l.strip()]
oracle=json.load(open(NAT+'/oracle.json'))
INPLACE=[(0x411520,0x461148,'.rdata'),(0x461180,0x563b78,'.data'),(0x56ad00,0x56ade0,'.data1')]
def inplace(a): return any(lo<=a<hi for lo,hi,_ in INPLACE)
named=re.compile(r'g_(?:unknown_|global_)?([0-9a-f]{6})(?![0-9a-f])')
# 1. data symbols in the native objects
syms={}      # name -> dict(obj, size, section, comdat, static)
statics=[]
for path in objs:
    c=Coff(path); base=os.path.basename(path)
    for s in c.sections:
        if s.flags & 0x20 or not s.symbols: continue
        if s.name.startswith(('.debug','.drectve','.CRT')): continue
        ys=sorted([y for y in s.symbols if not (y.storage==3 and y.name==s.name and y.value==0 and y.naux)],key=lambda y:y.value)
        size=s.rawsize if s.rawsize else s.vsize
        for y in ys:
            nxt=min([z.value for z in ys if z.value>y.value]+[size])
            n=nxt-y.value
            rec=dict(obj=base,size=n,section=s.name,comdat=bool(s.flags&0x1000),static=y.storage==3,bss=s.data is None)
            if y.storage==2:
                if y.name in syms and not rec['comdat']: print('duplicate definition',y.name,base,syms[y.name]['obj'])
                syms.setdefault(y.name,rec)
            elif y.storage==3: statics.append((y.name,rec))
print('external data symbols defined:',len(syms),' static data symbols:',len(statics))
# 2. oracle votes, limited to offsets inside the symbol
votes=collections.defaultdict(collections.Counter); beyond=collections.Counter()
lt_obj=lambda o:o.split(':')[-1]
for name,obj,static,off,theirs,n in oracle['raw']:
    if static: continue
    s=syms.get(name)
    if s is None: continue
    if off<max(s['size'],1): votes[name][theirs-off]+=n
    else: beyond[name]+=n
plan={}; report=collections.Counter(); notes=[]
for name,s in syms.items():
    m=named.search(name); a_name=int(m.group(1),16) if m else None
    if a_name is not None and not inplace(a_name): a_name=None if not (0x12000<=a_name<0x586000) else a_name
    v=votes.get(name)
    a_or=v.most_common(1)[0][0] if v else None
    kind='vtable' if name.startswith('??_7') else 'const' if name.startswith(('??_C@','__real@','__xmm@','__mask@')) else 'global'
    if kind=='const': report['string / float constants (left as ours)']+=1; continue
    if a_name is not None and a_or is not None:
        if a_name==a_or or a_name in v: choice=a_name; report['named, confirmed by matched code']+=1
        else:
            choice=a_or; report['named, but matched code says another address (matched code used)']+=1
            notes.append(('DISAGREE',name,hex(a_name),[(hex(a),n) for a,n in v.most_common(3)],s['size'],s['obj']))
    elif a_name is not None: choice=a_name; report['named, no matched code refers to it (name trusted)']+=1
    elif a_or is not None:
        choice=a_or; report[f'{kind}: address from matched code']+=1
        if len(v)>1: notes.append(('AMBIGUOUS',name,[(hex(a),n) for a,n in v.most_common(3)],s['size'],s['obj']))
    else: report[f'{kind}: no retail address known (left as ours)']+=1; continue
    if not inplace(choice):
        report['address outside the game data sections (left as ours)']+=1
        notes.append(('OUTSIDE',name,hex(choice),s['obj'])); continue
    plan[name]=dict(retail=choice,size=s['size'],obj=s['obj'],kind=kind,comdat=s['comdat'],bss=s['bss'])
for k,v in report.most_common(): print(f'  {v:5d}  {k}')
print('redirected to retail data in place:',len(plan))
for n in notes:
    if n[0]!='AMBIGUOUS': print('  ',n)
print('ambiguous:',sum(1 for n in notes if n[0]=='AMBIGUOUS'))
for n in [n for n in notes if n[0]=='AMBIGUOUS'][:12]: print('  ',n)
# overlaps between redirected symbols
iv=sorted((p['retail'],p['retail']+max(p['size'],1),n) for n,p in plan.items())
ov=0
for (a0,a1,n0),(b0,b1,n1) in zip(iv,iv[1:]):
    if b0<a1:
        ov+=1
        if ov<=15: print('   OVERLAP %s [%#x,%#x) with %s [%#x,%#x)'%(n0,a0,a1,n1,b0,b1))
print('overlapping pairs:',ov)
json.dump(dict(plan=plan,statics=[(n,r) for n,r in statics]),open(NAT+'/plan.json','w'))

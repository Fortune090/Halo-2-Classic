"""Find every pointer to code inside retail's data as it stands after the start-up initialisers, and decide what each
must point to in our build."""
import sys,os,json,csv,struct,collections
sys.path.insert(0,'/home/claude/scripts')
from xbeimg import XbeImage
NAT='/home/claude/native'
x=XbeImage('/home/claude/xbe/default.xbe')
rows={int(r['va'],16):r for r in csv.DictReader(open('/home/claude/h2decomp_src/halo2-decompiled-main/config/functions.csv'))}
funcmap={int(k,16):v for k,v in json.load(open(NAT+'/funcmap.json')).items()}
o2=json.load(open(NAT+'/oracle2.json')); libnames={int(k,16):v for k,v in o2['libnames'].items()}
unresolved=set(json.load(open(NAT+'/unresolved3.json')))
delta=json.load(open(NAT+'/init_delta.json'))
RANGES=[('.rdata',0x411520,0x461148),('.data',0x461180,0x563b78),('.data1',0x56ad00,0x56ade0)]
mem={}
for name,lo,hi in RANGES:
    mem[name]=bytearray(x.read(lo,hi-lo))
def sec_of(a):
    for name,lo,hi in RANGES:
        if lo<=a<hi: return name,lo
    return None,None
for a,hexb in delta['runs']:
    name,lo=sec_of(a)
    b=bytes.fromhex(hexb); mem[name][a-lo:a-lo+len(b)]=b
written=set(int(a) for a in delta['writer'])
printable=lambda b:0x20<=b<=0x7e
cands=[]; rejected=[]; mid=collections.Counter()
for name,lo,hi in RANGES:
    m=mem[name]
    start=(lo+3)&~3
    for a in range(start,hi-3,4):
        if 0x461180<=a<0x461860: continue      # the initialiser tables themselves
        v=struct.unpack_from('<I',m,a-lo)[0]
        if not (0x12000<=v<0x411520): continue
        r=rows.get(v)
        if r is None:
            mid[name]+=1; continue
        prev=m[a-lo-4:a-lo] if a-lo>=4 else b'\0\0\0\0'
        if all(printable(b) for b in prev) and all(printable(b) for b in m[a-lo:a-lo+3]):
            rejected.append((a,v,bytes(m[max(0,a-lo-12):a-lo+4]))); continue
        cands.append((a,v))
print('pointers to a function start found in data:',len(cands),'; looked like text and left alone:',len(rejected),'; values inside code but not at a function start:',dict(mid))
kinds=collections.Counter(); fix=[]; traps={}
for a,v in cands:
    r=rows[v]; f=funcmap.get(v)
    if f:
        kinds['our function'+(' (stand-in for a missing one)' if f['stub'] else '')]+=1
        fix.append(dict(at=a,retail=v,symbol=f['name'],static=f['static'],object=f['object']))
        continue
    lib=libnames.get(v) or ((r['name'],'') if r['name'] else None)
    owner=r['owner']
    if lib and lib[0] in unresolved:
        kinds['library function we provide']+=1; fix.append(dict(at=a,retail=v,symbol=lib[0],static=False,object='')); continue
    if owner=='other:library' and 0x3762e0<=v<0x379c60: kinds['start-up initialiser or exit handler (left alone)']+=1; continue
    if owner=='eh': kinds['exception-handling code (left alone)']+=1; continue
    kinds[f'no counterpart: {owner}']+=1
    traps.setdefault(v,dict(retail=v,owner=owner,name=(lib[0] if lib else r['name']) or '',uses=[]))['uses'].append(a)
    fix.append(dict(at=a,retail=v,symbol=None,trap=v))
for k,n in kinds.most_common(): print(f'  {n:6d}  {k}')
print('distinct functions with no counterpart:',len(traps))
byowner=collections.Counter(t['owner'] for t in traps.values()); print('  ',dict(byowner))
for t in sorted(traps.values(),key=lambda t:-len(t['uses']))[:25]: print('   %#x %-14s %-34s referenced %d times, first at %#x'%(t['retail'],t['owner'],t['name'][:34],len(t['uses']),t['uses'][0]))
print('where the pointers are:',collections.Counter((sec_of(d['at'])[0],'set by an initialiser' if d['at'] in written else 'in the file') for d in fix))
print('text-like rejects (first 12):')
for a,v,ctx in rejected[:12]: print('   %#x -> %#x %r'%(a,v,ctx))
json.dump(dict(fix=fix,traps=list(traps.values()),rejected=[(a,v) for a,v,_ in rejected]),open(NAT+'/fixups.json','w'))

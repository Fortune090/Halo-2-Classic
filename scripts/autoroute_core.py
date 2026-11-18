import csv,json,collections,bisect,sys,pickle
src=open('/home/claude/scripts/retailconv.py').read()
head=src[:src.index("routes={}; why={}")]
g={}
exec(compile(head,'retailconv_head','exec'),g)
fm=g['fm']; rows=g['rows']; classify=g['classify']; clob=g['clob']; need=g['need']
calls={va:[int(c,16) for c in r['calls'].split()] for va,r in rows.items()}
starts=sorted(rows)
def func_of(a):
    i=bisect.bisect_right(starts,a)-1
    va=starts[i]
    return va if a<va+int(rows[va]['size']) else None
lib=json.load(open('oracle2.json'))['libnames']
import os
if os.path.exists('libsig.json'):
    for k,v in json.load(open('libsig.json')).items(): lib.setdefault(k,v)
used=json.load(open('unresolved3.json')); used=set(used if isinstance(used,list) else used.keys())
KEEP={'__chkstk','__ftol2','__CIacos','__CIasin','__CIfmod','__CIpow','__allmul','__alldiv','__allrem','__aulldiv','__allshl','__except_list','___CxxFrameHandler','__purecall','__fltused'}
patched={int(va,16) for va,(name,where) in lib.items() if name in used and name not in KEEP and 'D3D' not in name and 'Direct3D' not in name}
BACK={0x1d2f0,0x439d0,0x15ca10}
HARD=('xdk:d3d8','xdk:d3dx8','xdk:d3dx','xdk:dsound','xdk:xvoice','xdk:xnet','xdk:xonline','xdk:xonlines','xdk:wmadec','third:bink','xdk:xapi','xdk:xapilib')
memo={}
def reach_bad(root):
    """library functions of the hardware-facing kinds that retail code starting at root reaches by direct calls and that do not lead to ours"""
    seen=set(); stack=[root]; bad=set()
    while stack:
        f=stack.pop()
        if f in seen or f not in rows: continue
        seen.add(f)
        o=rows[f]['owner']
        if o not in('game','third:havok'):
            if f in patched: continue
            if o in HARD: bad.add(f)
            continue                         # other library code: not followed (pure helpers), kernel calls trap by name
        if f in BACK and f!=root: continue
        for c in calls.get(f,[]):
            t=func_of(c)
            if t is not None and t not in seen: stack.append(t)
    return bad,seen
# a better answer to "which of ebx, esi, edi, ebp does retail's function hand back changed": a register counts as kept when
# the function pushes it before first writing it and pops it again somewhere; otherwise every write of its own or of a
# function it calls directly (transitively) counts
x=g['x']; md=g['md']; REG=g['REG']
from capstone.x86 import X86_OP_REG
SAVED=('ebx','esi','edi','ebp')
own={}; kept={}
for va,r in rows.items():
    if va<0x12000 or va>=0x411000: continue
    try: code=x.read(va,int(r['size']))
    except Exception: continue
    first_write={}; pushed={}; popped=set(); n=0
    for i in md.disasm(code,va):
        n+=1
        m=i.mnemonic
        if m=='push' and i.operands and i.operands[0].type==X86_OP_REG and REG.get(i.operands[0].reg) in SAVED:
            pushed.setdefault(REG[i.operands[0].reg],n); continue
        if m=='pop' and i.operands and i.operands[0].type==X86_OP_REG and REG.get(i.operands[0].reg) in SAVED:
            popped.add(REG[i.operands[0].reg]); continue
        if m=='leave': popped.add('ebp'); continue
        try: rd,wr=i.regs_access()
        except Exception: continue
        for w in wr:
            w=REG.get(w)
            if w in SAVED: first_write.setdefault(w,n)
    k={R for R in SAVED if R in pushed and R in popped and (R not in first_write or pushed[R]<first_write[R])}
    kept[va]=k; own[va]={R for R in first_write if R not in k}
changed={va:set(v) for va,v in own.items()}
moved=True
while moved:
    moved=False
    for va in changed:
        c=set(changed[va])
        for t in calls.get(va,[]):
            f=func_of(t)
            if f in changed and f!=va: c|=(changed[f]-kept[va])
        if c!=changed[va]: changed[va]=c; moved=True
clob={va:set(v) for va,v in changed.items()}
cands=[]; why=collections.Counter()
for k,v in fm.items():
    va=int(k,16); r=rows.get(va)
    if not r or r['owner'] not in('game','third:havok'): continue
    if r['status']=='matched': continue
    if v.get('static'): why['static symbol']+=1; continue
    try: kind,reason=classify(va,v['name'])
    except Exception as e: kind=None
    if kind!='jmp': why['convention not plain or unknown']+=1; continue
    bad_regs=set(clob[va])&{'ebx','esi','edi','ebp'}
    if bad_regs: why['changes a register its caller keeps']+=1; continue
    bad,seen=reach_bad(va)
    if bad: why['reaches hardware-facing library code']+=1; continue
    cands.append((va,v['name'],r['owner'],r['status'],bool(v.get('stub')),len(seen)))
print('unmatched game/havok functions with a symbol that could run from retail:',len(cands)); print(dict(why))
print(collections.Counter((c[2],c[3],'stub' if c[4] else 'draft') for c in cands))
json.dump([dict(va=c[0],name=c[1],owner=c[2],status=c[3],stub=c[4],reach=c[5]) for c in cands],open('/tmp/autoroute.json','w'))
ROUTE=[0xb7b40,0xb8540,0xb7430,0x6d4c0,0x1d3920,0x1f6090,0x1fbac0,0x1fc7e0,0x279410,0x2902b0,0x2ce9e0,0x2ce7b0,0x2e2f70,0xd4e60,0x329d20,0x32a850]
print('existing routes that change a register their caller keeps:',[(hex(r),sorted(set(clob.get(r,[]))&{'ebx','esi','edi','ebp'})) for r in ROUTE if set(clob.get(r,[]))&{'ebx','esi','edi','ebp'}])
print('0x1edf50:',classify(0x1edf50,fm['001edf50']['name']),sorted(clob[0x1edf50]),sorted(need[0x1edf50]),reach_bad(0x1edf50)[0])

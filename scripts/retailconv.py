"""For the functions the decompilation has only as empty stubs: does retail's own code take its arguments the way the
stub is declared (so that the stub can simply jump to retail's code in place), or in registers of the compiler's own
choosing (then it needs an adapter)?  -> native/retail_routes.json: stub symbol -> retail address, for the plain ones"""
import sys,json,re,subprocess,csv
sys.path.insert(0,'/home/claude/scripts')
from xbeimg import XbeImage
from capstone import *
from capstone.x86 import *
NAT='/home/claude/native'
x=XbeImage('/home/claude/xbe/default.xbe'); md=Cs(CS_ARCH_X86,CS_MODE_32); md.detail=True
rows={int(r['va'],16):r for r in csv.DictReader(open('/home/claude/h2decomp_src/halo2-decompiled-main/config/functions.csv'))}
fm=json.load(open(NAT+'/funcmap.json'))
stubs={int(k,16):v for k,v in fm.items() if v.get('stub')}
REG={X86_REG_EAX:'eax',X86_REG_ECX:'ecx',X86_REG_EDX:'edx',X86_REG_EBX:'ebx',X86_REG_ESI:'esi',X86_REG_EDI:'edi',X86_REG_EBP:'ebp',
     X86_REG_AL:'eax',X86_REG_CL:'ecx',X86_REG_DL:'edx',X86_REG_BL:'ebx',X86_REG_AX:'eax',X86_REG_CX:'ecx',X86_REG_DX:'edx',X86_REG_BX:'ebx',
     X86_REG_SI:'esi',X86_REG_DI:'edi',X86_REG_BP:'ebp',X86_REG_AH:'eax',X86_REG_CH:'ecx',X86_REG_DH:'edx',X86_REG_BH:'ebx'}
# ---- which registers a retail function needs on entry: liveness over its whole body, and through its direct calls
#      (a function that only hands an incoming register on to a callee of the compiler's own convention needs it too)
import collections
SAVED={'ebx','esi','edi','ebp'}
def decode(va,size):
    code=x.read(va,size); ins=[]
    for i in md.disasm(code,va):
        use=set(); deff=set(); target=None; kind='seq'; ret=None
        m=i.mnemonic
        if m.startswith('ret'): kind='ret'; ret=int(i.op_str,0) if i.op_str else 0
        elif m=='jmp':
            kind='jmp'
            if i.operands and i.operands[0].type==X86_OP_IMM: target=i.operands[0].imm
        elif m.startswith('j') or m.startswith('loop'):
            kind='jcc'; target=i.operands[0].imm if i.operands and i.operands[0].type==X86_OP_IMM else None
        elif m=='call':
            kind='call'; target=i.operands[0].imm if i.operands and i.operands[0].type==X86_OP_IMM else None
        if m=='push' and i.operands and i.operands[0].type==X86_OP_REG and REG.get(i.operands[0].reg) in SAVED: pass   # a save
        elif m in('xor','sub','sbb') and len(i.operands)==2 and i.operands[0].type==X86_OP_REG and i.operands[1].type==X86_OP_REG and i.operands[0].reg==i.operands[1].reg:
            deff.add(REG.get(i.operands[0].reg))
        # s9f: "or reg, -1" and "and reg, 0" set a register without depending on it (the compiler's way of writing -1 and 0);
        # read as a use they made 1e0850 and 1697c0 look as if they took esi from their callers
        elif m in('or','and') and len(i.operands)==2 and i.operands[0].type==X86_OP_REG and i.operands[1].type==X86_OP_IMM and i.operands[0].size==4 and \
             ((m=='or' and (i.operands[1].imm & 0xffffffff)==0xffffffff) or (m=='and' and (i.operands[1].imm & 0xffffffff)==0)):
            deff.add(REG.get(i.operands[0].reg))
        else:
            rd,wr=i.regs_access()
            use={REG[r] for r in rd if r in REG}; deff={REG[r] for r in wr if r in REG}
            if m=='pop' and i.operands and i.operands[0].type==X86_OP_REG: use.discard(REG.get(i.operands[0].reg))
        # writing only part of a register (al, ax) does not define the whole of it, but for arguments that is fine
        ins.append((i.address,i.size,kind,target,use,deff-{None},ret))
    return ins
functions={}
for va,r in rows.items():
    if va<0x12000 or va>=0x411000: continue
    try: functions[va]=decode(va,int(r['size']))
    except Exception: pass
# what each function leaves changed: every register it writes (or a callee of its writes) and did not save with a
# push among its first instructions. The compiler's own conventions also hand results back in registers other than eax.
saved={}; clob={}
for va,ins in functions.items():
    s=set()
    for a,size,kind,target,use,deff,ret in ins[:8]:
        pass
    code=x.read(va,min(24,int(rows[va]['size'])))
    for i in md.disasm(code,va):
        if i.mnemonic=='push' and i.operands and i.operands[0].type==X86_OP_REG and REG.get(i.operands[0].reg) in SAVED: s.add(REG[i.operands[0].reg])
        elif i.mnemonic in('sub','mov','lea','xor','test','cmp','movsx','movzx','and','fld','fstp'): continue
        else: break
    saved[va]=s
    c=set()
    for a,size,kind,target,use,deff,ret in ins: c|=deff
    clob[va]=(c|({'eax','ecx','edx'} if any(i[2]=='call' for i in ins) else set()))-s
moved=True
while moved:
    moved=False
    for va,ins in functions.items():
        c=set(clob[va])
        for a,size,kind,target,use,deff,ret in ins:
            if kind in('call','jmp') and target in clob and target!=va: c|=clob[target]
        c-=saved[va]
        if c!=clob[va]: clob[va]=c; moved=True
need={va:set() for va in functions}; rets_of={}
for va,ins in functions.items(): rets_of[va]={i[6] for i in ins if i[2]=='ret'}
changed=True; rounds=0
while changed and rounds<12:
    changed=False; rounds+=1
    for va,ins in functions.items():
        if not ins: continue
        index={i[0]:k for k,i in enumerate(ins)}
        end=ins[-1][0]+ins[-1][1]
        live=[set() for _ in ins]            # live before each instruction
        again=True
        while again:
            again=False
            for k in range(len(ins)-1,-1,-1):
                a,size,kind,target,use,deff,ret=ins[k]
                out=set()
                if kind in('seq','call','jcc') and k+1<len(ins): out|=live[k+1]
                if kind in('jmp','jcc') and target is not None:
                    if target in index: out|=live[index[target]]
                    elif kind=='jmp' and target in need: out|=need[target]         # a tail call
                if kind=='call':
                    u=set(need.get(target,set())) if target is not None else set()
                    new=(out-(clob.get(target,{'eax','ecx','edx'}) if target is not None else {'eax','ecx','edx'}))|u
                else: new=(out-deff)|use
                if new!=live[k]: live[k]=new; again=True
        if live[0]!=need[va]: need[va]=set(live[0]); changed=True
def live_in(va,size):
    return need.get(va,set()),rets_of.get(va,set())
def declared(name):
    u=subprocess.run(['llvm-undname',name],capture_output=True,text=True,stdin=subprocess.DEVNULL).stdout.strip().split('\n')[-1]
    m=re.search(r'(__stdcall|__fastcall|__cdecl|__thiscall)\s+[^(]*\((.*)\)\s*(const)?$',u)
    if not m: return None,None,u
    cc=m.group(1); args=m.group(2).strip()
    al=[] if args in('void','') else [a.strip() for a in re.split(r',(?![^<(]*[>)])',args)]
    size=sum(8 if a in('double','__int64','unsigned __int64') else 4 for a in al)
    return cc,size,u
def classify(va,name):
    r=rows.get(va)
    if not r: return None,'no row'
    cc,size,und=declared(name)
    if cc is None: return None,'no prototype'
    live,rets=live_in(va,int(r['size']))
    if not rets: return None,'no return found'
    if cc=='__stdcall' and rets=={size} and not live: return 'jmp',''
    if cc=='__cdecl' and rets=={0} and not live: return 'jmp',''
    if cc=='__fastcall':
        regs=min(2,size//4); stack=max(0,size-8)
        if rets=={stack} and live<=set(['ecx','edx'][:regs]): return 'jmp',''
        # declared with the file's default convention, but retail takes everything on the stack: an adapter can
        # push the register arguments
        if rets=={size} and not live and size in (4,8) : return 'fast%d'%(size//4),''
        if size==0 and rets=={0} and not live: return 'jmp',''
    if cc=='__thiscall' and rets=={size} and live<={'ecx'}: return 'jmp',''
    return None,'%s args %s, retail ret %s live-in %s'%(cc,size,sorted(rets),sorted(live))
routes={}; why={}
import collections
count=collections.Counter()
for k,v in fm.items():
    va=int(k,16); r=rows.get(va)
    if not r or v.get('static'): continue
    status=r['status']
    if not (v.get('stub') or status in('todo','near')): continue
    if r['owner'] not in('game','third:havok','other:library','xdk:d3dx'): continue
    kind,reason=classify(va,v['name'])
    count[(('stub' if v.get('stub') else status),kind or 'adapter needed')]+=1
    if kind: routes[v['name']]=dict(retail=va,kind=kind,stub=bool(v.get('stub')),status=status,source=r['source'])
    else: why[v['name']]=reason
json.dump(routes,open(NAT+'/retail_routes.json','w'),indent=0,sort_keys=True)
json.dump(why,open(NAT+'/retail_routes_not.json','w'),indent=0,sort_keys=True)
for k,n in sorted(count.items()): print(k,n)

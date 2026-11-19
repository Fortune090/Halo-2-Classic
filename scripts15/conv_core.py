"""conv_core.py: retailconv.py's liveness analysis (which registers a function needs on entry, through its direct calls,
and what its returns pop), over any XBE: analyse(read, {va: size}) -> need, rets, saved, clob."""
import collections
from capstone import *
from capstone.x86 import *
md=Cs(CS_ARCH_X86,CS_MODE_32); md.detail=True
REG={X86_REG_EAX:'eax',X86_REG_ECX:'ecx',X86_REG_EDX:'edx',X86_REG_EBX:'ebx',X86_REG_ESI:'esi',X86_REG_EDI:'edi',X86_REG_EBP:'ebp',
     X86_REG_AL:'eax',X86_REG_CL:'ecx',X86_REG_DL:'edx',X86_REG_BL:'ebx',X86_REG_AX:'eax',X86_REG_CX:'ecx',X86_REG_DX:'edx',X86_REG_BX:'ebx',
     X86_REG_SI:'esi',X86_REG_DI:'edi',X86_REG_BP:'ebp',X86_REG_AH:'eax',X86_REG_CH:'ecx',X86_REG_DH:'edx',X86_REG_BH:'ebx'}
SAVED={'ebx','esi','edi','ebp'}
def decode(va,size):
    code=_read(va,size); ins=[]
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
def analyse(rd_, sizes):
  global _read
  _read=rd_
  functions={}
  for va,n in sizes.items():
    try: functions[va]=decode(va,n)
    except Exception: pass
  return _analyse(functions, sizes)
def _analyse(functions, sizes):
    # what each function leaves changed: every register it writes (or a callee of its writes) and did not save with a
    # push among its first instructions. The compiler's own conventions also hand results back in registers other than eax.
    saved={}; clob={}
    for va,ins in functions.items():
        s=set()
        for a,size,kind,target,use,deff,ret in ins[:8]:
            pass
        code=_read(va,min(24,sizes[va]))
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

    return need, rets_of, saved, clob

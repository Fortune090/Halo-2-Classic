"""Run retail's start-up initialisers (the C++ static initialisers the linker collected into a table at the start of
.data) in an emulator over the retail image, and record what they leave in memory."""
import sys,csv,struct,json,collections
sys.path.insert(0,'/home/claude/scripts')
from xbeimg import XbeImage
from unicorn import *
from unicorn.x86_const import *
x=XbeImage(sys.argv[1])
rows={int(r['va'],16):r for r in csv.DictReader(open(sys.argv[2]))}
starts=sorted(rows)
import bisect
def func_of(va):
    i=bisect.bisect_right(starts,va)-1
    if i<0: return None
    r=rows[starts[i]]
    return r if va<starts[i]+int(r['size']) else None
TABLE=(0x461194,0x46180c)
inits=[x.u32(a) for a in range(TABLE[0],TABLE[1]+4,4)]
game_inits=[v for v in inits if 0x3762e0<=v<0x379c60]
byname={r['name']:va for va,r in rows.items() if r['name']}
ATEXIT=byname['_atexit']
uc=Uc(UC_ARCH_X86,UC_MODE_32)
uc.mem_map(0,0x10000)                      # fs:[0] lands here (flat segments)
IMG_LO,IMG_HI=0x10000,(0x10000+x.image_size+0xfff)&~0xfff
uc.mem_map(IMG_LO,IMG_HI-IMG_LO)
initial=bytearray(IMG_HI-IMG_LO)
for s in x.sections:
    b=x.x[s['raw']:s['raw']+s['rsize']]
    initial[s['va']-IMG_LO:s['va']-IMG_LO+len(b)]=b
uc.mem_write(IMG_LO,bytes(initial))
STACK=0x70000000; uc.mem_map(STACK,0x100000)
RET=0x7ff00000; uc.mem_map(RET,0x1000); uc.mem_write(RET,b'\xf4')
HEAP=0x60000000; uc.mem_map(HEAP,0x400000); heap_top=[HEAP]
# enable SSE (Havok uses it)
cr0=uc.reg_read(UC_X86_REG_CR0); uc.reg_write(UC_X86_REG_CR0,(cr0 & ~0x4)|0x2)
cr4=uc.reg_read(UC_X86_REG_CR4); uc.reg_write(UC_X86_REG_CR4,cr4|0x600)
writes={}          # addr -> index of the initialiser that last wrote it
outside=collections.Counter(); problems=[]; cur=[0,0]
def on_write(uc,access,addr,size,value,ud):
    if IMG_LO<=addr<IMG_HI:
        for i in range(size): writes[addr+i]=cur[0]
uc.hook_add(UC_HOOK_MEM_WRITE,on_write)
def on_block(uc,addr,size,ud):
    r=func_of(addr)
    if r is None:
        if not (RET<=addr<RET+0x1000): outside[('?',hex(addr))]+=1
        return
    if addr==int(r['va'],16) and r['owner'] not in('game','third:havok','other:library'):
        outside[(r['owner'],r['name'] or r['va'])]+=1
uc.hook_add(UC_HOOK_BLOCK,on_block)
def on_atexit(uc,addr,size,ud):
    esp=uc.reg_read(UC_X86_REG_ESP)
    ret=struct.unpack('<I',uc.mem_read(esp,4))[0]
    uc.reg_write(UC_X86_REG_EAX,0); uc.reg_write(UC_X86_REG_ESP,esp+4); uc.reg_write(UC_X86_REG_EIP,ret)
uc.hook_add(UC_HOOK_CODE,on_atexit,begin=ATEXIT,end=ATEXIT)
def on_bad(uc,access,addr,size,value,ud):
    problems.append((cur[0],hex(game_inits[cur[0]]),'unmapped access',access,hex(addr),hex(uc.reg_read(UC_X86_REG_EIP))))
    return False
uc.hook_add(UC_HOOK_MEM_UNMAPPED,on_bad)
done=0
for i,f in enumerate(game_inits):
    cur[0]=i
    esp=STACK+0xf0000
    uc.mem_write(esp,struct.pack('<I',RET))
    for reg in (UC_X86_REG_EAX,UC_X86_REG_EBX,UC_X86_REG_ECX,UC_X86_REG_EDX,UC_X86_REG_ESI,UC_X86_REG_EDI,UC_X86_REG_EBP): uc.reg_write(reg,0)
    uc.reg_write(UC_X86_REG_ESP,esp)
    uc.reg_write(UC_X86_REG_FPCW,0x27f); uc.reg_write(UC_X86_REG_MXCSR,0x1f80)
    try:
        uc.emu_start(f,RET,timeout=20*1000000,count=20000000)
        if uc.reg_read(UC_X86_REG_EIP)!=RET: problems.append((i,hex(f),'did not return',hex(uc.reg_read(UC_X86_REG_EIP))))
        else: done+=1
    except UcError as e:
        problems.append((i,hex(f),'stopped',str(e),hex(uc.reg_read(UC_X86_REG_EIP))))
final=bytes(uc.mem_read(IMG_LO,IMG_HI-IMG_LO))
changed=[a for a in sorted(writes) if final[a-IMG_LO]!=initial[a-IMG_LO]]
print('initialisers in the table:',len(inits),'of which game/Havok:',len(game_inits),'ran to their return:',done)
print('bytes written:',len(writes),'bytes left different from the file:',len(changed))
by_sec=collections.Counter()
for a in changed:
    s=x.section_at(a); by_sec[(s['name'], 'file part' if a-s['va']<s['rsize'] else 'zero-filled part') if s else '?']+=1
print(dict(by_sec))
print('library functions entered:',outside.most_common(30))
print('problems:',len(problems))
for p in problems[:30]: print('   ',p)
# save delta as runs
runs=[]; 
for a in changed:
    if runs and runs[-1][0]+len(runs[-1][1])==a: runs[-1][1].append(final[a-IMG_LO])
    else: runs.append([a,[final[a-IMG_LO]]])
json.dump(dict(runs=[[a,bytes(b).hex()] for a,b in runs],writer={str(a):writes[a] for a in changed},inits=game_inits),open('/home/claude/native/init_delta.json','w'))
print('delta runs:',len(runs))

"""Generate the assembly that ties our build to the retail data in place: one absolute symbol per redirected data
symbol, the list of XBE sections to load, what retail's start-up initialisers leave in memory, and the code pointers
to re-aim at our functions."""
import sys,os,json,struct,zlib,collections
sys.path.insert(0,'/home/claude/scripts')
from xbeimg import XbeImage
NAT='/home/claude/native'; OUT=NAT+'/layer'
x=XbeImage('/home/claude/xbe/default.xbe')
plan=json.load(open(NAT+'/plan.json'))['plan']
fix=json.load(open(NAT+'/fixups2.json'))['fix']
delta=json.load(open(NAT+'/init_delta.json'))
q=lambda n:'"'+n+'"'
with open(OUT+'/retail_syms.S','w') as f:
    f.write('/* generated: every data symbol of ours that stands for retail data, at the retail address */\n')
    for name,p in sorted(plan.items(),key=lambda t:t[1]['retail']):
        f.write(f'.globl {q(name)}\n{q(name)} = {p["retail"]:#x}\n')
LOAD=['.rdata','.data','.data1','BINKDATA','$$XTIMAGE','$$XSIMAGE','DSPImage']   # BINKDATA: s9s (the movies)
with open(OUT+'/retail_tables.S','w') as f:
    f.write('/* generated from the retail XBE (v1.0): what to load, what the start-up initialisers write, which code pointers to re-aim */\n')
    f.write('.section .rdata,"dr"\n.balign 4\n.globl _h2n_sections\n_h2n_sections:\n')
    names=[]
    for s in x.sections:
        if s['name'] in LOAD:
            crc=zlib.crc32(x.x[s['raw']:s['raw']+s['rsize']])&0xffffffff
            f.write(f'  .long LS{len(names)}, {s["va"]:#x}, {s["vsize"]:#x}, {s["rsize"]:#x}, {crc:#x}\n'); names.append(s['name'])
    f.write('  .long 0,0,0,0,0\n')
    for i,n in enumerate(names): f.write(f'LS{i}: .asciz "{n}"\n')
    f.write('.balign 4\n.globl _h2n_delta\n_h2n_delta:\n')
    total=0
    for a,hexb in delta['runs']:
        b=bytes.fromhex(hexb); total+=len(b)
        f.write(f'  .long {a:#x}, {len(b)}\n  .byte '+','.join(str(v) for v in b)+'\n  .balign 4\n')
    f.write('  .long 0,0\n')
    f.write('.balign 4\n.globl _h2n_fixups\n_h2n_fixups:\n')
    for d in fix:
        f.write(f'  .long {d["at"]:#x}, {d["retail"]:#x}, {q(d["symbol"])}\n')
    f.write('  .long 0,0,0\n')
print('symbols',len(plan),'sections',names,'delta bytes',total,'fixups',len(fix))

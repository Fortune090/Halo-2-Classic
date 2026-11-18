#!/usr/bin/env python3
"""dis.py <hex va> [bytes]: disassemble retail code from the XBE, naming call targets and data from the function list and the map of our build"""
import sys, struct, csv, re
from capstone import Cs, CS_ARCH_X86, CS_MODE_32
XBE='/home/claude/xbe/default.xbe'; CSV='/home/claude/h2decomp_src/halo2-decompiled-main/config/functions.csv'
d=open(XBE,'rb').read()
base,=struct.unpack_from('<I',d,0x104); nsec,=struct.unpack_from('<I',d,0x11c); sh,=struct.unpack_from('<I',d,0x120)
secs=[]
for i in range(nsec):
    fl,va,vs,ra,rs=struct.unpack_from('<5I',d,sh-base+i*56); secs.append((va,vs,ra,rs))
def off(va):
    for a,vs,ra,rs in secs:
        if a<=va<a+rs: return ra+va-a
    return None
funcs={}
for r in csv.DictReader(open(CSV)):
    funcs[int(r['va'],16)]=r
def fname(va):
    r=funcs.get(va)
    if not r: return None
    return '%s[%s%s]'%(r['name'] or 'function_%x'%va, r['status'], '' if r['source'] else ',no source')
va=int(sys.argv[1],16); n=int(sys.argv[2]) if len(sys.argv)>2 else int(funcs[va]['size']) if va in funcs else 64
md=Cs(CS_ARCH_X86,CS_MODE_32)
for ins in md.disasm(d[off(va):off(va)+n],va):
    s='%08x  %-7s %s'%(ins.address,ins.mnemonic,ins.op_str)
    for m in re.findall(r'0x([0-9a-f]{5,8})',ins.op_str):
        t=int(m,16); f=fname(t)
        if f: s+='   ; '+f
    print(s)

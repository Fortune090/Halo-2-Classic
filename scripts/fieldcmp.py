"""For a function: print the instructions that refer to a data symbol in the native object and the retail instructions
that refer to the given retail addresses, to compare what each field is used for.
usage: fieldcmp.py <retail va of the function> <symbol substring> <lo hex> <hi hex>"""
import sys,json,struct,os
sys.path.insert(0,'/home/claude/scripts')
from coff import Coff
import capstone
md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32)
NAT='/home/claude/native'
fva=int(sys.argv[1],16); sub=sys.argv[2]; lo=int(sys.argv[3],16); hi=int(sys.argv[4],16)
plan=json.load(open(NAT+'/plan.json'))['plan']; fm=json.load(open(NAT+'/funcmap.json'))
rows={}
for l in open('/home/claude/h2decomp_src/halo2-decompiled-main/config/functions.csv'):
    p=l.split(',')
    if p[0]!='va': rows[int(p[0],16)]=int(p[1])
f=fm['%08x'%fva]; key=f['object'].split(':')[-1]
objs={os.path.basename(l.strip()).replace('_keep.obj','.obj'):l.strip() for l in open(NAT+'/objects2.rsp') if l.strip()}
c=Coff(objs[key])
fy=[y for y in c.symbols if y.name==f['name'] and y.section>0][0]; sec=c.sections[fy.section-1]
later=[z.value for z in sec.symbols if z.value>fy.value and (z.type==0x20 or z.storage==2)]
end=min(later) if later else sec.rawsize
code=bytearray(c.d[sec.rawptr+fy.value:sec.rawptr+end])
rel={}
for va,symi,typ in sec.relocs:
    if fy.value<=va<end: rel[va-fy.value]=(c.by_index[symi].name,typ)
print('native (%s, %d bytes):'%(key,end-fy.value))
for i in md.disasm(bytes(code),0):
    for o in range(i.address,i.address+i.size):
        if o in rel and sub in rel[o][0]:
            add=struct.unpack_from('<I',code,o)[0]
            print('   +%04x  %-8s %-40s  -> %s+%d'%(i.address,i.mnemonic,i.op_str[:40],rel[o][0][:30],add))
d=open('/home/claude/xbe/default.xbe','rb').read()
base=struct.unpack_from('<I',d,0x104)[0]; nsec=struct.unpack_from('<I',d,0x11c)[0]; secs=struct.unpack_from('<I',d,0x120)[0]-base
def rd(a,n):
    for i in range(nsec):
        fl,va,vs,ra,rs=struct.unpack_from('<5I',d,secs+i*56)
        if va<=a<va+vs: return d[ra+a-va:ra+a-va+n]
rc=rd(fva,rows[fva])
print('retail (%d bytes):'%rows[fva])
for i in md.disasm(rc,fva):
    b=rc[i.address-fva:i.address-fva+i.size]
    for o in range(0,len(b)-3):
        v=struct.unpack_from('<I',b,o)[0]
        if lo<=v<hi: print('   +%04x  %-8s %-40s  -> %x'%(i.address-fva,i.mnemonic,i.op_str[:40],v)); break

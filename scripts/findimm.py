#!/usr/bin/env python3
"""findimm.py <hex value>...: where retail's code holds these 32-bit values (as immediates), with the function each is in"""
import sys, struct, csv, bisect
XBE='/home/claude/xbe/default.xbe'; CSV='/home/claude/h2decomp_src/halo2-decompiled-main/config/functions.csv'
d=open(XBE,'rb').read()
base,=struct.unpack_from('<I',d,0x104); nsec,=struct.unpack_from('<I',d,0x11c); sh,=struct.unpack_from('<I',d,0x120)
secs=[struct.unpack_from('<5I',d,sh-base+i*56) for i in range(nsec)]
funcs=sorted((int(r['va'],16),int(r['size']),r['name'] or 'function_%x'%int(r['va'],16),r['status']) for r in csv.DictReader(open(CSV)))
starts=[f[0] for f in funcs]
def where(va):
    i=bisect.bisect_right(starts,va)-1
    if i>=0 and va<funcs[i][0]+funcs[i][1]: return '%s+%x [%s] (%x, %d bytes)'%(funcs[i][2],va-funcs[i][0],funcs[i][3],funcs[i][0],funcs[i][1])
    return '?'
for v in sys.argv[1:]:
    want=struct.pack('<I',int(v,16)); hits=[]
    for fl,va,vs,ra,rs in secs:
        if va>=0x411000: continue
        blob=d[ra:ra+rs]; o=blob.find(want)
        while o>=0:
            hits.append(va+o); o=blob.find(want,o+1)
    print(v, len(hits),'places')
    for h in hits[:14]: print('   %08x  %s'%(h,where(h)))

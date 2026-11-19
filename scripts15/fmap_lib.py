"""fmap_lib.py (from fmap.py): map every v1.0 function (functions.csv) to its twin in the v1.5 XBE (u5) by masked bytes:
absolute values in 0x10000-0x700000 and rel32 call/jump operands are masked; library sections move by a fixed delta."""
import struct, csv, json, sys, collections
def load(p):
    d=open(p,'rb').read(); base=struct.unpack_from('<I',d,0x104)[0]; ns,sa=struct.unpack_from('<II',d,0x11C)
    secs=[]
    for i in range(ns):
        o=sa-base+56*i; fl,va,vs,raw,rs,na=struct.unpack_from('<6I',d,o)
        secs.append(dict(name=d[na-base:d.index(b'\0',na-base)].decode(),va=va,vs=vs,raw=raw,rs=rs,fl=fl))
    return d,secs

def text(D,S):
    s=[x for x in S if x['name']=='.text'][0]; return s['va'], D[s['raw']:s['raw']+s['rs']]
LO,HI=0x10000,0x700000
def mask(b):
    m=bytearray(b'\1'*len(b))
    for i in range(len(b)-3):
        v=struct.unpack_from('<I',b,i)[0]
        if LO<=v<HI: m[i:i+4]=b'\0\0\0\0'
    for i in range(len(b)-4):
        if b[i] in (0xE8,0xE9): m[i+1:i+5]=b'\0\0\0\0'
        if b[i]==0x0F and 0x80<=b[i+1]<=0x8F and i+6<=len(b): m[i+2:i+6]=b'\0\0\0\0'
    return bytes(m)

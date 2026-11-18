import sys,struct,hashlib
d=open(sys.argv[1],'rb').read()
print('size',len(d),'sha256',hashlib.sha256(d).hexdigest())
assert d[:4]==b'XBEH'
base,hsz,isz,ihsz,ts,cert,nsec,sech=struct.unpack_from('<8I',d,0x104)
print('base %#x image size %#x timestamp %#x sections %d'%(base,isz,ts,nsec))
tot=collections=None
rows=[]
for i in range(nsec):
    o=sech-base+i*56
    flags,va,vsz,raw,rsz,nameaddr=struct.unpack_from('<6I',d,o)
    name=d[nameaddr-base:d.index(b'\0',nameaddr-base)].decode('latin1')
    rows.append((name,va,vsz,rsz,flags))
for name,va,vsz,rsz,flags in rows:
    print(f'{name:10s} va {va:#08x} mem {vsz:8d} file {rsz:8d} zero-filled {max(0,vsz-rsz):8d} {"W" if flags&1 else "-"}{"X" if flags&4 else "-"}')

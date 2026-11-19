# callers.py <xbe> <target va hex>...: every E8 call to the targets in the XBE's code sections
import sys, struct
xbe = open(sys.argv[1], 'rb').read()
base = struct.unpack_from('<I', xbe, 0x104)[0]
nsec = struct.unpack_from('<I', xbe, 0x11c)[0]; sh = struct.unpack_from('<I', xbe, 0x120)[0] - base
targets = {int(a, 16) for a in sys.argv[2:]}
for i in range(nsec):
    fl, va, vs, ra, rs = struct.unpack_from('<IIIII', xbe, sh + i * 56)
    if not fl & 4: continue
    d = xbe[ra:ra + rs]
    j = d.find(b'\xe8')
    while j >= 0 and j + 5 <= len(d):
        t = (va + j + 5 + struct.unpack_from('<i', d, j + 1)[0]) & 0xffffffff
        if t in targets: print('%06x -> %06x' % (va + j, t))
        j = d.find(b'\xe8', j + 1)

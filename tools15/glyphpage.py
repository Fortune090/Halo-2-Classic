"""glyphpage.py <stop.bin> <out.png> [address hex] [width] [height]: the text's glyph page (a 16-bit A4R4G4B4 texture the
game writes its characters into, 256x256 at 0x84714000) from a stop file, as a picture (alpha as brightness, colour kept)"""
import sys
import struct
class Stop:                                   # (as scripts/readdump.py's)
    def __init__(self, path):
        d = open(path, 'rb').read(); assert d[:8] == b'H2NSTOP1'
        self.memory = []; at = 8
        while at + 4 <= len(d):
            kind, = struct.unpack_from('<I', d, at); at += 4
            if kind == 0: break
            if kind == 1:
                address, size = struct.unpack_from('<2I', d, at); at += 8
                self.memory.append((address, d[at:at + size])); at += size
            elif kind == 2:
                tid, flags, size = struct.unpack_from('<3I', d, at); at += 12 + size
            elif kind == 3:
                size, = struct.unpack_from('<I', d, at); at += 4 + size
            else: raise SystemExit('unknown record')
    def read(self, address, count):
        for start, data in self.memory:
            if start <= address and address + count <= start + len(data): return data[address - start: address - start + count]
        return None
from PIL import Image
s = Stop(sys.argv[1]); a = int(sys.argv[3], 16) if len(sys.argv) > 3 else 0x84714000
w = int(sys.argv[4]) if len(sys.argv) > 4 else 256; h = int(sys.argv[5]) if len(sys.argv) > 5 else 256
d = s.read(a, w * h * 2)
if d is None: raise SystemExit('the stop file does not hold %x' % a)
im = Image.new('RGB', (w, h)); px = im.load(); odd = 0
for y in range(h):
    for x in range(w):
        v = d[(y * w + x) * 2] | d[(y * w + x) * 2 + 1] << 8
        al, r, g, b = v >> 12, (v >> 8) & 15, (v >> 4) & 15, v & 15
        if (r, g, b) != (15, 15, 15) and v: odd += 1
        px[x, y] = (r * 17 * al // 15, g * 17 * al // 15, b * 17 * al // 15) if (r, g, b) == (15, 15, 15) else (r * 17, g * 17 // 2 + 60, b * 17)
im.resize((w * 3, h * 3), Image.NEAREST).save(sys.argv[2])
print('texels that are not white-with-alpha:', odd, 'of', w * h)

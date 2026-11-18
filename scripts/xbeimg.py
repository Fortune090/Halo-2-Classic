"""The retail XBE as an image: sections by address."""
import struct
class XbeImage:
    def __init__(self, path):
        x = self.x = open(path, 'rb').read()
        self.base, hsz, self.image_size, ihsz, ts, cert, nsec, sech = struct.unpack_from('<8I', x, 0x104)
        self.sections = []
        for i in range(nsec):
            o = sech - self.base + i * 56
            flags, va, vsz, raw, rsz, nameaddr = struct.unpack_from('<6I', x, o)
            name = x[nameaddr - self.base:x.index(b'\0', nameaddr - self.base)].decode('latin1')
            self.sections.append(dict(name=name, va=va, vsize=vsz, raw=raw, rsize=rsz, flags=flags))
        self.kernel_thunk = struct.unpack_from('<I', x, 0x158)[0] ^ 0x5B6D40B6
        self.entry = struct.unpack_from('<I', x, 0x128)[0] ^ 0xA8FC57AB
    def section_at(self, va):
        for s in self.sections:
            if s['va'] <= va < s['va'] + s['vsize']: return s
    def read(self, va, n):
        s = self.section_at(va)
        if s is None: return None
        off = va - s['va']; backed = max(0, min(n, s['rsize'] - off))
        return self.x[s['raw'] + off:s['raw'] + off + backed] + bytes(n - backed)
    def u32(self, va):
        b = self.read(va, 4)
        return struct.unpack('<I', b)[0] if b else None

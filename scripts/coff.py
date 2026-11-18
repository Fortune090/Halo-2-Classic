"""A small reader for the COFF objects the Xbox SDK's compiler writes."""
import struct

class Section:
    __slots__=('index','name','vsize','rawsize','rawptr','relptr','nrel','flags','data','relocs','symbols')
class Symbol:
    __slots__=('index','name','value','section','type','storage','naux','aux')

class Coff:
    def __init__(self, path):
        d = self.d = open(path, 'rb').read()
        self.path = path
        machine, nsec, ts, symptr, nsym, optsz, chars = struct.unpack_from('<HHIIIHH', d, 0)
        assert machine == 0x14c, (path, hex(machine))
        strtab = symptr + nsym * 18
        def name_of(raw):
            if raw[:4] == b'\0\0\0\0':
                o = struct.unpack_from('<I', raw, 4)[0]
                e = d.index(b'\0', strtab + o)
                return d[strtab + o:e].decode('latin1')
            return raw.rstrip(b'\0').decode('latin1')
        self.sections = []
        for i in range(nsec):
            o = 20 + optsz + i * 40
            raw = d[o:o + 8]
            vs, va, rs, rp, relp, lnp, nrel, nln, fl = struct.unpack_from('<IIIIIIHHI', d, o + 8)
            s = Section(); s.index = i + 1
            if raw[:1] == b'/':
                off = int(raw[1:].rstrip(b'\0')); e = d.index(b'\0', strtab + off); s.name = d[strtab + off:e].decode('latin1')
            else:
                s.name = raw.rstrip(b'\0').decode('latin1')
            s.vsize, s.rawsize, s.rawptr, s.relptr, s.nrel, s.flags = vs, rs, rp, relp, nrel, fl
            s.data = d[rp:rp + rs] if rp and not (fl & 0x80) else None
            s.relocs = []; s.symbols = []
            self.sections.append(s)
        self.symbols = []; self.by_index = {}
        i = 0
        while i < nsym:
            o = symptr + i * 18
            raw = d[o:o + 8]
            value, sec, typ, storage, naux = struct.unpack_from('<IhHBB', d, o + 8)
            y = Symbol(); y.index = i; y.name = name_of(raw); y.value = value; y.section = sec; y.type = typ; y.storage = storage; y.naux = naux
            y.aux = d[o + 18:o + 18 + 18 * naux]
            self.symbols.append(y); self.by_index[i] = y
            if sec > 0: self.sections[sec - 1].symbols.append(y)
            i += 1 + naux
        for s in self.sections:
            if s.nrel and s.relptr:
                n = s.nrel; p = s.relptr
                if (s.flags & 0x01000000) and n == 0xffff:
                    n = struct.unpack_from('<I', d, p)[0] - 1; p += 10
                for k in range(n):
                    va, symi, typ = struct.unpack_from('<IIH', d, p + k * 10)
                    s.relocs.append((va, symi, typ))
    def comdat_selection(self, s):
        """(selection, associated section) of a COMDAT section, from its section symbol's aux record"""
        for y in s.symbols:
            if y.storage == 3 and y.value == 0 and y.naux and y.name == s.name:
                length, nrel, nln, checksum, number, selection = struct.unpack_from('<IHHIHB', y.aux, 0)
                return selection, number
        return None, None

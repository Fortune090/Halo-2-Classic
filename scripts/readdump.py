"""Read h2native_stop.bin, the snapshot halo2_native.exe writes when it stops on a fault or hangs.

usage: readdump.py <h2native_stop.bin> <halo2_native.exe> <halo2_native.map> [options]
  (the exe and map must be the ones of the build that wrote the file: the kit of the same stage has the map)
  no option          every thread: registers, where it is, and its call chain (return addresses on its stack that
                     really follow a call instruction), named from the map
  --words ADDR [N]   N 32-bit words of memory at ADDR (hex), each named if it points into our code
  --bytes ADDR [N]   N bytes at ADDR as hex and text
  --stack ID [N]     the first N words of thread ID's stack, raw
  --find VALUE       every place in the snapshot that holds the 32-bit VALUE
"""
import sys, struct, re, bisect

def load_map(path):
    syms = []
    for line in open(path, errors='replace'):
        m = re.match(r'\s*[0-9a-f]{4}:[0-9a-f]{8}\s+(\S+)\s+([0-9a-f]{8,16})\s+(\S.*)$', line)
        if m and not m.group(1).startswith('$L'): syms.append((int(m.group(2), 16), m.group(1), m.group(3).strip()))
    syms.sort()
    return syms

class Exe:
    def __init__(self, path):
        d = self.d = open(path, 'rb').read()
        pe = struct.unpack_from('<I', d, 0x3c)[0]
        nsec, = struct.unpack_from('<H', d, pe + 6); opt, = struct.unpack_from('<H', d, pe + 20)
        self.base, = struct.unpack_from('<I', d, pe + 24 + 28)
        self.sections = []
        at = pe + 24 + opt
        for i in range(nsec):
            name = d[at:at + 8].rstrip(b'\0').decode('latin1')
            vsize, va, rsize, rptr = struct.unpack_from('<4I', d, at + 8)
            self.sections.append((name, self.base + va, vsize, rptr, rsize)); at += 40
        self.text = next(s for s in self.sections if s[0] == '.text')
    def code(self, address, count):
        for name, va, vsize, rptr, rsize in self.sections:
            if va <= address and address + count <= va + min(vsize, rsize):
                return self.d[rptr + address - va: rptr + address - va + count]
        return None

class Stop:
    def __init__(self, path):
        d = open(path, 'rb').read()
        assert d[:8] == b'H2NSTOP1', 'not an h2native stop file'
        self.memory, self.threads, self.notes = [], [], []
        at = 8
        while at + 4 <= len(d):
            kind, = struct.unpack_from('<I', d, at); at += 4
            if kind == 0: break
            if kind == 1:
                address, size = struct.unpack_from('<2I', d, at); at += 8
                self.memory.append((address, d[at:at + size])); at += size
            elif kind == 2:
                tid, flags, size = struct.unpack_from('<3I', d, at); at += 12
                c = d[at:at + size]; at += size
                names = ['edi', 'esi', 'ebx', 'edx', 'ecx', 'eax', 'ebp', 'eip', 'cs', 'eflags', 'esp']
                regs = dict(zip(names, struct.unpack_from('<11I', c, 0x9c)))
                self.threads.append((tid, flags, regs))
            elif kind == 3:
                size, = struct.unpack_from('<I', d, at); at += 4
                self.notes.append(d[at:at + size].decode('latin1')); at += size
            else: raise SystemExit('unknown record %d at %d' % (kind, at - 4))
        self.memory.sort()
    def read(self, address, count):
        for start, data in self.memory:
            if start <= address and address + count <= start + len(data):
                return data[address - start: address - start + count]
        return None
    def region(self, address):
        for start, data in self.memory:
            if start <= address < start + len(data): return start, data
        return None

class Retail:
    """retail's own code (the XBE and the decompilation's function list), for runs that execute it in place"""
    def __init__(self):
        import csv
        sys.path.insert(0, '/home/claude/scripts')
        from xbeimg import XbeImage
        self.x = XbeImage('/home/claude/xbe/default.xbe')
        self.rows = sorted((int(r['va'], 16), int(r['size']), r) for r in csv.DictReader(open('/home/claude/h2decomp_src/halo2-decompiled-main/config/functions.csv')))
        self.keys = [r[0] for r in self.rows]
    def name(self, a):
        i = bisect.bisect_right(self.keys, a) - 1
        va, size, r = self.rows[i]
        return 'RETAIL %08x +0x%x  [%s %s %s]' % (va, a - va, r['owner'], r['status'], r['source'].replace('src/', '') or r['name'])
    def is_return(self, a):
        if not (0x12007 <= a < 0x411000): return False
        b = self.x.read(a - 7, 7)
        if not b or len(b) < 7: return False
        return b[2] == 0xe8 or any(b[7 - n] == 0xff and (b[8 - n] & 0x38) == 0x10 for n in range(2, 8))

def main():
    if len(sys.argv) < 4: raise SystemExit(__doc__)
    stop, exe, syms = Stop(sys.argv[1]), Exe(sys.argv[2]), load_map(sys.argv[3])
    try: retail = Retail()
    except Exception: retail = None
    keys = [s[0] for s in syms]
    code_first, code_end = 0x586000, exe.text[1] + exe.text[2]
    def name(a):
        if retail and 0x12000 <= a < 0x411000: return retail.name(a)
        i = bisect.bisect_right(keys, a) - 1
        if i < 0: return '?'
        return '%s +0x%x  [%s]' % (syms[i][1], a - syms[i][0], syms[i][2])
    def is_return(a):
        if retail and a < 0x411000: return retail.is_return(a)
        if not (code_first <= a < code_end): return False
        b = exe.code(a - 7, 7)
        if not b: return False
        if b[2] == 0xe8: return True
        return any(b[7 - n] == 0xff and (b[7 - n + 1] & 0x38) == 0x10 for n in range(2, 8) if 7 - n + 1 < 7)
    def region_name(a):
        if 0x411000 <= a < 0x586000: return 'retail data'
        if 0x11000 <= a < 0x411000: return retail.name(a) if retail else 'RETAIL CODE address'
        if 0x80000000 <= a < 0x88000000: return 'game memory'
        if 0xf0000000 <= a < 0xf8000000: return 'game memory (high view)'
        if (a & 0xffff0000) in (0x0bad0000, 0x0bae0000): return 'STACK POISON'
        if code_first <= a < code_end: return name(a)
        return ''
    args = sys.argv[4:]
    if not args:
        for n in stop.notes: print('note:', n)
        print('memory in the file: %d ranges, %.1f MB' % (len(stop.memory), sum(len(d) for _, d in stop.memory) / 1048576.0))
        for tid, flags, r in stop.threads:
            print('\nthread %d%s' % (tid, '   <- the thread that stopped' if flags & 1 else ''))
            print('  eip %08x  %s' % (r['eip'], region_name(r['eip']) or '(outside the exe: a system library, e.g. waiting or sleeping)'))
            print('  eax %08x ebx %08x ecx %08x edx %08x esi %08x edi %08x ebp %08x esp %08x' % tuple(r[k] for k in ('eax', 'ebx', 'ecx', 'edx', 'esi', 'edi', 'ebp', 'esp')))
            got = stop.region(r['esp'] & ~3)
            if not got: print('  (no stack in the file)'); continue
            start, data = got
            first = (r['esp'] & ~3) - start
            shown = 0
            for off in range(first, len(data) - 3, 4):
                v, = struct.unpack_from('<I', data, off)
                if is_return(v):
                    print('  [esp+%05x] %08x  %s' % (off - first, v, name(v))); shown += 1
                    if shown >= 60: print('  ...'); break
        return
    if args[0] in ('--words', '--bytes'):
        a = int(args[1], 16); n = int(args[2], 0) if len(args) > 2 else 16
        if args[0] == '--words':
            for i in range(n):
                b = stop.read(a + 4 * i, 4)
                if b is None: print('%08x: (not in the file)' % (a + 4 * i)); break
                v, = struct.unpack('<I', b)
                print('%08x: %08x  %s' % (a + 4 * i, v, region_name(v)))
        else:
            b = stop.read(a, n)
            if b is None: raise SystemExit('not in the file')
            for i in range(0, n, 16):
                row = b[i:i + 16]
                print('%08x: %-48s %s' % (a + i, row.hex(' '), ''.join(chr(c) if 32 <= c < 127 else '.' for c in row)))
    elif args[0] == '--stack':
        tid = int(args[1]); n = int(args[2], 0) if len(args) > 2 else 64
        r = next(r for t, f, r in stop.threads if t == tid)
        for i in range(n):
            a = (r['esp'] & ~3) + 4 * i
            b = stop.read(a, 4)
            if b is None: break
            v, = struct.unpack('<I', b)
            print('[esp+%04x] %08x: %08x  %s' % (4 * i, a, v, (name(v) if is_return(v) else region_name(v))))
    elif args[0] == '--find':
        want = struct.pack('<I', int(args[1], 16)); hits = 0
        for start, data in stop.memory:
            at = data.find(want)
            while at >= 0 and hits < 200:
                if at % 4 == 0: print('%08x' % (start + at)); hits += 1
                at = data.find(want, at + 1)
    else: raise SystemExit(__doc__)

main()

#!/usr/bin/env python3
"""v15list.py <1.0 exe> <1.0 map> <1.5 exe> <1.5 map> <out exe>
The one program for both game versions: the 1.0 link, carrying the list of every place where the same objects linked
for the 1.5 title update differ (addresses of the game's data, field offsets, pointers in tables). main.c's v15_apply
writes those bytes over the program in memory, before anything of the game runs, when the game version is 1.5.

Checks first that the two links have the same layout: the same sections at the same addresses and sizes, and every
symbol that is not one of the game's fixed data addresses at the same address. The list: 'H2V5', runs, bytes; then per
run the address, the length and the bytes (padded to 4)."""
import sys, struct, re
a_exe, a_map, b_exe, b_map, out = sys.argv[1:6]
A = bytearray(open(a_exe, 'rb').read()); B = open(b_exe, 'rb').read()

def pe(d):
    o = struct.unpack_from('<I', d, 0x3c)[0]
    assert d[o:o + 4] == b'PE\0\0'
    nsec = struct.unpack_from('<H', d, o + 6)[0]; opt = struct.unpack_from('<H', d, o + 20)[0]
    base = struct.unpack_from('<I', d, o + 24 + 28)[0]
    dirs = [struct.unpack_from('<II', d, o + 24 + 96 + 8 * i) for i in range(16)]
    secs = []
    for i in range(nsec):
        s = o + 24 + opt + 40 * i
        name = d[s:s + 8].rstrip(b'\0').decode()
        vsize, va, rsize, raw = struct.unpack_from('<IIII', d, s + 8)
        secs.append((name, va, vsize, rsize, raw, struct.unpack_from('<I', d, s + 36)[0]))
    return base, secs, dirs

base, SA, DA = pe(A); base_b, SB, DB = pe(B)
assert base == base_b == 0x10000
if [(s[0], s[1], s[2], s[3]) for s in SA] != [(s[0], s[1], s[2], s[3]) for s in SB]:
    for x, y in zip(SA, SB):
        if x[:4] != y[:4]: print('section differs: %s va %x size %x raw %x | %s va %x size %x raw %x' % (x[:4] + y[:4]))
    sys.exit('v15list: the two links do not have the same layout')

def symbols(path):
    """(name, object) -> address for the symbols of the link map that have a place in the program (not <absolute>: the
    game's own data addresses, which are what differs)"""
    r = {}
    for l in open(path, errors='replace'):
        m = re.match(r'\s*([0-9a-f]{4}):([0-9a-f]{8})\s+(\S+)\s+([0-9a-f]{16})\s+(?:f\s+)?(?:i\s+)?(\S.*)$', l)
        if m and m.group(1) != '0000': r.setdefault((m.group(3), m.group(5).strip()), int(m.group(4), 16))
    return r
ma, mb = symbols(a_map), symbols(b_map)
moved = [(n, ma[n], mb[n]) for n in ma if n in mb and ma[n] != mb[n]]
assert len(ma) > 20000, 'the link map was not read'
if moved or set(ma) != set(mb):
    for n, x, y in moved[:10]: print('symbol at another address: %s %x | %x' % (n[0], x, y))
    only = sorted(set(ma) ^ set(mb))[:10]
    if only: print('symbols in one link only:', only)
    sys.exit('v15list: the two links do not place the same symbols at the same addresses')

# what the loader itself writes or reads must be the same in both: imports, their address table, the TLS directory
guard = [(base + va, base + va + size, nm) for (va, size), nm in zip(DA, ['export', 'import', 'resource', 'exception', 'security',
         'reloc', 'debug', 'arch', 'gp', 'tls', 'loadcfg', 'bound', 'iat', 'delay', 'clr', '']) if size and nm in ('import', 'iat', 'tls', 'delay')]
runs = []; nbytes = 0; lst = None; skipped = 0
for (name, va, vsize, rsize, raw, flags), sb in zip(SA, SB):
    if name == '.h2nv15': lst = (raw, rsize, va); continue
    if name in ('.rsrc', '.reloc') or flags & 0x02000000: continue            # (discardable: not in memory)
    da = A[raw:raw + rsize]; db = B[sb[4]:sb[4] + rsize]
    if da == db: continue
    n = min(rsize, vsize); i = 0
    while i < n:
        if da[i] == db[i]: i += 1; continue
        j = i + 1; last = i
        while j < n and j - last <= 8:                                         # (a gap of up to 8 equal bytes costs less than a new run)
            if da[j] != db[j]: last = j
            j += 1
        addr = base + va + i; ln = last + 1 - i
        for lo, hi, nm in guard:
            if addr < hi and addr + ln > lo: sys.exit('v15list: the links differ inside the %s directory (%x)' % (nm, addr))
        runs.append((addr, bytes(db[i:last + 1]))); nbytes += ln
        i = last + 1
# the debug directory's record (the link's own identity) is not worth carrying
dbg_va, dbg_size = DA[6]
if dbg_size:
    def off(rva):
        for name, va, vsize, rsize, raw, flags in SA:
            if va <= rva < va + max(vsize, rsize): return raw + rva - va
    o = off(dbg_va); lo = base + dbg_va; hi = lo + dbg_size
    size_data, rva_data = struct.unpack_from('<II', A, o + 16)
    spans = [(lo, hi), (base + rva_data, base + rva_data + size_data)]
    keep = []
    for addr, b in runs:
        if any(addr >= x and addr + len(b) <= y for x, y in spans): skipped += 1; nbytes -= len(b)
        else: keep.append((addr, b))
    runs = keep
assert lst, 'no .h2nv15 section'
blob = bytearray(struct.pack('<III', 0x35563248, len(runs), nbytes))
for addr, b in runs:
    blob += struct.pack('<II', addr, len(b)) + b + b'\0' * (-len(b) % 4)
raw, rsize, va = lst
if len(blob) > rsize: sys.exit('v15list: the list needs %d bytes, the section has %d (H2N_V15_LIST_SIZE)' % (len(blob), rsize))
assert not any(A[raw:raw + rsize]), 'the list section is not empty'
A[raw:raw + len(blob)] = blob
open(out, 'wb').write(A)
print('v15list: %d symbols at the same addresses in both links' % len(ma))
print('v15list: %d places, %d bytes of the program differ for 1.5; the list takes %d of %d bytes (%d of the link record left out)' % (len(runs), nbytes, len(blob), rsize, skipped))

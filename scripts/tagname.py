#!/usr/bin/env python3
"""tagname.py <map file> <tag indices in hex...>: the names of tags in a Halo 2 Xbox map (header 0x2c0 count, 0x2c4 the names,
0x2cc the per-tag offsets into them; the tag index is the position in the tag table)"""
import sys, struct
f = open(sys.argv[1], 'rb'); h = f.read(0x800)
count, names_at, names_size, index_at = struct.unpack_from('<4I', h, 0x2c0)
f.seek(index_at); offs = struct.unpack('<%dI' % count, f.read(4 * count))
f.seek(names_at); names = f.read(names_size)
tags_at, = struct.unpack_from('<I', h, 0x10)
f.seek(tags_at); th = f.read(32)
def name(i): return names[offs[i]:names.index(b'\0', offs[i])].decode('latin1') if i < count else '?'
if len(sys.argv) > 2 and sys.argv[2] == 'find':
    for i in range(count):
        n = name(i)
        if all(w in n for w in sys.argv[3:]): print('%04x %s' % (i, n))
else:
    for a in sys.argv[2:]: print(a, name(int(a, 16)))

#!/usr/bin/env python3
"""mkxiso.py <folder> <out.iso> [offset]: a plain Xbox disc image (XDVDFS) of a folder, for testing the exe's disc-image
reader here. With an offset (e.g. 0x18300000) the file system starts that far into the file, as in a dump of a whole disc.
Layout as extract-xiso's source describes it: sector 2048, descriptor at sector 32, directory entries
(u16 left, u16 right, u32 sector, u32 size, u8 attributes, u8 name length, name), offsets in 4-byte units, 0xff padding."""
import os, struct, sys
SECTOR = 2048
src, out = sys.argv[1], sys.argv[2]
base = int(sys.argv[3], 0) if len(sys.argv) > 3 else 0
next_sector = 0x108
def take(nbytes):
    global next_sector
    s = next_sector; next_sector += max(1, -(-nbytes // SECTOR)); return s
def directory_bytes(entries):
    """entries: (name, sector, size, attributes), as a chain to the right in name order"""
    entries = sorted(entries, key=lambda e: e[0].upper())
    blob = bytearray(); starts = []
    for name, sector, size, attr in entries:
        n = 14 + len(name); n += -n % 4
        if len(blob) % SECTOR + n > SECTOR: blob += b'\xff' * (-len(blob) % SECTOR)
        starts.append(len(blob)); blob += b'\0' * n
    for i, (name, sector, size, attr) in enumerate(entries):
        at = starts[i]; right = starts[i + 1] // 4 if i + 1 < len(entries) else 0
        e = struct.pack('<HHIIBB', 0, right, sector, size, attr, len(name)) + name.encode('ascii')
        e += b'\xff' * (-len(e) % 4)
        blob[at:at + len(e)] = e
    blob += b'\xff' * (-len(blob) % SECTOR)
    return bytes(blob)
writes = []            # (sector, bytes or path)
def build(folder):
    entries = []
    for name in sorted(os.listdir(folder)):
        p = os.path.join(folder, name)
        if os.path.isdir(p):
            sector, size = build(p); entries.append((name, sector, size, 0x10))
        else:
            size = os.path.getsize(p); sector = take(size) if size else 0
            if size: writes.append((sector, p))
            entries.append((name, sector, size, 0x20))
    blob = directory_bytes(entries)
    sector = take(len(blob)); writes.append((sector, blob))
    return sector, len(blob)
root_sector, root_size = build(src)
with open(out, 'wb') as f:
    d = bytearray(SECTOR); d[0:20] = b'MICROSOFT*XBOX*MEDIA'; struct.pack_into('<II', d, 20, root_sector, root_size); d[-20:] = b'MICROSOFT*XBOX*MEDIA'
    f.seek(base + 32 * SECTOR); f.write(d)
    for sector, what in writes:
        f.seek(base + sector * SECTOR)
        if isinstance(what, bytes): f.write(what)
        else:
            with open(what, 'rb') as g:
                while True:
                    b = g.read(1 << 22)
                    if not b: break
                    f.write(b)
    f.seek(base + next_sector * SECTOR - 1); f.write(b'\0')
print(out, os.path.getsize(out), 'bytes; root directory at sector', root_sector, 'size', root_size)

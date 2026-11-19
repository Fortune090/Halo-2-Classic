#!/usr/bin/env python3
"""keyscan.py <signed saved file> <file>...: which 16 bytes of the given files, used as the console's certificate key, make
the signature that the saved file carries (its last 20 bytes). Says where; never prints the bytes."""
import sys, hmac, struct
d = open('/home/claude/xbe/default.xbe', 'rb').read()
base, cert = struct.unpack_from('<I', d, 0x104)[0], struct.unpack_from('<I', d, 0x118)[0]
seed = d[cert - base + 0xc0:cert - base + 0xd0]
data = open(sys.argv[1], 'rb').read(); body, want = data[:-20], data[-20:]
def scan(name, blob):
    hits = []; seen = set()
    for i in range(len(blob) - 15):
        k = blob[i:i + 16]
        if k in seen: continue
        seen.add(k)
        if hmac.digest(hmac.digest(k, seed, 'sha1')[:16], body, 'sha1') == want: hits.append(i)
    print('%-28s %7d bytes: %s' % (name, len(blob), ('the key is at offset ' + ', '.join(hex(h) for h in hits)) if hits else 'no 16 bytes here make the signature'))
    return hits
def rc4(key, blob):
    s = list(range(256)); j = 0
    for i in range(256): j = (j + s[i] + key[i % len(key)]) & 255; s[i], s[j] = s[j], s[i]
    out = bytearray(); i = j = 0
    for b in blob:
        i = (i + 1) & 255; j = (j + s[i]) & 255; s[i], s[j] = s[j], s[i]; out.append(b ^ s[(s[i] + s[j]) & 255])
    return bytes(out)
blobs = {}
for f in sys.argv[2:]: blobs[f] = open(f, 'rb').read(); scan(f, blobs[f])
mcpx = [b for b in blobs.values() if len(b) == 512]; roms = [(f, b) for f, b in blobs.items() if len(b) >= 0x40000]
for m in mcpx:
    for f, rom in roms:
        for size in (0x40000, 0x80000, 0x100000):
            if len(rom) < size: continue
            image = rom[:size]
            scan('%s: second loader (%dK image), unlocked with the boot ROM' % (f, size >> 10), rc4(m[0x1a5:0x1b5], image[size - 0x6200:size - 0x200]))

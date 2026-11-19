#!/usr/bin/env python3
"""make_keys.py <mcpx_1.0.bin> <BIOS image> [output]

Makes the key file H2Classic wants (keys.bin, 16 bytes) from two dumps of your own console:
the 512-byte boot ROM (MCPX 1.0) and a BIOS image made for it (256K, 512K or 1M).

The key that saved-file signatures are made with sits in the BIOS image's second-stage loader,
which is stored scrambled (RC4) with a key held in the boot ROM. This script unscrambles the
loader in memory and writes out the 16 bytes at offset 0x7C of it. Nothing is printed of the key.
Put the result in the h2classic folder next to where you start H2Classic.exe:  h2classic\\keys.bin
(The launcher's "Generate Xbox keys.bin file" button does the same.)
"""
import sys

def rc4(key, data):
    s = list(range(256)); j = 0
    for i in range(256):
        j = (j + s[i] + key[i % len(key)]) & 255; s[i], s[j] = s[j], s[i]
    out = bytearray(); i = j = 0
    for b in data:
        i = (i + 1) & 255; j = (j + s[i]) & 255; s[i], s[j] = s[j], s[i]
        out.append(b ^ s[(s[i] + s[j]) & 255])
    return bytes(out)

def main():
    if len(sys.argv) < 3:
        print(__doc__); return 1
    mcpx = open(sys.argv[1], 'rb').read()
    bios = open(sys.argv[2], 'rb').read()
    out = sys.argv[3] if len(sys.argv) > 3 else 'keys.bin'
    if len(mcpx) != 512:
        print('the first file is not a 512-byte MCPX boot ROM (%d bytes)' % len(mcpx)); return 1
    if len(bios) not in (0x40000, 0x80000, 0x100000):
        print('the second file is not a 256K, 512K or 1M BIOS image (%d bytes)' % len(bios)); return 1
    loader = rc4(mcpx[0x1A5:0x1B5], bios[len(bios) - 0x6200:len(bios) - 0x200])
    key = loader[0x7C:0x8C]
    if len(set(key)) < 8:
        print('what came out does not look like a key: this BIOS image is probably not one for the MCPX 1.0 boot ROM'); return 1
    open(out, 'wb').write(key)
    print('wrote %s (16 bytes). Put it at h2classic\\keys.bin and check it with H2N_SIGCHECK (see the README).' % out)
    return 0

sys.exit(main())

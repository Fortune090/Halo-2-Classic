#!/usr/bin/env python3
"""sandbox_sign.py <in file> <out file>: copy a saved file, with its signature (its last 20 bytes) made again for the test
build as it runs HERE, which has no console key (a certificate key of zeros). For testing a user's saved files in this
workspace only: a file signed this way is good in a build without the key and nowhere else."""
import sys, hmac, struct
d = open('/home/claude/xbe/default.xbe', 'rb').read()
base, cert = struct.unpack_from('<I', d, 0x104)[0], struct.unpack_from('<I', d, 0x118)[0]
derived = hmac.new(bytes(16), d[cert - base + 0xc0:cert - base + 0xd0], 'sha1').digest()[:16]
data = open(sys.argv[1], 'rb').read()
body = data[:-20]
open(sys.argv[2], 'wb').write(body + hmac.new(derived, body, 'sha1').digest())
print(sys.argv[2], len(data), 'bytes')

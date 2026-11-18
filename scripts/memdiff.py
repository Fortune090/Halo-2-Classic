#!/usr/bin/env python3
"""memdiff.py <stop file A> <stop file B> [lo hi]: the words of the game's data that differ between two memory files"""
import sys, struct
src=open('/home/claude/scripts/readdump.py').read(); ns={}
exec(compile(src[:src.index('class Retail:')],'rd','exec'),ns)
class readdump: Stop=ns['Stop']
a = readdump.Stop(sys.argv[1]); b = readdump.Stop(sys.argv[2])
lo = int(sys.argv[3], 16) if len(sys.argv) > 3 else 0x411000; hi = int(sys.argv[4], 16) if len(sys.argv) > 4 else 0x586000
n = 0
for ad in range(lo, hi, 4):
    x = a.read(ad, 4); y = b.read(ad, 4)
    if not x or not y or len(x) < 4 or len(y) < 4 or x == y: continue
    fx, = struct.unpack('<f', x); fy, = struct.unpack('<f', y)
    n += 1
    if n <= 400: print('%08x: %08x -> %08x   %g -> %g' % (ad, struct.unpack('<I', x)[0], struct.unpack('<I', y)[0], fx, fy))
print(n, 'words differ')

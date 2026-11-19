#!/usr/bin/env python3
# ring.py <stop.bin> [1.5 address of g_glyphs_storage] : the text page's ring of letters from a stop file
import sys, struct, subprocess, re
stop = sys.argv[1]; base = int(sys.argv[2], 16) if len(sys.argv) > 2 else 0x4c2ac0
out = subprocess.run(['python3', '-I', '/home/claude/scripts/readdump.py', stop, '/home/claude/pkg/b40_internal/halo2_native.exe', '/home/claude/pkg/b40_internal/halo2_native.map',
                      '--words', '%x' % base, str(4 + 0x400)], capture_output=True, text=True).stdout
w = {}
for l in out.split('\n'):
    m = re.match(r'([0-9a-f]{8}): ([0-9a-f]{8})', l)
    if m: w[int(m.group(1), 16)] = int(m.group(2), 16)
if base not in w: sys.exit('the stop file does not hold %x' % base)
h0, h1, h2, h3 = (w[base + 4 * i] for i in range(4))
first = h0 >> 16; nxt = h1 & 0xffff; x = h1 >> 16; y = h2 & 0xffff; rowh = h2 >> 16
print('first %d next %d  pen x %d y %d row height %d  bitmap %08x  (word0 low %04x)' % (first, nxt, x, y, rowh, h3, h0 & 0xffff))
n = (nxt - first) & 0x1ff; bad = 0
for k in range(0x200):
    ci = w[base + 16 + 8 * k]; p = w[base + 20 + 8 * k]; px = p & 0xffff; py = p >> 16
    live = ((k - first) & 0x1ff) < n
    odd = live and (px > 256 or py > 256)
    if odd: bad += 1
    if odd or '-a' in sys.argv: print('  slot %3d %s character %08x at (%d,%d)%s' % (k, 'live' if live else '    ', ci, px, py, '  <-- outside the page' if odd else ''))
print('%d letters in the ring, %d of them outside the page' % (n, bad))

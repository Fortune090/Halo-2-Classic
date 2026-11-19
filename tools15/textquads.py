#!/usr/bin/env python3
# textquads.py <log> <frame> <text page bmp (512x256 from F11)> <out png> [page address]
# the letters' quads of a described frame (F11): where each lies on the screen and which cell of the text page it shows;
# draws them from the saved page (coverage half) onto a 640x480 canvas, 2x, and prints the quads in reading order
import sys, re
from PIL import Image
log, frame, page, out = sys.argv[1], int(sys.argv[2]), sys.argv[3], sys.argv[4]
addr = (sys.argv[5] if len(sys.argv) > 5 else '84714000').upper()
L = open(log, errors='replace').read().split('\n')
a = next(i for i, l in enumerate(L) if 'F11: frame %u is described' % frame in l)
b = next(i for i in range(a, len(L)) if 'frame %u: the back buffer' % frame in L[i])
quads = []; cur = None; uses = False; verts = []
def flush():
    if uses and len(verts) >= 4:
        for k in range(0, len(verts) - 3, 4): quads.append((cur, verts[k:k + 4]))
for l in L[a:b]:
    m = re.match(r'\s+draw (\d+):', l)
    if m: flush(); cur = int(m.group(1)); uses = False; verts = []; continue
    if 'texture 0: %s' % addr in l.upper().replace('TEXTURE 0', 'texture 0') or re.search(r'texture \d: %s' % addr, l, re.I): uses = True
    m = re.match(r'\s+vertex \d+: (.*)', l)
    if m:
        d = {}
        for p in re.finditer(r'v(\d+) (\S+) (\S+) (\S+) (\S+)', m.group(1)): d[int(p.group(1))] = tuple(float(p.group(k)) for k in range(2, 6))
        verts.append(d)
flush()
pg = Image.open(page).convert('RGB'); W = pg.size[0] // 2; cov = pg.crop((0, 0, W, pg.size[1])).convert('L')
canvas = Image.new('L', (1280, 960), 0)
rows = []
for dn, v in quads:
    xs = [q[0][0] for q in v]; ys = [q[0][1] for q in v]
    tk = 3 if 3 in v[0] else sorted(k for k in v[0] if k not in (0, 9))[0]
    us = [q[tk][0] for q in v]; vs = [q[tk][1] for q in v]
    x0, x1, y0, y1 = min(xs), max(xs), min(ys), max(ys); u0, u1, v0, v1 = min(us), max(us), min(vs), max(vs)
    cell = cov.crop((int(u0), int(v0), int(u1), int(v1))) if u1 > u0 and v1 > v0 else None
    ink = sum(1 for p in cell.getdata() if p > 64) if cell else 0
    rows.append((y0, x0, dn, x1 - x0, y1 - y0, u0, v0, u1 - u0, v1 - v0, ink))
    if cell and x1 > x0 and y1 > y0:
        canvas.paste(cell.resize((int((x1 - x0) * 2), int((y1 - y0) * 2)), Image.NEAREST), (int(x0 * 2), int(y0 * 2)))
canvas.save(out)
print('%d quads of letters in frame %d' % (len(rows), frame))
rows.sort(key=lambda r: (round(r[0] / 6), r[1]))
empty = [r for r in rows if r[9] == 0]
print('quads whose cell of the page is empty:', len(empty))
for r in empty[:30]: print('   draw %d at screen (%g,%g) %gx%g cell (%g,%g) %gx%g' % (r[2], r[1], r[0], r[3], r[4], r[5], r[6], r[7], r[8]))

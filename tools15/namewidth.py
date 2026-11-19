#!/usr/bin/env python3
"""namewidth.py <frame.bmp>...: the extent of the light-blue name text (0.46, 0.73, 1.0) in the left view's upper two thirds."""
import sys
from PIL import Image
for f in sys.argv[1:]:
    im = Image.open(f).convert('RGB'); w, h = im.size; px = im.load()
    xs = []; ys = []
    for y in range(int(h * 0.08), int(h * 0.62)):
        for x in range(0, w // 2):
            r, g, b = px[x, y]
            if b > 200 and 120 < g < 215 and 60 < r < 160 and b - r > 80 and b - g > 30: xs.append(x); ys.append(y)
    if not xs: print(f[-9:-4], 'no name'); continue
    # the densest row band: keep rows within 14 lines of the median row
    ys2 = sorted(ys); m = ys2[len(ys2) // 2]
    keep = [(x, y) for x, y in zip(xs, ys) if abs(y - m) <= 8 * h // 480]
    print(f[-9:-4], 'x %d..%d (%d wide)  y %d..%d (%d tall)  %d pixels  window %dx%d' % (min(x for x, _ in keep), max(x for x, _ in keep), max(x for x, _ in keep) - min(x for x, _ in keep) + 1, min(y for _, y in keep), max(y for _, y in keep), max(y for _, y in keep) - min(y for _, y in keep) + 1, len(keep), w, h))

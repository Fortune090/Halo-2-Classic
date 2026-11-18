#!/usr/bin/env python3
"""menu_row.py <picture>: which of the main menu's five rows is the highlighted one (0 = CAMPAIGN), from a 1280x960 frame picture"""
import sys
from PIL import Image
im = Image.open(sys.argv[1]).convert('L'); w, h = im.size
best = []
for k, y in enumerate((575, 631, 687, 743, 799)):
    box = im.crop((int(w * 400 / 1280), int(h * (y - 16) / 960), int(w * 880 / 1280), int(h * (y + 16) / 960)))
    px = list(box.getdata()); best.append(sum(px) / len(px))
print(best.index(max(best)))

#!/usr/bin/env python3
"""rawaddr.py [file ...]: literals in the layer that look like addresses inside the game's image (0x11000-0x586000)
and are not inside H2N_VA(...). Each is either version-neutral on purpose or a 1.0 address that 1.5 must not use."""
import re, glob, sys
blank = lambda m: re.sub(r'[^\n]', ' ', m.group(0))
files = sys.argv[1:] or sorted(glob.glob('*.c') + glob.glob('*.cpp') + glob.glob('*.h'))
for f in files:
    if f.startswith('retail_') or f == 'stubs_typed.cpp': continue
    src = open(f, errors='replace').read()
    s = re.sub(r'/\*.*?\*/', blank, src, flags=re.S)
    s = re.sub(r'//[^\n]*', blank, s)
    s = re.sub(r'"(\\.|[^"\\\n])*"', blank, s)
    s = re.sub(r'H2N_VA\s*\([^()]*\)', blank, s)
    L = src.splitlines(); seen = set()
    for m in re.finditer(r'0x([0-9A-Fa-f]{5,6})[uUlL]*\b', s):
        v = int(m.group(1), 16)
        if 0x11000 <= v < 0x586000:
            ln = s.count('\n', 0, m.start()) + 1
            if ln in seen: continue
            seen.add(ln); print('%s:%d: %s' % (f, ln, L[ln - 1].strip()[:128]))

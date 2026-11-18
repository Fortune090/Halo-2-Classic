#!/usr/bin/env python3
"""vpdis.py <file with "program xxxxxxxx, N instructions: words..."> : the console's vertex program, readable"""
import sys
MAC = ['nop','mov','mul','add','mad','dp3','dph','dp4','dst','min','max','slt','sge','arl']
ILU = ['nop','mov','rcp','rcc','rsq','exp','log','lit']
OUT = {0:'oPos',3:'oD0',4:'oD1',5:'oFog',6:'oPts',7:'oB0',8:'oB1',9:'oT0',10:'oT1',11:'oT2',12:'oT3'}
def swz(s): return ''.join('xyzw'[(s >> k) & 3] for k in (6, 4, 2, 0))
def mask(m): return ''.join(c for c, b in zip('xyzw', (8, 4, 2, 1)) if m & b)
def src(mux, reg, neg, s, const, attr):
    name = {1: 'r%d' % reg, 2: 'v%d' % attr, 3: 'c[%d]' % (const - 96)}.get(mux, '?')
    return ('-' if neg else '') + name + '.' + swz(s)
for line in open(sys.argv[1]):
    if 'instructions:' not in line: continue
    w = [int(x, 16) for x in line.split('instructions:')[1].split()]
    for n in range(len(w) // 4):
        w0, w1, w2, w3 = w[4 * n:4 * n + 4]
        ilu, mac, const, attr = (w1 >> 25) & 7, (w1 >> 21) & 15, (w1 >> 13) & 255, (w1 >> 9) & 15
        a = src((w2 >> 26) & 3, (w2 >> 28) & 15, (w1 >> 8) & 1, w1 & 255, const, attr)
        b = src((w2 >> 11) & 3, (w2 >> 13) & 15, (w2 >> 25) & 1, (w2 >> 17) & 255, const, attr)
        c = src((w3 >> 28) & 3, ((w2 & 3) << 2) | (w3 >> 30), (w2 >> 10) & 1, (w2 >> 2) & 255, const, attr)
        mt, rt, it, om = (w3 >> 24) & 15, (w3 >> 20) & 15, (w3 >> 16) & 15, (w3 >> 12) & 15
        oilu, oaddr, orb = (w3 >> 2) & 1, (w3 >> 3) & 255, (w3 >> 11) & 1
        parts = []
        if mac:
            ops = {1: [a], 13: [a], 2: [a, b], 3: [a, c], 4: [a, b, c]}.get(mac, [a, b])
            d = []
            if mt: d.append('r%d.%s' % (rt, mask(mt)))
            if om and not oilu: d.append(('%s.%s' % (OUT.get(oaddr, 'o%d' % oaddr), mask(om))) if orb else 'c[%d].%s' % (oaddr - 96, mask(om)))
            parts.append('%s %s <- %s' % (MAC[mac], ', '.join(d) or '(nothing)', ', '.join(ops)))
        if ilu:
            d = []
            if it: d.append('r1.%s' % mask(it))
            if om and oilu: d.append(('%s.%s' % (OUT.get(oaddr, 'o%d' % oaddr), mask(om))) if orb else 'c[%d].%s' % (oaddr - 96, mask(om)))
            parts.append('%s %s <- %s' % (ILU[ilu], ', '.join(d) or '(nothing)', c))
        print('%3d  %s' % (n, '   ||   '.join(parts)))

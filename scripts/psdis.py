#!/usr/bin/env python3
"""psdis.py <log> <hash>: the console's combiner setting of that hash (the "combiners xxxxxxxx:" line of an F11 trace), readable"""
import sys
line = [l for l in open(sys.argv[1], errors='replace') if ('combiners %s:' % sys.argv[2]) in l]
if not line: sys.exit('no "combiners %s:" line in that log' % sys.argv[2])
w = [int(x, 16) for x in line[0].split(':', 1)[1].split()]
ai, ri, ao, ro = w[0:8], w[8:16], w[16:24], w[24:32]
fabcd, fefg, count, modes, dotmap, intex, cmp_ = w[32:39]
REG = {0:'0',1:'C0',2:'C1',3:'FOG',4:'V0',5:'V1',8:'T0',9:'T1',10:'T2',11:'T3',12:'R0',13:'R1',14:'V1R0',15:'EF'}
MAP = ['', '1-', 'exp ', '-exp ', 'hb ', '-hb ', 's ', '-s ']
def inp(b, alpha): return MAP[b >> 5] + REG.get(b & 15, '?%d' % (b & 15)) + ('.a' if b & 16 else ('.b' if alpha else '.rgb'))
def outs(o):
    cd, ab, s = o & 15, (o >> 4) & 15, (o >> 8) & 15
    f = [n for bit, n in ((0x1000, 'CDdot'), (0x2000, 'ABdot'), (0x4000, 'MUX'), (0x40000, 'AB b->a'), (0x80000, 'CD b->a')) if o & bit]
    sh = (o >> 15) & 7
    if sh: f.append(['', 'bias', 'x2', 'x2 bias', 'x4', '?5', '/2', '?7'][sh])
    return 'AB->%s CD->%s SUM->%s %s' % (REG[ab] if ab else '-', REG[cd] if cd else '-', REG[s] if s else '-', ' '.join(f))
print('stages %d, flags %x, texture modes %05x, dot mapping %x, input texture %x, compare %x, rest %s' % (count & 255, count >> 8, modes, dotmap, intex, cmp_, ' '.join('%x' % x for x in w[39:])))
for i in range(count & 15):
    b = [(ri[i] >> s) & 255 for s in (24, 16, 8, 0)]; a = [(ai[i] >> s) & 255 for s in (24, 16, 8, 0)]
    print('%d rgb: A=%s B=%s C=%s D=%s | %s' % (i, *[inp(x, 0) for x in b], outs(ro[i])))
    print('  a  : A=%s B=%s C=%s D=%s | %s' % (*[inp(x, 1) for x in a], outs(ao[i])))
b = [(fabcd >> s) & 255 for s in (24, 16, 8, 0)]; e = [(fefg >> s) & 255 for s in (24, 16, 8)]
print('final: A=%s B=%s C=%s D=%s E=%s F=%s G=%s flags %02x' % (*[inp(x, 0) for x in b], inp(e[0], 0), inp(e[1], 0), inp(e[2], 1), fefg & 255))

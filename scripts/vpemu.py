#!/usr/bin/env python3
"""vpemu.py <log> <program hash> [draw number]: run the console's vertex program of an F11-traced draw on the vertices that
H2N_GFXVERTS=<hash> printed, with the constants that H2N_GFXCONST=2 printed. Each vertex is run twice: with plain IEEE
arithmetic (as a PC's Direct3D 9 does) and with the console's rules as xemu has them (a product with a 0 factor is 0 even
against infinity or NaN; LOG(0) = (-inf, 1, -inf, 1); RCC clamped away from 0 and infinity). Reports the vertices whose
outputs are not finite, and what the texture coordinates are.
Decoding: the field table of xemu's vsh.c (w1: ILU 25-27, MAC 21-24, CONST 13-20, V 9-12, A_NEG 8, A_SWZ 0-7; w2: A_R
28-31, A_MUX 26-27, B_NEG 25, B_SWZ 17-24, B_R 13-16, B_MUX 11-12, C_NEG 10, C_SWZ 2-9, C_R_HIGH 0-1; w3: C_R_LOW 30-31,
C_MUX 28-29, MAC mask 24-27, OUT_R 20-23, ILU mask 16-19, O mask 12-15, ORB 11, address 3-10, OUT_MUX 2, A0X 1, FINAL 0).
r12 is oPos. A standalone ILU writes OUT_R; paired with a MAC it writes r1."""
import sys, re, math, struct

INF = float('inf')
FLTMAX = 3.4028234663852886e38
def frc(x): return x - math.floor(x) if math.isfinite(x) else float('nan')
def exp2(x):
    if math.isnan(x): return float('nan')
    if x > 1023: return INF
    if x < -1100: return 0.0
    return 2.0 ** x
def fbits(h): return struct.unpack('<f', struct.pack('<I', int(h, 16)))[0]
def isfin(v): return all(math.isfinite(x) for x in v)

def parse(logname, phash, want_draw=None):
    t = open(logname, errors='replace').read()
    m = re.search(r'program %s, (\d+) instructions: ([0-9a-f ]+)' % phash, t)
    words = [int(x, 16) for x in m.group(2).split()]
    # the described frames: draws using this program, with their constants and vertices
    draws = []
    for b in re.split(r'\n(?=\s+draw \d+:)', t):
        h = re.match(r'\s+draw (\d+):.*?program (\w+)', b)
        if not h or h.group(2) != phash: continue
        verts = []
        for l in b.split('\n'):
            if l.strip().startswith('vertex '):
                f = l.split(':', 1)[1].split()
                v = {}
                i = 0
                while i < len(f):
                    a = int(f[i][1:]); v[a] = [float(x) for x in f[i + 1:i + 5]]; i += 5
                verts.append(v)
        if not verts: continue
        consts = [[0.0] * 4 for _ in range(192)]
        for l in b.split('\n'):
            c = re.match(r'\s+c(\d+): .*\((\w+) (\w+) (\w+) (\w+)\)', l)
            if c: consts[int(c.group(1))] = [fbits(c.group(k)) for k in range(2, 6)]
        changed = re.search(r'changed (\d+) pixels', b)
        draws.append((int(h.group(1)), consts, verts, int(changed.group(1)) if changed else 0))
    if want_draw is not None: draws = [d for d in draws if d[0] == want_draw]
    return words, draws

def decode(words):
    prog = []
    for n in range(len(words) // 4):
        w0, w1, w2, w3 = words[4 * n:4 * n + 4]
        ins = dict(ilu=(w1 >> 25) & 7, mac=(w1 >> 21) & 15, const=(w1 >> 13) & 255, v=(w1 >> 9) & 15,
                   a=((w2 >> 26) & 3, (w2 >> 28) & 15, (w1 >> 8) & 1, w1 & 255),
                   b=((w2 >> 11) & 3, (w2 >> 13) & 15, (w2 >> 25) & 1, (w2 >> 17) & 255),
                   c=((w3 >> 28) & 3, ((w2 & 3) << 2) | (w3 >> 30), (w2 >> 10) & 1, (w2 >> 2) & 255),
                   macmask=(w3 >> 24) & 15, outr=(w3 >> 20) & 15, ilumask=(w3 >> 16) & 15, omask=(w3 >> 12) & 15,
                   orb=(w3 >> 11) & 1, addr=(w3 >> 3) & 255, outmux=(w3 >> 2) & 1, a0x=(w3 >> 1) & 1, final=w3 & 1)
        prog.append(ins)
        if ins['final']: break
    return prog

def mul(a, b, rule):
    if rule is True and (a == 0.0 or b == 0.0): return 0.0
    return a * b

def run(prog, consts, inputs, rule):
    R = [[0.0, 0.0, 0.0, 0.0] for _ in range(16)]       # r12 is oPos
    O = {}
    a0 = 0
    def read(op):
        mux, reg, neg, s = op
        if mux == 1: v = R[reg]
        elif mux == 2: v = inputs.get(ins['v'], [0.0, 0.0, 0.0, 1.0])
        else:
            k = ins['const'] + (a0 if ins['a0x'] else 0)
            v = consts[k] if 0 <= k < 192 else [0.0] * 4
        v = [v[(s >> k) & 3] for k in (6, 4, 2, 0)]
        return [-x for x in v] if neg else v
    def write(dst, val, m):
        for k, bit in enumerate((8, 4, 2, 1)):
            if m & bit: dst[k] = val[k]
    for ins in prog:
        A, B, C = read(ins['a']), read(ins['b']), read(ins['c'])
        macv = iluv = None
        mac, ilu = ins['mac'], ins['ilu']
        if mac == 1: macv = A
        elif mac == 2: macv = [mul(A[k], B[k], rule) for k in range(4)]
        elif mac == 3: macv = [A[k] + C[k] for k in range(4)]
        elif mac == 4: macv = [mul(A[k], B[k], rule) + C[k] for k in range(4)]
        elif mac in (5, 6, 7):
            n = 3 if mac in (5, 6) else 4
            s = 0.0
            for k in range(n): s += A[k] * B[k]
            if mac == 6: s += B[3]
            macv = [s] * 4
        elif mac == 8: macv = [1.0, mul(A[1], B[1], rule), A[2], B[3]]
        elif mac == 9: macv = [min(A[k], B[k]) for k in range(4)]
        elif mac == 10: macv = [max(A[k], B[k]) for k in range(4)]
        elif mac == 11: macv = [1.0 if A[k] < B[k] else 0.0 for k in range(4)]
        elif mac == 12: macv = [1.0 if A[k] >= B[k] else 0.0 for k in range(4)]
        elif mac == 13: a0 = int(math.floor(A[0] + 0.001)); macv = None
        x = C[0]
        if ilu == 1: iluv = C
        elif ilu == 2: r = INF if x == 0 else 1.0 / x; iluv = [r] * 4
        elif ilu == 3:
            r = INF if x == 0 else 1.0 / x
            if rule is True:
                if r > 0: r = min(max(r, 5.42101e-20), 1.884467e+19)
                else: r = max(min(r, -5.42101e-20), -1.884467e+19)
            iluv = [r] * 4
        elif ilu == 4: r = INF if x == 0 else 1.0 / math.sqrt(abs(x)); iluv = [r] * 4
        elif ilu == 5:
            if rule in ('d3d', 'd3dfix'):
                if rule == 'd3dfix' and not math.isnan(x): x = max(min(x, FLTMAX), -FLTMAX)
                f = frc(x); fl = x - f
                iluv = [exp2(fl), f, exp2(x), 1.0]
            else:
                iluv = [2.0 ** math.floor(x) if math.isfinite(x) else (0.0 if x < 0 else INF), x - math.floor(x) if math.isfinite(x) else float('nan'),
                        2.0 ** x if x < 1024 else INF, 1.0]
        elif ilu == 6 and rule in ('d3d', 'd3dfix'):
            t = -INF if x == 0 else (INF if math.isinf(x) else math.log2(abs(x)))
            if rule == 'd3dfix': t = max(min(t, FLTMAX), -FLTMAX)
            f = frc(t)
            iluv = [t - f, exp2(f), t, 1.0]
        elif ilu == 6:
            t = abs(x)
            if t == 0: iluv = [-INF, 1.0, -INF, 1.0]
            elif math.isinf(t): iluv = [INF, 1.0, INF, 1.0]
            else:
                e = math.floor(math.log2(t)); iluv = [e, t / 2.0 ** e, math.log2(t), 1.0]
        elif ilu == 7:
            sx, sy, sw = max(C[0], 0.0), max(C[1], 0.0), min(max(C[3], -127.9961), 127.9961)
            iluv = [1.0, sx, (2.0 ** (sw * math.log2(sy)) if sy > 0 else 0.0) if sx > 0 else 0.0, 1.0]
        if macv is not None and ins['macmask']: write(R[ins['outr']], macv, ins['macmask'])
        if iluv is not None and ins['ilumask']: write(R[1] if mac else R[ins['outr']], iluv, ins['ilumask'])
        if ins['omask'] and ins['orb']:
            val = iluv if ins['outmux'] else macv
            if val is not None:
                if ins['addr'] == 0: write(R[12], val, ins['omask'])
                else: write(O.setdefault(ins['addr'], [0.0, 0.0, 0.0, 1.0]), val, ins['omask'])
    O[0] = R[12]
    return O, R

if __name__ == '__main__':
    words, draws = parse(sys.argv[1], sys.argv[2], int(sys.argv[3]) if len(sys.argv) > 3 else None)
    prog = decode(words)
    names = {0: 'oPos', 3: 'oD0', 4: 'oD1', 5: 'oFog', 9: 'oT0', 10: 'oT1', 11: 'oT2', 12: 'oT3'}
    for draw, consts, verts, changed in draws:
        bad = {False: 0, True: 0, 'd3d': 0, 'd3dfix': 0}
        examples = []
        for i, v in enumerate(verts):
            for rule in (False, True, 'd3d', 'd3dfix'):
                O, R = run(prog, consts, v, rule)
                fin = all(isfin(O.get(k, [0])) for k in (10, 11))
                if not fin:
                    bad[rule] += 1
                    if len(examples) < 4: examples.append((i, rule, O.get(10), O.get(11)))
        print('draw %d: %d vertices (changed %d pixels): non-finite oT1/oT2 - IEEE %d, console (xemu) %d, this build on log(0)=-inf %d, with the clamp %d'
              % (draw, len(verts), changed, bad[False], bad[True], bad['d3d'], bad['d3dfix']))
        for i, rule, t1, t2 in examples:
            print('   vertex %d %s: oT1 %s  oT2 %s' % (i, rule, ['%.4g' % x for x in t1[:3]], ['%.4g' % x for x in t2[:3]]))

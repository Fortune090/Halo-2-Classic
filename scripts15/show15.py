"""show15.py <1.0 address>...: what settles a convention by reading - 1.5's prologue, retail 1.0's call sites, the source's
declaration, and what the tables say (adapt15 / back15_adapt / callsite15)."""
import sys, json, struct, re, glob
sys.path.insert(0, '/home/claude/v15')
from xmap15_lib import r15, r10, md
from fmap_lib import load
A = json.load(open('/home/claude/v15/adapt15.json')); BK = json.load(open('/home/claude/v15/back15_adapt.json'))
C = json.load(open('/home/claude/v15/callsite15.json')); M = json.load(open('/home/claude/v15/map15.json'))['funcs']
D, S = load('/home/claude/xbe/default.xbe'); t = [s for s in S if s['name'] == '.text'][0]; b = D[t['raw']:t['raw'] + t['rs']]
for k in sys.argv[1:]:
    a10 = int(k, 16); e = A.get(k) or next((v for v in BK.values() if v.get('a10') == a10), {})
    a15 = M[k][0]
    print('== %s -> 1.5 %x' % (k, a15), {x: e.get(x) for x in ('name', 'cc', 'nargs', 'regs', 'stack', 'pop', 'decl_pop', 'live15', 'live')}, C.get(k, {}).get('result'))
    for i in list(md.disasm(r15(a15, 90), a15))[:12]: print('   %s %s' % (i.mnemonic, i.op_str))
    i = b.find(b'\xe8'); n = 0
    while i >= 0 and n < 3:
        if t['va'] + i + 5 + struct.unpack_from('<i', b, i + 1)[0] == a10:
            h = t['va'] + i; ins = list(md.disasm(r10(h - 40, 45), h - 40))
            print('  1.0 call at %x: %s' % (h, ' | '.join('%s %s' % (x.mnemonic, x.op_str) for x in ins[-7:]))); n += 1
        i = b.find(b'\xe8', i + 1)
    nm = re.sub(r'^\?', '', e.get('name', '')).split('@')[0]
    for f in glob.glob('/home/claude/h2decomp_src/halo2-decompiled-main/src/*.cpp'):
        for ln, l in enumerate(open(f, errors='replace')):
            if nm and re.search(r'\b%s\(' % re.escape(nm), l) and len(l) < 220: print('  %s:%d %s' % (f.split('/')[-1], ln + 1, l.strip()[:160]))

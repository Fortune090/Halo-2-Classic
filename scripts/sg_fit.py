#!/usr/bin/env python3
"""sg_fit.py <stock v1.0 default.xbe> <Spooky Ghost default.xbe>: what the mod changes in the game's own code, where its
section sits, which game functions its code calls, and how each of those meets this native build (whose game code is
compiled from the decompilation and sits at other addresses; retail's code only runs for the routed functions)."""
import sys, struct, json, collections, io, contextlib
from capstone import Cs, CS_ARCH_X86, CS_MODE_32
from capstone.x86 import X86_OP_IMM, X86_OP_MEM
stock, mod = open(sys.argv[1], 'rb').read(), open(sys.argv[2], 'rb').read()
def sections(d):
    base, = struct.unpack_from('<I', d, 0x104); n, sh = struct.unpack_from('<II', d, 0x11c)
    out = []
    for i in range(n):
        flags, va, vsize, raw, rsize, name = struct.unpack_from('<IIIIII', d, sh - base + i * 56)
        nm = d[name - base:d.index(b'\0', name - base)].decode('latin1')
        out.append((nm, va, vsize, raw, rsize, flags))
    return out
S0, S1 = sections(stock), sections(mod)
names0 = {s[0] for s in S0}
print('sections: stock %d, mod %d' % (len(S0), len(S1)))
extra = [s for s in S1 if s[0] not in names0]
for s in extra: print('  the mod\'s own section %-10s at %06x..%06x (%d bytes in memory, %d in the file)' % (s[0], s[1], s[1] + s[2], s[2], s[4]))
top = max(s[1] + s[2] for s in S0)
print('  the game\'s image ends at %06x; this build\'s own code begins at 586000' % top)
# what changed inside the game's sections
g = {'__name__': 'x'}
src = open('/home/claude/scripts/autoroute_core.py').read(); core = src[:src.index("ROUTE=[0xb7b40")]
import os; os.chdir('/home/claude/native')
with contextlib.redirect_stdout(io.StringIO()): exec(compile(core, 'core', 'exec'), g)
rows, func_of, classify = g['rows'], g['func_of'], g['classify']
routed = set()
for f in ('routes_unproven.json', 'routes_called.json'):
    routed |= {e['va'] for e in json.load(open(f))}
routed |= {0xb7b40, 0xb8540, 0xb7430, 0x6d4c0, 0x1d3920, 0x1f6090, 0x1fbac0, 0x1fc7e0, 0x279410, 0x2902b0, 0x2ce9e0, 0x2ce7b0, 0x2e2f70, 0xd4e60, 0x1608e0, 0x1f38c0}
runs = []
for nm, va, vsize, raw, rsize, flags in S0:
    m = [s for s in S1 if s[0] == nm]
    if not m: continue
    a, b = stock[raw:raw + rsize], mod[m[0][3]:m[0][3] + min(rsize, m[0][4])]
    i = 0
    while i < len(b):
        if a[i] != b[i]:
            j = i
            while j < len(b) and (a[j] != b[j] or (j + 1 < len(b) and a[j + 1] != b[j + 1]) or (j + 2 < len(b) and a[j + 2] != b[j + 2])): j += 1
            runs.append((nm, va + i, j - i, a[i - 1] if i else 0)); i = j
        else: i += 1
kind = collections.Counter(); where = collections.Counter(); site = []
for nm, at, n, before in runs:
    f = func_of(at)
    call = before in (0xe8, 0xe9) and n <= 4
    kind['a call or jump re-aimed' if call else 'other bytes (%s)' % nm] += 1
    if f is None or f not in rows: where['not inside a known function (%s)' % nm] += 1; continue
    r = rows[f]
    if r['owner'] != 'game' and not r['owner'].startswith('third'): w = 'in console library code (%s)' % r['owner']
    elif f in routed: w = 'in a function that runs as retail code here: the change would take effect as it is'
    else: w = 'in a function this build runs from its own compiled copy (%s): retail\'s bytes there never run' % r['status']
    where[w] += 1; site.append((at, f, call))
print('changes inside the game\'s own sections: %d runs' % len(runs))
for k, v in kind.most_common(): print('  %4d  %s' % (v, k))
for k, v in where.most_common(): print('  %4d  %s' % (v, k))
# what the mod's code calls in the game
md = Cs(CS_ARCH_X86, CS_MODE_32); md.detail = True
targets = collections.Counter(); data = collections.Counter()
for nm, va, vsize, raw, rsize, flags in extra:
    code = mod[raw:raw + rsize]
    for i in md.disasm(code, va):
        for op in i.operands:
            if op.type == X86_OP_IMM and i.mnemonic in ('call', 'jmp') and 0x12000 <= op.imm < top: targets[op.imm & 0xffffffff] += 1
            if op.type == X86_OP_MEM and op.mem.base == 0 and op.mem.index == 0 and 0x12000 <= (op.mem.disp & 0xffffffff) < top: data[op.mem.disp & 0xffffffff] += 1
cls = collections.Counter()
for t in targets:
    f = func_of(t)
    if f is None or f not in rows: cls['not a known function start'] += 1; continue
    r = rows[f]
    if r['owner'].startswith('xdk:d3d'): c = 'the console\'s Direct3D library (drives the graphics chip)'
    elif r['owner'] != 'game' and not r['owner'].startswith('third'): c = 'other console library code (%s)' % r['owner'].split(':')[0]
    else:
        try: k = classify(f)
        except Exception: k = None
        c = 'a game function retail takes the plain way (could be led to ours)' if k == 'jmp' else 'a game function retail takes in registers of its own choosing (needs an adapter each)'
    cls[c] += 1
print('the mod\'s code calls %d different addresses in the game (disassembled linearly; data inside the section may add noise)' % len(targets))
for k, v in cls.most_common(): print('  %4d  %s' % (v, k))
kern = sum(1 for d in data if 0x411000 > d >= 0x410000 or any(s[0] == '.rdata' and s[1] <= d < s[1] + 0x400 for s in S0))
print('it names %d different addresses of the game\'s data directly' % len(data))

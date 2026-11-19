#!/usr/bin/env python3
# xmmlive15.py : at 1.5's own call sites of functions that are ours in 1.5 (send-backs), which xmm registers and
# which x87 results 1.5's code reads after the call before writing them - a value it expects kept across the call, or
# a result handed back in a register
import json, sys, struct, bisect, collections
sys.path.insert(0, '/home/claude/v15')
from fmap_lib import load
import capstone
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32); md.detail = True
D, S = load('/home/claude/v15/default_v15.xbe')
t = [x for x in S if x['name'] == '.text'][0]; code = bytes(D[t['raw']:t['raw'] + t['rs']]); va0 = t['va']
BK = json.load(open('/home/claude/v15/back15_adapt.json'))
targets = {int(k, 16): e for k, e in BK.items()}
M = json.load(open('/home/claude/v15/map15.json'))['funcs']
same15 = {v[0] for v in M.values() if v and v[0] and v[1] == 'same'}
starts = sorted({v[0] for v in M.values() if v and v[0]})
res = collections.defaultdict(lambda: collections.Counter()); sites = collections.Counter()
for i in range(len(code) - 5):
    if code[i] != 0xe8: continue
    tg = (va0 + i + 5 + struct.unpack_from('<i', code, i + 1)[0]) & 0xffffffff
    if tg not in targets: continue
    k = bisect.bisect_right(starts, va0 + i) - 1
    if k < 0 or starts[k] in same15: continue                  # the caller is ours in 1.5: its retail copy does not run
    sites[tg] += 1; seen_w = set(); n = 0
    for ins in md.disasm(code[i + 5:i + 5 + 80], va0 + i + 5):
        n += 1
        if n > 14: break
        try: rr, ww = ins.regs_access()
        except Exception: break
        rn = [ins.reg_name(r) for r in rr]; wn = [ins.reg_name(r) for r in ww]
        ops = ins.op_str.replace(' ', '').split(',')
        idiom = ins.mnemonic in ('xorps', 'pxor', 'xorpd') and len(ops) == 2 and ops[0] == ops[1]
        for r in rn:
            if r.startswith('xmm') and r not in seen_w and not idiom: res[tg][r] += 1; seen_w.add(r)
        if ins.mnemonic in ('fstp', 'fst', 'fistp', 'fild') and 'st0' not in seen_w and ins.mnemonic.startswith('fst'): res[tg]['st0'] += 1; seen_w.add('st0')
        if ins.mnemonic.startswith('fld'): seen_w.add('st0')
        for r in wn + (ops[:1] if idiom else []):
            if r.startswith('xmm'): seen_w.add(r)
        if ins.mnemonic.startswith(('j', 'call', 'ret')): break
print('send-back targets called from 1.5 code that runs:', len(sites), 'call sites:', sum(sites.values()))
for tg, c in sorted(res.items()):
    e = targets[tg]; x = {r: v for r, v in c.items() if r != 'st0'}
    if x or '-a' in sys.argv: print('%6x (1.0 %6x) %-52s sites %2d  reads after the call: %s' % (tg, e['a10'], (e['name'] or '')[:52], sites[tg], dict(c)))

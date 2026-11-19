#!/usr/bin/env python3
# xmmcheck15.py : adapters (forward: 1.5's function; back: 1.5's twin of ours) that read an xmm register before writing
# it on the straight path from the entry - a float argument passed in a register, which the adapters must carry
import json, sys
sys.path.insert(0, '/home/claude/v15')
from xmap15_lib import r10, r15
import capstone
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32); md.detail = True
s = json.load(open('/home/claude/v15/settled15.json'))
A = json.load(open('/home/claude/v15/adapt15.json')); B = json.load(open('/home/claude/v15/back15_adapt.json'))
a15 = {int(k, 16): e['a15'] for k, e in A.items() if e.get('a15')}
a15.update({e['a10']: int(k, 16) for k, e in B.items()})
def scan(rd, a, n=60):
    seen_w = set(); out = []
    k = 0
    for i in md.disasm(rd(a, 600), a):
        k += 1
        if k > n: break
        try: rr, ww = i.regs_access()
        except Exception: rr, ww = [], []
        rn = [i.reg_name(r) for r in rr]; wn = [i.reg_name(r) for r in ww]
        ops = i.op_str.replace(' ', '').split(',')
        idiom = i.mnemonic in ('xorps', 'pxor', 'xorpd') and len(ops) == 2 and ops[0] == ops[1]
        for r in rn:
            if r.startswith('xmm') and r not in seen_w and not idiom: out.append((r, '%x: %s %s' % (i.address, i.mnemonic, i.op_str)))
        for r in wn + (ops[:1] if idiom else []):
            if r.startswith('xmm'): seen_w.add(r)
        if i.mnemonic in ('ret', 'jmp', 'call'): break       # a call may clobber xmm: stop there
    return out
for nm in ('forward', 'back'):
    for e in s[nm]:
        t = a15.get(e['a10'])
        if not t: continue
        o = scan(r15, t)
        if o: print(nm, '%x' % e['a10'], '-> %x' % t, e['kind'], (e['name'] or '')[:44], '|', o[0][0], o[0][1])

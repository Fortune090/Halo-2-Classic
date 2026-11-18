#!/usr/bin/env python3
"""unproven_routes.py: which unproven drafts of the decompilation run as retail's own code instead (routes_unproven.json,
read by gen_code.py).

The rule. A function is routed to retail's code when all of this holds:
  - it is game or Havok code that is not matched (a draft, a near-match or a stub), with a symbol of its own;
  - retail's DATA points to it (a virtual-function table or a callback table: fixups2.json). Whatever is called through a
    pointer has the standard calling convention in retail, so the two sides can only differ in what the draft declares;
  - retail's code takes its arguments as the draft's declaration says (retailconv.py's classification 'jmp');
  - retail's code hands back ebx, esi, edi and ebp unchanged (it pushes each before first writing it and pops it again,
    and so does everything it calls);
  - nothing retail's code reaches from it by direct calls is library code that touches hardware and is not led to ours,
    nor a game function with the console's Direct3D written into it (those use Direct3D's own data, which this build
    does not have), nor a direct kernel call.
First case (stage 9): c_vertex_shape::ray_test, a draft declared to return nothing where retail returns a pointer in eax
that the (matched) caller reads: every map with a vehicle stopped at load."""
import json, bisect, struct, sys, io, contextlib
NAT = '/home/claude/native'
src = open('/home/claude/scripts/autoroute_core.py').read()
core = src[:src.index("ROUTE=[0xb7b40")]
g = {}
buf = io.StringIO()
with contextlib.redirect_stdout(buf):
    import os; os.chdir(NAT)
    exec(compile(core, 'autoroute_core', 'exec'), g)
rows = g['rows']; calls = g['calls']; func_of = g['func_of']; patched = g['patched']; x = g['x']; md = g['md']; cands = g['cands']
fix = json.load(open(NAT + '/fixups2.json'))['fix']
taken = {e['retail'] for e in fix}
BACK = {0x1d2f0, 0x439d0, 0x15ca10}
# game functions with the console's Direct3D written into them: any operand that is an address inside Direct3D's own data
from capstone.x86 import X86_OP_MEM, X86_OP_IMM
md.detail = True
D3D_LO, D3D_HI, KT_LO, KT_HI = 0x403af8, 0x4086a4, 0x411520, 0x411790
def uses(f):
    d3d = kernel = False
    for i in md.disasm(x.read(f, int(rows[f]['size'])), f):
        for op in i.operands:
            v = (op.mem.disp & 0xffffffff) if op.type == X86_OP_MEM else (op.imm & 0xffffffff) if op.type == X86_OP_IMM else None
            if v is None: continue
            if D3D_LO <= v < D3D_HI: d3d = True
            if op.type == X86_OP_MEM and op.mem.base == 0 and op.mem.index == 0 and KT_LO <= v < KT_HI: kernel = True
    return d3d, kernel
# code addresses that a function names as plain numbers (push offset f, mov reg, offset f): callbacks it hands on. Retail's
# code names retail's functions this way, and nothing re-aims a number inside code, so what is named may run as retail too.
starts = set(rows)
# numbers that only look like a function's address: Havok's key check at 0x31c7d0 divides by 0x15180 = 86400, the seconds in a day
NOT_NAMES = {(0x31c7d0, 0x15180)}
def named(f):
    out = set()
    for i in md.disasm(x.read(f, int(rows[f]['size'])), f):
        if i.mnemonic in ('call', 'jmp') or i.mnemonic.startswith('j'): continue
        for op in i.operands:
            if op.type == X86_OP_IMM and (op.imm & 0xffffffff) in starts and 0x12000 <= (op.imm & 0xffffffff) < 0x411000 and (op.imm & 0xffff) and (f, op.imm & 0xffffffff) not in NOT_NAMES: out.add(op.imm & 0xffffffff)
    return out
HARD = g['HARD']
# library addresses that are plain helpers in spite of their owner: 0x319740 is three instructions (store the argument at
# this+0x2c) that the linker folded together with CSocket::SetCbRecvBuf; the game's own classes share it
HARMLESS = {0x319740}
named_memo = {}
def closure2(root):
    """everything retail's code can reach from root by direct calls and by the functions it names; and why that is a problem, if it is"""
    seen = set(); stack = [root]
    while stack:
        f = stack.pop()
        if f in seen or f not in rows: continue
        seen.add(f)
        o = rows[f]['owner']
        if o not in ('game', 'third:havok'):
            if f in patched or f in HARMLESS: continue
            if o in HARD: return 'reaches %x (%s), console library code that is not led to ours' % (f, o)
            continue
        if f in BACK and f != root: continue
        if f not in memo: memo[f] = uses(f)
        if memo[f][0]: return 'reaches %x, which has Direct3D written into it' % f
        if memo[f][1]: return 'reaches %x, which calls the kernel itself' % f
        if f not in named_memo: named_memo[f] = named(f)
        for c in list(calls.get(f, [])) + list(named_memo[f]):
            t = func_of(c)
            if t is not None and t not in seen: stack.append(t)
    return ''
memo = {}
def closure(root):
    seen = set(); stack = [root]
    while stack:
        f = stack.pop()
        if f in seen or f not in rows or rows[f]['owner'] not in ('game', 'third:havok'): continue
        if f in BACK and f != root: continue
        seen.add(f)
        for c in calls.get(f, []):
            t = func_of(c)
            if t is not None and t not in seen: stack.append(t)
    return seen
# stage 9, second rule: a draft that nothing calls through a pointer but that a MATCHED function calls directly. The matched
# caller's code is retail's code, instruction for instruction, so the way it passes the arguments is retail's way; with
# retail's function taking them as the declaration says (classify == 'jmp'), both sides are proven. (routes_called.json)
callers = {}
for f, cs in calls.items():
    for c in cs:
        if c in rows: callers.setdefault(c, set()).add(f)
proven_caller = {va for va, cl in callers.items() if any(rows[c]['status'] == 'matched' for c in cl)}
out = []; dropped = []; out2 = []; dropped2 = []
for va, name, owner, status, stub, reach in cands:
    if va not in taken:
        if va not in proven_caller: continue
        bad = closure2(va)
        (dropped2 if bad else out2).append(dict(va=va, name=name, owner=owner, status=status, why=bad))
        continue
    bad = ''
    for f in closure(va):
        if f not in memo: memo[f] = uses(f)
        if memo[f][0]: bad = 'reaches %x, which has Direct3D written into it' % f
        elif memo[f][1]: bad = 'reaches %x, which calls the kernel itself' % f
        if bad: break
    (dropped if bad else out).append(dict(va=va, name=name, owner=owner, status=status, why=bad))
json.dump(out, open(NAT + '/routes_unproven.json', 'w'), indent=0)
skip = set(json.load(open(NAT + '/routes_called_not.json'))) if os.path.exists(NAT + '/routes_called_not.json') else set()
out2 = [e for e in out2 if e['va'] not in skip]
json.dump(out2, open(NAT + '/routes_called.json', 'w'), indent=0)
print('unproven functions with a proven caller that run as retail code:', len(out2), '; left as drafts:', len(dropped2), '; taken out by hand (routes_called_not.json):', len(skip))
print('of the pointer-called ones, the stricter rule (named functions followed) would leave out:', sum(1 for e in out if closure2(e['va'])))
for e in dropped2: print('   called: %06x %s: %s' % (e['va'], e['name'][:50], e['why']))
print('unproven functions that retail calls through a pointer and that run as retail code:', len(out), '; left as drafts:', len(dropped))
for e in dropped: print('   %06x %s: %s' % (e['va'], e['name'][:60], e['why']))

"""callsite15.py: which declared argument travels where, read off the call sites. For a function G that our build and
retail call differently, a matched caller F calls it in both: our compile of F hands G its arguments as declared
(ecx / edx, then pushes in reverse order), retail's F (1.0, byte-identical to 1.5's) puts them where the link-time
convention wants them (some registers, the rest pushed - in the declared order, as every case read so far shows). The
values that can be told apart - constants, absolute addresses, fields at a given offset - pin down the assignment:
every way of putting the arguments in retail's registers is tried (the rest go on the stack in order), and the one
that agrees with every call site is kept, if it is the only one.
Input: adapt15.json and back15_adapt.json (the registers 1.5 needs on entry, what it pops); output callsite15.json."""
import json, sys, struct, csv, itertools, collections, re
sys.path.insert(0, '/home/claude/v15')
from xmap15_lib import r10, md
from capstone import x86
EXE = '/home/claude/native/out/halo2_native.exe'
d = open(EXE, 'rb').read(); pe = struct.unpack_from('<I', d, 0x3c)[0]; ns = struct.unpack_from('<H', d, pe + 6)[0]
opt = struct.unpack_from('<H', d, pe + 20)[0]; base = struct.unpack_from('<I', d, pe + 24 + 28)[0]; sec = pe + 24 + opt
def rdx(va, n):
    for i in range(ns):
        o = sec + 40 * i; vs, vaddr, rs, raw = struct.unpack_from('<4I', d, o + 8)
        if base + vaddr <= va < base + vaddr + rs: return d[raw + va - base - vaddr:raw + va - base - vaddr + n]
sym = {}; size_ours = {}
lines = [l.split() for l in open('/home/claude/native/out/halo2_native.map', errors='replace')]
pts = sorted((int(q[2], 16), q[1]) for q in lines if len(q) >= 4 and q[0].startswith('0001:') and re.fullmatch('[0-9a-fA-F]{8,16}', q[2]))
for i, (a, n) in enumerate(pts):
    sym[n] = a; size_ours[n] = (pts[i + 1][0] - a) if i + 1 < len(pts) else 0x400
rows = {int(r[0], 16): r for r in csv.reader(open('/home/claude/h2decomp_src/halo2-decompiled-main/config/functions.csv'))
        if r and r[0][:1] in '0123456789abcdef' and r[1].isdigit()}
fm = {int(k, 16): v for k, v in json.load(open('/home/claude/native/funcmap.json')).items()}
NAMES = {x86.X86_REG_EAX: 'eax', x86.X86_REG_ECX: 'ecx', x86.X86_REG_EDX: 'edx', x86.X86_REG_EBX: 'ebx', x86.X86_REG_ESI: 'esi',
         x86.X86_REG_EDI: 'edi', x86.X86_REG_EBP: 'ebp', x86.X86_REG_ESP: 'esp', x86.X86_REG_AL: 'eax', x86.X86_REG_CL: 'ecx',
         x86.X86_REG_DL: 'edx', x86.X86_REG_BL: 'ebx', x86.X86_REG_AX: 'eax', x86.X86_REG_CX: 'ecx', x86.X86_REG_DX: 'edx'}
def key(i, op):
    """what a value is, as far as two compiles of the same source can be compared: None = cannot tell"""
    if op.type == x86.X86_OP_IMM: return ('imm', op.imm & 0xffffffff)
    if op.type == x86.X86_OP_MEM:
        b = NAMES.get(op.mem.base)
        if op.mem.base == 0 and op.mem.index == 0: return ('abs', op.mem.disp & 0xffffffff)
        if b in ('esp', 'ebp') or op.mem.index: return None
        return ('mem', op.mem.disp, op.size)
    return None
def sites(code, at, target):
    """for each call to target in code: (pushes nearest first, {register: key of its last write})"""
    out = []; window = []
    for i in md.disasm(code, at):
        if i.mnemonic == 'call' and i.operands and i.operands[0].type == x86.X86_OP_IMM:
            if i.operands[0].imm == target:
                pushes = []; regs = {}
                for w in reversed(window):
                    if w.mnemonic == 'push': pushes.append(key(w, w.operands[0]) if w.operands else None)
                    elif w.operands and w.operands[0].type == x86.X86_OP_REG:
                        r = NAMES.get(w.operands[0].reg)
                        if r and r not in regs:
                            if w.mnemonic in ('mov', 'movzx', 'movsx') and len(w.operands) == 2: regs[r] = key(w, w.operands[1])
                            elif w.mnemonic == 'xor' and len(w.operands) == 2 and w.operands[1].type == x86.X86_OP_REG and w.operands[0].reg == w.operands[1].reg: regs[r] = ('imm', 0)
                            else: regs[r] = None
                out.append((pushes, regs))
            window = []; continue
        if i.mnemonic.startswith(('j', 'ret')): window = []; continue
        window.append(i)
    return out
from xmap15_lib import r15
G15 = {}
def homes15(a15, live):
    """where 1.5's function first keeps each incoming register (the register it is moved into, or itself)"""
    h = {}; pushes = 0
    for i in md.disasm(r15(a15, 200), a15):
        if i.mnemonic.startswith(('j', 'call', 'ret')): break
        if i.mnemonic in ('mov', 'movzx', 'movsx') and len(i.operands) == 2 and i.operands[0].type == x86.X86_OP_REG and i.operands[1].type == x86.X86_OP_REG:
            src = NAMES.get(i.operands[1].reg); dst = NAMES.get(i.operands[0].reg)
            if src in live and src not in h and dst: h[src] = dst
    return h
def homes_ours(name, cc, nargs):
    """where our compile first keeps each declared argument"""
    a = sym.get(name)
    if not a: return {}
    reg_args = {'ecx': 0} if cc == '__thiscall' else ({'ecx': 0, 'edx': 1} if cc == '__fastcall' else {})
    first_stack = len([1 for r, i in reg_args.items() if i < nargs]); h = {}; pushes = 0
    for i in md.disasm(rdx(a, 200), a):
        m = i.mnemonic
        if m.startswith(('j', 'call', 'ret')): break
        if m == 'push': pushes += 1; continue
        if m == 'sub' and i.operands[0].type == x86.X86_OP_REG and NAMES.get(i.operands[0].reg) == 'esp' and i.operands[1].type == x86.X86_OP_IMM: pushes += i.operands[1].imm // 4; continue
        if m in ('mov', 'movzx', 'movsx') and len(i.operands) == 2 and i.operands[0].type == x86.X86_OP_REG:
            dst = NAMES.get(i.operands[0].reg); s2 = i.operands[1]; k = None
            if s2.type == x86.X86_OP_REG and NAMES.get(s2.reg) in reg_args: k = reg_args[NAMES[s2.reg]]
            elif s2.type == x86.X86_OP_MEM and NAMES.get(s2.mem.base) == 'esp' and s2.mem.index == 0:
                q = s2.mem.disp - 4 * pushes - 4
                if q >= 0 and q % 4 == 0: k = first_stack + q // 4
            if k is not None and k < nargs and k not in h and dst: h[k] = dst
    return h
def derive(g10, cc, nargs, live, pop, callers):
    """-> (regs {reg: arg}, stack [args], anchors, n sites) or None"""
    import declplaces
    _cc, _pl = declplaces.places(fm[g10]['name']) if g10 in fm else (None, None)
    if _cc == cc and _pl and len(_pl) == nargs:
        reg_args = {i: p for i, p in enumerate(_pl) if isinstance(p, str)}
        stack_index = {i: p[1] for i, p in enumerate(_pl) if not isinstance(p, str)}
    else:
        reg_args = {0: 'ecx'} if cc == '__thiscall' else ({0: 'ecx', 1: 'edx'} if cc == '__fastcall' else {})
        reg_args = {i: r for i, r in reg_args.items() if i < nargs}
        stack_index = {i: i - len(reg_args) for i in range(nargs) if i not in reg_args}
    ns = pop // 4
    if len(live) + ns != nargs: return None
    pairs = []
    our_g = sym.get(fm[g10]['name']) if g10 in fm else None
    if not our_g: return None
    for f10 in callers:
        if f10 not in rows or f10 not in fm: continue
        rs = sites(r10(f10, int(rows[f10][1])), f10, g10)
        n = fm[f10]['name']
        if n not in sym: continue
        os_ = sites(rdx(sym[n], size_ours[n]), sym[n], our_g)
        if len(rs) != len(os_) or not rs: continue
        for (rp, rr), (op, orr) in zip(rs, os_):
            ours = {}
            for i in range(nargs):
                if i in reg_args: ours[i] = orr.get(reg_args[i])
                else:
                    k = stack_index[i]
                    ours[i] = op[k] if k < len(op) else None
            theirs_stack = rp[:ns]
            pairs.append((ours, theirs_stack, {r: rr.get(r) for r in live}))
    if not pairs: return None
    good = []
    for chosen in itertools.permutations(range(nargs), len(live)):
        regmap = dict(zip(sorted(live), chosen))
        stack = [i for i in range(nargs) if i not in chosen]
        ok = True; anchors = 0
        for ours, ts, tr in pairs:
            for j, i in enumerate(stack):
                a, b = ours[i], ts[j] if j < len(ts) else None
                if a is not None and b is not None:
                    if a != b: ok = False; break
                    anchors += 1
            if not ok: break
            for r, i in regmap.items():
                a, b = ours[i], tr[r]
                if a is not None and b is not None:
                    if a != b: ok = False; break
                    anchors += 1
            if not ok: break
        if ok: good.append((anchors, regmap, stack))
    if not good: return ('none', len(pairs))
    best = max(a for a, _, _ in good)
    top = [g for g in good if g[0] == best]
    if len(good) == 1: return (good[0][1], good[0][2], good[0][0], len(pairs))
    if len(top) != 1 or best == 0:
        # a tie the call sites cannot break: where each version keeps the value (1.5's incoming register moved into the
        # same register our compile moves the argument into) decides, if exactly one candidate agrees on every register
        a15 = G15.get(g10); h15 = homes15(a15, live) if a15 else {}; ho = homes_ours(fm[g10]['name'], cc, nargs)
        agree = [g for g in good if all(h15.get(r) and ho.get(i) == h15[r] for r, i in g[1].items())]
        # (where each version keeps a value proved no evidence: 0xd0870's edi went to the wrong argument by it)
        return ('ambiguous', len(pairs), [(a, r) for a, r, s in good][:4])
    return (top[0][1], top[0][2], best, len(pairs))
if __name__ == '__main__':
    A = json.load(open('/home/claude/v15/adapt15.json')); BK = json.load(open('/home/claude/v15/back15_adapt.json'))
    B = json.load(open('/home/claude/v15/boundary15.json'))
    callers_of = collections.defaultdict(set)
    for f, r in rows.items():
        if r[9] == 'matched':
            for c in r[7].split(): callers_of[int(c, 16)].add(f)
    out = {}; st = collections.Counter()
    G15.update({int(k, 16): e['a15'] for k, e in A.items()}); G15.update({e['a10']: int(k, 16) for k, e in BK.items()})
    todo = [(int(k, 16), e) for k, e in A.items()] + [(e['a10'], e) for k, e in BK.items() if 'cc' in e]
    for g10, e in todo:
        if e.get('cc') is None: continue
        live = sorted(e.get('live15') or e.get('live') or [])
        res = derive(g10, e['cc'], e['nargs'], live, e['pop'], sorted(callers_of.get(g10, ())))
        if res is None: st['no matched call site / counts'] += 1; continue
        if res[0] in ('none', 'ambiguous'): st[res[0]] += 1; out['%x' % g10] = dict(result=res[0], detail=str(res[1:])); continue
        regmap, stack, anchors, n = res[:4]
        old = (e['regs'] == {r: i for r, i in regmap.items()} and e['stack'] == stack)
        st['settled (agrees)' if old else 'settled (differs from adapt15)'] += 1
        out['%x' % g10] = dict(result='settled', regs=regmap, stack=stack, anchors=anchors, sites=n, agrees=old,
                               was=dict(regs=e['regs'], stack=e['stack'], kind=e['kind']))
    json.dump(out, open('/home/claude/v15/callsite15.json', 'w'), indent=0)
    print(dict(st))
    for k, v in out.items():
        if v['result'] == 'settled' and not v['agrees']: print(' differs', k, 'call sites say', v['regs'], v['stack'], 'adapt15 said', v['was'])

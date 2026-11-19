"""deadstore15.py: settled conventions (settled15.json) against retail 1.0's call sites, by a rule that does not use the
callee's own code: eax, ecx and edx do not survive a call, so a value a caller puts in one of them right before a call
and does not read again before it is an argument of that call. Where every call site of a function does that with a
register the settled convention does not take, or the convention takes one of the three that no call site loads, the
convention is suspect. Prints the suspects."""
import json, sys, struct
sys.path.insert(0, '/home/claude/v15')
from xmap15_lib import r10, md
from capstone import x86
S = json.load(open('/home/claude/v15/settled15.json'))
xbe = open('/home/claude/xbe/default.xbe', 'rb').read()
base = struct.unpack_from('<I', xbe, 0x104)[0]; nsec = struct.unpack_from('<I', xbe, 0x11c)[0]; sh = struct.unpack_from('<I', xbe, 0x120)[0] - base
secs = [struct.unpack_from('<IIIII', xbe, sh + i * 56) for i in range(nsec)]
calls = {}
for fl, va, vs, ra, rs in secs:
    if not (fl & 4): continue
    b = xbe[ra:ra + rs]; p = b.find(b'\xe8')
    while p >= 0:
        if p + 5 <= len(b): calls.setdefault(va + p + 5 + struct.unpack_from('<i', b, p + 1)[0], []).append(va + p)
        p = b.find(b'\xe8', p + 1)
FULL = {x86.X86_REG_EAX: 'eax', x86.X86_REG_AX: 'eax', x86.X86_REG_AL: 'eax', x86.X86_REG_AH: 'eax', x86.X86_REG_ECX: 'ecx', x86.X86_REG_CX: 'ecx',
        x86.X86_REG_CL: 'ecx', x86.X86_REG_CH: 'ecx', x86.X86_REG_EDX: 'edx', x86.X86_REG_DX: 'edx', x86.X86_REG_DL: 'edx', x86.X86_REG_DH: 'edx'}
def window(site, n=14):
    for back in range(72, 6, -1):
        b = r10(site - back, back + 5)
        if not b: continue
        ins = list(md.disasm(b, site - back))
        if ins and ins[-1].address == site and len(ins) > n: return ins[-n - 1:-1]
    return None
def dead_stores(site):
    """the three volatile registers that are written and not read again before the call (walking back from the call,
    stopping at the first call, jump or return: a straight line only)"""
    w = window(site)
    if w is None: return None
    out = set(); resolved = set(); later_read = set()
    for i in reversed(w):
        if i.mnemonic == 'call' or i.mnemonic.startswith('j') or i.mnemonic.startswith('ret') or i.mnemonic == 'leave': break
        try: rd, wr = i.regs_access()
        except Exception: break
        rd = {FULL[r] for r in rd if r in FULL}; wr = {FULL[r] for r in wr if r in FULL}
        for r in wr:
            if r not in resolved:
                resolved.add(r)                      # the last write to r before the call
                if r not in later_read: out.add(r)   # ... and nothing reads it before the call: it is for the call
        for r in rd:
            if r not in resolved: later_read.add(r)
    return out
n = 0
for side in ('forward', 'back'):
    for e in S[side]:
        if e.get('kind') not in ('adapter', 'plain'): continue
        regs = dict(e.get('regs') or {})
        if side == 'forward' and e.get('kind') == 'plain':
            regs = {'ecx': 0} if e.get('cc') == '__thiscall' else ({'ecx': 0, 'edx': 1} if e.get('cc') == '__fastcall' else {})
            regs = {r: i for r, i in regs.items() if i < (e.get('nargs') or 0)}
        sites = calls.get(e['a10'], [])
        ds = [d for d in (dead_stores(s) for s in sites) if d is not None]
        if not ds: continue
        every = set.intersection(*ds); some = set.union(*ds)
        extra = every - set(regs)
        missing = {r for r in regs if r in ('eax', 'ecx', 'edx') and r not in some}
        if extra or (missing and len(ds) >= 1):
            n += 1
            print('%-7s %6x %-50s takes %s stack %s (%s, %d sites): every site loads %s; none loads %s' % (side, e['a10'], e['name'][:50], regs, e.get('stack'), e.get('how'), len(ds), sorted(extra) or '-', sorted(missing) or '-'))
print(n, 'suspects')

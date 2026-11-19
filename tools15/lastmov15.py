"""lastmov15.py: a check of the settled conventions (settled15.json) against retail 1.0's own call sites: where the
instruction right before every call of a function loads a register (mov / lea / xor / pop into it) that the settled
convention does not take as an argument, the convention is suspect (the compiler does not load a register right before
a call for nothing). Also: a register the convention takes that no call site's last eight instructions write."""
import json, sys, struct
sys.path.insert(0, '/home/claude/v15')
from xmap15_lib import r10, md
import capstone
S = json.load(open('/home/claude/v15/settled15.json'))
xbe = open('/home/claude/xbe/default.xbe', 'rb').read()
base = struct.unpack_from('<I', xbe, 0x104)[0]; nsec = struct.unpack_from('<I', xbe, 0x11c)[0]; sh = struct.unpack_from('<I', xbe, 0x120)[0] - base
secs = [struct.unpack_from('<IIIII', xbe, sh + i * 56) for i in range(nsec)]
calls = {}
for fl, va, vs, ra, rs in secs:
    if not (fl & 4): continue
    b = xbe[ra:ra + rs]; p = b.find(b'\xe8')
    while p >= 0:
        if p + 5 <= len(b):
            t = va + p + 5 + struct.unpack_from('<i', b, p + 1)[0]
            calls.setdefault(t, []).append(va + p)
        p = b.find(b'\xe8', p + 1)
REG = {'al': 'eax', 'ax': 'eax', 'eax': 'eax', 'bl': 'ebx', 'bx': 'ebx', 'ebx': 'ebx', 'cl': 'ecx', 'cx': 'ecx', 'ecx': 'ecx', 'dl': 'edx', 'dx': 'edx',
       'edx': 'edx', 'esi': 'esi', 'si': 'esi', 'edi': 'edi', 'di': 'edi', 'ebp': 'ebp'}
def before(site, n=8):
    for back in range(48, 6, -1):
        ins = list(md.disasm(r10(site - back, back + 5) or b'', site - back))
        if ins and ins[-1].address == site and len(ins) > n: return ins[-n - 1:-1]
    return []
def written(i):
    if i.mnemonic in ('mov', 'lea', 'movsx', 'movzx', 'xor', 'pop', 'or', 'and', 'add', 'sub', 'sar', 'shl', 'shr', 'imul', 'inc', 'dec', 'neg', 'sbb', 'setne', 'sete'):
        d = i.op_str.split(',')[0].strip()
        return REG.get(d)
    return None
for side in ('forward', 'back'):
    for e in S[side]:
        if e.get('kind') not in ('adapter', 'plain'): continue
        regs = set(e.get('regs') or {})
        if side == 'forward' and e.get('kind') == 'plain': regs = {'ecx'} if e.get('cc') in ('__thiscall',) else ({'ecx', 'edx'} if e.get('cc') == '__fastcall' else set())
        sites = calls.get(e['a10'], [])
        if not sites: continue
        last = []; win = set()
        for s in sites:
            b = before(s)
            if not b: continue
            last.append(written(b[-1]))
            for i in b:
                w = written(i)
                if w: win.add(w)
        if not last: continue
        odd = {x for x in last if x} - regs
        if len(set(last)) == 1 and last[0] and last[0] not in regs and last[0] != 'ebp':
            print('%-7s %x %-52s takes %s stack %s (%s) but every call site (%d) loads %s right before the call' % (side, e['a10'], e['name'][:52], sorted(regs), e.get('stack'), e.get('how'), len(last), last[0]))
        never = [r for r in (e.get('regs') or {}) if r not in win and r not in ('esi', 'edi', 'ebx', 'ebp')]
        if never and side == 'forward' and e.get('how') != 'call sites':
            print('%-7s %x %-52s takes %s (%s): no call site writes %s in its last 8 instructions' % (side, e['a10'], e['name'][:52], sorted(regs), e.get('how'), never))

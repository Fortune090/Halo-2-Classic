#!/usr/bin/env python3
"""code_immediates.py: the places in retail's code that name a game function as a plain number (cmp / mov / push with the
function's address), to be re-aimed at this build's function like the pointers in retail's data are (native/code_immediates.json).
Why: retail's 0x155760 asks "is this player's camera procedure function_23c110?" by comparing with the number 0x23c110; the
pointer it looks at was stored by this build's code and is this build's function, so the answer was always no - and with
0x186ab0 running as retail code since s9d, that answer switched the zoom off. Both sides must mean the same function.
A number counts as a function's address when it is the start of a game function that has a symbol here AND that function is
either pointed to by retail's data or named this way in at least two different functions (0x15180 = 86400 is neither).
Left alone: functions whose table entries stay retail's (routes_pointer.json) - their address must go on meaning retail's."""
import sys, json, csv, struct, bisect
sys.path.insert(0, '/home/claude/scripts')
from xbeimg import XbeImage
from capstone import Cs, CS_ARCH_X86, CS_MODE_32
from capstone.x86 import X86_OP_IMM
NAT = '/home/claude/native'
x = XbeImage('/home/claude/xbe/default.xbe'); md = Cs(CS_ARCH_X86, CS_MODE_32); md.detail = True
rows = {int(r['va'], 16): r for r in csv.DictReader(open('/home/claude/functions_upstream.csv'))}
fm = json.load(open(NAT + '/funcmap.json'))
linked = set()
for l in open(NAT + '/out/halo2_native.map', errors='replace'):
    q = l.split()
    if len(q) >= 4 and q[0].startswith('0001:'): linked.add(q[1])
sym = {int(k, 16): v['name'] for k, v in fm.items() if not v.get('static') and v['name'] in linked}
taken = {e['retail'] for e in json.load(open(NAT + '/fixups2.json'))['fix']}
keep = {e['va'] for e in json.load(open(NAT + '/routes_pointer.json'))}
found = {}          # target -> [(field address, in function, mnemonic)]
for va, r in rows.items():
    if r['owner'] not in ('game', 'third:havok') or va < 0x12000 or va >= 0x411000: continue
    code = x.read(va, int(r['size']))
    if code is None: continue
    for i in md.disasm(code, va):
        if i.mnemonic not in ('cmp', 'mov', 'push'): continue
        if i.encoding.imm_size != 4: continue
        for op in i.operands:
            if op.type != X86_OP_IMM: continue
            t = op.imm & 0xffffffff
            if t in sym and (t & 0xffff) and rows[t]['owner'] in ('game', 'third:havok'):
                found.setdefault(t, []).append((i.address + i.encoding.imm_offset, va, i.mnemonic))
out = []; skipped = 0
for t, places in sorted(found.items()):
    if t in keep: skipped += 1; continue
    if t not in taken and len({p[1] for p in places}) < 2: skipped += 1; continue
    for field, fn, m in places:
        assert struct.unpack('<I', x.read(field, 4))[0] == t
        out.append(dict(field=field, target=t, name=sym[t], within=fn, how=m))
json.dump(out, open(NAT + '/code_immediates.json', 'w'), indent=0)
print('places in retail code that name a game function as a number:', len(out), 'for', len({e['target'] for e in out}), 'functions; left alone:', skipped)
print('  0x23c110:', sum(1 for e in out if e['target'] == 0x23c110), 'places;', 'cmp', sum(1 for e in out if e['how'] == 'cmp'), 'mov', sum(1 for e in out if e['how'] == 'mov'), 'push', sum(1 for e in out if e['how'] == 'push'))

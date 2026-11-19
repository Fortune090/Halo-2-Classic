"""entrycheck15.py: settled conventions (settled15.json) against the retail function's own first instructions, by a
simple rule that needs no analysis of calls: along the straight line from the entry (conditional jumps not taken, up to
the first return, unconditional jump or 60 instructions),
  - a register the convention takes that the function writes before it reads it is not an argument;
  - a register the convention does not take that the function reads before it writes it is one (pushes are left
    out: they save registers; so are xor r, r and the alloca probe's use of eax).
After a call eax, ecx and edx count as written. Prints the suspects. Forward entries are checked on 1.5's function,
send-backs on 1.0's (the function retail calls)."""
import json, sys
sys.path.insert(0, '/home/claude/v15')
from xmap15_lib import r10, r15, md
from capstone import x86
S = json.load(open('/home/claude/v15/settled15.json')); M = json.load(open('/home/claude/v15/map15.json'))['funcs']
FULL = {}
for names, full in ((('EAX', 'AX', 'AL', 'AH'), 'eax'), (('ECX', 'CX', 'CL', 'CH'), 'ecx'), (('EDX', 'DX', 'DL', 'DH'), 'edx'), (('EBX', 'BX', 'BL', 'BH'), 'ebx'),
                    (('ESI', 'SI'), 'esi'), (('EDI', 'DI'), 'edi'), (('EBP', 'BP'), 'ebp')):
    for n in names: FULL[getattr(x86, 'X86_REG_' + n)] = full
CHK = {'0x320560', '0x32a660'}
where = {}
def live_in(rd, a, limit=60):
    b = rd(a, 400)
    if not b: return None, None
    written = set(); live = set(); dead = set(); n = 0; stopped = ''; where.clear()
    for i in md.disasm(b, a):
        n += 1
        if n > limit: break
        m = i.mnemonic
        if m.startswith('ret') or m == 'jmp': stopped = m; break
        if m == 'call':
            if i.op_str in CHK: written.add('eax'); continue
            # the callee may take registers this function was given (they pass through): unknown, so stop judging
            # eax, ecx and edx as "not read" from here on; they are written by the call
            for r in ('eax', 'ecx', 'edx'):
                if r not in written: dead.discard(r)
            written |= {'eax', 'ecx', 'edx'}; stopped = 'call'
            continue
        if m == 'push' and n > 3 and len(i.operands) == 1 and i.operands[0].type == x86.X86_OP_REG and FULL.get(i.operands[0].reg) in ('eax', 'ecx', 'edx'):
            r = FULL[i.operands[0].reg]              # past the prologue, a volatile register is pushed to hand it on
            if r not in written and r not in live: where[r] = 'push ' + r
            if r not in written: live.add(r)
            continue
        if m in ('push', 'pop'):
            if m == 'pop':
                try: _, wr = i.regs_access()
                except Exception: wr = []
                for r in wr:
                    if r in FULL: written.add(FULL[r])
            continue
        try: rd_, wr_ = i.regs_access()
        except Exception: break
        rd_ = {FULL[r] for r in rd_ if r in FULL}; wr_ = {FULL[r] for r in wr_ if r in FULL}
        # ways of writing a register that read it in form only: or r, -1; and r, 0; lea r, [r]; mov r, r
        if len(i.operands) == 2 and i.operands[0].type == x86.X86_OP_REG:
            o1 = i.operands[1]
            if (m == 'or' and o1.type == x86.X86_OP_IMM and (o1.imm & 0xffffffff) == 0xffffffff) or (m == 'and' and o1.type == x86.X86_OP_IMM and o1.imm == 0):
                rd_ -= wr_
            if m == 'mov' and o1.type == x86.X86_OP_REG and o1.reg == i.operands[0].reg: continue
            if m == 'lea' and o1.type == x86.X86_OP_MEM and o1.mem.base == i.operands[0].reg and o1.mem.index == 0 and o1.mem.disp == 0: continue
        if m in ('xor', 'sub', 'sbb') and len(i.operands) == 2 and i.operands[0].type == x86.X86_OP_REG and i.operands[1].type == x86.X86_OP_REG and i.operands[0].reg == i.operands[1].reg and m != 'sbb':
            rd_ -= wr_
        # a callee-saved register stored into the frame is being saved, not used
        if m == 'mov' and len(i.operands) == 2 and i.operands[0].type == x86.X86_OP_MEM and i.operands[1].type == x86.X86_OP_REG \
           and FULL.get(i.operands[0].mem.base) in (None, 'ebp') and i.operands[0].mem.base in (x86.X86_REG_ESP, x86.X86_REG_EBP) \
           and FULL.get(i.operands[1].reg) in ('ebx', 'esi', 'edi', 'ebp'):
            rd_.discard(FULL.get(i.operands[1].reg))
        for r in rd_:
            if r not in written and r not in live: where[r] = '%s %s' % (m, i.op_str)
            if r not in written: live.add(r)
        for r in wr_:
            if r not in written and r not in live and r not in rd_: dead.add(r)     # written first
            written.add(r)
    return live, dead
n = 0
for side in ('forward', 'back'):
    for e in S[side]:
        if e.get('kind') not in ('adapter', 'plain'): continue
        regs = dict(e.get('regs') or {})
        if e.get('kind') == 'plain':
            regs = {'ecx': 0} if e.get('cc') == '__thiscall' else ({'ecx': 0, 'edx': 1} if e.get('cc') == '__fastcall' else {})
            regs = {r: i for r, i in regs.items() if i < (e.get('nargs') or 0)}
        a10 = e['a10']; v = M.get('%x' % a10)
        if side == 'forward':
            if not v or not v[0]: continue
            live, dead = live_in(r15, v[0])
        else: live, dead = live_in(r10, a10)
        if live is None: continue
        wrong = sorted(r for r in regs if r in dead)
        more = sorted(r for r in live if r not in regs and r not in ('ebp',))
        if wrong or more:
            n += 1
            print('%-7s %6x %-46s takes %s stack %s (%s): %s%s' % (side, a10, e['name'][:46], regs, e.get('stack'), e.get('how'),
                  ('writes %s before reading it; ' % wrong) if wrong else '', ('reads %s first: %s' % (more, '; '.join(where.get(r, '') for r in more))) if more else ''))
print(n, 'suspects')

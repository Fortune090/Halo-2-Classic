"""reach15.py: compare a changed function with its 1.5 place by the code its entry actually reaches (following its
branches, stopping at returns and at jumps out of it), not by the size functions.csv gives it: many 'changed' entries
are labels inside other code (shared tails, folded destructors) whose listed size runs into code that 1.5 changed.
A function is 'same' here if both versions reach the same instructions with the same bytes outside address fields,
and every address field agrees with map15.json. Writes reach15.json {a10: 'same' | reason}."""
import json, sys, csv, struct
sys.path.insert(0, '/home/claude/v15')
from xmap15_lib import r10, r15, md
from fmap_lib import mask
from capstone import x86
M = json.load(open('/home/claude/v15/map15.json'))
funcs = {int(k, 16): v for k, v in M['funcs'].items()}
dmap = {int(k, 16): v for k, v in M['data'].items()}
rows = {int(r[0], 16): r for r in csv.reader(open('/home/claude/h2decomp_src/halo2-decompiled-main/config/functions.csv'))
        if r and r[0][:1] in '0123456789abcdef' and r[1].isdigit()}
def reach(rd, a, limit):
    """instructions reachable from a: {offset: (size, bytes, branch target or None)}; None if an indirect jump"""
    seen = {}; work = [a]
    while work:
        p = work.pop()
        while p - a not in seen:
            if not (a <= p < a + limit): return None
            b = rd(p, 16)
            if not b: return None
            ins = next(md.disasm(b, p), None)
            if ins is None: return None
            t = None
            if ins.operands and ins.operands[0].type == x86.X86_OP_IMM and (ins.group(x86.X86_GRP_JUMP) or ins.group(x86.X86_GRP_CALL)):
                t = ins.operands[0].imm
            seen[p - a] = (ins.size, bytes(b[:ins.size]), t, ins.mnemonic)
            m = ins.mnemonic
            if m.startswith('ret') or m == 'int3': break
            if m == 'jmp':
                if t is None: return None                       # a jump table or a jump through a register
                if a <= t < a + limit and t != p: work.append(t)
                break
            if ins.group(x86.X86_GRP_JUMP) and t is not None and a <= t < a + limit: work.append(t)
            p += ins.size
    return seen
out = {}
import collections; st = collections.Counter()
from strict_lib import strict as _strict
for a10, (a15, how) in funcs.items():
    if how == 'same' or not a15 or a10 not in rows: continue
    n = int(rows[a10][1])
    if not _strict(a10, a15, n): out['%x' % a10] = 'another number in it'; st['strict'] += 1; continue   # (s10r)
    R10 = reach(r10, a10, max(n, 64) * 2); R15 = reach(r15, a15, max(n, 64) * 4)
    if R10 is None or R15 is None: out['%x' % a10] = 'indirect jump'; st['indirect'] += 1; continue
    why = None
    if sorted(R10) != sorted(R15): why = 'different code'
    else:
        for off in R10:
            s1, b1, t1, mn = R10[off]; s2, b2, t2, _ = R15[off]
            if s1 != s2: why = 'different code'; break
            mk = mask(b1)
            if any(y and x != z for x, y, z in zip(b1, mk, b2)): why = 'different code'; break
            if t1 is not None and not (a10 <= t1 < a10 + max(n, 64) * 2):         # a call or jump to another function
                e = funcs.get(t1, [None])[0]
                if e is None or e != t2: why = 'calls %x -> %s, 1.5 calls %x' % (t1, e and hex(e), t2); break
            elif t1 is None:
                for i in range(len(b1) - 3):
                    if not mk[i] and (i == 0 or mk[i - 1]):
                        v1 = struct.unpack_from('<I', b1, i)[0]; v2 = struct.unpack_from('<I', b2, i)[0]
                        e = funcs.get(v1, [None])[0] if v1 in funcs else dmap.get(v1)
                        if v1 != v2 and e != v2: why = 'uses %x -> %s, 1.5 uses %x' % (v1, e and hex(e), v2); break
                if why: break
    out['%x' % a10] = why or 'same'; st[why.split()[0] if why else 'same'] += 1
json.dump(out, open('/home/claude/v15/reach15.json', 'w'))
print(dict(st))
B = json.load(open('/home/claude/v15/boundary15.json'))
print('boundary functions the same by reachable code:', sum(out.get(k) == 'same' for k in B), 'of', len(B))

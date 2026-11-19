"""taint15.py: which declared argument arrives where, by what the function does with it. Our compile of the source takes
each argument where the declaration puts it; 1.5's own code takes it where link-time code generation put it. Both
bodies do the same things to the same argument: mask it to a datum index, read the same fields off it, compare it with
the same constants, hand it on. Following each incoming place (register, or stack slot) through the first instructions
of each body gives it a set of such uses; the pairing of 1.5's places with our arguments that shares the most uses is
the answer, when it is clearly better than any other.
Use: taint15.match(our_address, our_reader, our_places, a15, places15) -> ({place15: arg}, score, margin)"""
import sys, itertools, collections
sys.path.insert(0, '/home/claude/v15')
from capstone import x86
from xmap15_lib import md
md.detail = True
R = {x86.X86_REG_EAX: 'eax', x86.X86_REG_ECX: 'ecx', x86.X86_REG_EDX: 'edx', x86.X86_REG_EBX: 'ebx', x86.X86_REG_ESI: 'esi',
     x86.X86_REG_EDI: 'edi', x86.X86_REG_EBP: 'ebp', x86.X86_REG_ESP: 'esp', x86.X86_REG_AL: 'eax', x86.X86_REG_CL: 'ecx',
     x86.X86_REG_DL: 'edx', x86.X86_REG_BL: 'ebx', x86.X86_REG_AX: 'eax', x86.X86_REG_CX: 'ecx', x86.X86_REG_DX: 'edx',
     x86.X86_REG_SI: 'esi', x86.X86_REG_DI: 'edi', x86.X86_REG_BP: 'ebp', x86.X86_REG_AH: 'eax', x86.X86_REG_BH: 'ebx',
     x86.X86_REG_CH: 'ecx', x86.X86_REG_DH: 'edx'}
def uses(read, a, places, limit=90):
    """{place: Counter(feature)} for places: register names and ('stk', k) for the k-th stack argument"""
    feats = collections.defaultdict(collections.Counter)
    taint = {p: {p} for p in places if isinstance(p, str)}       # register -> places its value comes from
    depth = 0; frame = None; n = 0
    code = read(a, 0x600)
    if not code: return feats
    for i in md.disasm(code, a):
        n += 1
        if n > limit: break
        m = i.mnemonic; ops = i.operands
        if m.startswith('ret'): break
        src = set(); mem_src = set()
        for op in ops:
            if op.type == x86.X86_OP_MEM:
                b = R.get(op.mem.base)
                if b == 'esp':
                    q = op.mem.disp - depth - 4
                    if q >= 0 and q % 4 == 0 and ('stk', q // 4) in places and op.access & 1: src.add(('stk', q // 4))
                elif b == 'ebp' and frame is not None:
                    q = op.mem.disp - frame - 4
                    if q >= 0 and q % 4 == 0 and ('stk', q // 4) in places and op.access & 1: src.add(('stk', q // 4))
                elif b in taint:
                    for p in taint[b]: feats[p][('field', op.mem.disp, op.size)] += 1
                    mem_src |= taint[b]
                if op.mem.index and R.get(op.mem.index) in taint:
                    for p in taint[R[op.mem.index]]: feats[p][('index', op.mem.scale, op.mem.disp & 0xff)] += 1
            elif op.type == x86.X86_OP_REG and R.get(op.reg) in taint and (op.access & 1 or m in ('push', 'test', 'cmp')):
                src |= taint[R[op.reg]]
        imm = next((op.imm & 0xffffffff for op in ops if op.type == x86.X86_OP_IMM), None)
        for p in src: feats[p][(m, imm if imm is not None and imm < 0x10000 else None)] += 1
        # how the value travels: copies and arithmetic into a register carry its places on
        if m in ('mov', 'movzx', 'movsx', 'lea', 'and', 'or', 'add', 'sub', 'shl', 'shr', 'sar', 'imul', 'neg', 'not', 'inc', 'dec') \
                and ops and ops[0].type == x86.X86_OP_REG:
            dst = R.get(ops[0].reg)
            carried = (src | mem_src) if m == 'lea' or m.startswith('mov') else (src | (taint.get(dst, set()) if m not in ('mov', 'movzx', 'movsx') else set()))
            if m == 'mov' and len(ops) == 2 and ops[1].type == x86.X86_OP_MEM and R.get(ops[1].mem.base) in taint: carried = set()  # a field, not the argument
            if dst and dst != 'esp':
                if carried: taint[dst] = set(carried)
                else: taint.pop(dst, None)
        elif ops and ops[0].type == x86.X86_OP_REG and ops[0].access & 2 and m not in ('cmp', 'test', 'push'):
            taint.pop(R.get(ops[0].reg), None)
        if m == 'call':
            for r in ('eax', 'ecx', 'edx'): taint.pop(r, None)
        # the stack depth (to read [esp+n] right) and an ebp frame
        if m == 'push': depth += 4
        elif m == 'pop': depth -= 4; taint.pop(R.get(ops[0].reg) if ops and ops[0].type == x86.X86_OP_REG else None, None)
        elif m == 'sub' and ops[0].type == x86.X86_OP_REG and R.get(ops[0].reg) == 'esp' and imm is not None: depth += imm
        elif m == 'add' and ops[0].type == x86.X86_OP_REG and R.get(ops[0].reg) == 'esp' and imm is not None: depth -= imm
        elif m == 'mov' and len(ops) == 2 and ops[0].type == x86.X86_OP_REG and R.get(ops[0].reg) == 'ebp' and ops[1].type == x86.X86_OP_REG and R.get(ops[1].reg) == 'esp':
            frame = depth
        elif m == 'and' and ops[0].type == x86.X86_OP_REG and R.get(ops[0].reg) == 'esp': pass
    return feats
def score(f1, f2):
    return sum(min(f1[k], f2[k]) for k in f1 if k in f2 and k[0] not in ('push', 'mov'))
def match(our_a, our_read, our_places, a15, read15, places15):
    """our_places: [place of argument 0, 1, ...]; places15: the places 1.5 takes arguments in (registers, ('stk', k)).
    -> (assignment {place15: arg}, best score, margin over the next) or None"""
    fo = uses(our_read, our_a, set(our_places)); f5 = uses(read15, a15, set(places15))
    n = len(our_places)
    if len(places15) != n: return None
    best = [];
    for perm in itertools.permutations(range(n)):
        # the stack keeps the declared order: stack slots must take increasing arguments
        stk = [(p, perm[j]) for j, p in enumerate(places15) if not isinstance(p, str)]
        if any(stk[x][1] > stk[x + 1][1] for x in range(len(stk) - 1)): continue
        s = sum(score(f5[p], fo[our_places[perm[j]]]) for j, p in enumerate(places15))
        best.append((s, perm))
    if not best: return None
    best.sort(reverse=True)
    s0, p0 = best[0]; s1 = best[1][0] if len(best) > 1 else -1
    return ({p: p0[j] for j, p in enumerate(places15)}, s0, s0 - s1)

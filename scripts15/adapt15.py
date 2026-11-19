"""adapt15.py: for each changed function that our same code calls (boundary15.json), how to call 1.5's own function
from our callers: which of the declared arguments 1.5's code takes in which register, which on the stack, and what it
pops. 1.5 takes them as 1.0 did (its callers are byte-identical), and 1.0's link-time code generation passes an
argument in the register the function keeps it in - so our own compile of the same source (declared convention), which
moves each incoming argument into its register first, names the register of each argument. Every answer is checked:
each register 1.5 needs on entry is accounted for, and the arguments left over match what 1.5 pops.
Writes adapt15.json {a10: {"name", "a15", "regs": {reg: arg}, "stack": [args in order], "pop": n, "decl_pop": n}}."""
import json, struct, sys, re, collections, capstone
from capstone import x86
sys.path.insert(0, '/home/claude/v15')
import declplaces
C = json.load(open('/home/claude/v15/conv15.json'))
B = json.load(open('/home/claude/v15/boundary15.json'))
R = json.load(open('/home/claude/v15/reach15.json'))
EXE = '/home/claude/native/out/halo2_native.exe'
d = open(EXE, 'rb').read(); pe = struct.unpack_from('<I', d, 0x3c)[0]; ns = struct.unpack_from('<H', d, pe + 6)[0]
opt = struct.unpack_from('<H', d, pe + 20)[0]; base = struct.unpack_from('<I', d, pe + 24 + 28)[0]; sec = pe + 24 + opt
def rd(va, n):
    for i in range(ns):
        o = sec + 40 * i; vs, vaddr, rs, raw = struct.unpack_from('<4I', d, o + 8)
        if base + vaddr <= va < base + vaddr + rs: return d[raw + va - base - vaddr:raw + va - base - vaddr + n]
sym = {}
for l in open('/home/claude/native/out/halo2_native.map', errors='replace'):
    q = l.split()
    if len(q) >= 3 and q[0].startswith("0001:") and re.fullmatch("[0-9a-fA-F]{8,16}", q[2]): sym[q[1]] = int(q[2], 16)
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32); md.detail = True
NAMES = {x86.X86_REG_EAX: 'eax', x86.X86_REG_ECX: 'ecx', x86.X86_REG_EDX: 'edx', x86.X86_REG_EBX: 'ebx',
         x86.X86_REG_ESI: 'esi', x86.X86_REG_EDI: 'edi', x86.X86_REG_EBP: 'ebp', x86.X86_REG_ESP: 'esp',
         x86.X86_REG_CL: 'ecx', x86.X86_REG_DL: 'edx', x86.X86_REG_CX: 'ecx', x86.X86_REG_DX: 'edx'}
def ours(name, cc, nargs):
    """where our compile keeps each declared argument: {reg: arg} from its first moves, and its own pop"""
    a = sym.get(name)
    if a is None: return None
    code = rd(a, 400)
    loc = {}                    # location -> argument index: 'ecx', 'edx', ('stk', k)
    if cc in ('__fastcall',):
        if nargs >= 1: loc['ecx'] = 0
        if nargs >= 2: loc['edx'] = 1
        first_stack = min(nargs, 2)
    elif cc == '__thiscall': loc['ecx'] = 0; first_stack = 1
    else: first_stack = 0
    alias = collections.defaultdict(set)      # argument -> registers that hold it (or a value made from it alone)
    holds = {r: k for r, k in loc.items()}     # register -> argument it holds now
    for r, k in loc.items(): alias[k].add(r)
    pushes = 0; pop = None
    def src_arg(op):
        if op.type == x86.X86_OP_REG: return holds.get(NAMES.get(op.reg))
        if op.type == x86.X86_OP_MEM and op.mem.index == 0:
            b = NAMES.get(op.mem.base)
            if b == 'esp':
                q = op.mem.disp - 4 * pushes - 4
                if q >= 0 and q % 4 == 0 and first_stack + q // 4 < nargs: return first_stack + q // 4
            elif b in holds and op.size == 0: return holds[b]        # lea reg, [arg + n]
        return None
    for i in md.disasm(code, a):
        m = i.mnemonic; ops = i.operands
        if m.startswith('ret'): pop = int(i.op_str, 0) if i.op_str else 0; break
        if m == 'push': pushes += 1; continue
        if m == 'pop': pushes -= 1
        if m == 'sub' and ops[0].type == x86.X86_OP_REG and NAMES.get(ops[0].reg) == 'esp' and ops[1].type == x86.X86_OP_IMM:
            pushes += ops[1].imm // 4; continue
        if m.startswith('call') or m.startswith('j') or m.startswith('loop'): break
        k = None
        if m in ('mov', 'movzx', 'movsx', 'lea') and ops[0].type == x86.X86_OP_REG:
            src = ops[1]
            if m == 'lea': k = holds.get(NAMES.get(src.mem.base)) if src.mem.index == 0 else None
            else: k = src_arg(src)
        written = [NAMES[r] for r in i.regs_access()[1] if r in NAMES]
        for r in written: holds.pop(r, None)
        if k is not None and ops[0].type == x86.X86_OP_REG and NAMES.get(ops[0].reg):
            holds[NAMES[ops[0].reg]] = k; alias[k].add(NAMES[ops[0].reg])
    # the real end of the function for its pop: the first ret anywhere in the first 4 KB
    if pop is None:
        for i in md.disasm(rd(a, 4096), a):
            if i.mnemonic.startswith('ret'): pop = int(i.op_str, 0) if i.op_str else 0; break
    regs = {}
    for k2, rs in alias.items():
        for r in rs: regs.setdefault(r, set()).add(k2)
    return regs, pop, first_stack
from xmap15_lib import r15
def first_use15(a, reg):
    """how 1.5's function first touches reg in its straight-line start: 'push', 'read', 'write' or None"""
    prologue = True
    for i in md.disasm(r15(a, 256), a):
        rr, ww = i.regs_access()
        rr = {NAMES.get(x) for x in rr}; ww = {NAMES.get(x) for x in ww}
        if i.mnemonic == 'push' and i.operands[0].type == x86.X86_OP_REG and NAMES.get(i.operands[0].reg) == reg:
            return 'push0' if prologue else 'push'
        if not (i.mnemonic == 'push' and i.operands[0].type == x86.X86_OP_REG and NAMES.get(i.operands[0].reg) in ('ebx', 'esi', 'edi', 'ebp')) \
           and not (i.mnemonic == 'sub' and i.op_str.startswith('esp')): prologue = False
        if reg in rr: return 'read'
        if reg in ww: return 'write'
        if i.mnemonic.startswith(('j', 'call', 'ret')): return None
    return None
# conventions settled by reading both versions' code (our compile, 1.5's) where the analysis stops short:
#   0x58ee0 network_session_manager_initialize (fastcall, 6): (s10p, corrected) read off the retail call site, the same in
#   both versions (1.0 0x8dc3c-0x8dc9f, 1.5 0x6eadf): ecx = 4th (session_a, the session array), edx = 3rd (the session
#   list), eax = 6th (session_b, array + 0xf170), and on the stack the 1st (gateway), the 2nd (observer), the 5th
#   (session_c, array + 0x78b8); ret 0xc. 1.5's body agrees: client.session = eax, and it hands session_owner_initialize
#   ecx as session_a and edx as the third. The earlier reading (ecx = 1st, edx = 2nd, 3rd-5th on the stack) gave the
#   session manager the message gateway as its session: the Split Screen stop in update_player_properties.
#   0x18f58c player_slots_initialize (void): 1.5's saves ebx, esi, edi and sets eax, ecx, edi before any use (its ret lies past
#   the size the table gives it); 1.5's player slot is 0xc74 bytes (1.0: 0xc70) - a plain jump
MANUAL = {'18f58c': dict(regs={}, stack=[], pop=0, decl_pop=0, cc='__fastcall', nargs=0), '58ee0': dict(regs={'eax': 5, 'ecx': 3, 'edx': 2}, stack=[0, 1, 4], pop=12)}
# (s10q) four more whose 1.5 function shows more than one "ret n" inside the size the table gives it (so the loop below
# passed them by: traps); each from its retail 1.0 call and the 1.5 entry (gen_code15.py's MANUAL has the notes)
MANUAL.update({'61ac0': dict(regs={}, stack=[0], pop=4), '8a400': dict(regs={'ecx': 0, 'eax': 1}, stack=[], pop=0),
               'b35d0': dict(regs={'eax': 0}, stack=[], pop=0), 'b58c0': dict(regs={}, stack=[0, 1], pop=8)})
out = {}; st = collections.Counter(); bad = []
for k in B:
    e = C.get(k)
    if not e or R.get(k) == 'same': continue
    cc = e['cc']; nargs = (e['args'] or 0) // 4 + (1 if cc == '__thiscall' else 0)
    if cc is None: bad.append((k, 'no prototype')); continue
    o = ours(e['name'], cc, nargs)
    if o is None: bad.append((k, 'not linked')); continue
    regs10, decl_pop, first_stack = o
    # (s10s) what our own function pops is what its declaration says (the callee pops its stack arguments unless
    # __cdecl). ours() reads it off our compiled code, and for a function without a "ret" of its own - one that ends
    # in a jump, as a destructor does into its base class's - it took the next function's: ~c_mp_pause_game_screen's
    # adapter returned with "ret 4" and the pause menu's End Game / Save and Quit stopped the game.
    _dcc, _dpl = declplaces.places(e['name'])
    if _dcc is not None and _dpl is not None:
        decl_pop = 0 if _dcc == '__cdecl' else 4 * sum(1 for _p in _dpl if not isinstance(_p, str))
    L = set(e['live15']); rets = e['rets15']
    if len(rets) != 1 and k not in MANUAL: bad.append((k, 'returns pop %s' % rets)); continue
    if len(rets) != 1: rets = [MANUAL[k]['pop']]
    pop = rets[0]
    want = {}
    for r in sorted(L):
        c = regs10.get(r, set()) - set(want.values())
        if len(c) == 1: want[r] = next(iter(c))
    left = [i for i in range(nargs) if i not in want.values()]
    unexplained = L - set(want)
    # "push ecx" (or eax / edx) at the top of 1.5's function only makes room for a local: not an argument
    for r in sorted(unexplained):
        if r == 'ecx' and first_use15(e['a15'], r) == 'push0': unexplained.discard(r); L = L - {r}
    # one register left and one argument more than the stack takes: that argument
    if len(unexplained) == 1 and pop % 4 == 0 and len(left) - pop // 4 == 1 and (cc != '__cdecl'):
        regs_left = [i for i in left if not any(i in v for v in regs10.values()) or True]
        cand = [i for i in left]
        if len(cand) == 1: want[next(iter(unexplained))] = cand[0]; left = []; unexplained = set()
    # the source's arguments are all there is (a link-time convention adds none): when the arguments left over are
    # exactly what 1.5 pops, a register still unexplained is the liveness analysis' over-reach (a callee's own false
    # positive, a register saved later than the prologue), not an argument
    # (only where no argument could be misplaced by it: nothing declared, or every argument already placed; a register
    # matched to the wrong argument can hide behind a count that works out - 0x76ff0's ecx in 1.0)
    if unexplained and pop % 4 == 0 and len(left) == pop // 4 and (nargs == 0 or not left or (cc == '__stdcall' and pop == 4 * nargs)) and cc != '__cdecl':
        rec_fp = sorted(unexplained); unexplained = set()
    else: rec_fp = []
    ok = not unexplained and (pop == 4 * len(left) or (pop == 0 and cc == '__cdecl'))
    if not ok and unexplained and pop == 4 * len(left):
        # the leftover registers: compiler idioms (push ecx for a local, the shared epilogue's ebp) if our compile also
        # has no argument for them
        pass
    rec = dict(name=e['name'], a15=e['a15'], cc=cc, nargs=nargs, regs=want, stack=left, pop=pop, decl_pop=decl_pop,
               live15=sorted(L), unexplained=sorted(unexplained), ignored=rec_fp)
    out[k] = rec
    kind = 'plain' if (not want or want == {'ecx': 0} and cc in ('__thiscall', '__fastcall') or want == {'ecx': 0, 'edx': 1}) and ok and pop == decl_pop else ('adapter' if ok else 'unresolved')
    if k in MANUAL:
        rec.update(MANUAL[k]); rec['unexplained'] = []
        kind = 'plain' if not rec['regs'] and not rec['stack'] and rec['pop'] == (rec['decl_pop'] or 0) else 'adapter'
    rec['kind'] = kind; st[kind] += 1
json.dump(out, open('/home/claude/v15/adapt15.json', 'w'), indent=0)
print(dict(st), 'not handled:', len(bad), bad[:8])
for k, r in out.items():
    if r['kind'] == 'unresolved': print(k, r['cc'], r['nargs'], 'live', r['live15'], 'pop', r['pop'], 'ours', r['regs'], 'left', r['stack'], 'unexplained', r['unexplained'], r['name'][:40])

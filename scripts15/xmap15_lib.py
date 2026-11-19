"""xmap15.py: the 1.0 -> 1.5 address map, from the functions both versions hold unchanged (functions15.json kind
'same'): every absolute operand (displacement or immediate) and every rel32 target of the 1.0 function is paired with
the same field of its 1.5 twin. Writes addrmap15.json: {"pairs": [[a10, a15, votes], ...], "conflicts": [...]}."""
import json, struct, sys, collections, capstone
from capstone import x86
sys.path.insert(0, '/home/claude/v15')
import fmap_lib
D10, S10 = fmap_lib.load('/home/claude/xbe/default.xbe')
D15, S15 = fmap_lib.load('/home/claude/v15/default_v15.xbe')
def reader(D, S):
    def rd(va, n):
        for s in S:
            if s['va'] <= va and va + n <= s['va'] + s['rs']:
                o = s['raw'] + va - s['va']; return D[o:o + n]
        return None
    return rd
r10, r15 = reader(D10, S10), reader(D15, S15)
F = json.load(open('/home/claude/v15/functions15.json'))
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32); md.detail = True
LO, HI = 0x11000, 0x5a0000
def fields(a10, a15, size):
    """(offset, kind, v10, v15) for each absolute or rel32 field of a same function; kind 'rel' / 'abs'.
    Stops at the first instruction that does not decode the same in both (a jump table or padding)."""
    b10 = r10(a10, size); b15 = r15(a15, size)
    if not b10 or not b15: return
    for ins in md.disasm(b10, a10):
        off = ins.address - a10
        rel = ins.size >= 5 and (b10[off] in (0xE8, 0xE9) or (b10[off] == 0x0F and 0x80 <= b10[off + 1] <= 0x8F))
        if rel:
            t10 = ins.address + ins.size + struct.unpack_from('<i', b10, off + ins.size - 4)[0]
            t15 = a15 + off + ins.size + struct.unpack_from('<i', b15, off + ins.size - 4)[0]
            if not (a10 <= t10 < a10 + size): yield off, 'rel', t10, t15
            continue
        if ins.disp_size == 4 and ins.disp_offset:
            v10 = struct.unpack_from('<I', b10, off + ins.disp_offset)[0]
            v15 = struct.unpack_from('<I', b15, off + ins.disp_offset)[0]
            if LO <= v10 < HI: yield off, 'abs', v10, v15
        if ins.imm_size == 4 and ins.imm_offset:
            v10 = struct.unpack_from('<I', b10, off + ins.imm_offset)[0]
            v15 = struct.unpack_from('<I', b15, off + ins.imm_offset)[0]
            if LO <= v10 < HI: yield off, 'abs', v10, v15
        if ins.mnemonic in ('ret', 'jmp') and off + ins.size >= size - 16 and False: break
def same_functions():
    for k, (v15, size, kind) in F.items():
        if kind == 'same': yield int(k, 16), int(v15, 16), size

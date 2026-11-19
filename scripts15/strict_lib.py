"""strict_lib.py: is a 1.5 function the 1.0 function's twin instruction for instruction? The two may differ only in
rel32 targets, in 32-bit displacement / immediate fields that are addresses in both, and in jump-table slots that point
into the function itself at the same offset. (fmap_lib.mask hides any three bytes that a zero byte follows when the
third is 0x01-0x6f - a structure offset or a small constant that 1.5 changed passed as the same.)"""
import struct, sys
sys.path.insert(0, '/home/claude/v15')
from xmap15_lib import r10, r15, md
LO, HI = 0x11000, 0x5c0000
def addr(v): return LO <= v < HI
_memo = {}
# (s10r) kept ours in 1.5 all the same: functions compiled from H2Classic's own main_menu.cpp (the main menu's sixth item,
# EXIT, has its own widget in the list: a layout 1.5's own functions do not know - with them the list ran one item
# past its array). Their only difference in 1.5 is the player slot's size (imul 0xc70 -> 0xc74), which syms15.py writes
# into the 1.5 objects (SIZE_PATCH), so they count as twins here.
KEPT = {0x2308e0: 0x23737c,     # main_menu_list_choose_xbox_live
        0x230c2b: 0x2376c7}     # c_main_menu_screen::v10
def strict(a10, a15, n):
    if KEPT.get(a10) == a15: return True
    k = (a10, a15, n)
    if k in _memo: return _memo[k]
    _memo[k] = r = _strict(a10, a15, n)
    return r
def _strict(a10, a15, n):
    if n <= 0: return False
    b10 = r10(a10, n); b15 = r15(a15, n)
    if not b10 or not b15: return False
    if b10 == b15: return True
    ok = bytearray(n)
    for ins in md.disasm(b10, a10):
        off = ins.address - a10
        if ins.size >= 5 and (b10[off] in (0xE8, 0xE9) or (b10[off] == 0x0F and 0x80 <= b10[off + 1] <= 0x8F)):
            ok[off + ins.size - 4:off + ins.size] = b'\1\1\1\1'
            continue
        for fo, fs in ((ins.disp_offset, ins.disp_size), (ins.imm_offset, ins.imm_size)):
            if fs == 4 and fo:
                x = struct.unpack_from('<I', b10, off + fo)[0]; y = struct.unpack_from('<I', b15, off + fo)[0]
                if addr(x) and addr(y): ok[off + fo:off + fo + 4] = b'\1\1\1\1'
    for i in range(n - 3):
        if b10[i:i + 4] != b15[i:i + 4]:
            x = struct.unpack_from('<I', b10, i)[0]; y = struct.unpack_from('<I', b15, i)[0]
            if a10 <= x < a10 + n and x - a10 == y - a15: ok[i:i + 4] = b'\1\1\1\1'
    return all(b10[i] == b15[i] or ok[i] for i in range(n))

import re as _re
_num = _re.compile(r'0x[0-9a-f]+|\b\d+\b')
def same_shape(a10, a15, n):
    """the two functions are the same instructions with other numbers in them (immediates, displacements, targets):
    what a structure that grew, or a constant that changed, does to a function"""
    b10 = r10(a10, n); b15 = r15(a15, n)
    if not b10 or not b15: return False
    i10 = list(md.disasm(b10, a10)); i15 = list(md.disasm(b15, a15))
    if len(i10) != len(i15) or not i10: return False
    if sum(i.size for i in i10) != sum(i.size for i in i15): return False
    for x, y in zip(i10, i15):
        if x.mnemonic != y.mnemonic or x.size != y.size: return False
        if _num.sub('N', x.op_str) != _num.sub('N', y.op_str): return False
    return True

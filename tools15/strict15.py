"""strict15.py: check every function map15.json calls 'same' instruction by instruction: the 1.5 twin may differ from
the 1.0 function only in rel32 targets and in 32-bit displacement / immediate fields that are addresses in both
(fmap_lib.mask hides any three bytes followed by a zero byte whose third is 0x01-0x6f, so a changed structure offset or
small constant was passing as 'same'). Writes strict15.json {a10: [offset, 1.0 bytes hex, 1.5 bytes hex, text]}."""
import json, struct, sys, csv, collections
sys.path.insert(0, '/home/claude/v15')
from xmap15_lib import r10, r15, md
m = json.load(open('/home/claude/v15/map15.json'))
size = {int(r[0], 16): int(r[1]) for r in csv.reader(open('/home/claude/h2decomp_src/halo2-decompiled-main/config/functions.csv'))
        if r and r[0][:1] in '0123456789abcdef' and r[1].isdigit()}
LO, HI = 0x11000, 0x5c0000
def addr(v): return LO <= v < HI
out = {}; st = collections.Counter()
for k, v in m['funcs'].items():
    if not v or v[0] is None or v[1] not in ('same',): continue
    a10 = int(k, 16); a15 = v[0]; n = size.get(a10, 0)
    if n <= 0: continue
    b10 = r10(a10, n); b15 = r15(a15, n)
    if not b10 or not b15: st['unreadable'] += 1; continue
    if b10 == b15: st['identical'] += 1; continue
    ok = bytearray(n)                      # 1 = this byte may differ
    pos = 0; bad = None
    for ins in md.disasm(b10, a10):
        off = ins.address - a10; pos = off + ins.size
        rel = ins.size >= 5 and (b10[off] in (0xE8, 0xE9) or (b10[off] == 0x0F and 0x80 <= b10[off + 1] <= 0x8F))
        if rel:
            for i in range(off + ins.size - 4, off + ins.size): ok[i] = 1
            continue
        for fo, fs in ((ins.disp_offset, ins.disp_size), (ins.imm_offset, ins.imm_size)):
            if fs == 4 and fo:
                x = struct.unpack_from('<I', b10, off + fo)[0]; y = struct.unpack_from('<I', b15, off + fo)[0]
                if addr(x) and addr(y):
                    for i in range(off + fo, off + fo + 4): ok[i] = 1
    # what the linear decode did not reach (a jump table after the code): 4-byte address slots may differ
    i = pos
    while i + 4 <= n:
        x = struct.unpack_from('<I', b10, i)[0]; y = struct.unpack_from('<I', b15, i)[0]
        if addr(x) and addr(y): ok[i:i + 4] = b'\1\1\1\1'; i += 4
        else: i += 1
    # a jump table's slots point into the function itself, at the same offset in both (the linear decode runs on
    # through such tables as if they were code)
    for i in range(n - 3):
        if b10[i:i + 4] != b15[i:i + 4]:
            x = struct.unpack_from('<I', b10, i)[0]; y = struct.unpack_from('<I', b15, i)[0]
            if a10 <= x < a10 + n and x - a10 == y - a15: ok[i:i + 4] = b'\1\1\1\1'
    for i in range(n):
        if b10[i] != b15[i] and not ok[i]: bad = i; break
    if bad is None: st['same'] += 1; continue
    # the instruction that holds the differing byte, in both
    t10 = t15 = ''
    for ins in md.disasm(b10, a10):
        if ins.address - a10 <= bad < ins.address - a10 + ins.size: t10 = '%s %s' % (ins.mnemonic, ins.op_str); o = ins.address - a10; break
    else: o = bad
    for ins in md.disasm(b15[o:o + 16], a15 + o):
        t15 = '%s %s' % (ins.mnemonic, ins.op_str); break
    out[k] = [bad, '%x' % a15, t10, t15]; st['differs'] += 1
json.dump(out, open('/home/claude/v15/strict15.json', 'w'), indent=0)
print(dict(st))

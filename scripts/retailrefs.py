"""Every place in the retail code that names a given address (a global, a function), with the function it is in and
that function's state in the decompilation.   usage: retailrefs.py <hex address> [...]"""
import sys, struct, csv, bisect
sys.path.insert(0, '/home/claude/scripts')
from xbeimg import XbeImage
from capstone import Cs, CS_ARCH_X86, CS_MODE_32
x = XbeImage('/home/claude/xbe/default.xbe')
rows = sorted((int(r['va'], 16), int(r['size']), r) for r in csv.DictReader(open('/home/claude/h2decomp_src/halo2-decompiled-main/config/functions.csv')))
keys = [r[0] for r in rows]
md = Cs(CS_ARCH_X86, CS_MODE_32)
for arg in sys.argv[1:]:
    target = int(arg, 16); pat = struct.pack('<I', target)
    print('references to %08x:' % target)
    for s in x.sections:
        if s['va'] >= 0x411000: continue
        code = x.x[s['raw']:s['raw'] + s['rsize']]
        at = code.find(pat)
        while at >= 0:
            va = s['va'] + at
            i = bisect.bisect_right(keys, va) - 1
            fva, fsz, r = rows[i]
            text = '?'
            for insn in md.disasm(x.read(fva, fsz), fva):
                if insn.address <= va < insn.address + insn.size: text = insn.mnemonic + ' ' + insn.op_str; break
            print('  %08x in %08x  %-12s %-8s %-28s %s' % (va, fva, r['owner'], r['status'], r['source'].replace('src/', ''), text))
            at = code.find(pat, at + 1)

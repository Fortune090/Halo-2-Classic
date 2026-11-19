"""trapinfo.py <1.0 address hex>...: what is needed to settle how 1.5's own function takes its arguments: adapt15's
entry, the declaration and a source call, the instructions before two retail 1.0 calls of it, and the 1.5 entry."""
import json, sys, struct, subprocess, glob, re
sys.path.insert(0, '/home/claude/v15')
from xmap15_lib import r10, r15, md
A = json.load(open('/home/claude/v15/adapt15.json')); M = json.load(open('/home/claude/v15/map15.json'))['funcs']
xbe = open('/home/claude/xbe/default.xbe', 'rb').read()
base = struct.unpack_from('<I', xbe, 0x104)[0]; nsec = struct.unpack_from('<I', xbe, 0x11c)[0]; sh = struct.unpack_from('<I', xbe, 0x120)[0] - base
secs = [struct.unpack_from('<IIIII', xbe, sh + i * 56) for i in range(nsec)]
def callers(t):
    out = []
    for fl, va, vs, ra, rs in secs:
        if not (fl & 4): continue
        b = xbe[ra:ra + rs]; p = b.find(b'\xe8')
        while p >= 0:
            if p + 5 <= len(b) and va + p + 5 + struct.unpack_from('<i', b, p + 1)[0] == t: out.append(va + p)
            p = b.find(b'\xe8', p + 1)
    return out
def before(site, n=9):
    # decode from a few starts and keep one that lands on the call
    for back in range(48, 8, -1):
        ins = list(md.disasm(r10(site - back, back + 5), site - back))
        if ins and ins[-1].address == site and len(ins) >= n: return ins[-n - 1:]
    return []
for k in sys.argv[1:]:
    e = A.get(k, {}); a15 = M[k][0]
    print('==== %s -> 1.5 %x  %s' % (k, a15, e.get('name')))
    print('   cc %s nargs %s pop15 %s decl_pop %s live15 %s unexplained %s' % (e.get('cc'), e.get('nargs'), e.get('pop'), e.get('decl_pop'), e.get('live15'), e.get('unexplained')))
    nm = re.search(r'\?(\w+)@', e.get('name', '') or '')
    if nm:
        g = subprocess.run('grep -rn "%s(" /home/claude/h2decomp_src/halo2-decompiled-main/src --include=*.cpp --include=*.h | head -4' % nm.group(1), shell=True, capture_output=True, text=True).stdout
        for l in g.splitlines(): print('   src', l.split('/src/')[-1][:150])
    for site in callers(int(k, 16))[:2]:
        print('   1.0 call at %x:' % site, '; '.join('%s %s' % (i.mnemonic, i.op_str) for i in before(site)))
    print('   1.5 entry:', '; '.join('%s %s' % (i.mnemonic, i.op_str) for i in list(md.disasm(r15(a15, 48), a15))[:13]))

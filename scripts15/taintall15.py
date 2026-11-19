"""taintall15.py -> taint15.json: taint15.py's answer for every changed function the tables leave unsettled (forward and
send-back), tried with each plausible set of 1.5's argument registers (the liveness analysis' set, the registers 1.5
reads in its first block, their overlap); kept when one pairing clearly beats every other (margin >= 2, or >= 1 with a
score of 6 or more) - on the ten conventions settled by reading, that rule takes 8 right and leaves the other 2."""
import sys, json
sys.argv = ['x']
sys.path.insert(0, '/home/claude/v15')
from taintcal15 import rec, run, sym, r15
gsrc = open('/home/claude/v15/gen_code15.py').read()
ns = {'r15': r15, '__builtins__': __builtins__}
exec(gsrc[gsrc.index('from capstone import x86 as _x86'):gsrc.index('def settle(')], ns)
early_regs = ns['early_regs']
out = {}; st = {}
for k, e in rec.items():
    if 'nargs' not in e or e.get('cc') is None or e.get('name') not in sym: continue
    n = e['nargs']; cc = e['cc']
    live = set(e.get('live15') or []); E = early_regs(e['a15']) & {'eax', 'ecx', 'edx', 'ebx', 'esi', 'edi', 'ebp'}
    best = None
    for regs in {frozenset(live), frozenset(E), frozenset(live & E), frozenset(live | E)}:
        if cc == '__cdecl': nsk = n - len(regs)
        else:
            if e['pop'] % 4: continue
            nsk = e['pop'] // 4
        if len(regs) + nsk != n or nsk < 0: continue
        r = run(k, regs15=sorted(regs), nstack=nsk)
        if r and (best is None or (r[2], r[1]) > (best[2], best[1])): best = r
    if best is None: continue
    ok = best[2] >= 2 or (best[2] >= 1 and best[1] >= 6)
    regs = {p: a for p, a in best[0].items() if isinstance(p, str)}
    stack = [a for p, a in sorted(((p, a) for p, a in best[0].items() if not isinstance(p, str)), key=lambda x: x[0][1])]
    out[k] = dict(regs=regs, stack=stack, score=best[1], margin=best[2], ok=ok)
    st[ok] = st.get(ok, 0) + 1
json.dump(out, open('/home/claude/v15/taint15.json', 'w'), indent=0)
print('accepted / not:', st)

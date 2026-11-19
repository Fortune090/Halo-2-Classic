import sys, json
sys.path.insert(0, '/home/claude/v15'); sys.argv = ['x']
src = open('/home/claude/v15/callsite15.py').read().split("if __name__ == '__main__':")[0]
exec(src)                                   # sym, rdx (our exe), fm, rows
import taint15
from xmap15_lib import r15
A = json.load(open('/home/claude/v15/adapt15.json')); BK = json.load(open('/home/claude/v15/back15_adapt.json'))
rec = dict(A); rec.update({'%x' % e['a10']: dict(e, a15=int(k, 16), live15=e.get('live')) for k, e in BK.items() if 'cc' in e})
import declplaces
def our_places(cc, n, name=None):
    if name:
        _cc, pl = declplaces.places(name)
        if _cc == cc and pl and len(pl) == n: return pl
    if cc == '__fastcall': return [('ecx' if i == 0 else 'edx' if i == 1 else ('stk', i - 2)) for i in range(n)]
    if cc == '__thiscall': return [('ecx' if i == 0 else ('stk', i - 1)) for i in range(n)]
    return [('stk', i) for i in range(n)]
def run(k, regs15=None, nstack=None):
    e = rec[k]; n = e['nargs']; cc = e['cc']
    live = regs15 if regs15 is not None else sorted(e.get('live15') or [])
    ns = nstack if nstack is not None else (e['pop'] // 4 if cc != '__cdecl' else n - len(live))
    p15 = list(live) + [('stk', j) for j in range(ns)]
    return taint15.match(sym[e['name']], rdx, our_places(cc, n, e['name']), e['a15'], r15, p15)
if __name__ == '__main__':
    truth = {'e8720': {'eax': 4}, 'd0870': {'edi': 0}, 'fff40': {'edi': 0}, '1060a0': {'eax': 0, 'ecx': 1}, '14be90': {'eax': 1},
             '22f9bf': {'ebx': 0}, '1c4a80': {'eax': 0}, '3db00': {'eax': 0}, '58ee0': {'eax': 5, 'ecx': 0, 'edx': 1}, '122b40': {'eax': 0}}
    for k, t in truth.items():
        r = run(k, regs15=sorted(t))
        got = {p: a for p, a in r[0].items() if isinstance(p, str)} if r else None
        print(k, 'truth', t, 'taint', got, 'score %s margin %s' % (r[1], r[2]) if r else '', 'OK' if got == t else 'WRONG')

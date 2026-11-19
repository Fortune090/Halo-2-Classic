"""backadapt15.py: how retail code calls each same function back15.py sends back to ours (its own link-time convention,
the same in 1.0 and 1.5 since the function is the same), against how our build declares it: adapt15.py's derivation the
other way round. Also which registers 1.5's own function leaves untouched (link-time register allocation lets its
callers keep values there across the call), so the adapter keeps them too. -> back15_adapt.json"""
import declplaces
import json, sys, pickle, subprocess, re, collections, bisect, csv
sys.path.insert(0, '/home/claude/v15')
sys.argv = ['x']
src = open('/home/claude/v15/adapt15.py').read().split("out = {}; st = collections.Counter(); bad = []")[0]
exec(src)                                  # ours(), first_use15(), sym, rd, md ...
import conv_core
from xmap15_lib import r15
BK = json.load(open('/home/claude/v15/back15.json'))
fm = {int(k, 16): v for k, v in json.load(open('/home/claude/native/funcmap.json')).items()}
M = json.load(open('/home/claude/v15/map15.json'))['funcs']
rows = {int(r[0], 16): r for r in csv.reader(open('/home/claude/h2decomp_src/halo2-decompiled-main/config/functions.csv')) if r and r[0][:1] in '0123456789abcdef' and r[1].isdigit()}
starts15 = sorted({v[0] for v in M.values() if v[0]})
s15 = {}
for k, (a15, how) in M.items():
    if not a15 or not (0x12000 <= a15 < 0x41b000): continue
    a10 = int(k, 16)
    if how == 'same': s15[a15] = int(rows[a10][1])
    else:
        q = bisect.bisect_right(starts15, a15); s15[a15] = (starts15[q] - a15) if q < len(starts15) else 64
need, rets, saved, clob = conv_core.analyse(r15, s15)
def declared(name):
    u = subprocess.run(['llvm-undname', name], capture_output=True, text=True, stdin=subprocess.DEVNULL).stdout.strip().split('\n')[-1]
    m = re.search(r'(__stdcall|__fastcall|__cdecl|__thiscall)\s+([^(]*)\((.*)\)\s*(const)?$', u)
    if not m: return None, None, None
    cc = m.group(1); args = m.group(3).strip()
    al = [] if args in ('void', '') else [a.strip() for a in re.split(r',(?![^<(]*[>)])', args)]
    ret = m.group(2)
    return cc, sum(8 if a in ('double', '__int64', 'unsigned __int64') else 4 for a in al), u
# (s10r) what 1.5's twin pops where it shows no single "ret n" (it never returns, or leaves by a jump); the
# conventions themselves are in gen_code15.py's MANUAL
BPOP = {'199a57': 0, '2238f4': 12}
out = {}; st = collections.Counter()
for k, (a10, fr) in BK.items():
    a15 = int(k, 16); name = fm[a10]['name']
    cc, size, und = declared(name)
    if cc is None: st['no prototype'] += 1; continue
    nargs = size // 4 + (1 if cc == '__thiscall' else 0)
    o = ours(name, cc, nargs)
    if o is None: st['not linked'] += 1; continue
    regs10, decl_pop, first_stack = o
    # (s10s) what our own function pops is what its declaration says (the callee pops its stack arguments unless
    # __cdecl). ours() reads it off our compiled code, and for a function without a "ret" of its own - one that ends
    # in a jump, as a destructor does into its base class's - it took the next function's: ~c_mp_pause_game_screen's
    # adapter returned with "ret 4" and the pause menu's End Game / Save and Quit stopped the game.
    _dcc, _dpl = declplaces.places(name)
    if _dcc is not None and _dpl is not None:
        decl_pop = 0 if _dcc == '__cdecl' else 4 * sum(1 for _p in _dpl if not isinstance(_p, str))
    L = set(need.get(a15, ())); R = sorted(rets.get(a15, ()))
    if len(R) != 1 and ('%x' % a10) in BPOP: R = [BPOP['%x' % a10]]
    if len(R) != 1: st['returns'] += 1; out[k] = dict(a10=a10, name=name, kind='unresolved', why='pops %s' % R); continue
    pop = R[0]; want = {}
    for r in sorted(L):
        c = regs10.get(r, set()) - set(want.values())
        if len(c) == 1: want[r] = next(iter(c))
    left = [i for i in range(nargs) if i not in want.values()]
    unexplained = L - set(want)
    if unexplained and pop % 4 == 0 and len(left) == pop // 4 and (nargs == 0 or not left or (cc == '__stdcall' and pop == 4 * nargs)) and cc != '__cdecl': unexplained = set()
    if len(unexplained) == 1 and len(left) == 1 and pop == 0 and cc != '__cdecl':
        want[next(iter(unexplained))] = left[0]; left = []; unexplained = set()
    ok = not unexplained and (pop == 4 * len(left) or (pop == 0 and cc == '__cdecl'))
    keep = sorted({'eax', 'ecx', 'edx'} - set(clob.get(a15, {'eax', 'ecx', 'edx'})))
    plain = ok and pop == decl_pop and (not want or all(regs10.get(r) and r in ('ecx', 'edx') and (
        (cc == '__thiscall' and r == 'ecx' and i == 0) or (cc == '__fastcall' and ((r == 'ecx' and i == 0) or (r == 'edx' and i == 1))))
        for r, i in want.items())) and not keep
    kind = 'plain' if plain else ('adapter' if ok else 'unresolved')
    out[k] = dict(a10=a10, name=name, und=und, cc=cc, nargs=nargs, regs=want, stack=left, pop=pop, decl_pop=decl_pop, keep=keep, kind=kind,
                  live=sorted(L), why='' if ok else 'unexplained %s, left %s, pop %d' % (sorted(unexplained), left, pop))
    st[kind] += 1
json.dump(out, open('/home/claude/v15/back15_adapt.json', 'w'), indent=0)
print(dict(st))
for k, e in out.items():
    if e['kind'] == 'unresolved': print(' ', k, e['name'][:50], e.get('cc'), e.get('nargs'), e.get('live'), e['why'])

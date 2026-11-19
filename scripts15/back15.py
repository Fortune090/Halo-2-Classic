"""back15.py: where 1.5's own code, run in place for the functions 1.5 changed, would reach the console's Direct3D (the
D3D and XPP sections, which are never started here), and where to send it back to our build: the same functions
(compiled from the source, with our Direct3D underneath) that retail code calls on its way there. Starting points are
the 1.5 places our routes jump to (retail_code.S). Writes back15.json {a15_of_same_function: [a10, [callers...]]}."""
import json, csv, bisect, struct, sys, re, collections
sys.path.insert(0, '/home/claude/v15')
from fmap_lib import load
from xmap15_lib import md
D15, S15 = load('/home/claude/v15/default_v15.xbe')
F = json.load(open('/home/claude/v15/map15.json'))['funcs']
R = json.load(open('/home/claude/v15/reach15.json'))
fm = {int(k, 16): v for k, v in json.load(open('/home/claude/native/funcmap.json')).items()}
inv = {}; same15 = set()
for k, (a15, how) in F.items():
    if a15:
        inv[a15] = int(k, 16)
        if how == 'same' or R.get(k) == 'same': same15.add(a15)
s15 = sorted(inv)
def fn15(a):
    i = bisect.bisect_right(s15, a) - 1
    return s15[i] if i >= 0 else None
code = [s for s in S15 if s['fl'] & 4 or s['name'] in ('.text',)]
D3D = [(s['va'], s['va'] + s['vs']) for s in S15 if s['name'] in ('D3D', 'XPP')]
def in_d3d(a): return any(lo <= a < hi for lo, hi in D3D)
# direct calls and jumps out of every function (by its 1.5 extent: up to the next known start)
calls = collections.defaultdict(set)
for s in S15:
    if s['va'] >= 0x41b000 or not s['rs']: continue
    b = D15[s['raw']:s['raw'] + s['rs']]
    for i in range(len(b) - 5):
        if b[i] in (0xe8, 0xe9):
            t = s['va'] + i + 5 + struct.unpack_from('<i', b, i + 1)[0]
            if 0x12000 <= t < 0x41b000:
                f = fn15(s['va'] + i)
                if f is not None: calls[f].add(t if t in inv or in_d3d(t) else (fn15(t) or t))
# which functions reach Direct3D through direct calls
reach = {}
def reaches(f, stack=()):
    if f in reach: return reach[f]
    if in_d3d(f): reach[f] = True; return True
    if f in stack: return False
    reach[f] = False
    r = any(reaches(t, stack + (f,)) for t in calls.get(f, ()))
    reach[f] = r; return r
sys.setrecursionlimit(100000)
starts = set(int(x, 16) for x in re.findall(r'^  \.long "[^"]*", 0x([0-9a-f]+), LC\d+, (?:0x[0-9a-f]+|\d+)$', open('/home/claude/native15/layer/retail_code.S').read(), re.M))
starts |= set(int(x, 16) for x in re.findall(r'^RV[0-9a-f]+: \.long 0x([0-9a-f]+)$', open('/home/claude/native15/layer/retail_code.S').read(), re.M))
starts -= {0x13aac0, 0x157fe0}      # the tick and the director's update: retail's only under H2N_TICK=retail (off)
# walk retail code from the starting points; at a same function of ours that reaches Direct3D, stop and send it back
back = collections.defaultdict(set); seen = set(); work = list(starts)
while work:
    f = work.pop()
    if f in seen: continue
    seen.add(f)
    for t in calls.get(f, ()):
        if t in same15 and inv.get(t) in fm and not fm[inv[t]]['stub'] and reaches(t) and t not in starts:   # (a routed one runs retail's: sending it back would loop)
            back[t].add(f); continue
        if in_d3d(t): back[('d3d', t)].add(f); continue
        work.append(t)
out = {}
for t, fr in back.items():
    if isinstance(t, tuple): print('retail code that calls Direct3D itself: %x from %s' % (t[1], ' '.join('%x' % x for x in sorted(fr)))); continue
    out['%x' % t] = [inv[t], sorted('%x' % x for x in fr)]
json.dump(out, open('/home/claude/v15/back15.json', 'w'), indent=0)
print('retail functions reached from the routes:', len(seen), 'same functions to send back to ours:', len(out))
for k, (a10, fr) in out.items(): print('  1.5 %s = 1.0 %x %s  <- %s' % (k, a10, fm[a10]['name'][:50], ' '.join(fr[:5])))

"""conv15.py: how 1.5's own code takes the arguments of each changed function that our same code calls (the boundary,
boundary15.json), against how the source declares it: liveness on entry and the pops of its returns (conv_core.py,
retailconv.py's analysis), on 1.5's code and on 1.0's for comparison. Writes conv15.json {a10: {...}}."""
import json, csv, sys, bisect, subprocess, re, collections
sys.path.insert(0, '/home/claude/v15')
import conv_core
from xmap15_lib import r10, r15
M = json.load(open('/home/claude/v15/map15.json'))['funcs']
rows = {int(r[0], 16): r for r in csv.reader(open('/home/claude/h2decomp_src/halo2-decompiled-main/config/functions.csv'))
        if r and r[0][:1] in '0123456789abcdef' and r[1].isdigit()}
fm = {int(k, 16): v for k, v in json.load(open('/home/claude/native/funcmap.json')).items()}
B = json.load(open('/home/claude/v15/boundary15.json'))
CODE_END10, CODE_END15 = 0x411000, 0x41b600
s10 = {a: int(r[1]) for a, r in rows.items() if 0x12000 <= a < CODE_END10 and int(r[1]) > 0}
starts15 = sorted({v[0] for v in M.values() if v[0]})
s15 = {}
for k, (a15, how) in M.items():
    if not a15 or not (0x12000 <= a15 < CODE_END15): continue
    a10 = int(k, 16)
    if how == 'same': s15[a15] = int(rows[a10][1])
    else:
        q = bisect.bisect_right(starts15, a15)
        s15[a15] = (starts15[q] - a15) if q < len(starts15) else 64
need10, rets10, _, _ = conv_core.analyse(r10, s10)
need15, rets15, _, _ = conv_core.analyse(r15, s15)
def declared(name):
    u = subprocess.run(['llvm-undname', name], capture_output=True, text=True, stdin=subprocess.DEVNULL).stdout.strip().split('\n')[-1]
    m = re.search(r'(__stdcall|__fastcall|__cdecl|__thiscall)\s+[^(]*\((.*)\)\s*(const)?$', u)
    if not m: return None, None, u
    cc = m.group(1); args = m.group(2).strip()
    al = [] if args in ('void', '') else [a.strip() for a in re.split(r',(?![^<(]*[>)])', args)]
    return cc, sum(8 if a in ('double', '__int64', 'unsigned __int64') else 4 for a in al), u
def plain(cc, size, live, rets):
    if not rets: return False
    if cc == '__stdcall' and rets == {size} and not live: return True
    if cc == '__cdecl' and rets == {0} and not live: return True
    if cc == '__fastcall':
        regs = min(2, size // 4); stack = max(0, size - 8)
        if rets == {stack} and live <= set(['ecx', 'edx'][:regs]): return True
    if cc == '__thiscall' and rets == {max(0, size)} and live <= {'ecx'}: return True
    return False
import pickle
pickle.dump((need10, rets10, need15, rets15), open('/home/claude/v15/live15.pkl', 'wb'))
out = {}; st = collections.Counter()
ALL = '--all' in sys.argv
for k in (M if ALL else B):
    a10 = int(k, 16); a15 = M[k][0]
    if a10 not in fm or not a15: continue
    cc, size, und = declared(fm[a10]['name'])
    l15, r_15 = sorted(need15.get(a15, [])), sorted(rets15.get(a15, []))
    l10, r_10 = sorted(need10.get(a10, [])), sorted(rets10.get(a10, []))
    e = dict(name=fm[a10]['name'], a15=a15, cc=cc, args=size, live15=l15, rets15=r_15, live10=l10, rets10=r_10,
             plain15=plain(cc, size, set(l15), set(r_15)), same_conv=(l15 == l10 and r_15 == r_10))
    out[k] = e; st[(e['plain15'], e['same_conv'])] += 1
json.dump(out, open('/home/claude/v15/conv15%s.json' % ('_all' if ALL else ''), 'w'), indent=0)
print('boundary functions (plain as declared in 1.5, 1.5 takes them as 1.0 did):', dict(st))

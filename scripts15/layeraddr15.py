"""layeraddr15.py: every address of the 1.0 XBE written into the layer's source (outside comments), with its 1.5
counterpart: a function start or a place inside a same function (the same offset in its 1.5 twin), data (datamap15),
or 'CHECK' where the map cannot say (inside a changed function, or no map). -> layeraddr15.json, and a table on stdout."""
import re, glob, json, bisect, csv, sys, collections
sys.path.insert(0, '/home/claude/v15')
import datamap15
M = json.load(open('/home/claude/v15/map15.json'))['funcs']
R = json.load(open('/home/claude/v15/reach15.json'))
rows = sorted((int(r[0], 16), int(r[1])) for r in csv.reader(open('/home/claude/h2decomp_src/halo2-decompiled-main/config/functions.csv'))
              if r and r[0][:1] in '0123456789abcdef' and r[1].isdigit())
starts = [a for a, n in rows]; sizes = dict(rows)
syms = [int(x, 16) for x in re.findall(r'=\s*0x([0-9a-f]+)', open('/home/claude/native/layer/retail_syms.S').read())]
datamap15.set_objects(syms)
def map_addr(a):
    if a < 0x12000: return a, 'low (header)'
    if a < 0x411000:
        i = bisect.bisect_right(starts, a) - 1
        if i < 0: return None, 'CHECK'
        f = starts[i]; off = a - f
        if off >= max(sizes[f], 1) + 16: return None, 'CHECK (between functions)'
        g = M.get('%x' % f)
        if not g or not g[0]: return None, 'CHECK (function not mapped)'
        same = g[1] == 'same' or R.get('%x' % f) == 'same'
        if off == 0: return g[0], 'function %s' % ('same' if same else g[1])
        if same: return g[0] + off, 'inside same function %x+%x' % (f, off)
        return None, 'CHECK (inside changed function %x+%x)' % (f, off)
    b, how = datamap15.map_data(a)
    return b, 'data ' + how
out = []
for f in sorted(glob.glob('/home/claude/native/layer2/*.[cSh]') + glob.glob('/home/claude/native/layer2/*.cpp')):
    if 'gfx_aa_shaders' in f or 'smaa' in f: continue
    t = open(f, errors='replace').read()
    # blank out comments, keeping offsets
    t2 = re.sub(r'/\*.*?\*/', lambda m: re.sub(r'[^\n]', ' ', m.group(0)), t, flags=re.S)
    t2 = re.sub(r'//[^\n]*', lambda m: ' ' * len(m.group(0)), t2)
    if f.endswith('.S'): t2 = re.sub(r'#[^\n]*', lambda m: m.group(0) if re.match(r'#\s*(if|ifdef|ifndef|else|endif|define|include)', m.group(0)) else ' ' * len(m.group(0)), t2)
    for m in re.finditer(r'0x([0-9a-fA-F]{5,6})\b', t2):
        a = int(m.group(1), 16)
        if not (0x11000 <= a < 0x596000): continue
        line = t2.count('\n', 0, m.start()) + 1
        b, how = map_addr(a)
        src = t.split('\n')[line - 1].strip()
        out.append(dict(file=f.split('/')[-1], line=line, a10=a, a15=b, how=how, src=src[:150]))
json.dump(out, open('/home/claude/v15/layeraddr15.json', 'w'), indent=0)
st = collections.Counter(o['how'].split(' (')[0].split(' ')[0] for o in out); print(dict(st))
for o in out:
    if o['how'].startswith('CHECK') or 'uncertain' in o['how']:
        print('%-10s %4d %06x %-40s %s' % (o['file'], o['line'], o['a10'], o['how'][:40], o['src'][:90]))

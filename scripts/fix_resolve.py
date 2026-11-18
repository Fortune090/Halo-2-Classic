"""fix_resolve.py: the clean-up after gen.py that was done by hand until s10m. A fix-up in layer/retail_tables.S
(_h2n_fixups: a code pointer in retail's data, re-aimed at our function) whose symbol no game object defines is
re-aimed from the class's vector deleting destructor (??_E...) to its scalar deleting destructor (??_G...) when that
is defined, and otherwise dropped (the slot keeps its retail address). Writes the decisions to fix_resolve.json and
says where they differ from the previous record. Run it once after every gen.py."""
import json, re, subprocess, sys
NAT = '/home/claude/native'
objs = [l.strip() for l in open(NAT + '/objects_r.rsp') if l.strip()]
# what the link defines: the game objects, the layer's objects and the mingw runtime (it provides __purecall)
import glob
objs += sorted(glob.glob(NAT + '/layer2/build/*.o')) + sorted(glob.glob(NAT + '/mingw/*.lib')) + sorted(glob.glob(NAT + '/mingw/*.obj'))
defined = set()
for i in range(0, len(objs), 200):
    out = subprocess.run(['llvm-nm', '--defined-only', '-g'] + objs[i:i + 200], capture_output=True, text=True).stdout
    for line in out.splitlines():
        p = line.split()
        if len(p) >= 3: defined.add(p[2])
path = NAT + '/layer/retail_tables.S'
S = open(path).read()
i = S.index('_h2n_fixups:\n') + len('_h2n_fixups:\n'); j = S.index('  .long 0,0', i)
entry = re.compile(r'  \.long (0x[0-9a-f]+), (0x[0-9a-f]+), "(.*)"$')
dec, out, dropped, reaimed = {}, [], 0, 0
for line in S[i:j].splitlines():
    m = entry.match(line)
    if not m: sys.exit('not a fix-up line: ' + line)
    sym = m.group(3)
    if sym not in defined:
        if sym not in dec:
            g = '??_G' + sym[4:] if sym.startswith('??_E') else None
            dec[sym] = g if g and g in defined else None
        if dec[sym] is None: dropped += 1; continue
        reaimed += 1
        line = f'  .long {m.group(1)}, {m.group(2)}, "{dec[sym]}"'
    out.append(line)
try: old = json.load(open(NAT + '/fix_resolve.json'))['resolve']
except Exception: old = {}
changed = sorted(s for s in dec if s in old and old[s] != dec[s])
open(path, 'w').write(S[:i] + ''.join(l + '\n' for l in out) + S[j:])
json.dump({'resolve': dec, 'defined_count': len(defined)}, open(NAT + '/fix_resolve.json', 'w'), indent=0)
print(f'fix-ups {len(out) + dropped}: {reaimed} re-aimed to a scalar deleting destructor, {dropped} dropped '
      f'({len(dec)} symbols; {len(changed)} decided otherwise than the previous record: {changed[:6]})')

"""make_retail_tables.py [init_delta.json] [template] [out]: native/layer/retail_tables.S from the published template
(generated/retail_tables.template.S: the XBE sections to load, with their CRCs, and the code pointers in the game's data
to re-aim, already cleaned up as fix_resolve.json records) and what retail's start-up initialisers leave in memory
(init_delta.json, made from YOUR default.xbe by scripts/emuinit.py). The initialisers' bytes come from the XBE, so they
are not published; this puts them into the template exactly as gen.py writes them."""
import sys, json
delta_path = sys.argv[1] if len(sys.argv) > 1 else '/home/claude/native/init_delta.json'
tmpl_path = sys.argv[2] if len(sys.argv) > 2 else '/home/claude/native/layer/retail_tables.template.S'
out_path = sys.argv[3] if len(sys.argv) > 3 else '/home/claude/native/layer/retail_tables.S'
delta = json.load(open(delta_path))
lines = []
total = 0
for a, hexb in delta['runs']:
    b = bytes.fromhex(hexb); total += len(b)
    lines.append(f'  .long {a:#x}, {len(b)}\n  .byte ' + ','.join(str(v) for v in b) + '\n  .balign 4\n')
tmpl = open(tmpl_path).read()
if tmpl.count('@@H2N_DELTA@@\n') != 1: sys.exit('the template has no (or more than one) @@H2N_DELTA@@ line')
open(out_path, 'w').write(tmpl.replace('@@H2N_DELTA@@\n', ''.join(lines)))
print(f'{out_path}: {len(delta["runs"])} initialiser runs, {total} bytes')

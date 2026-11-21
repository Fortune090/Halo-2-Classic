"""gen_tu.py <decomp>: writes build/gen/<stem>_tu.cpp for every source that has marked functions - the source followed
by a caller for each of its functions - exactly as the decompilation's tools/build.py does before it compiles, without
compiling anything. native_compile.py compiles those files. Run it in a tree whose sources changed (the patched tree,
after a new snapshot); it rewrites only the files whose text changed, so the objects of the others stay current."""
import sys, os, json
root = os.path.abspath(sys.argv[1])
sys.path.insert(0, os.path.join(root, 'tools')); os.chdir(root)
import build
from inventory import read_rows
src = os.path.join(root, 'src'); gen = os.path.join(root, 'build', 'gen')
os.makedirs(gen, exist_ok=True)
config = json.load(open(os.path.join(root, 'config', 'files.json'), encoding='utf-8'))
headers = [os.path.join(d, n) for base in ('include', 'src') for d, _, ns in os.walk(os.path.join(root, base)) for n in ns if n.endswith('.h')]
marked_by_path = {}
for m in build.marked_sources(root): marked_by_path.setdefault(m.path, []).append(m)
parsed = [os.path.join(src, name) for name in build._source_names(root)] + headers
classes = set().union(*(build._read(path, 'declared', build._declared) for path in parsed))
polymorphic = build._polymorphic(build._read(path, 'class bodies', build._class_bodies) for path in parsed)
outside = build.outside_callees(read_rows(os.path.join(root, 'config', 'functions.csv')))
written = same = 0
for name in build._source_names(root):
    path = os.path.join(src, name); stem = os.path.splitext(name)[0]
    marked = marked_by_path.get(f'src/{name}')
    if not marked: continue
    flags = build.file_flags(build._read(path), name, config)
    text = build.tu_source(os.path.abspath(path), marked, stem, classes, polymorphic, outside, keep_results='/GL-' in flags)
    out = os.path.join(gen, f'{stem}_tu.cpp')
    if os.path.exists(out) and open(out, encoding='utf-8').read() == text: same += 1; continue
    open(out, 'w', encoding='utf-8').write(text); written += 1
print('generated sources: written', written, 'unchanged', same)

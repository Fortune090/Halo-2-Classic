#!/usr/bin/env python3
"""closure_all.py <address>: every obstacle retail's code reaches from a function (not only the first), with one path to each"""
import sys, io, contextlib
src = open('/home/claude/scripts/unproven_routes.py').read()
core = src[:src.index("# stage 9, second rule")]
g = {}
with contextlib.redirect_stdout(io.StringIO()):
    exec(compile(core, 'ur', 'exec'), g)
rows = g['rows']; calls = g['calls']; func_of = g['func_of']; patched = g['patched']; HARD = g['HARD']; BACK = g['BACK']
import json, os
CACHE='/tmp/closure_cache.json'
cache=json.load(open(CACHE)) if os.path.exists(CACHE) else {}
def info(f):
    k='%x'%f
    if k not in cache: cache[k]=[list(g['uses'](f)), sorted(g['named'](f))]
    return cache[k]
root = int(sys.argv[1], 16)
stop = {int(a, 16) for a in sys.argv[2:]}
parent = {root: None}; stack = [root]; bad = []
while stack:
    f = stack.pop()
    if f not in rows: continue
    o = rows[f]['owner']
    if o not in ('game', 'third:havok'):
        if f in patched or f == 0x319740: continue
        if o in HARD: bad.append((f, o + ' ' + rows[f]['name']))
        continue
    if f in BACK and f != root: continue
    if f in stop: continue
    u, nm = info(f)
    if u[0]: bad.append((f, 'Direct3D inside')); continue
    if u[1]: bad.append((f, 'kernel call')); continue
    for c in list(calls.get(f, [])) + list(nm):
        t = func_of(c)
        if t is not None and t not in parent: parent[t] = f; stack.append(t)
def path(f):
    p = []
    while f is not None: p.append(f); f = parent[f]
    return ' < '.join('%x' % a for a in p[:9])
json.dump(cache, open(CACHE,'w'))
print(len(parent), 'reached;', len(bad), 'obstacles')
for f, why in sorted(bad): print('%06x %s\n      %s' % (f, why[:60], path(f)))

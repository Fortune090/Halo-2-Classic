#!/usr/bin/env python3
"""closure_check.py <addresses>: what retail's code reaches from a function, and whether it may run as retail code"""
import sys, io, contextlib
src = open('/home/claude/scripts/unproven_routes.py').read()
core = src[:src.index("# stage 9, second rule")]
g = {}
with contextlib.redirect_stdout(io.StringIO()):
    exec(compile(core, 'ur', 'exec'), g)
rows = g['rows']; calls = g['calls']; func_of = g['func_of']
def reach(root):
    seen = set(); stack = [root]
    while stack:
        f = stack.pop()
        if f in seen or f not in rows: continue
        seen.add(f)
        if rows[f]['owner'] not in ('game', 'third:havok'): continue
        if f not in g['named_memo']: g['named_memo'][f] = g['named'](f)
        for c in list(calls.get(f, [])) + list(g['named_memo'][f]):
            t = func_of(c)
            if t is not None and t not in seen: stack.append(t)
    return seen
for a in sys.argv[1:]:
    a = int(a, 16)
    bad = g['closure2'](a)
    r = reach(a)
    game = [f for f in r if rows[f]['owner'] in ('game', 'third:havok')]
    un = [f for f in game if rows[f]['status'] != 'matched']
    print('%06x: %s; reaches %d functions, %d game, %d of them not matched' % (a, bad or 'may run as retail code', len(r), len(game), len(un)))
if len(sys.argv) > 1 and sys.argv[-1].startswith('all') is False:
    pass

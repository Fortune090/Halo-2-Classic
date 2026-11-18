#!/usr/bin/env python3
"""pointer_routes.py: unproven functions that retail's DATA points to (tables of callbacks, virtual-function tables) whose
table entries stay retail's address (routes_pointer.json, read by gen_code.py).
Why this is right whatever the draft declares: a function whose address is in a table has the standard convention of its
real declaration in retail, and whoever calls through the table - retail's code, or a function of the source - passes the
arguments as the table's type says. A draft whose declaration disagrees with retail's code would be wrong for that
caller in any case. Left out: functions from which retail's code reaches the console's Direct3D, hardware-facing library
code or the kernel (closure2), and functions that are matched (the source's version is proven)."""
import sys, io, contextlib, json, os
src = open('/home/claude/scripts/unproven_routes.py').read()
core = src[:src.index("# stage 9, second rule")]
g = {}
with contextlib.redirect_stdout(io.StringIO()):
    exec(compile(core, 'ur', 'exec'), g)
rows = g['rows']; NAT = g['NAT']; taken = g['taken']; fm = g['fm'] if 'fm' in g else json.load(open(NAT + '/funcmap.json'))
routed = {e['va'] for e in json.load(open(NAT + '/routes_unproven.json'))}
out = []; bad = []; nosym = 0
for va in sorted(taken):
    r = rows.get(va)
    if not r or r['owner'] not in ('game', 'third:havok') or r['status'] == 'matched': continue
    why = g['closure2'](va)
    (bad if why else out).append(dict(va=va, status=r['status'] or 'draft', why=why, routed=va in routed))
json.dump(out, open(NAT + '/routes_pointer.json', 'w'), indent=0)
print('unproven functions in retail\'s tables:', len(out) + len(bad), '; entries left as retail\'s:', len(out), '(of them already routed whole:', sum(e['routed'] for e in out), '); not possible:', len(bad))
import collections
print(collections.Counter(e['why'].split(',')[0][-40:] for e in bad).most_common(6))

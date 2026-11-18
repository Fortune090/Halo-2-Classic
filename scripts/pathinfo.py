#!/usr/bin/env python3
"""pathinfo.py <addresses>: for each function its status, source and whether retail takes its arguments as declared"""
import sys, io, contextlib, os
os.chdir('/home/claude/native')
src = open('/home/claude/scripts/autoroute_core.py').read()
core = src[:src.index("ROUTE=[0xb7b40")]
g = {}
with contextlib.redirect_stdout(io.StringIO()):
    exec(compile(core, 'ac', 'exec'), g)
rows = g['rows']; fm = g['fm']; classify = g['classify']
for a in sys.argv[1:]:
    va = int(a, 16); r = rows.get(va); v = fm.get('%08x' % va)
    if not r: print(a, 'no row'); continue
    k = classify(va, v['name']) if v else ('-', 'no symbol')
    print('%06x %-8s %-28s %-6s %s | %s %s' % (va, r['status'] or 'draft', r['source'][4:32], r['size'], (v or {}).get('name', '')[:44], k[0], k[1]))

#!/usr/bin/env python3
# declpop15.py : adapters whose "what our own function pops" (decl_pop, read off our compiled code by adapt15 /
# backadapt15) is not what its declaration pops (the callee pops its stack arguments unless __cdecl)
import json, sys
sys.path.insert(0, '/home/claude/v15')
import declplaces
s = json.load(open('/home/claude/v15/settled15.json')); n = 0
for nm in ('forward', 'back'):
    for e in s[nm]:
        if not e.get('name') or e.get('decl_pop') is None: continue
        cc, pl = declplaces.places(e['name'])
        if cc is None or pl is None: continue
        want = 0 if cc == '__cdecl' else 4 * sum(1 for p in pl if not isinstance(p, str))
        if want != e['decl_pop']:
            n += 1; print('%-7s %6x %-8s declared pops %2d, tables say %2d  %s' % (nm, e['a10'], e['kind'], want, e['decl_pop'], e['name'][:60]))
print(n, 'differ')

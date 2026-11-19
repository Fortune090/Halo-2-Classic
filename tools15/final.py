#!/usr/bin/env python3
# final.py [-b] <1.0 addr>... : the convention gen_code15 finally uses (settled15.json), one short line each
import json, sys
s = json.load(open('/home/claude/v15/settled15.json'))
args = sys.argv[1:]; back = '-b' in args; args = [a for a in args if a != '-b']
tab = {('%x' % e['a10']): e for e in s['back' if back else 'forward']}
for a in args:
    e = tab.get(a.lower().lstrip('0x') if not a.startswith('0x') else a[2:])
    if not e: print(a, '-'); continue
    print(a, e['kind'], e.get('how'), 'regs', e['regs'], 'stack', e['stack'], 'pop', e['pop'], '/decl', e['decl_pop'], e['cc'], e['name'][:50])

#!/usr/bin/env python3
"""add_globals.py: data symbols that newly compiled source defines and that stand for retail data (their names carry
the address) are added to the placement plan and to retail_syms.S, so that rewrite.py turns their definitions into
references to retail's memory. The list is native/extra_globals.json: {mangled name: retail address}. A name the plan
already holds at another address is corrected (g_4b8478 had been placed on top of the back buffer pointer at 0x50935c
by one vote from an unmatched function)."""
import json, re
NAT = '/home/claude/native'
extra = json.load(open(NAT + '/extra_globals.json'))
# "object|vtable symbol" entries are vtables to take from retail's table (added to vtables_plan.json); the rest are globals
vt = {k: v for k, v in extra.items() if '|' in k}
extra = {k: v for k, v in extra.items() if '|' not in k}
if vt:
    vp = json.load(open(NAT + '/vtables_plan.json')); vp.update(vt); json.dump(vp, open(NAT + '/vtables_plan.json', 'w'))
    print('vtables placed by hand:', len(vt))
plan = json.load(open(NAT + '/plan.json'))
syms = open(NAT + '/layer/retail_syms.S').read()
added = 0
for name, address in extra.items():
    if name not in plan['plan']:
        plan['plan'][name] = {'retail': address, 'size': 0, 'obj': 'extra_globals', 'kind': 'global', 'comdat': False, 'bss': True}; added += 1
    elif plan['plan'][name]['retail'] != address:           # a correction: the plan had placed it wrongly
        print('corrected', name, hex(plan['plan'][name]['retail']), '->', hex(address)); plan['plan'][name]['retail'] = address
    if ('"%s" = ' % name) not in syms:
        syms += '.globl "%s"\n"%s" = %#x\n' % (name, name, address)
    else:
        syms = re.sub(r'("%s" = )0x[0-9a-f]+' % re.escape(name), lambda m: m.group(1) + '%#x' % address, syms)
json.dump(plan, open(NAT + '/plan.json', 'w'))
open(NAT + '/layer/retail_syms.S', 'w').write(syms)
print('extra globals:', len(extra), 'new in the plan:', added)

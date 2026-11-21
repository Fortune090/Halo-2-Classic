#!/usr/bin/env python3
"""keep_routes.py [the retail_code.S of the build before]: matched functions that go on running as retail's own code
(native/routes_keep.json, read by gen_code.py).

Why. gen_code.py takes a function off the route list once the decompilation has it matched: its compiled source is
retail's code. But until then everything that function reached by direct calls ran as retail's code too - retail's code
calls retail's functions - and some of what it reaches may still be an unproven draft that no rule routes (a draft that
takes its arguments in registers of the compiler's choosing cannot simply be jumped to). Compiled, the newly matched
function would call this build's functions, and those drafts would run where retail's code ran in every build so far.
For 0x186ab0 (the player control, matched on 20 Nov 2026) that is the whole look, 0x185be0 - and the mouse's hooks sit
in retail's copies of both (helpers.S h2n_look_hook at retail's 0x186b16, h2n_assist_hook at 0x1861a1).

The rule. A matched function that was a route in the build before (or is in routes_keep.json already) stays one while
at least one game or Havok function that is not matched and not a route itself can be reached from it by direct calls
through functions that are not routes. It leaves the list by itself once everything below it is matched or routed.
Run after unproven_routes.py and a first gen_code.py (it reads the routes that gen_code.py would make without it), then
gen_code.py again."""
import sys, os, re, csv, json
NAT = '/home/claude/native'
rows = {int(r['va'], 16): r for r in csv.DictReader(open('/home/claude/h2decomp_src/halo2-decompiled-main/config/functions.csv', encoding='utf-8', errors='replace'))}
def routes(path):
    t = open(path).read(); i = t.index('_h2n_routes:'); j = t.index('\n_', i + 20)
    return {int(m.group(1), 16) for m in re.finditer(r'\.long [^\n]*?, (0x[0-9a-f]+), ', t[i:j])}
keep_path = NAT + '/routes_keep.json'
old_keep = {e['va'] for e in json.load(open(keep_path))} if os.path.exists(keep_path) else set()
before = routes(sys.argv[1]) if len(sys.argv) > 1 else set()
now = routes(NAT + '/layer/retail_code.S') - old_keep           # the routes the other rules make
unproven = lambda a: rows[a]['owner'] in ('game', 'third:havok') and rows[a]['status'] != 'matched'
out = []
for g in sorted((before | old_keep) - now):
    if g not in rows or rows[g]['status'] != 'matched': continue
    seen = set(); todo = [g]; below = []
    while todo:
        a = todo.pop()
        for c in rows[a]['calls'].split():
            c = int(c, 16)
            if c in rows and c not in seen and c not in now:
                seen.add(c); todo.append(c)
                if unproven(c): below.append(c)
    if below: out.append(dict(va=g, below=sorted(below)))
json.dump(out, open(keep_path, 'w'), indent=0)
print('matched functions that stay retail code because something unproven is below them:', len(out))
for e in out: print('   %06x: %d below, e.g. %s' % (e['va'], len(e['below']), ' '.join('%x' % x for x in e['below'][:5])))

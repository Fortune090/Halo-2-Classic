#!/usr/bin/env python3
"""sg_hooks.py <obj_r dir> [check]: the Spooky Ghost Menu's hooks in the compiled game code.

On the console the mod patches `call <function>` instructions in the XBE. Here most of the game is compiled source, so
the same calls are pointed at the port's C functions (layer3/sg/sg_hooks.c) in the objects, the way scene_hook.py does
it for the anti-aliasing: the call's relocation is aimed at another external symbol. Nothing is recompiled; it is the
same as writing sg_w_...(args) in place of function_...(args) in the caller's source. The hooked function itself is
untouched and is what the wrapper calls.

native/sg_hooks.json lists the hooks: the function's symbol, the wrapper's symbol, and which callers - "all" (the mod
hooks every call of it in the XBE), or the callers' retail addresses (it hooks only those functions' calls). A call from
the function to itself is never touched (the mod does not hook those either). Only calls (REL32) are re-aimed; a
function's address stored as data stays the function's (the XBE patch does not reach those either).

The tool prints, for every hook, how many calls it re-aimed and in which functions, and stops if a listed caller has no
call of the function (the source's function then differs from the XBE's there and needs looking at)."""
import sys, os, json, struct
sys.path.insert(0, '/home/claude/scripts')
NAT = '/home/claude/native'
hooks = json.load(open(NAT + '/sg_hooks.json'))
funcmap = json.load(open(NAT + '/funcmap.json'))
name_of_va = {int(k, 16): v['name'] for k, v in funcmap.items()}
objdir = sys.argv[1]; check = len(sys.argv) > 2

def read(path):
    d = bytearray(open(path, 'rb').read())
    machine, nsec, stamp, symptr, nsym, optsize, chars = struct.unpack_from('<HHIIIHH', d, 0)
    assert machine == 0x14c, path
    strtab = symptr + nsym * 18
    assert strtab + struct.unpack_from('<I', d, strtab)[0] == len(d), (path, 'the string table is not at the end')
    syms = []; i = 0
    while i < nsym:
        o = symptr + i * 18
        if struct.unpack_from('<I', d, o)[0] == 0:
            off = struct.unpack_from('<I', d, o + 4)[0]; name = bytes(d[strtab + off:d.index(b'\0', strtab + off)]).decode('latin1')
        else: name = bytes(d[o:o + 8]).rstrip(b'\0').decode('latin1')
        value, section, typ, storage, naux = struct.unpack_from('<IhHBB', d, o + 8)
        syms.append((i, name, value, section, typ, storage))
        i += 1 + naux
    secs = []
    for k in range(nsec):
        h = 20 + optsize + k * 40
        relptr, nrel = struct.unpack_from('<I', d, h + 24)[0], struct.unpack_from('<H', d, h + 32)[0]
        secs.append((k + 1, relptr, nrel))
    return d, symptr, nsym, strtab, syms, secs

total = {h['wrapper']: [] for h in hooks}
for f in sorted(os.listdir(objdir)):
    if not f.endswith('.obj'): continue
    path = os.path.join(objdir, f)
    raw = open(path, 'rb').read()
    if not any(h['symbol'].encode('latin1') in raw for h in hooks): continue
    d, symptr, nsym, strtab, syms, secs = read(path)
    byname = {}
    for s in syms: byname.setdefault(s[1], s)
    # the functions of each section, by offset
    funcs = {}
    for i, name, value, section, typ, storage in syms:
        if section > 0 and typ == 0x20: funcs.setdefault(section, []).append((value, name))
    for v in funcs.values(): v.sort()
    def holder(section, offset):
        best = None
        for value, name in funcs.get(section, ()):
            if value <= offset: best = name
            else: break
        return best
    changed = False
    for h in hooks:
        if h['wrapper'] in byname and not check: raise SystemExit('%s: %s is already there (the hooks go on freshly rewritten objects)' % (f, h['wrapper']))
        t = byname.get(h['symbol'])
        if not t: continue
        wanted = None if h['callers'] == 'all' else {name_of_va[int(c, 16)] for c in h['callers']}
        places = []
        for section, relptr, nrel in secs:
            for r in range(nrel):
                o = relptr + r * 10
                va, sym, typ = struct.unpack_from('<IIH', d, o)
                if sym != t[0] or typ != 0x14: continue
                fn = holder(section, va)
                if fn == h['symbol']: continue                       # the function calling itself
                if fn and fn.startswith('?standin_'): continue     # the build's own stand-in callers (they make the compiler emit every function; never run)
                if wanted is not None and fn not in wanted: continue
                places.append((o, fn))
        if not places: continue
        total[h['wrapper']] += [fn for _, fn in places]
        if check: continue
        # the wrapper as a new external symbol at the end of the symbol table; its name at the end of the string table
        name = h['wrapper'].encode('latin1') + b'\0'
        strsize = struct.unpack_from('<I', d, strtab)[0]
        record = struct.pack('<IIIhHBB', 0, strsize, 0, 0, 0x20, 2, 0)
        index = nsym
        d = d[:strtab] + record + d[strtab:] + name
        nsym += 1; strtab += 18
        struct.pack_into('<I', d, 12, nsym)
        struct.pack_into('<I', d, strtab, strsize + len(name))
        for o, fn in places: struct.pack_into('<I', d, o + 4, index)
        changed = True
    if changed: open(path, 'wb').write(d)
bad = 0
for h in hooks:
    got = total[h['wrapper']]
    per = {}
    for fn in got: per[fn] = per.get(fn, 0) + 1
    print('%-14s %-28s %d call(s) in %d function(s)%s' % (h['wrapper'], h['symbol'][:28], len(got), len(per), '' if h['callers'] == 'all' else ' (listed callers: %d)' % len(h['callers'])))
    if h['callers'] != 'all':
        for c in h['callers']:
            if name_of_va[int(c, 16)] not in per: print('   *** no call of it in', c, name_of_va[int(c, 16)][:50]); bad = 1
    if not got: print('   *** nothing re-aimed'); bad = 1
    if h.get('calls') is not None and h['calls'] != len(got): print('   (the XBE has %d such calls)' % h['calls'])
sys.exit(bad)

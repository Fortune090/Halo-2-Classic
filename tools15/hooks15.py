#!/usr/bin/env python3
# hooks15.py : the game's compiled functions that call into the layer (h2n_* hooks in the source), and what each is in 1.5
import json, struct, os, collections
NAT = '/home/claude/native'
fm = {v['name']: int(k, 16) for k, v in json.load(open(NAT + '/funcmap.json')).items()}
M = json.load(open('/home/claude/v15/map15.json'))['funcs']
out = collections.defaultdict(set)
for p in [l.strip() for l in open(NAT + '/objects_r.rsp') if l.strip()]:
    d = open(p, 'rb').read()
    nsec, symptr, nsym = struct.unpack_from('<H', d, 2)[0], struct.unpack_from('<I', d, 8)[0], struct.unpack_from('<I', d, 12)[0]
    strtab = symptr + nsym * 18
    def symname(i):
        o = symptr + 18 * i
        if d[o:o + 4] == b'\0\0\0\0':
            so = struct.unpack_from('<I', d, o + 4)[0]; e = d.index(b'\0', strtab + so); return d[strtab + so:e].decode('latin1')
        return d[o:o + 8].rstrip(b'\0').decode('latin1')
    hooks = {i for i in range(nsym) if 'h2n_' in symname(i)} if b'h2n_' in d else set()
    if not hooks: continue
    funcs = collections.defaultdict(list); i = 0
    while i < nsym:
        o = symptr + 18 * i; val, sec, typ = struct.unpack_from('<IhH', d, o + 8)
        if sec > 0 and typ == 0x20: funcs[sec].append((val, symname(i)))
        i += 1 + d[o + 17]
    for si in range(nsec):
        h = 20 + 40 * si
        relp, nrel = struct.unpack_from('<I', d, h + 24)[0], struct.unpack_from('<H', d, h + 32)[0]
        L = sorted(funcs.get(si + 1, []))
        for r in range(nrel):
            off, sidx, typ = struct.unpack_from('<IIH', d, relp + 10 * r)
            if sidx in hooks:
                f = [n for v, n in L if v <= off]
                out[(os.path.basename(p), f[-1] if f else '?')].add(symname(sidx).lstrip('_'))
bad = 0
for (obj, fn), hs in sorted(out.items()):
    a = fm.get(fn); s = M.get('%x' % a) if a else None
    st = s[1] if s else ('-' if a is None else 'none')
    if st != 'same': bad += 1
    if st != 'same' or '-a' in os.sys.argv: print('%-9s %-22s %-44s %s' % (st, obj[:22], fn[:44], ' '.join(sorted(hs))[:70]))
print(len(out), 'functions call into the layer;', bad, 'of them are not the compiled source in 1.5')

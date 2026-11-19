#!/usr/bin/env python3
# copies15.py : compiled functions that stay ours in the 1.5 link without being a strict twin of a 1.5 function (file-local
# copies, helpers without a retail address) and that hold one of the structure sizes 1.5 changed (0xc70, 0x528)
import json, struct, os, re, sys, collections
NAT = '/home/claude/native'
fm = {v['name']: int(k, 16) for k, v in json.load(open(NAT + '/funcmap.json')).items()}
M = json.load(open('/home/claude/v15/map15.json'))['funcs']
same = {int(k, 16) for k, v in M.items() if v and v[1] == 'same'}
routed = set(re.findall(r'^\s*\.long "([^"]+)", (?:AV|TV|0x)', open('/home/claude/native15/layer/retail_code.S').read(), re.M))
pats = {b'\x70\x0c\x00\x00': '0xc70', b'\x28\x05\x00\x00': '0x528'}
out = collections.Counter()
for p in [l.strip() for l in open(NAT + '/objects_r.rsp') if l.strip()]:
    d = open(p.replace('/native/obj_r/', '/native15/obj_r/'), 'rb').read()
    nsec, symptr, nsym = struct.unpack_from('<H', d, 2)[0], struct.unpack_from('<I', d, 8)[0], struct.unpack_from('<I', d, 12)[0]
    strtab = symptr + nsym * 18
    def symname(i):
        o = symptr + 18 * i
        if d[o:o + 4] == b'\0\0\0\0':
            so = struct.unpack_from('<I', d, o + 4)[0]; e = d.index(b'\0', strtab + so); return d[strtab + so:e].decode('latin1')
        return d[o:o + 8].rstrip(b'\0').decode('latin1')
    by = collections.defaultdict(list); i = 0
    while i < nsym:
        o = symptr + 18 * i
        val, sec, typ, cls, aux = struct.unpack_from('<IhHBB', d, o + 8)
        if sec > 0 and typ == 0x20: by[sec].append((val, symname(i), cls))
        i += 1 + aux
    for sec, L in by.items():
        h = 20 + 40 * (sec - 1); size, raw = struct.unpack_from('<II', d, h + 16)
        L.sort()
        for k, (val, nm, cls) in enumerate(L):
            end = L[k + 1][0] if k + 1 < len(L) else size
            body = d[raw + val:raw + end]
            hit = [t for pt, t in pats.items() if pt in body]
            if not hit: continue
            a10 = fm.get(nm)
            if cls == 2 and a10 in same: continue                 # a strict twin: the number is 1.5's too
            if cls == 2 and nm in routed: continue                # runs as 1.5's own code
            kind = 'static' if cls == 3 else 'global'
            print('%-22s %-7s %-6s %s  retail %s %s' % (os.path.basename(p), kind, ','.join(hit), nm[:60], ('%x' % a10) if a10 else '-',
                  '' if a10 is None else ('same' if a10 in same else ('routed' if nm in routed else M.get('%x' % a10, ['', '?'])[1]))))
            out[kind] += 1
print(dict(out))

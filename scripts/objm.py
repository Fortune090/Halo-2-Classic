#!/usr/bin/env python3
"""objm.py <obj 1.0 dir> <obj 1.5 dir> <out dir> <objects_r.rsp 1.0> <out rsp>: the 1.0 objects for the one program that
runs both versions. A 1.5 object differs from its 1.0 object in field offsets (section bytes) and in which file-local
functions are visible to the tables (symbol storage class 2, and a file-local copy's own name); the 1.0 object gets
the same visibility and names, nothing else, so
that both sets link to the same layout and the same table names resolve in both."""
import os, sys, struct, filecmp
a, b, out, rsp, orsp = sys.argv[1:6]
os.makedirs(out, exist_ok=True)
changed = {}
for f in sorted(os.listdir(a)):
    pa, pb = os.path.join(a, f), os.path.join(b, f)
    if filecmp.cmp(pa, pb, shallow=False): continue
    da, db = bytearray(open(pa, 'rb').read()), open(pb, 'rb').read()
    assert len(da) == len(db), f
    ptr, n = struct.unpack_from('<II', da, 8)
    assert struct.unpack_from('<II', db, 8) == (ptr, n), f
    i = 0; k = 0
    while i < n:
        o = ptr + 18 * i
        if da[o + 16] != db[o + 16]:
            assert db[o + 16] == 2 and da[o + 16] == 3, (f, i, da[o + 16], db[o + 16])
            da[o + 16] = 2; k += 1
        i += 1 + da[o + 17]
    st = ptr + 18 * n                                     # (a file-local copy's own name: syms15.py's STATIC_COPY)
    if da[st:] != db[st:]: da[st:] = db[st:]; k += 1
    if k:
        open(os.path.join(out, f), 'wb').write(da); changed[f] = k
lines = []
for l in open(rsp).read().split('\n'):
    f = os.path.basename(l.strip().strip('"'))
    lines.append(l.replace('/obj_r/', '/' + os.path.basename(out.rstrip('/')) + '/') if f in changed else l)
open(orsp, 'w').write('\n'.join(lines))
print('objects with functions made visible: %d (%d symbols); the rest are the 1.0 objects' % (len(changed), sum(changed.values())))

#!/usr/bin/env python3
"""libsig.py: names for retail's library functions that no matched function calls, taken from the SDK's own libraries.
Each function of an SDK library is a section of its own. A retail library function gets a library function's name when
the bytes are the same with the relocated fields left out AND every function it calls or names matches the same way
(followed to depth 6), and when exactly one name fits. Writes native/libsig.json {retail address: [name, where]}."""
import sys, struct, json, os, tempfile, csv, collections
sys.path.insert(0, '/home/claude/scripts')
from coff import Coff
import xbeimg
NAT = '/home/claude/native'
LIBS = [a for a in sys.argv[1:] if a != '-v'] or ['dsound', 'xapilib', 'xonlines', 'xvoice', 'xnets', 'xnet']
OWN = {'dsound': 'xdk:dsound', 'xapilib': 'xdk:xapi', 'xonlines': 'xdk:x', 'xvoice': 'xdk:xvoice', 'xnets': 'xdk:xnet', 'xnet': 'xdk:xnet', 'xonline': 'xdk:x'}
o2 = json.load(open(NAT + '/oracle2.json'))['libnames']
x = xbeimg.XbeImage('/home/claude/xbe/default.xbe')
rows = {int(r['va'], 16): r for r in csv.DictReader(open('/home/claude/functions_upstream.csv'))}
import bisect
starts = sorted(rows)
def members(path):
    d = open(path, 'rb').read(); assert d[:8] == b'!<arch>\n'
    p = 8; longnames = b''
    while p + 60 <= len(d):
        h = d[p:p + 60]; size = int(h[48:58]); name = h[:16].decode('latin1').rstrip()
        body = d[p + 60:p + 60 + size]; p += 60 + size + (size & 1)
        if name == '//': longnames = body; continue
        if name in ('/', ''): continue
        if name.startswith('/') and name[1:].isdigit():
            o = int(name[1:]); name = longnames[o:longnames.index(b'\0', o)].decode('latin1')
        yield name.rstrip('/'), body
out = {}
tmp = tempfile.mkdtemp()
for lib in LIBS:
    funcs = {}          # name -> (data, mask, [(offset, type, target name or None)], where)
    for mname, body in members('/home/claude/sdk/xbox/lib/%s.lib' % lib):
        if len(body) < 20 or struct.unpack_from('<H', body, 0)[0] != 0x14c: continue
        f = os.path.join(tmp, 'm.obj'); open(f, 'wb').write(body)
        try: c = Coff(f)
        except Exception: continue
        for s in c.sections:
            if s.data is None or not (s.flags & 0x20) or not s.rawsize: continue
            syms = [y for y in s.symbols if y.storage == 2 and y.value == 0]
            if not syms: continue
            mask = bytearray(b'\xff' * s.rawsize); rel = []
            for va, symi, typ in s.relocs:
                if typ in (6, 7, 0x14):
                    for k in range(4):
                        if va + k < s.rawsize: mask[va + k] = 0
                    t = c.by_index.get(symi)
                    rel.append((va, typ, t.name if t is not None and t.storage == 2 else None))
            for y in syms: funcs.setdefault(y.name, (s.data, bytes(mask), rel, '%s:%s' % (lib, os.path.basename(mname.replace('\\', '/')))))
    memo = {}
    def fits(name, a, depth=0):
        k = (name, a)
        if k in memo: return memo[k]
        if name not in funcs: return None          # data or an import: no opinion
        data, mask, rel, where = funcs[name]
        theirs = x.read(a, len(data))
        if theirs is None or any((p ^ q) & m for p, q, m in zip(data, theirs, mask)): memo[k] = False; return False
        memo[k] = True
        if depth < 6:
            for off, typ, t in rel:
                if t is None or t not in funcs: continue
                v = struct.unpack_from('<I', theirs, off)[0]
                ta = (a + off + 4 + v) & 0xffffffff if typ == 0x14 else v
                if fits(t, ta, depth + 1) is False: memo[k] = False; return False
        return True
    by_first = collections.defaultdict(list)
    for name, (data, mask, rel, where) in funcs.items(): by_first[bytes(p & m for p, m in zip(data[:4], mask[:4]))].append(name)
    new = amb = 0
    # besides each library function's start, every address inside library code that some function calls: the csv keeps
    # runs of small entry points (sendto, recvfrom, ...) as one row
    cand = {a: r for a, r in rows.items()}
    for va, r in rows.items():
        for c in r['calls'].split():
            c = int(c, 16)
            if c not in cand:
                i = bisect.bisect_right(starts, c) - 1
                if i >= 0 and c < starts[i] + int(rows[starts[i]]['size']): cand[c] = rows[starts[i]]
    for a, r in cand.items():
        if not r['owner'].startswith(OWN[lib]) or '%08x' % a in o2: continue
        head = x.read(a, 4)
        if head is None: continue
        got = []
        for name, (data, mask, rel, where) in funcs.items():
            if any((p ^ q) & m for p, q, m in zip(data[:4], head, mask[:4])): continue
            if fits(name, a): got.append(name)
        if len(got) == 1 and a not in out: out[a] = (got[0], funcs[got[0]][3]); new += 1
        elif got:
            amb += 1
            if '-v' in sys.argv or a in (0x37b6df, 0x37c70a): print('   several: %x'%a, got[:6])
    # the names matched code already gave must come out the same: a check on the method
    agree = differ = 0
    for k, (name, where) in o2.items():
        if where.split(':')[0] != lib or name not in funcs: continue
        if fits(name, int(k, 16)): agree += 1
        else: differ += 1; print('   contradicted:', k, name, where)
    print('%-9s library functions %5d   new names %4d   more than one name fits %4d   known names confirmed %d, contradicted %d' % (lib, len(funcs), new, amb, agree, differ))
json.dump({'%08x' % a: v for a, v in sorted(out.items())}, open(NAT + '/libsig.json', 'w'), indent=0)

"""syms15.py: the 1.5 build's data symbols and field references.
 1. native15/layer/retail_syms.S: every symbol of retail_syms.S at its 1.5 address (datamap15.map_data).
 2. native15/obj_r: the rewritten objects again, where a reference to a retail symbol plus an offset (a field) lands on
    a 1.5 address other than symbol + the same offset: the field moved inside its object (1.5 grew some structures,
    g_network_configuration among them). Only for fields same code uses (a vote), so the reference is the address 1.5's
    own same function uses there. Prints what it changed."""
import re, os, sys, struct, json, shutil, collections
sys.path.insert(0, '/home/claude/v15')
import datamap15
NAT = '/home/claude/native'; N15 = '/home/claude/native15'
os.makedirs(N15 + '/layer', exist_ok=True); os.makedirs(N15 + '/obj_r', exist_ok=True)
txt = open(NAT + '/layer/retail_syms.S').read()
syms = {}
for m in re.finditer(r'^\s*(".*?"|[^\s=]+)\s*=\s*0x([0-9a-f]+)', txt, re.M): syms[m.group(1).strip('"')] = int(m.group(2), 16)
datamap15.set_objects(syms.values())
s15 = {}; st = collections.Counter(); report = []
SLOTS = {int(k, 16): v for k, v in json.load(open('/home/claude/v15/slots15.json'))['slots'].items()}
VOTES = {int(k, 16): v for k, v in datamap15.M['data'].items()}
# (s10s) a vtable is where 1.5's table holds its functions slot for slot (vt15.py): that comes first
VT = {int(k, 16): v[0] for k, v in json.load(open('/home/claude/v15/vt15.json')).items()} if os.path.exists('/home/claude/v15/vt15.json') else {}
for n, a in syms.items():
    # an address same code uses (a vote) first, then the word pairing of the two versions' data (datalign15.py: tables
    # and vtables that look alike once their pointers are masked can otherwise land on one another - 0x4138d4's
    # listener vtables did), then the data map
    if a in VT: b, how = VT[a], 'its own table'
    elif a in VOTES: b, how = VOTES[a], 'vote'
    elif a in SLOTS: b, how = SLOTS[a], 'paired word'
    else: b, how = datamap15.map_data(a)
    s15[n] = b; st[how] += 1
    if how in ('uncertain', 'outside'): report.append('symbol %s 0x%x -> 0x%x (%s)' % (n, a, b, how))
out = re.sub(r'^(\s*(".*?"|[^\s=]+)\s*=\s*)0x([0-9a-f]+)', lambda m: m.group(1) + '0x%x' % s15[m.group(2).strip('"')], txt, flags=re.M)
out = out.replace('at the retail address */', 'at the retail address (v1.5: syms15.py from map15.json) */', 1)
open(N15 + '/layer/retail_syms.S', 'w').write(out)
print('symbols:', dict(st))
_by = collections.defaultdict(set)
for n, a in syms.items(): _by[s15[n]].add(a)
_coll = {b: sorted(a) for b, a in _by.items() if len(a) > 1}
print('1.5 addresses that two different 1.0 addresses map to:', len(_coll), [(hex(b), [hex(x) for x in a]) for b, a in list(_coll.items())[:6]])
votes = {int(k, 16): v for k, v in datamap15.M['data'].items()}
# 2. the field references
fixed = collections.Counter(); nref = 0; nabs = [0]
SLOTS = {int(k, 16): v for k, v in json.load(open('/home/claude/v15/slots15.json'))['slots'].items()}
# static functions the 1.5 routes name (a changed function our same code calls can be file-local): made visible
_A = json.load(open('/home/claude/v15/adapt15.json')); _B = json.load(open('/home/claude/v15/boundary15.json'))
_fm = {int(k, 16): v for k, v in json.load(open(NAT + '/funcmap.json')).items()}
make_global = {e['name'] for e in _A.values()} | {_fm[int(k, 16)]['name'] for k in _B if int(k, 16) in _fm}
nglobal = 0
objs = [l.strip() for l in open(NAT + '/objects_r.rsp') if l.strip()]
# (s10r) a file-local copy of a function that another file defines for everyone (a "static inline" second definition,
# compiled out of line, with a convention of the compiler's choosing) cannot take the same visible name: it gets a name
# of its own, of the same length, and gen_code15.py's STATIC_COPIES routes it to 1.5's function from the copy's own
# convention. Each is listed by hand, after reading the copy's body; one that is not listed stops the run.
STATIC_COPY = {('unknown_29f5b0.obj', '?player_slot_get@@YIPAUs_player_slot_view@@J@Z'): '?player_slot_geT@@YIPAUs_player_slot_view@@J@Z'}
def _defined_globals():
    g = collections.defaultdict(set)
    for p in objs:
        d = open(p, 'rb').read()
        symptr, nsym = struct.unpack_from('<II', d, 8); strtab = symptr + nsym * 18; i = 0
        while i < nsym:
            o = symptr + 18 * i
            if d[o + 16] == 2 and struct.unpack_from('<h', d, o + 12)[0] > 0 and struct.unpack_from('<H', d, o + 14)[0] == 0x20:
                if d[o:o + 4] == b'\0\0\0\0':
                    so = struct.unpack_from('<I', d, o + 4)[0]; e = d.index(b'\0', strtab + so); nm = d[strtab + so:e].decode('latin1')
                else: nm = d[o:o + 8].rstrip(b'\0').decode('latin1')
                g[nm].add(os.path.basename(p))
        
            i += 1 + d[o + 17]
    return g
GLOBALS = _defined_globals(); ncopy = 0
SIZE_PATCH = {'?player_slot_profile_get@@YIPAUs_player_slot_profile@@J@Z', '?v10@c_main_menu_screen@@UAE_NPAUs_widget_event@@@Z',
              '?main_menu_list_choose_xbox_live@@YGXPAVc_main_menu_list@@PAPAUs_controller_reference@@@Z'}
npatch = collections.Counter()
new = []
for p in objs:
    d = bytearray(open(p, 'rb').read())
    nsec, symptr, nsym = struct.unpack_from('<H', d, 2)[0], struct.unpack_from('<I', d, 8)[0], struct.unpack_from('<I', d, 12)[0]
    strtab = symptr + nsym * 18
    def symname(i):
        o = symptr + 18 * i
        if d[o:o + 4] == b'\0\0\0\0':
            so = struct.unpack_from('<I', d, o + 4)[0]; e = d.index(b'\0', strtab + so); return d[strtab + so:e].decode('latin1')
        return d[o:o + 8].rstrip(b'\0').decode('latin1')
    names = {}
    changed = False
    # absolute symbols the rewrite put into the object itself (statics_plan, vtables_plan, function-local statics):
    # at their 1.5 places too (a slot datalign15 pairs first, else the data map)
    absval = {}
    for i in range(nsym):
        o = symptr + 18 * i
        if struct.unpack_from('<h', d, o + 12)[0] == -1:
            v = struct.unpack_from('<I', d, o + 8)[0]
            if 0x11000 <= v < 0x586000:
                v15 = VT.get(v) or SLOTS.get(v)
                if v15 is None: v15, how = datamap15.map_data(v)
                struct.pack_into('<I', d, o + 8, v15); changed = True; nabs[0] += 1
                absval[i] = (v, v15)
    for i in range(nsym):
        o = symptr + 18 * i
        if d[o + 16] == 3 and struct.unpack_from('<h', d, o + 12)[0] > 0 and struct.unpack_from('<H', d, o + 14)[0] == 0x20:
            if symname(i) in make_global:
                nm0 = symname(i)
                if GLOBALS.get(nm0):
                    key = (os.path.basename(p), nm0)
                    if key not in STATIC_COPY: sys.exit('syms15: %s has a file-local copy of %s, which %s defines: list it in STATIC_COPY' % (key[0], nm0, sorted(GLOBALS[nm0])))
                    assert d[o:o + 4] == b'\0\0\0\0' and len(STATIC_COPY[key]) == len(nm0)
                    so = strtab + struct.unpack_from('<I', d, o + 4)[0]
                    d[so:so + len(nm0)] = STATIC_COPY[key].encode('latin1'); ncopy += 1
                d[o + 16] = 2; changed = True; nglobal += 1
    # (s10r) the player slot's size, in functions that stay ours although 1.5 changed them (strict_lib.KEPT) and in the
    # out-of-line helper they call (retail has it inline; no function that is the same in 1.5 can be calling it)
    fsyms = collections.defaultdict(list); i = 0
    while i < nsym:
        o = symptr + 18 * i
        val, sec, typ = struct.unpack_from('<IhH', d, o + 8)
        if sec > 0 and typ == 0x20: fsyms[sec].append((val, symname(i)))
        i += 1 + d[o + 17]
    for sec, L in fsyms.items():
        h = 20 + 40 * (sec - 1); ssize, sraw = struct.unpack_from('<II', d, h + 16); L.sort()
        for k, (val, nm) in enumerate(L):
            if nm not in SIZE_PATCH: continue
            end = L[k + 1][0] if k + 1 < len(L) else ssize
            q = sraw + val
            while True:
                q = d.find(b'\x70\x0c\x00\x00', q, sraw + end)
                if q < 0: break
                if d[q - 2] == 0x69 and d[q - 1] >= 0xc0:           # imul r32, r32, 0xc70
                    d[q:q + 4] = struct.pack('<I', 0xc74); changed = True; npatch[nm] += 1
                q += 4
    for si in range(nsec):
        h = 20 + 40 * si
        raw, relp, nrel = struct.unpack_from('<I', d, h + 20)[0], struct.unpack_from('<I', d, h + 24)[0], struct.unpack_from('<H', d, h + 32)[0]
        flags = struct.unpack_from('<I', d, h + 36)[0]
        if flags & 0x01000000 and nrel == 0xffff: nrel = struct.unpack_from('<I', d, relp)[0]; relp += 10
        for r in range(nrel):
            off, sidx, typ = struct.unpack_from('<IIH', d, relp + 10 * r)
            if typ != 6: continue                    # IMAGE_REL_I386_DIR32
            if sidx not in names: names[sidx] = symname(sidx)
            n = names[sidx]
            if sidx in absval: base10, base15 = absval[sidx]
            elif n in syms: base10, base15 = syms[n], s15[n]
            else: continue
            nref += 1
            add = struct.unpack_from('<i', d, raw + off)[0]
            if add == 0: continue
            a10 = base10 + add
            v = votes.get(a10)
            if v is None or v == base15 + add: continue
            struct.pack_into('<i', d, raw + off, v - base15); changed = True
            fixed[n] += 1
            if len(report) < 400: report.append('field %s+0x%x: 0x%x -> 0x%x (not 0x%x) in %s' % (n, add, a10, v, base15 + add, os.path.basename(p)))
    q = N15 + '/obj_r/' + os.path.basename(p)
    if changed: open(q, 'wb').write(d)
    else: shutil.copyfile(p, q)
    new.append(q)
open(N15 + '/objects_r.rsp', 'w').write('\n'.join(new) + '\n')
open('/home/claude/v15/syms15_report.txt', 'w').write('\n'.join(report) + '\n')
print('player slot size written into kept functions:', dict((k[:40], v) for k, v in npatch.items()))
print('static functions made visible for the 1.5 routes:', nglobal, '; absolute symbols inside the objects moved:', nabs[0])
print('references to retail symbols:', nref, 'fields moved inside their object:', sum(fixed.values()), 'in', len(fixed), 'objects:',
      ', '.join('%s %d' % (k[:40], v) for k, v in fixed.most_common(8)))

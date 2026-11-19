"""datalign15.py: line up 1.0's and 1.5's initialised data word by word (difflib over tokens: a pointer to code is
known by the first 16 bytes of the code it points to, with address fields masked; a pointer into the data is just
'data'; anything else is its value), so that tables and vtables pair slot for slot even where no code names them.
Writes slots15.json {"slots": {s10: s15}, "code": {t10: t15}} for the paired code pointers."""
import json, struct, sys, difflib, collections
sys.path.insert(0, '/home/claude/v15')
from fmap_lib import load, mask
D10, S10 = load('/home/claude/xbe/default.xbe'); D15, S15 = load('/home/claude/v15/default_v15.xbe')
def reader(D, S):
    def rd(va, n):
        for s in S:
            if s['va'] <= va and va + n <= s['va'] + s['rs']:
                o = s['raw'] + va - s['va']; return D[o:o + n]
        return None
    return rd
r10, r15 = reader(D10, S10), reader(D15, S15)
CODE10, CODE15 = (0x12000, 0x411000), (0x12000, 0x41b600)
def tokens(D, S, sec, rd, code):
    s = [x for x in S if x['name'] == sec][0]
    b = D[s['raw']:s['raw'] + s['rs']]; out = []
    for i in range(0, len(b) - 3, 4):
        w = struct.unpack_from('<I', b, i)[0]
        if code[0] <= w < code[1]:
            c = rd(w, 16)
            out.append(('C', bytes(x if y else 0 for x, y in zip(c, mask(c)))) if c else ('C',))
        elif 0x10000 <= w < 0x600000: out.append(('D',))
        else: out.append(('V', w))
    return s['va'], out
slots = {}; code = collections.defaultdict(collections.Counter)
for sec in ('.rdata', '.data', '.data1'):
    a10, t10 = tokens(D10, S10, sec, r10, CODE10); a15, t15 = tokens(D15, S15, sec, r15, CODE15)
    sm = difflib.SequenceMatcher(None, t10, t15, autojunk=False)
    n = 0
    for i, j, k in sm.get_matching_blocks():
        for q in range(k):
            s10 = a10 + 4 * (i + q); s15 = a15 + 4 * (j + q); slots[s10] = s15; n += 1
            if t10[i + q][0] == 'C':
                v10 = struct.unpack('<I', r10(s10, 4))[0]; v15 = struct.unpack('<I', r15(s15, 4))[0]
                code[v10][v15] += 1
    print(sec, 'words', len(t10), len(t15), 'paired', n, flush=True)
json.dump({'slots': {'%x' % a: b for a, b in slots.items()},
           'code': {'%x' % a: c.most_common(1)[0][0] for a, c in code.items()},
           'code_ties': {'%x' % a: {('%x' % x): y for x, y in c.items()} for a, c in code.items() if len(c) > 1}},
          open('/home/claude/v15/slots15.json', 'w'))

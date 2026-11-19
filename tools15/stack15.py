# stack15.py <stop.bin> <thread id> [n]: return addresses on a thread's stack that point into 1.5 retail code,
# each named by the 1.0 function it maps to (map15.json funcs inverted) and the 1.5 function start.
import sys, json, struct, subprocess, bisect
sys.path.insert(0, '/home/claude/scripts')
stop, tid = sys.argv[1], sys.argv[2]
n = int(sys.argv[3]) if len(sys.argv) > 3 else 600
m = json.load(open('/home/claude/v15/map15.json'))['funcs']
inv = {}
for k, (v, kind) in m.items():
    if v is not None: inv.setdefault(v, (int(k, 16), kind))
starts = sorted(inv)
xbe = open('/home/claude/v15/default_v15.xbe', 'rb').read()
# section table of the XBE (to read code bytes before a return address)
base = struct.unpack_from('<I', xbe, 0x104)[0]
nsec = struct.unpack_from('<I', xbe, 0x11c)[0]; sh = struct.unpack_from('<I', xbe, 0x120)[0] - base
secs = []
for i in range(nsec):
    fl, va, vs, ra, rs = struct.unpack_from('<IIIII', xbe, sh + i * 56)
    secs.append((va, vs, ra, rs))
def byte_at(va):
    for s_va, vs, ra, rs in secs:
        if s_va <= va < s_va + min(vs, rs): return xbe[ra + va - s_va]
    return None
def is_ret(va):
    b5 = byte_at(va - 5)
    if b5 == 0xe8: return True
    for k in (2, 3, 6, 7):
        b = byte_at(va - k)
        if b == 0xff:
            mod = byte_at(va - k + 1)
            if mod is not None and (mod >> 3) & 7 == 2: return True
    return False
out = subprocess.run(['python3', '/home/claude/scripts/readdump.py', stop, '/home/claude/native15/out/halo2_native15.exe',
                      '/home/claude/native15/out/halo2_native15.map', '--stack', tid, str(n)], capture_output=True, text=True).stdout
for line in out.splitlines():
    parts = line.split()
    if len(parts) < 2: continue
    try: off = parts[0] + parts[1]; w = int(parts[2], 16)
    except ValueError: continue
    if 0x11000 <= w < 0x420000 and is_ret(w):
        i = bisect.bisect_right(starts, w) - 1
        s15 = starts[i] if i >= 0 else 0
        f10, kind = inv.get(s15, (0, '?'))
        print('%s %08x  1.5 fn %06x+%x  = 1.0 %06x (%s)' % (off, w, s15, w - s15, f10, kind))

# kcalls15.py <1.5 function va hex>...: the kernel imports reachable from these XAPI functions (direct calls followed
# inside 0x2d0000-0x2e2000; each function disassembled until its last ret within 0x600 bytes)
import sys, re, subprocess, struct, tempfile, os
rc = open('/home/claude/native15/layer/retail_code.S').read()
table = int(re.search(r'_h2n_kernel_table: \.long (0x[0-9a-f]+)', rc).group(1), 16)
labels = dict(re.findall(r'^(LC\d+): \.asciz "([^"]*)"', rc, re.M))
imports = re.findall(r'^  \.long (\d+), (LC\d+), KT\d+', rc[rc.index('_h2n_kernel_imports:'):], re.M)
slot = {table + 4 * i: (int(o), labels[l]) for i, (o, l) in enumerate(imports)}
xbe = open('/home/claude/v15/default_v15.xbe', 'rb').read()
base = struct.unpack_from('<I', xbe, 0x104)[0]
nsec = struct.unpack_from('<I', xbe, 0x11c)[0]; sh = struct.unpack_from('<I', xbe, 0x120)[0] - base
secs = [struct.unpack_from('<IIIII', xbe, sh + i * 56) for i in range(nsec)]
def dis(va0, n=0x600):
    for fl, va, vs, ra, rs in secs:
        if va <= va0 < va + rs:
            data = xbe[ra + va0 - va: ra + va0 - va + n]; break
    fd, tmp = tempfile.mkstemp(); os.write(fd, data); os.close(fd)
    out = subprocess.run(['objdump', '-D', '-b', 'binary', '-mi386', '-Mintel', '--adjust-vma=0x%x' % va0, tmp], capture_output=True, text=True).stdout
    os.unlink(tmp)
    return [l for l in out.splitlines()[7:] if ':' in l]
seen, todo, used = set(), [int(a, 16) for a in sys.argv[1:]], {}
while todo:
    f = todo.pop()
    if f in seen: continue
    seen.add(f)
    lines = dis(f)
    # cut at the last ret before a run of int3/nop padding or the next function's prologue
    end = len(lines)
    for i, l in enumerate(lines):
        if re.search(r'\tret', l):
            nxt = lines[i + 1] if i + 1 < len(lines) else ''
            if re.search(r'\t(int3|nop|push   ebp|mov    edi,edi)', nxt) or True:
                end = i + 1
                # keep going if a jump inside the function targets beyond this ret
                tail = [int(m, 16) for l2 in lines[:i] for m in re.findall(r'\tj\w+\s+0x([0-9a-f]+)', l2)]
                here = int(l.split(':')[0], 16)
                if not any(t > here for t in tail): break
    for l in lines[:end]:
        m = re.search(r'call\s+0x([0-9a-f]+)', l)
        if m:
            t = int(m.group(1), 16)
            if 0x2d0000 <= t < 0x2e2000: todo.append(t)
        for m in re.finditer(r'ds:0x([0-9a-f]+)', l):
            a = int(m.group(1), 16)
            if a in slot: used.setdefault(slot[a], set()).add(f)
for (o, n), fs in sorted(used.items(), key=lambda x: x[0][1]):
    print('%-36s #%-3d from %s' % (n, o, ' '.join('%x' % x for x in sorted(fs))))
print('functions walked:', ' '.join('%x' % x for x in sorted(seen)))

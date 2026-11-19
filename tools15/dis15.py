# dis15.py <va hex> <length> [xbe]: disassemble the 1.5 (or given) XBE at a virtual address
import sys, struct, subprocess, tempfile, os
va0, n = int(sys.argv[1], 16), int(sys.argv[2], 0)
path = sys.argv[3] if len(sys.argv) > 3 else '/home/claude/v15/default_v15.xbe'
xbe = open(path, 'rb').read()
base = struct.unpack_from('<I', xbe, 0x104)[0]
nsec = struct.unpack_from('<I', xbe, 0x11c)[0]; sh = struct.unpack_from('<I', xbe, 0x120)[0] - base
for i in range(nsec):
    fl, va, vs, ra, rs = struct.unpack_from('<IIIII', xbe, sh + i * 56)
    if va <= va0 < va + rs:
        data = xbe[ra + va0 - va: ra + va0 - va + n]
        break
else: raise SystemExit('not in a section')
fd, tmp = tempfile.mkstemp(); os.write(fd, data); os.close(fd)
out = subprocess.run(['objdump', '-D', '-b', 'binary', '-mi386', '-Mintel', '--adjust-vma=0x%x' % va0, tmp], capture_output=True, text=True).stdout
os.unlink(tmp)
print('\n'.join(l for l in out.splitlines()[7:]))

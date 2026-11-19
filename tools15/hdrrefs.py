# hdrrefs.py <xbe>: every instruction in the XBE's code sections whose operand is an address in the XBE header (0x10000-0x10fff)
import sys, struct, subprocess, tempfile, os, re
path = sys.argv[1]
xbe = open(path, 'rb').read()
base = struct.unpack_from('<I', xbe, 0x104)[0]
nsec = struct.unpack_from('<I', xbe, 0x11c)[0]; sh = struct.unpack_from('<I', xbe, 0x120)[0] - base
for i in range(nsec):
    fl, va, vs, ra, rs, nameaddr = struct.unpack_from('<IIIIII', xbe, sh + i * 56)
    name = xbe[nameaddr - base: xbe.index(b'\0', nameaddr - base)].decode()
    if not fl & 4: continue          # executable sections only
    data = xbe[ra: ra + rs]
    fd, tmp = tempfile.mkstemp(); os.write(fd, data); os.close(fd)
    out = subprocess.run(['objdump', '-D', '-b', 'binary', '-mi386', '-Mintel', '--adjust-vma=0x%x' % va, tmp], capture_output=True, text=True).stdout
    os.unlink(tmp)
    for line in out.splitlines():
        m = re.search(r'ds:0x(10[0-9a-f]{3})\b', line)
        if m and 0x10000 <= int(m.group(1), 16) < 0x11000:
            print(name, line.strip()[:110])

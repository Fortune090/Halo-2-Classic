import sys
a = open(sys.argv[1], 'rb').read()
b = open(sys.argv[2], 'rb').read() if len(sys.argv) > 2 else None
def dump(d, name):
    print('---', name, len(d), 'bytes')
    for o in range(0, len(d), 16):
        row = d[o:o + 16]
        print('%04x  %-48s %s' % (o, ' '.join('%02x' % c for c in row), ''.join(chr(c) if 32 <= c < 127 else '.' for c in row)))
dump(a, sys.argv[1])
if b is not None:
    dump(b, sys.argv[2])
    diffs = [i for i in range(min(len(a), len(b))) if a[i] != b[i]]
    print('--- differing offsets:', len(diffs))
    runs = []
    for i in diffs:
        if runs and i == runs[-1][1] + 1: runs[-1][1] = i
        else: runs.append([i, i])
    for lo, hi in runs: print('   %04x-%04x (%d bytes)' % (lo, hi, hi - lo + 1))

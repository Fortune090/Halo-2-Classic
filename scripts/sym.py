"""Name addresses in the native exe from its link map."""
import sys,re,bisect,subprocess
syms=[]
for line in open(sys.argv[1],errors='replace'):
    m=re.match(r'\s*[0-9a-f]{4}:[0-9a-f]{8}\s+(\S+)\s+([0-9a-f]{8,16})\s+(\S.*)$',line)
    if m and not m.group(1).startswith("$L"): syms.append((int(m.group(2),16),m.group(1),m.group(3).strip()))
syms.sort(); addrs=[s[0] for s in syms]
names=[s[1] for s in syms if s[1].startswith('?')]
def dem(n):
    if not n.startswith('?'): return n
    out=subprocess.run(['llvm-undname',n],capture_output=True,text=True,stdin=subprocess.DEVNULL).stdout.strip().split('\n')
    return out[-1] if out else n
for a in sys.argv[2:]:
    v=int(a,16); i=bisect.bisect_right(addrs,v)-1
    if i<0: print(a,'?'); continue
    print('%s: %s +%#x  [%s]'%(a,dem(syms[i][1])[:110],v-syms[i][0],syms[i][2][-40:]))

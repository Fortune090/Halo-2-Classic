import subprocess,sys,bisect,re,csv
exe=sys.argv[1]
out=subprocess.run(['llvm-nm','-n',exe],capture_output=True,text=True).stdout
syms=[]
for l in out.splitlines():
    p=l.split(None,2)
    if len(p)==3 and p[1] in 'TtRrDdBb': syms.append((int(p[0],16),p[2]))
syms.sort(); a=[s[0] for s in syms]
names=[s[1] for s in syms]
def find(addr):
    i=bisect.bisect_right(a,addr)-1
    return (syms[i][1],addr-syms[i][0]) if i>=0 else ('?',0)
for x in sys.argv[2:]:
    addr=int(x,16)+0x400000
    n,o=find(addr)
    d=subprocess.run(['llvm-undname',n],capture_output=True,text=True).stdout.strip().split('\n')[-1] if n.startswith('?') else n
    print(f'{x}: {d} +{o:#x}')

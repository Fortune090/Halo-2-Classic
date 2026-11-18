import subprocess,sys,os,collections,re,json
objdir=sys.argv[1]
objs=sorted(os.path.join(objdir,f) for f in os.listdir(objdir) if f.endswith('.obj'))
defined=collections.defaultdict(list); undef=collections.defaultdict(set)
for i in range(0,len(objs),200):
    out=subprocess.run(['llvm-nm','-A','--no-sort']+objs[i:i+200],capture_output=True,text=True).stdout
    for line in out.splitlines():
        m=re.match(r'(.*?\.obj):\s*([0-9a-f]*)\s+(\S)\s+(.*)$',line)
        if not m: continue
        f,addr,t,name=m.groups(); f=os.path.basename(f)
        if t=='U': undef[name].add(f)
        elif t.isupper() or t in 'w': defined[name].append((f,t))
missing={n:sorted(fs) for n,fs in undef.items() if n not in defined}
dups={n:d for n,d in defined.items() if len([x for x in d if x[1] not in 'CwW'])>1}
print('objects',len(objs),'defined',len(defined),'undefined (unique)',len(undef),'unresolved after combining',len(missing))
json.dump(missing,open(sys.argv[2],'w'),indent=0)
def kind(n):
    if n.startswith('__imp_'): return 'import'
    if n.startswith('?'): return 'C++ decorated'
    if re.match(r'^@\w+@\d+$',n): return 'fastcall'
    if re.match(r'^_\w+@\d+$',n): return 'stdcall'
    if n.startswith('_'): return 'cdecl or data'
    return 'other'
c=collections.Counter(kind(n) for n in missing); print(dict(c))
for k in c:
    print('==',k); print('  '+'  '.join(sorted(n for n in missing if kind(n)==k))[:2600])
print('duplicate strong definitions:',len(dups)); 
for n,d in list(dups.items())[:15]: print('  ',n[:70],d[:3])

import re,os,sys,collections
root=sys.argv[1]
pat=re.compile(r'\b([A-Za-z_]\w*?)_?((?:4[1-9a-f]|5[0-7])[0-9a-f]{4})\b')
pref=collections.Counter(); addrs=collections.defaultdict(set)
for base in ('src','include'):
    for d,_,ns in os.walk(os.path.join(root,base)):
        for n in ns:
            if not n.endswith(('.cpp','.h')): continue
            t=open(os.path.join(d,n),encoding='utf-8',errors='replace').read()
            t=re.sub(r'//.*','',t)
            for m in pat.finditer(t):
                p=m.group(1).rstrip('_')
                key=re.sub(r'\d+$','',p)[:14]
                pref[key]+=1; addrs[key].add(m.group(2))
tot=set()
for k,c in pref.most_common(25):
    print(f'{k:16s} uses {c:7d} distinct addrs {len(addrs[k])}')
for k in addrs: tot|=addrs[k]
print('distinct data addresses named anywhere:',len(tot))

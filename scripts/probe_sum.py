import json,sys,re,collections
res=json.load(open(sys.argv[1]))
kinds=collections.Counter(); fk=collections.Counter(); ex={}
for f,(rc,errs) in res.items():
    seen=set()
    for e in errs:
        m=re.search(r'error: (.*)',e); msg=m.group(1) if m else e
        k=re.sub(r"'[^']*'","'X'",msg); k=re.sub(r'\d+','N',k)[:90]
        kinds[k]+=1
        if k not in seen: fk[k]+=1; seen.add(k)
        ex.setdefault(k,e[-230:])
print('files with rc!=0:',sum(1 for rc,_ in res.values() if rc))
for k,c in fk.most_common(22): print(f'{c:5d} files {kinds[k]:7d} errs  {k}\n        e.g. {ex[k]}')

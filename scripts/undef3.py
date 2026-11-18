import subprocess,sys,os,collections,re,json
objs=[l.strip() for l in open('/home/claude/native/objects2.rsp') if l.strip()]
defined=set(); undef=collections.defaultdict(set)
for i in range(0,len(objs),200):
    out=subprocess.run(['llvm-nm','-A','--no-sort']+objs[i:i+200],capture_output=True,text=True).stdout
    for line in out.splitlines():
        m=re.match(r'(.*?\.obj):\s*([0-9a-f]*)\s+(\S)\s+(.*)$',line)
        if not m: continue
        f,addr,t,name=m.groups()
        if t=='U': undef[name].add(os.path.basename(f))
        elif t.isupper(): defined.add(name)
missing={n:sorted(fs) for n,fs in undef.items() if n not in defined}
json.dump(missing,open('/home/claude/native/unresolved3.json','w'),indent=0)
old=set(json.load(open('/home/claude/native/unresolved2.json')))
print('objects',len(objs),'unresolved',len(missing),'new vs before:',sorted(set(missing)-old)[:30],'gone:',len(old-set(missing)))

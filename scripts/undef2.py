import subprocess,sys,os,collections,re,json
objdir='/home/claude/native/obj'; keep=json.load(open('/home/claude/native/keep_manifest.json'))
replaced={os.path.basename(k).replace('_keep.obj','.obj') for k in keep}
objs=sorted(os.path.join(objdir,f) for f in os.listdir(objdir) if f.endswith('.obj') and f not in replaced)+sorted(keep)
open('/home/claude/native/objects.rsp','w').write('\n'.join(objs)+'\n')
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
json.dump(missing,open('/home/claude/native/unresolved2.json','w'),indent=0)
print('objects',len(objs),'unresolved',len(missing))
print([n for n in missing if n.startswith('?')])

import json,re,os,subprocess,sys,collections
root=sys.argv[1]
m=json.load(open('/home/claude/native/unresolved.json'))
game=[n for n in m if n.startswith('?') and 'D3D@@' not in n and not n.startswith('??')]
und=subprocess.run(['llvm-undname']+game,capture_output=True,text=True).stdout
dem={}
cur=None
for line in und.splitlines():
    line=line.strip()
    if line in m: cur=line
    elif cur and line: dem[cur]=line; cur=None
texts={}
for base in('src','include'):
    for d,_,ns in os.walk(os.path.join(root,base)):
        if 'stubs' in d: continue
        for n in ns:
            if n.endswith(('.cpp','.h')): texts[os.path.join(base,n) if base=='include' else os.path.join('src',n)]=open(os.path.join(d,n),encoding='utf-8',errors='replace').read()
res={}
for s in game:
    d=dem.get(s,'?')
    q=re.search(r'([\w:~]+)\(',d).group(1); name=q.split('::')[-1]
    pat=re.compile(r'(^|\n)[^\n;{}()]*\b'+re.escape(name)+r'\s*\([^;{}]*\)\s*(const\s*)?\{')
    hits=[]
    for f,t in texts.items():
        for mm in pat.finditer(t):
            head=mm.group(0).strip().split('\n')[-1] if '\n' in mm.group(0).strip() else mm.group(0).strip()
            hits.append((f,mm.group(0).strip()[:90].replace('\n',' ')))
    res[s]=(d,hits)
    print(d[:95]); 
    for h in hits[:4]: print('      ',h, 'users:',m[s][:2])
    if not hits: print('       NO DEFINITION FOUND; users:',m[s][:3])
json.dump(res,open('/home/claude/native/inline_missing.json','w'),indent=1)

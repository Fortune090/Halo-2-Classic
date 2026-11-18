"""The decompilation defines some functions 'inline' in one source and calls them from others, which only links under
whole-program optimisation. For an ordinary build, compile each defining source once more with the functions' addresses
taken (which makes the compiler emit them) and keep only those functions from that object."""
import json,re,os,sys,subprocess,collections
root=sys.argv[1]
sys.path.insert(0,os.path.join(root,'tools')); import build
res=json.load(open('/home/claude/native/inline_missing.json'))
cfg=json.load(open(os.path.join(root,'config','files.json')))
xdk=os.environ['XDK_DIR']
by=collections.defaultdict(list); unplaced=[]
for sym,(dem,hits) in res.items():
    q=re.search(r'([\w:~]+)\(',dem).group(1); name=q.split('::')[-1]
    good=[f for f,t in hits if re.match(r'^(static\s+)?(inline\s+)?[\w\s\*&]*?\b(\w+::)?'+re.escape(name)+r'\s*\(',t) and f.startswith('src/')]
    if len(good)!=1: good=[f for f,tx in hits if re.match(r'^(static\s+)?inline\b',tx) and f.startswith('src/')]
    if len(good)!=1: unplaced.append((sym,dem,[h[0] for h in hits])); continue
    m=re.match(r'^(?:public: |private: |protected: )?(.*?)\s*(__\w+)\s+([\w:]+)\((.*)\)\s*(const)?$',dem)
    ret,conv,qual,args,const=m.groups()
    if '::' in qual:
        cls=qual.rsplit('::',1)[0]; decl=f'{ret} ({cls}::*native_keep_%d)({args}){" const" if const else ""} = &{qual};'
    else: decl=f'{ret} ({conv} *native_keep_%d)({args}) = &{qual};'
    by[good[0]].append((sym,decl))
print('placed',sum(len(v) for v in by.values()),'in',len(by),'sources; unplaced',len(unplaced))
for u in unplaced: print('  UNPLACED',u)
k=0; ok=0
manifest={}
for f,items in by.items():
    stem=os.path.basename(f)[:-4]; p=os.path.join(root,f)
    flags=[x for x in build.file_flags(open(p,encoding='utf-8').read(),os.path.basename(f),cfg) if x.upper() not in('/GL','/GL-')]
    gen=f'/home/claude/native/gen/{stem}_keep.cpp'
    lines=[f'#include "{p}"']
    for sym,decl in items: lines.append(decl%k); k+=1
    open(gen,'w').write('\n'.join(lines)+'\n')
    obj=f'/home/claude/native/obj_keep/{stem}_keep.obj'
    env=dict(os.environ,INCLUDE=build.tool_arg(os.path.join(xdk,'include')))
    cmd=[os.path.join(xdk,'bin','vc71','CL.Exe'),'/nologo','/c','/Gr','/Zl',*flags,*[build.tool_arg(a) for a in ['/I',os.path.join(root,'include'),'/I',os.path.join(root,'src'),gen,'/Fo'+obj]]]
    r=subprocess.run(cmd,cwd=root,env=env,capture_output=True,text=True)
    if r.returncode: print('FAIL',f,(r.stdout+r.stderr)[-600:])
    else: ok+=1; manifest[obj]=[s for s,_ in items]
json.dump(manifest,open('/home/claude/native/keep_manifest.json','w'),indent=1)
print('compiled',ok,'of',len(by))

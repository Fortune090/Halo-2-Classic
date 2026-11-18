"""Compile every source of the decompilation with the Xbox SDK's compiler as ordinary (non-LTCG) COFF objects."""
import sys,os,json,subprocess,time
from concurrent.futures import ThreadPoolExecutor
root,out,jobs=sys.argv[1],sys.argv[2],int(sys.argv[3])
sys.path.insert(0,os.path.join(root,'tools'))
import build
xdk=os.environ['XDK_DIR']
cfg=json.load(open(os.path.join(root,'config','files.json')))
inc=['/I',os.path.join(root,'include'),'/I',os.path.join(root,'src')]
work=[]
src=os.path.join(root,'src')
for n in sorted(os.listdir(src)):
    if not n.endswith('.cpp'): continue
    p=os.path.join(src,n); flags=[f for f in build.file_flags(open(p,encoding='utf-8').read(),n,cfg) if f.upper() not in('/GL','/GL-')]
    tu=os.path.join(root,'build','gen',n[:-4]+'_tu.cpp')       # the source followed by a caller for each of its functions (tools/build.py):
    work.append((tu if os.path.exists(tu) else p,os.path.join(out,n[:-4]+'.obj'),['/c','/Gr','/Zl',*flags]))   # it makes the compiler emit every vtable and inline function the full build has
st=os.path.join(src,'stubs')
for n in sorted(os.listdir(st)):
    if n.endswith('.cpp'):
        work.append((os.path.join(st,n),os.path.join(out,'stubs_'+n[:-4]+'.obj'),['/c','/Zl','/Og','/Oi','/Ot','/Oy','/Ob2','/Gs','/GF','/Gr']))
log=open(os.path.join(out,'..','compile_log.txt'),'a')
def one(w):
    p,o,fl=w
    if os.path.exists(o) and os.path.getmtime(o)>os.path.getmtime(p): return p,0,''
    env=dict(os.environ,INCLUDE=build.tool_arg(os.path.join(xdk,'include')),LIB=build.tool_arg(os.path.join(xdk,'lib')))
    cmd=[os.path.join(xdk,'bin','vc71','CL.Exe'),'/nologo',*[build.tool_arg(a) for a in fl+inc+[p,'/Fo'+o]]]
    try:
        r=subprocess.run(cmd,cwd=root,env=env,capture_output=True,text=True,timeout=1200)
        return p,r.returncode,r.stdout+r.stderr
    except subprocess.TimeoutExpired:
        return p,-9,'timeout'
t=time.time(); bad=0
with ThreadPoolExecutor(jobs) as ex:
    for i,(p,rc,txt) in enumerate(ex.map(one,work)):
        if rc: bad+=1; log.write(f'FAIL {rc} {p}\n{txt}\n'); log.flush()
        if i%100==0: print(i,len(work),'failed',bad,round(time.time()-t),flush=True)
print('done',len(work),'failed',bad,round(time.time()-t),flush=True)

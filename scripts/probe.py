import os,sys,subprocess,re,collections,json
from concurrent.futures import ThreadPoolExecutor
root,stub,out=sys.argv[1],sys.argv[2],sys.argv[3]
src=os.path.join(root,'src')
files=sorted(f for f in os.listdir(src) if f.endswith('.cpp'))
base=[sys.executable,'-m','ziglang','c++','-target','x86-windows-gnu','-fsyntax-only','-std=c++03','-fms-extensions','-fdeclspec','-fasm-blocks','-msse','-msse2',
      '-Xclang','-fdefault-calling-conv=fastcall','-w','-ferror-limit=0','-fno-color-diagnostics','-I',os.path.join(root,'include'),'-I',src,'-I',stub]
import threading
def run(f):
    try:
        env=dict(os.environ,ZIG_GLOBAL_CACHE_DIR='/home/claude/probe/zc_%d'%(threading.get_ident()%100000),ZIG_LOCAL_CACHE_DIR='/home/claude/probe/zl_%d'%(threading.get_ident()%100000))
        p=subprocess.run(base+[os.path.join(src,f)],capture_output=True,text=True,timeout=300,env=env)
        errs=[l for l in p.stderr.splitlines() if ': error:' in l or 'fatal error' in l]
        return f,p.returncode,errs
    except subprocess.TimeoutExpired:
        return f,-1,['timeout']
res={}
with ThreadPoolExecutor(8) as ex:
    for f,rc,errs in ex.map(run,files): res[f]=(rc,errs)
json.dump(res,open(out,'w'))
ok=sum(1 for rc,_ in res.values() if rc==0)
print('files',len(files),'compile clean',ok)

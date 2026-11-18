import sys,os,time
root=sys.argv[1]
sys.path.insert(0,os.path.join(root,'tools'))
os.chdir(root)
import build
t=time.time(); tm={}
print('map:',build.build(jobs=2,timings=tm),flush=True)
print('timings',tm,'total',time.time()-t,flush=True)

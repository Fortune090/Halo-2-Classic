# sites.py: every hook site of the mod - the function it is in and how H2Classic runs that function in each version
import sys,json,csv,bisect,re
sys.path.insert(0,'/home/claude/sgport')
H=json.load(open('hooks.json'))
F={}
for r in csv.DictReader(open('/home/claude/h2decomp_src/halo2-decompiled-main/config/functions.csv')): F[int(r['va'],16)]=r
starts=sorted(F)
def owner(va):
    i=bisect.bisect_right(starts,va)-1; s=starts[i]
    return s if va<s+int(F[s]['size'],0) else None
S=open('/home/claude/native/layer/retail_code.S').read()
i=S.index('_h2n_routes:'); j=S.find('.globl',i)
routes={int(m.group(2),16) for m in re.finditer(r'\.long\s+([^\s,]+)\s*,\s*(0x[0-9a-f]+)',S[i:j])}
m15=json.load(open('/home/claude/v15/map15.json'))['funcs']
KEEP=sys.argv[1].split(',') if len(sys.argv)>1 else None
out={}
c10=H['v1.0']['calls']; c15=H['v1.5']['calls']
for h in c10:
    if KEEP and h not in KEEP: continue
    rows=[]
    for k,(s,t) in enumerate(c10[h]):
        s=int(s,16); o=owner(s); st=F[o]['status']
        mode10='route' if o in routes else 'src'
        e=m15.get('%x'%o); mode15=e[1] if e else '?'
        s15=c15[h][k][0] if h in c15 and k<len(c15[h]) else '?'
        rows.append('%x@%x %s/%s 1.5:%s@%s'%(s,o,st[:4],mode10,mode15,s15))
    out[h]=rows
    print(h,'->',c10[h][0][1],':',' ; '.join(rows))

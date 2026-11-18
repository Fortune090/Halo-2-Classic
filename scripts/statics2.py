"""Match the unnamed statics seen through matched code (statics_fields.json: our LTCG address <-> retail address) with
the static data symbols of the plain objects, by which functions use them.  -> native/statics_plan.json"""
import sys,os,json,collections,bisect
sys.path.insert(0,'/home/claude/scripts')
from coff import Coff
NAT='/home/claude/native'
d=json.load(open(NAT+'/statics_fields.json'))
W=lambda r: 0x461180<=r<0x586000
byobj=collections.defaultdict(lambda: collections.defaultdict(lambda: dict(retail=set(),funcs=set())))
for fva,obj,mine,theirs,fname,after,off in d['fields']:
    if not W(theirs): continue
    e=byobj[obj][mine]; e['retail'].add(theirs); e['funcs'].add(fname)
verbose='-v' in sys.argv
result={}; stats=collections.Counter()
for obj in sorted(byobj):
    path=os.path.join(NAT,'obj2',obj)
    if not os.path.exists(path): stats['no object']+=len(byobj[obj]); continue
    c=Coff(path)
    # static data symbols: storage class 3, defined in a data/bss section, not a section symbol or a literal
    secs={s.index:s for s in c.sections}
    stat=[]
    for s in c.symbols:
        i=s.index
        if s.storage!=3 or s.section<=0: continue
        sec=secs.get(s.section)
        if sec is None or sec.name.split('$')[0] not in ('.data','.bss'): continue
        if s.name.startswith(('$SG','??_C@','__real@','.','$T','$L')) or s.name==sec.name: continue
        stat.append((i,s,sec))
    # size of each: to the next symbol in the same section
    def size_of(sym,sec):
        nxt=[t.value for t in c.symbols if t.section==sym.section and t.value>sym.value]
        end=min(nxt) if nxt else max(sec.vsize,sec.rawsize)
        return end-sym.value
    # which functions refer to each symbol: relocations in code sections, attributed to the function symbol before them
    users=collections.defaultdict(set)
    for sec in c.sections:
        if not sec.relocs or not sec.name.startswith('.text'): continue
        fs=sorted((t.value,t.name) for t in c.symbols if t.section==sec.index and (t.type&0x30)==0x20)
        starts=[f[0] for f in fs]
        for off,symidx,typ in sec.relocs:
            k=bisect.bisect_right(starts,off)-1
            if k>=0: users[symidx].add(fs[k][1])
    L=byobj[obj]
    kind_of_retail=lambda r: '.data' if r<0x4800fc else '.bss'
    for kind in ('.data','.bss'):
        ss=sorted(((s.value,s.name,size_of(s,sec),users.get(i,set())) for i,s,sec in stat if sec.name.split('$')[0]==kind))
        aa=sorted(a for a in L if kind_of_retail(min(L[a]['retail']))==kind)
        if not aa: continue
        n,m=len(ss),len(aa)
        import functools
        def take(i,j):
            """the index after the last address static i covers when its base is address j, or None"""
            v,name,size,fn=ss[i]
            if not (L[aa[j]]['funcs']&fn): return None
            k=j
            while k<m and aa[k]<aa[j]+size:
                if not (L[aa[k]]['funcs']&fn): return None
                if min(L[aa[k]]['retail'])-min(L[aa[j]]['retail'])!=aa[k]-aa[j]: return None
                k+=1
            return k
        @functools.lru_cache(None)
        def ways(i,j):
            if j==m: return 1
            if i==n: return 0
            w=ways(i+1,j)
            k=take(i,j)
            if k is not None: w+=ways(i+1,k)
            return min(w,2)
        w=ways(0,0)
        if w==1:
            i=j=0
            while j<m:
                k=take(i,j)
                if k is not None and ways(i+1,k)==1 and not (ways(i+1,j)>=1):
                    v,name,size,fn=ss[i]
                    result[obj+'|'+name]=dict(retail=min(L[aa[j]]['retail']),size=size,ours=aa[j]); stats['matched']+=1
                    j=k
                i+=1
        else:
            stats['unresolved addresses (%s)'%('no way' if w==0 else 'more than one way')]+=m
            if verbose or True: print(obj,kind,'ways',w,[('%x'%a,'%x'%min(L[a]['retail'])) for a in aa][:6],[(x[1][:38],x[2]) for x in ss][:8])
json.dump(result,open(NAT+'/statics_plan.json','w'),indent=0,sort_keys=True)
print(dict(stats))

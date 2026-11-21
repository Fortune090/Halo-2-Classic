import sys,re,collections,importlib.util,json
src=open(sys.argv[1]).read()
# evaluate only the VERSIONS literal
i=src.index('VERSIONS = {'); j=src.index('\nPROBES')
ns={}
exec(src[i:j],ns)
V=ns['VERSIONS']
out={}
for v in V:
    d=V[v]
    c=collections.OrderedDict()
    for s,t,h in d['calls']: c.setdefault(h,[]).append((s,t))
    out[v]=dict(calls={h:[(hex(s),hex(t)) for s,t in l] for h,l in c.items()},
                bytes=[(hex(a),o,n) for a,o,n in d['bytes']],
                symrefs=[(hex(a),hex(o),n) for a,o,n in d['symrefs']])
    print(v,'calls',len(d['calls']),'hooks',len(c),'bytes',len(d['bytes']),'symrefs',len(d['symrefs']), [k for k in d if k not in('calls','bytes','symrefs')])
json.dump(out,open(sys.argv[2],'w'),indent=1)
for h,l in out['v1.0']['calls'].items(): print(h,len(l),l[0][1],end=' | ')

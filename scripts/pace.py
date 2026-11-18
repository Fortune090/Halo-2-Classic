import re,sys,collections
t=open(sys.argv[1],encoding='utf-8').read()
day=collections.OrderedDict(); hv={}
cur=None
for line in t.splitlines():
    m=re.match(r'## (\d{4}-\d\d-\d\d)',line)
    if m: cur=m.group(1); continue
    m=re.match(r'matched (\d+) of (\d+) game functions \((\d+) of (\d+) bytes',line)
    if m and cur:
        n=int(m.group(1)); b=int(m.group(3))
        if cur not in day or n>day[cur][0]: day[cur]=(n,b,int(m.group(2)))
    m=re.match(r'matched (\d+) of (\d+) Havok functions \((\d+) of',line)
    if m and cur:
        n=int(m.group(1))
        if cur not in hv or n>hv[cur][0]: hv[cur]=(n,int(m.group(3)),int(m.group(2)))
prev=None
for d in sorted(day):
    n,b,tot=day[d]; h=hv.get(d,('','',''))
    print(d,n,'of',tot,f'{b} bytes','havok',h[0],h[1],'' if prev is None else f'+{n-prev[0]} funcs +{b-prev[1]} bytes')
    prev=(n,b)

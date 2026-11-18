import re,csv,sys,bisect,collections
addr=sys.argv[1]; fcsv=sys.argv[2]; patch=sys.argv[3]
rows=sorted(csv.DictReader(open(fcsv)),key=lambda r:int(r['va'],16))
starts=[int(r['va'],16) for r in rows]
def func(a):
    i=bisect.bisect_right(starts,a)-1
    if i<0: return None
    r=rows[i]
    return r if a<starts[i]+int(r['size']) else None
defs=[]
for line in open(addr,encoding='utf-8',errors='replace'):
    m=re.match(r'#define\s+(\w+)\s+(0x[0-9A-Fa-f]+)\b(.*)',line)
    if m: defs.append((m.group(1),int(m.group(2),16),m.group(3).strip()))
code=[(n,a,c) for n,a,c in defs if 0x12000<=a<0x412000]
data=[(n,a,c) for n,a,c in defs if 0x412000<=a<0x586000]
print('addr_v10.h defines',len(defs),'code',len(code),'data',len(data),'other',len(defs)-len(code)-len(data))
st=collections.Counter()
un=[]
for n,a,c in code:
    r=func(a)
    if not r: st['not in a function']+=1; continue
    k=(r['owner'] if r['owner'] in('game','third:havok') else 'library', r['status'] if r['owner'] in('game','third:havok') else '')
    st[k]+=1
    if r['owner'] in('game','third:havok') and r['status']!='matched': un.append((n,hex(a),r['va'],r['size'],r['owner'],r['status'],r['source']))
for k,v in st.items(): print('  ',k,v)
print('our named code addresses that sit in functions the decomp has NOT matched:')
for u in un: print('   ',*u)
# hooks in patch_xbe: call sites and targets
t=open(patch,encoding='utf-8').read()
v10=t.split("'v1.5'")[0]
calls=re.findall(r'\((0x[0-9a-f]+),\s*(0x[0-9a-f]+),\s*\'(\w+)\'\)',v10)
targets=collections.Counter(); hostf=collections.Counter()
for site,tgt,name in calls:
    r=func(int(tgt,16)); 
    if r: targets[(r['va'],r['owner'],r['status'])]+=1
    r=func(int(site,16))
    if r: hostf[(r['va'],r['owner'],r['status'])]+=1
print('hooked call sites:',len(calls),'distinct original targets:',len(targets),'distinct host functions:',len(hostf))
c=collections.Counter((o,s) for (_,o,s) in targets); print('  targets by status',dict(c))
c=collections.Counter((o,s) for (_,o,s) in hostf); print('  host functions by status',dict(c))
print('  unmatched host functions:',[k for k in hostf if k[2]!='matched' and k[1] in('game','third:havok')])
print('  unmatched targets:',[k for k in targets if k[2]!='matched' and k[1] in('game','third:havok')])

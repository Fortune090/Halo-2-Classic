# routecheck10.py <routes json>...: for each routed function, retail's registers needed on entry and bytes popped
# (conv_core over the v1.0 XBE) against what the mangled declaration provides
import sys, json, csv, re
sys.path.insert(0,'/home/claude/v15'); sys.path.insert(0,'/home/claude/scripts')
import conv_core
from xbeimg import XbeImage
x=XbeImage('/home/claude/xbe/default.xbe')
def rd(va,n): return x.read(va,n)
rows={int(r['va'],16):r for r in csv.DictReader(open('/home/claude/h2decomp_src/halo2-decompiled-main/config/functions.csv'))}
def params(name):
    # split the mangled parameter list into simple codes (enough for size and float-ness)
    m=re.search(r'@@(Y[AGI]|[QUA]AE)(.*)$', name)
    if not m: return None, None
    conv=m.group(1); rest=m.group(2)
    # skip the return type
    def one(s, i):
        c=s[i]
        if c in 'CDEFGHIJKMN_' :
            if c=='_': return i+2, ('w8' if s[i+1] in 'JK' else 'i')
            return i+1, ('f' if c=='M' else 'd' if c=='N' else 'i')
        if c=='X': return i+1, 'v'
        if c in 'PQ':                       # pointer: P<cv><type>
            j=i+2
            if s[j]=='6':                   # function pointer: skip to the matching Z
                k=s.index('Z', j); return k+1, 'i'
            j,_=one(s, j); return j, 'i'
        if c in 'TUV':                      # struct/union/class by name: up to @@
            k=s.index('@@', i); return k+2, 's'
        if c=='A':                          # reference
            j,_=one(s, i+2); return j, 'i'
        if c.isdigit(): return i+1, 'i'      # back reference to an earlier type
        raise ValueError(s[i:])
    i=0
    i,_=one(rest, 0)                        # return type
    out=[]
    while i < len(rest) and rest[i] not in '@Z':
        i,t=one(rest, i); out.append(t)
    return conv, out
routes=[]
for f in sys.argv[1:]: routes+=json.load(open(f))
sizes={e['va']:int(rows[e['va']]['size']) for e in routes if e['va'] in rows}
need,rets=conv_core.analyse(rd, sizes)[:2]
for e in routes:
    va=e['va']; n=e['name']
    try: conv,ps=params(n)
    except Exception as ex: conv,ps='?',None
    regs=[]; stack=0
    if ps is not None:
        if ps==['v']: ps=[]
        if conv=='YI':
            for t in ps:
                if t=='i' and len(regs)<2: regs.append(['ecx','edx'][len(regs)])
                else: stack+=8 if t in('d','w8') else 4
        elif conv in('QAE','UAE','AAE'):
            regs=['ecx']; stack=sum(8 if t in('d','w8') else 4 for t in ps)
        else: stack=sum(8 if t in('d','w8') else 4 for t in ps)
    pops=rets.get(va); nd=sorted(need.get(va,set()))
    want_pop=0 if conv=='YA' else stack
    flag=[]
    if set(nd)-set(regs): flag.append('NEEDS %s' % sorted(set(nd)-set(regs)))
    if pops and want_pop not in pops: flag.append('POPS %s not %d' % (sorted(pops), want_pop))
    if len(regs)==2 and set(nd)>={'ecx','edx'}: flag.append('two register args: check their order')
    print('%06x %-4s regs %-14s need %-18s pops %-6s %s %s' % (va, conv, ','.join(regs), ','.join(nd), sorted(pops) if pops else '-', '; '.join(flag), n[:60]))

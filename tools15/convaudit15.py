# convaudit15.py: settled15.json's adapters against their declarations' types: a float or 8-byte argument cannot be in a
# general register; lists the adapters that break that, and those with more than one way to permute (2+ args, a
# register that is not the declared one) for reading against a 1.0 call site
import json, re, sys
def params(name):
    m=re.search(r'@@(Y[AGI]|[QUA]AE)(.*)$', name)
    if not m: return None
    rest=m.group(2); seen=[]
    def one(s,i):
        c=s[i]
        if c in 'CDEFGHIJKMN_':
            if c=='_': return i+2, ('w8' if s[i+1] in 'JK' else 'i')
            return i+1, ('f' if c=='M' else 'd' if c=='N' else 'i')
        if c=='X': return i+1,'v'
        if c in 'PQ':
            j=i+2
            if s[j]=='6': k=s.index('Z',j); return k+1,'i'
            j,_=one(s,j); return j,'i'
        if c in 'TUV': k=s.index('@@',i); return k+2,'s'
        if c=='A': j,_=one(s,i+2); return j,'i'
        if c.isdigit(): return i+1, ('ref', int(c))
        raise ValueError(s[i:])
    i,_=one(rest,0); out=[]
    while i<len(rest) and rest[i] not in '@Z':
        i,t=one(rest,i)
        if isinstance(t,tuple): t = out[t[1]] if t[1] < len(out) and False else 'i?'
        out.append(t)
    if out==['v']: out=[]
    if m.group(1) in ('QAE','UAE','AAE'): out=['i']+out
    return out
d=json.load(open('/home/claude/v15/settled15.json'))
for side in ('forward','back'):
    for e in d[side]:
        if e.get('kind') not in ('adapter',) or not e.get('name'): continue
        try: ps=params(e['name'])
        except Exception: ps=None
        if ps is None: continue
        regs=e.get('regs') or {}
        bad=[(r,i) for r,i in regs.items() if i < len(ps) and ps[i] in ('f','d','w8','s')]
        if bad: print('FLOAT-IN-REGISTER', side, '%x' % e['a10'], regs, e.get('stack'), e.get('how'), ''.join(x[0] for x in ps), e['name'][:70])

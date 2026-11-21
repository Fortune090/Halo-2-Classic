# prune.py <spooky.c> remove <names...> | prune.py <spooky.c> unused <clang stderr>
# Take whole top-level function definitions (and their forward declarations) out of the port's source.
import sys,re
path=sys.argv[1]; mode=sys.argv[2]
L=open(path).read().split('\n')
def strip(l):
    l=re.sub(r'/\*.*?\*/','',l); l=re.sub(r'"(\\.|[^"\\])*"','""',l); l=re.sub(r"'(\\.|[^'\\])'","' '",l); return l
def find_def(name):
    pat=re.compile(r'^[A-Za-z_][^;=]*\b'+re.escape(name)+r'\s*\(')
    for i,l in enumerate(L):
        if l is None or not pat.match(l): continue
        s=strip(l)
        # a definition has its '{' on this line or on one of the next lines before any ';' at depth 0
        j=i; depth=0; seen=False; incomment=False
        while j<len(L):
            t=L[j] or ''
            # block comments spanning lines
            if incomment:
                if '*/' in t: t=t.split('*/',1)[1]; incomment=False
                else: j+=1; continue
            t2=strip(t)
            if '/*' in t2: t2=t2.split('/*',1)[0]; incomment=True
            for ch in t2:
                if ch=='{': depth+=1; seen=True
                elif ch=='}': depth-=1
                elif ch==';' and not seen and depth==0: seen=None; break
            if seen is None: break
            if seen and depth==0: return i,j
            j+=1
    return None
def remove(name):
    n=0
    while True:
        d=find_def(name)
        if not d: break
        i,j=d
        for k in range(i,j+1): L[k]=None
        n+=1
    # forward declarations on one line
    pat=re.compile(r'^(static |extern )?[A-Za-z_][^;={]*\b'+re.escape(name)+r'\s*\([^;{]*\)\s*(__attribute__\(\([^)]*\)\))?\s*;')
    for i,l in enumerate(L):
        if l is not None and pat.match(l):
            rest=pat.sub('',l).strip()
            if rest=='' or rest.startswith('/*'): L[i]=None; n+=1
    return n
if mode=='remove': names=sys.argv[3:]
else:
    names=sorted(set(re.findall(r"warning: unused function '(\w+)'",open(sys.argv[3]).read())))
tot=0; miss=[]
for nm in names:
    k=remove(nm); tot+=k
    if not k: miss.append(nm)
out=[l for l in L if l is not None]
# collapse runs of blank lines left behind
res=[]; blank=0
for l in out:
    if l.strip()=='': blank+=1
    else: blank=0
    if blank<=1: res.append(l)
open(path,'w').write('\n'.join(res))
print('removed',tot,'blocks for',len(names),'names; not found:',miss[:20],'; lines',len(L),'->',len(res))

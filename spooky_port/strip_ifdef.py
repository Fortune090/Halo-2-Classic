# strip_ifdef.py <in> <out>: remove the host-test, size-test and trainer branches (HOST, BIG_SIZE_TEST, SPOOKY_TRAINER are
# never defined in the H2Classic port); every other conditional is left as it is.
import sys,re
UNDEF={'HOST','BIG_SIZE_TEST','SPOOKY_TRAINER'}
src=open(sys.argv[1]).read().split('\n')
out=[]; stack=[]   # entries: ('known', taking_now, taken_before) or ('other',)
def emitting(): return all(e[0]=='other' or e[1] for e in stack)
i=0
while i<len(src):
    l=src[i]; s=re.sub(r'/\*.*?\*/','',l).strip()
    m=re.match(r'#\s*(ifdef|ifndef|if|elif|else|endif)\b\s*(.*)',s)
    if not m: 
        if emitting(): out.append(l)
        i+=1; continue
    k,rest=m.group(1),m.group(2).strip()
    if k in('ifdef','ifndef') and rest in UNDEF:
        take=(k=='ifndef'); stack.append(['known',take,take])
    elif k=='if' and rest=='defined(BIG_SIZE_TEST) && !defined(HOST)':
        stack.append(['known',False,False])
    elif k in('ifdef','ifndef','if'):
        stack.append(['other'])
        if emitting(): out.append(l)
    elif k=='elif':
        assert stack[-1][0]=='other',(i+1,l)
        if emitting(): out.append(l)
    elif k=='else':
        if stack[-1][0]=='known': stack[-1][1]=not stack[-1][2]; stack[-1][2]=True
        elif emitting(): out.append(l)
    elif k=='endif':
        e=stack.pop()
        if e[0]=='other' and emitting(): out.append(l)
    i+=1
assert not stack
open(sys.argv[2],'w').write('\n'.join(out))
print(len(src),'->',len(out),'lines')

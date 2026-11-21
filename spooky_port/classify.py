# classify.py <addr_v10.h> <native dir> <functions.csv> : where each code address the mod names lands in H2Classic
import sys,re,csv,json
hdr,nat,fcsv=sys.argv[1:4]
A={}
for l in open(hdr):
    m=re.match(r'#define\s+ADDR_(\w+)\s+(0x[0-9A-Fa-f]+)',l)
    if m: A[m.group(1)]=int(m.group(2),16)
S=open(nat+'/layer/retail_code.S').read()
def block(name):
    i=S.index('_'+name+':'); j=S.find('.globl',i); return S[i:j]
allf={}
for m in re.finditer(r'\.long\s+(0x[0-9a-f]+)\s*,\s*([^\s,]+)',block('h2n_all_functions')): allf[int(m.group(1),16)]=m.group(2)
routes={}
for m in re.finditer(r'\.long\s+([^\s,]+)\s*,\s*(0x[0-9a-f]+)',block('h2n_routes')): routes[int(m.group(2),16)]=m.group(1)
lib={}
for m in re.finditer(r'\.long\s+(0x[0-9a-f]+)\s*,\s*([^\s,]+)',block('h2n_library_patches')): lib[int(m.group(1),16)]=m.group(2)
gp={}
for m in re.finditer(r'\.long\s+(0x[0-9a-f]+)\s*,\s*([^\s,]+)',block('h2n_game_patches')): gp[int(m.group(1),16)]=m.group(2)
F={}
for r in csv.DictReader(open(fcsv)): F[int(r['va'],16)]=r
starts=sorted(F)
import bisect
def owner(va):
    i=bisect.bisect_right(starts,va)-1
    if i<0: return None
    s=starts[i]; 
    return s if va<s+int(F[s]['size'],0) else None
if __name__=='__main__':
    print(len(allf),'functions',len(routes),'routes',len(lib),'library patches',len(gp),'game patches')
    rows=[]
    for n,va in sorted(A.items(),key=lambda x:x[1]):
        if va>=0x411000 or va<0x11000: continue
        o=owner(va); st=F[o]['status'] if o else '-'; own=F[o]['owner'] if o else '-'
        k='route' if va in routes else 'ours' if va in allf else 'libpatch' if va in lib else 'NONE'
        if o is not None and o!=va: k='inside %x'%o + (' (route)' if o in routes else '')
        rows.append('%s=%x %s/%s %s'%(n,va,own,st,k))
    print(' | '.join(rows))

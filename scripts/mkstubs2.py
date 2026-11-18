"""Generate a stand-in for every Xbox library symbol the game objects need and the layer does not define:
it records the call and returns 0, or the value given for it in the optional fourth argument (a JSON object,
symbol -> return value; used so that Xbox Live calls fail cleanly instead of "succeeding" with nothing behind them)."""
import json,re,sys
m=json.load(open(sys.argv[1])); have=set(open(sys.argv[2]).read().split())
returns=json.load(open(sys.argv[4])) if len(sys.argv)>4 else {}
values=[]
xbox=re.compile(r'^_?@?(D3D|Direct3D|IDirectSound|DirectSound|CDirectSound|XOnline|_XOnline|XNet|XHV|X[A-Z]\w*|Wma|Mm[A-Z])')
cpp={'?LazySetStateVB@CDevice@D3D@@QAEXK@Z':4,'?SetFence@D3D@@YGKK@Z':4,
     '?FindSurfaceWithinTexture@PixelJar@D3D@@YGXPAUD3DPixelContainer@@W4_D3DCUBEMAP_FACES@@IPAPAEPAK333@Z':32}
out=['/* generated: stand-ins for the Xbox libraries */','.text']; names=[]
for n in sorted(m):
    if n in have: continue
    if n in cpp: ret=cpp[n]
    elif n.startswith('?'): continue
    elif xbox.match(n):
        s=re.match(r'^_\w+@(\d+)$',n); f=re.match(r'^@\w+@(\d+)$',n)
        if not s and not f: continue          # data, defined in the layer
        ret=int(s.group(1)) if s else max(0,int(f.group(1))-8)
    else: continue
    k=len(names); names.append(n); value=int(returns.get(n,0)) & 0xffffffff; values.append(value)
    out+= [f'.globl "{n}"',f'"{n}":','  pushl %ecx','  pushl %edx',f'  pushl ${k}','  call _h2n_stub_hit','  addl $4,%esp','  popl %edx','  popl %ecx',f'  movl $0x{value:x},%eax' if value else '  xorl %eax,%eax',f'  ret ${ret}' if ret else '  ret']
out+=['.section .rdata,"dr"','.globl _h2n_stub_names','_h2n_stub_names:']+[f'  .long LN{k}' for k in range(len(names))]
out+=[f'LN{k}: .asciz "{n}"' for k,n in enumerate(names)]
out+=['.globl _h2n_stub_returns','_h2n_stub_returns:']+[f'  .long 0x{v:x}' for v in values]
out+=['.globl _h2n_stub_count','_h2n_stub_count: .long %d'%len(names),'.globl __except_list','__except_list = 0']
open(sys.argv[3],'w').write('\n'.join(out)+'\n')
print('stand-ins:',len(names),'of which',sum(1 for v in values if v),'return a failure code')

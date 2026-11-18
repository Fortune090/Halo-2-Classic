"""Generate a stand-in for every Xbox library symbol the game objects need: it records the call and returns 0."""
import json,re,sys
m=json.load(open(sys.argv[1]))
import subprocess
have={l.split()[-1] for l in subprocess.run(['llvm-nm',sys.argv[3]],capture_output=True,text=True).stdout.splitlines() if len(l.split())==3 and l.split()[1] in 'TDBR'}
xbox=re.compile(r'^_?@?(D3D|Direct3D|IDirectSound|DirectSound|CDirectSound|XOnline|_XOnline|XNet|XHV|XInput|XGet|XFind|XCreate|XDelete|XLaunch|XLoad|XFree|XMem|XMount|XPhysical|XCalculate|XInit|XAudio|XRestore|XSave|XSet|XapiBoot|XUnmount|XQuery|XClose|XWrite|XRead|XFormat|XMU|XAuto|XCleanup|XSignature|X[A-Z]\w*|Wma|Mm[A-Z])')
cpp={'?MakeRequestedSpace@D3D@@YGPCKKK@Z':None,'?m_PushSegmentSize@CDevice@D3D@@2KA':None,   # in layer.c
     '?LazySetStateVB@CDevice@D3D@@QAEXK@Z':4,'?SetFence@D3D@@YGKK@Z':4,
     '?FindSurfaceWithinTexture@PixelJar@D3D@@YGXPAUD3DPixelContainer@@W4_D3DCUBEMAP_FACES@@IPAPAEPAK333@Z':32}
data={'_D3D__DirtyFlags','_D3D__RenderState','_D3D__TextureState','_D3D__pDevice','_XDEVICE_TYPE_GAMEPAD_TABLE','_XDEVICE_TYPE_MEMORY_UNIT_TABLE',
      '_XDEVICE_TYPE_VOICE_HEADPHONE_TABLE','_XDEVICE_TYPE_VOICE_MICROPHONE_TABLE','_g_dwDirectSoundDeltaPanicCount'}
out=['/* generated: stand-ins for the Xbox libraries */','.text']; names=[]; left=[]
for n in sorted(m):
    if n in have or n in data or (n in cpp and cpp[n] is None): continue
    if n in cpp: ret=cpp[n]
    elif n.startswith('?'): left.append(n); continue
    elif xbox.match(n):
        s=re.match(r'^_\w+@(\d+)$',n); f=re.match(r'^@\w+@(\d+)$',n)
        ret=int(s.group(1)) if s else max(0,int(f.group(1))-8) if f else 0
    else: left.append(n); continue
    k=len(names); names.append(n)
    out+= [f'.globl "{n}"',f'"{n}":','  pushl %ecx','  pushl %edx',f'  pushl ${k}','  call _h2n_stub_hit','  addl $4,%esp','  popl %edx','  popl %ecx','  xorl %eax,%eax',f'  ret ${ret}' if ret else '  ret']
out+=['.section .rdata,"dr"','.globl _h2n_stub_names','_h2n_stub_names:']+[f'  .long LN{k}' for k in range(len(names))]
out+=[f'LN{k}: .asciz "{n}"' for k,n in enumerate(names)]
out+=['.globl _h2n_stub_count','_h2n_stub_count: .long %d'%len(names),'.globl __except_list','__except_list = 0']
open(sys.argv[2],'w').write('\n'.join(out)+'\n')
print('stand-ins',len(names),'left for the Windows libraries',len(left)); print(' '.join(left))

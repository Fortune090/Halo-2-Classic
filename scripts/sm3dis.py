import sys,re
words=[int(x,16) for l in open(sys.argv[1]).read().split('\n')[1:] for x in l.split() if re.fullmatch(r'[0-9a-f]{8}',x)]
ops={1:'mov',2:'add',3:'sub',4:'mad',5:'mul',6:'rcp',7:'rsq',8:'dp3',9:'dp4',10:'min',11:'max',12:'slt',13:'sge',14:'exp',15:'log',16:'lit',17:'dst',18:'lrp',19:'frc',31:'dcl',35:'abs',46:'mova',65:'texkill',66:'tex',88:'cmp'}
tn={0:'r',1:'v',2:'c',3:'a',6:'o',8:'oC',10:'s'}
def reg(t):
    ty=((t>>28)&7)|((t>>8)&0x18); return tn.get(ty,'?%d'%ty)+str(t&0x7ff)
def src(t):
    s=(t>>16)&0xff; sw=''.join('xyzw'[(s>>(2*i))&3] for i in range(4))
    return ('-' if (t>>24)&0xf==1 else '')+reg(t)+('[a]' if t&0x2000 else '')+'.'+sw
def dst(t):
    m=(t>>16)&0xf; return reg(t)+'.'+''.join('xyzw'[i] for i in range(4) if m>>i&1)
i=1; n=0
while i<len(words):
    w=words[i]
    if w==0x0000ffff: print('end'); break
    op=w&0xffff; cnt=(w>>24)&0xf; a=words[i+1:i+1+cnt]
    if op==31: s='dcl %08x %s'%(a[0],dst(a[1]))
    else:
        out=[dst(a[0])]; k=1
        while k<len(a):
            t=a[k]; x=src(t)
            if t&0x2000: k+=1; x=x.replace('[a]','[%s]'%src(a[k]))
            out.append(x); k+=1
        s=ops.get(op,'op%d'%op)+' '+', '.join(out)
    print(n,s); n+=1; i+=1+cnt

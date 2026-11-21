import sys,re
s=open('/home/claude/uploads_sg/x/Spooky_Ghost_Menu_v2.0.40_R89/source/hooks.S').read()
# blocks start at ".globl name"
parts=re.split(r'(?m)^\s*\.globl\s+(\w+)\s*$',s)
blocks={parts[i]:parts[i+1] for i in range(1,len(parts)-1,2)}
for n in sys.argv[1:]:
    b=blocks.get(n)
    if b is None: print('##',n,'NOT FOUND'); continue
    # cut the trailing comment that belongs to the next block
    b=re.sub(r'/\*(?:(?!\*/).)*\*/\s*$','',b,flags=re.S)
    code='\n'.join(l for l in b.split('\n') if l.strip())
    print('##',n); print(code[:int(1e4)])

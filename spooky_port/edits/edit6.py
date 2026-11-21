# edit6.py: the game's code is not where the console build expects it, and the console's hardware is not there
p='/home/claude/native/layer3/sg/spooky.c'; s=open(p).read()
def rep(a,b):
    global s
    assert s.count(a)==1,(a[:70],s.count(a)); s=s.replace(a,b)
# 1. a Havok shape's type is read from its getType's code: this build's function there -> the XBE's code for it (in place)
rep('''	fn = M32(vt + 0x14);
	if (fn < 0x10000 || fn >= ADDR_DATA_LO) return -1;
	if (M8(fn) == 0xB8''','''	fn = sg_xbe_code(M32(vt + 0x14));       /* H2Classic: the table holds this build's function; its XBE code is read (sg_hooks.c) */
	if (!fn) return -1;
	if (M8(fn) == 0xB8''')
# 2. Havok's own velocity setter, called through the motion's table: a function of the game, wherever this build has it
rep('''	fn = M32(vt + 20 * 4);
	if (fn < 0x10000 || fn >= ADDR_DATA_LO) return;''','''	fn = M32(vt + 20 * 4);
	if (!sg_xbe_code(fn)) return;           /* H2Classic: a function of the game (this build's, or the XBE's code) */''')
# 3. the game's memory here ends where the layer's view of the console's memory ends (128 MB), as on a 128 MB console
rep('static u32 g_ptr_top = 0x84000000u;','static u32 g_ptr_top = 0x88000000u;   /* H2Classic: the layer\'s 128 MB (physmem.c) */')
# 4. Render Scale's memory probe
i=s.index('rs_probe_step();'); j=s.index('\n',i)+1; k=s.rindex('\n',0,i)+1
assert s[k:i].strip()=='' , s[k:j]
s=s[:k]+s[j:]
open(p,'w').write(s); print('ok')

# edit3.py: leftovers of what this build leaves out or has in the layer
p='/home/claude/native/layer3/sg/spooky.c'; s=open(p).read()
i=s.index('static float ws_mul(void)\n{'); j=s.index('\n}\n',i)+3
s=s[:i]+'''static float ws_mul(void)
{
	float v = h2n_ws_factor();            /* H2Classic: the layer's factor (xapi.c) - what the game's projection is given here */
	return v > 1.01f && v < 4.f ? v : 4.f / 3.f;
}
'''+s[j:]
a='	if (what == 14) { tb_toggle_restart(); return; }'
i=s.index(a); j=s.index('\n',i)+1; s=s[:i]+s[j:]
open(p,'w').write(s); print('ok')
# (second part) the excluded hooks' pointers
s=open(p).read()
for a in ('extern void rec_hook(void);\n','void *spooky_recp = (void *)rec_hook;','extern void mm_proj_thunk(void), mm_water_thunk(void);\n','void *spooky_mm_proj = (void *)mm_proj_thunk;','void *spooky_mm_water = (void *)mm_water_thunk;'):
    i=s.index(a); j=s.index('\n',i)+1; s=s[:i]+s[j:]
open(p,'w').write(s); print('ok2')

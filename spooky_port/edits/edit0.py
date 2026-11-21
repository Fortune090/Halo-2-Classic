# edit0.py: small fixes so the file compiles here
p='/home/claude/native/layer3/sg/spooky.c'; s=open(p).read()
a='"Spooky Ghost Menu v2.0.40 running in Halo 2 " H2_VERSION_TEXT " (modified default.xbe); per-frame hook OK; log on "'
assert a in s; s=s.replace(a,'"Spooky Ghost Menu v2.0.40 running in H2Classic; per-frame hook OK; log on "')
assert 'static const u32 at[4] = { ADDR_PROJ_STORE' in s
s=s.replace('static const u32 at[4] = { ADDR_PROJ_STORE','const u32 at[4] = { ADDR_PROJ_STORE',1)
a="""	spooky_light_loop = 0;                                                  /* 6i.86 */
	if (g_rs_on && g_rsc[4] < 99) g_rsc[4]++;                              /* 6i.64: the fallback counted */
	rs_stretch();                                                           /* 6i.61 */
"""
assert a in s; s=s.replace(a,'')
a='		else if (SPOOKY_LOG && SPOOKY_CAPTURE_KEY && white && !(buttons & BTN_BACK) && (pressed & BTN_UP)) capture_ab_start();'
i=s.index(a); j=s.index('\n',i)+1; s=s[:i]+s[j:]
open(p,'w').write(s); print('ok')

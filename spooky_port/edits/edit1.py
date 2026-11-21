# edit1.py: the first hand edits of the port's spooky.c (run once on the pruned copy; asserts every anchor)
import re
p='/home/claude/native/layer3/sg/spooky.c'; s=open(p).read()
def cut_fn(s,head,new):
    i=s.index(head); j=s.index('\n}\n',i)+3
    return s[:i]+new+s[j:]
def rm(s,text,count=1):
    assert s.count(text)==count,(text[:60],s.count(text)); return s.replace(text,'')
# E1: the frame clock. H2Classic's own pacing (timing.c) does what the mod's Unlock FPS did; the mod keeps its 30 Hz clock.
s=cut_fn(s,'static void frame_time_update(void)\n{','''static void frame_time_update(void)
{
	/* H2Classic: the mod's 30 Hz clock from the game's vertical-blank count, as the console build keeps it with Unlock FPS
	 * on. The rate is the one the layer gives the game (timing.c, h2n_refresh: the display's with Unlock performance, 60
	 * without); the pacing, the display settings, the frame-time figures and the render switches the console build also
	 * set here belong to features this build has in the layer or leaves out. */
	u32 now = M32(ADDR_VBLANK_COUNT), hz = (u32)(s32)h2n_refresh, v;
	if (!(hz >= 24u && hz <= 500u)) hz = 60u;
	g_hz = hz;
	v = now - g_vb_last; g_vb_last = now;
	{ u32 vmax = (8u * hz + 30u) / 60u; if (v > vmax) v = vmax; }         /* the first frame on, a level load: 2/15 s at most */
	g_vb = (s32)v;
	g_step_acc += v * 30u;
	g_steps = (s32)(g_step_acc / hz); g_step_acc %= hz;
	g_fdt = (float)v / (float)hz;
	g_t30 += (u32)g_steps;
}
''')
# E2: the per-pass update
s=rm(s,'''	if (g_ip_own_set || g_ip_blended) guarded_call(ip_unblend);             /* 6i.48: (none should be: the draw puts all back) */
	if (g_ip_mem > 0 && !o_unlock_fps && o_speed >= 10) ip_give_back("Unlock FPS off");   /* 6i.2 */
''')
s=rm(s,'''	guarded_call(af_step);                                                /* 6i.128: the rocket launcher's animation fix */
''')
s=rm(s,'''	draw_dist_frame();                                                    /* v2.0.11: the instance fade's constants for Draw Distance */
''')
s=rm(s,'''	zoom_hold_fix();                                                      /* v2.0: the zoom's hold, by the clock */
''')
i=s.index('		guarded_call(mem_size_note);                                         /* 6i.171 */'); j=s.index('#endif\n',i)+7
s=s[:i]+s[j:]
i=s.index('	{\n		static u32 more;'); j=s.index('	}\n',i)+3
s=s[:i]+s[j:]
s=rm(s,'''			timing_lines();                                                     /* 6i.3 / 6i / 6i.4 */
''')
# E3: the menus this build does not have (Game Hacks and all under it): their slots stay, so every menu keeps its number
s=rm(s,'''	{ "Game Hacks  >", K_SUB, 0, 10, 0 },                   /* 6i.40 */
''')
for name in ('m_hacks','m_perf','m_tb_confirm','m_lc_confirm'):
    i=s.index('static const s_item %s[] = {'%name); j=s.index('\n};\n',i)+4
    s=s[:i]+s[j:]
s=s.replace('static s_menu g_menus[] = {','static const s_item m_none[] = { { "Back", K_BACK, 0, 0, 0 } };   /* H2Classic: a menu of the console build this build does not have */\nstatic s_menu g_menus[] = {',1)
for a in ('{ "SPOOKY GHOST  Game Hacks", m_hacks, NOF(m_hacks), 0 }','{ "SPOOKY GHOST  Performance Mode Settings", m_perf, NOF(m_perf), 10 }',
          '{ "This will force the game to restart, are you sure?", m_tb_confirm, NOF(m_tb_confirm), 10 }','{ "This will restart the level, are you sure?", m_lc_confirm, NOF(m_lc_confirm), 10 }'):
    assert s.count(a)==1,a; s=s.replace(a,'{ "", m_none, NOF(m_none), 0 }')
s=rm(s,'''	if (it->kind == K_STAY && it->lo == 20) { if (activate) perf_defaults(); return; }   /* 6i.120: at once, the menu left open */
''')
i=s.index('		if (it->value == &o_big_cache && !big_cache_can())'); j=s.index('\n',s.index('if (it->value == &o_big_cache && lc_level_loaded())',i))+1
s=s[:i]+s[j:]
open(p,'w').write(s)
print('ok')

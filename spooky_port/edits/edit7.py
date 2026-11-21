# edit7.py: the settings file is read at the first main-loop pass (the console build reads it from its Direct3D
# device-creation hook, with the Triple Buffering switch that this build does not have)
p='/home/claude/native/layer3/sg/spooky.c'; s=open(p).read()
a='''	u32 t;
	g_count_main++;
	frame_time_update();'''
assert s.count(a)==1
s=s.replace(a,'''	u32 t;
	if (!g_settings_loaded) settings_load();                              /* H2Classic: spooky_settings.txt (h2classic\\\\T), once */
	g_count_main++;
	frame_time_update();''')
open(p,'w').write(s); print('ok')

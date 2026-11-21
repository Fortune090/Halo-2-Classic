# edit8.py: Triple Buffering (a console setting) out of the settings file; settings_load answers 0 (no such line)
p='/home/claude/native/layer3/sg/spooky.c'; s=open(p).read()
def rep(a,b):
    global s
    assert s.count(a)==1,(a[:70],s.count(a)); s=s.replace(a,b)
rep('''	s32 at = 0, m, i;
	const char *t = "Triple Buffering=";
	while (*t && at < cap - 8) b[at++] = *t++;
	b[at++] = spooky_triple ? '1' : '0'; b[at++] = '\\r'; b[at++] = '\\n';
''','''	s32 at = 0, m, i;
	const char *t;
''')
rep('''	if (klen == 16 && s_same(key, "Triple Buffering", 16)) { spooky_triple = (u8)(v != 0); return 1; }
''','')
open(p,'w').write(s); print('ok')
s=open(p).read()
i=s.index('		if (g_menu == 13 || g_menu == LC_MENU)'); j=s.index('		}\n',i)+4
s=s[:i]+s[j:]
open(p,'w').write(s); print('ok2')
# Larger Caches (128 MB consoles): its action and its default
s=open(p).read()
for a in ('	if (what == 15) { lc_toggle_restart(); return; }','	lc_default();  '):
    i=s.index(a); j=s.index('\n',i)+1; s=s[:i]+s[j:]
open(p,'w').write(s); print('ok3')

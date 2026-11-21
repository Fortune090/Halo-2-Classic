# edit9.py: Game Hacks is back with the one row of it this build has - Tick Rate (25 / 30 / 60)
p='/home/claude/native/layer3/sg/spooky.c'; s=open(p).read()
a='static const s_item m_none[] = {'
assert s.count(a)==1
s=s.replace(a,'''static const s_item m_hacks[] = {              /* Game Hacks: of the console build's rows, Tick Rate (the others are H2Classic's own settings or the console's hardware) */
	{ "Tick Rate", K_INT, &o_trate, 0, 2 },                              /* 6i.120: 25 / 30 / 60; 6i.121: from Other Tools */
};
'''+a)
a='{ "", m_none, NOF(m_none), 0 }'
assert s.count(a)==4
s=s.replace(a,'{ "SPOOKY GHOST  Game Hacks", m_hacks, NOF(m_hacks), 0 }',1)      # slot 10, the first of the four
a='	{ "Text scale", K_INT, &o_scale, 6, 14 },'
assert s.count(a)==1
s=s.replace(a,'	{ "Game Hacks  >", K_SUB, 0, 10, 0 },                   /* 6i.40 */\n'+a)
open(p,'w').write(s); print('ok')

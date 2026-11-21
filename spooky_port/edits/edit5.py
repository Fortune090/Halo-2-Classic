# edit5.py: the mod's log. Off as in the console build (SPOOKY_LOG 0); a test build made with -DSG_LOG=1 has every line
# of it in H2Classic's own log (h2classic_log.txt), in place of T:\spooky_log.txt and the console's live page.
p='/home/claude/native/layer3/sg/spooky.c'; s=open(p).read()
a='#define SPOOKY_LOG 0\n'
assert s.count(a)==1; s=s.replace(a,'#ifdef SG_LOG\n#define SPOOKY_LOG 1\n#else\n#define SPOOKY_LOG 0\n#endif\n')
i=s.index('static void log_write(void)\n{\n\tu32 n = 0, written;'); j=s.index('\n}\n',i)+3
s=s[:i]+'static void log_write(void) { if (o_log) h2n_log("sg: %s\\n", g_log); }   /* H2Classic: into the program\'s own log */\n'+s[j:]
open(p,'w').write(s); print('ok')
s=open(p).read()
a='static s32 o_diag = 0;\n'
assert s.count(a)==1; s=s.replace(a,'#ifdef SG_DIAG\nstatic s32 o_diag = 1;     /* (a test build here: the development lines too) */\n#else\nstatic s32 o_diag = 0;\n#endif\n')
open(p,'w').write(s); print('ok diag')

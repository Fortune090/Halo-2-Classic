# edit4.py: what the port does not have YET stays out of reach (menu rows and shortcuts), until its hooks are in:
# SG_CAMERAS (third person, free camera, Field of View, Hide HUD, Centered Crosshair), SG_TRIGGERS (Trigger Volume
# Overlay), SG_WIREFRAME. Each switch is in sg_port.h; the code stays.
p='/home/claude/native/layer3/sg/spooky.c'; s=open(p).read()
def rep(a,b):
    global s
    assert s.count(a)==1,(a[:70],s.count(a)); s=s.replace(a,b)
rep('		if ((buttons & BTN_BACK) && black && !((pad->buttons & BTN_BACK) && pad->black)) r = 6;','		if (SG_CAMERAS && (buttons & BTN_BACK) && black && !((pad->buttons & BTN_BACK) && pad->black)) r = 6;')
rep('		else if ((buttons & BTN_BACK) && white && !pad->white) r = 7;','		else if (SG_CAMERAS && (buttons & BTN_BACK) && white && !pad->white) r = 7;')
rep('		else if ((buttons & BTN_BACK) && (pressed & BTN_DOWN)) r = 5;','		else if (SG_CAMERAS && (buttons & BTN_BACK) && (pressed & BTN_DOWN)) r = 5;')
def row(a,sw):
    global s
    i=s.index(a); j=s.index('\n',i)+1
    s=s[:i]+'#if '+sw+'\n'+s[i:j]+'#endif\n'+s[j:]
row('	{ "Camera Settings  >", K_SUB, 0, 3, 0 },','SG_CAMERAS')
row('	{ "Trigger Volume Overlay", K_TOGGLE, &o_trig, 0, 1 },','SG_TRIGGERS')
row('	{ "Trigger Volume Overlay Settings  >", K_SUB, 0, 12, 0 },','SG_TRIGGERS')
row('	{ "Wireframe", K_INT, &o_wire, 0, 2 },','SG_WIREFRAME')
open(p,'w').write(s); print('ok')

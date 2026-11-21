/* H2Classic: the controller. The game asks the console for the state of the gamepad in port 1; this file answers
 * from a real controller (through whichever XInput library Windows has) and from the keyboard, while the game's
 * window is the active one. */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void h2n_log(const char *format, ...);
double h2n_seconds(void);
unsigned h2n_frames(void);
void *gx_window_handle(void);
int gx_pointer_over(void);

typedef struct h2n_pad { unsigned buttons; unsigned char analog[8]; short lx, ly, rx, ry; } h2n_pad;
enum { PAD_UP = 1, PAD_DOWN = 2, PAD_LEFT = 4, PAD_RIGHT = 8, PAD_START = 0x10, PAD_BACK = 0x20, PAD_LEFT_THUMB = 0x40, PAD_RIGHT_THUMB = 0x80 };
enum { AN_A, AN_B, AN_X, AN_Y, AN_BLACK, AN_WHITE, AN_LEFT_TRIGGER, AN_RIGHT_TRIGGER };

typedef struct pc_state { DWORD packet; WORD buttons; BYTE left_trigger, right_trigger; SHORT lx, ly, rx, ry; } pc_state;
typedef struct pc_vibration { WORD left, right; } pc_vibration;     /* XInput's XINPUT_VIBRATION */
static DWORD (WINAPI *g_pc_get_state)(DWORD, pc_state *);
static DWORD (WINAPI *g_pc_set_state)(DWORD, pc_vibration *);
static const char *g_pc_library;
static int g_pc_looked, g_pc_seen;

/* presses asked for on the command line of a test: H2N_KEYS=start@12,a@15.5 (a name, and the second it is pressed at) */
static struct { unsigned button; int analog; double at; unsigned frame; } g_script[64];     /* at a second, or (name@f123) at a frame */
static unsigned g_script_count, g_script_polls[64];
static int g_script_read;

static const struct { const char *name; unsigned button; int analog; } g_names[] = {
	{ "lthumb", PAD_LEFT_THUMB, -1 }, { "rthumb", PAD_RIGHT_THUMB, -1 }, { "start", PAD_START, -1 }, { "back", PAD_BACK, -1 }, { "up", PAD_UP, -1 }, { "down", PAD_DOWN, -1 }, { "left", PAD_LEFT, -1 }, { "right", PAD_RIGHT, -1 },
	{ "a", 0, AN_A }, { "b", 0, AN_B }, { "x", 0, AN_X }, { "y", 0, AN_Y }, { "black", 0, AN_BLACK }, { "white", 0, AN_WHITE }, { "lt", 0, AN_LEFT_TRIGGER }, { "rt", 0, AN_RIGHT_TRIGGER } };

/* presses fed to a running test: H2N_KEYFILE names a text file, and every line added to it while the game runs is one press
 * (a button name, or forward/backward/left/right-of-stick names, optionally name*readings to hold it), "shot" to save the frame, or "trace" for what F11 does */
void gx_request_shot(void);
void h2n_write_stop_file(EXCEPTION_POINTERS *e, const char *reason);
void gx_request_trace(void);
static const char *g_keyfile;
static long g_keyfile_offset;
static int g_keyfile_looked, g_live_polls, g_live_hold, g_live_analog = -1, g_live_stick = -1, g_live_port;
static int g_live_shots;                    /* "name*N+shots": every frame of the hold saved too (for testing here) */   /* g_live_port: "2:a" presses port 2's A */
static unsigned g_live_button;
static volatile LONG g_mouse_tdx, g_mouse_tdy;  /* (tests) mouse counts from the key file: "mouse <dx> <dy>" */
static int g_km_last, g_mouse_used;
static void apply_press(h2n_pad *pad, int *lx, int *ly, int *rx, int *ry)
{
	pad->buttons |= g_live_button;
	if (g_live_analog >= 0) pad->analog[g_live_analog] = 255;
	switch (g_live_stick)
	{
	case 0: *ly += 32767; break; case 1: *ly -= 32767; break; case 2: *lx -= 32767; break; case 3: *lx += 32767; break;
	case 4: *ry += 32767; break; case 5: *ry -= 32767; break; case 6: *rx -= 32767; break; case 7: *rx += 32767; break;
	}
}
static void live_presses(h2n_pad *pad, int *lx, int *ly, int *rx, int *ry, unsigned port)
{
	static const char *const sticks[8] = { "forward", "backward", "strafeleft", "straferight", "lookup", "lookdown", "lookleft", "lookright" };
	unsigned i;
	if (!g_keyfile_looked) { g_keyfile_looked = 1; g_keyfile = getenv("H2N_KEYFILE"); if (g_keyfile) h2n_log("       controller: presses are also read from %s as lines are added to it (H2N_KEYFILE)\n", g_keyfile); }
	if (!g_keyfile) return;
	if (port)                                               /* ports 2-4: the press now being held, if it is theirs */
	{
		if (g_live_polls > 2 && g_live_port == (int)port) apply_press(pad, lx, ly, rx, ry);
		return;
	}
	if (g_live_polls <= 0)
	{
		char line[64];
		DWORD n = 0;
		char *end, *star;
		/* Windows' own file calls, found by name: the ones this exe links to are the game's (logged, and with the console's drive letters) */
		static HANDLE (WINAPI *create)(const char *, DWORD, DWORD, void *, DWORD, DWORD, HANDLE);
		static BOOL (WINAPI *read)(HANDLE, void *, DWORD, DWORD *, void *);
		static DWORD (WINAPI *seek)(HANDLE, LONG, LONG *, DWORD);
		HANDLE f;
		if (!create)
		{
			HMODULE k = GetModuleHandleA("kernel32.dll");
			create = (void *)GetProcAddress(k, "CreateFileA"); read = (void *)GetProcAddress(k, "ReadFile"); seek = (void *)GetProcAddress(k, "SetFilePointer");
		}
		if (!create || !read || !seek) return;
		f = create(g_keyfile, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);     /* opened for reading only */
		if (f == INVALID_HANDLE_VALUE) return;
		seek(f, g_keyfile_offset, NULL, FILE_BEGIN);
		if (!read(f, line, sizeof(line) - 1, &n, NULL)) n = 0;
		CloseHandle(f);
		line[n] = 0;
		end = strchr(line, '\n');
		if (!end) return;                                   /* a line counts once it is complete */
		g_keyfile_offset += (long)(end - line) + 1;
		*end = 0;
		if ((end = strchr(line, '\r')) != NULL) *end = 0;
		g_live_hold = 2;
		g_live_shots = 0;
		if ((star = strstr(line, "+shots")) != NULL) { *star = 0; g_live_shots = 1; }
		if ((star = strchr(line, '*')) != NULL) { *star = 0; g_live_hold = atoi(star + 1); if (g_live_hold < 1) g_live_hold = 1; }
		g_live_button = 0; g_live_analog = -1; g_live_stick = -1; g_live_port = 0;
		if (line[0] >= '1' && line[0] <= '4' && line[1] == ':') { g_live_port = line[0] - '1'; memmove(line, line + 2, strlen(line + 2) + 1); }
		if (!strcmp(line, "shot")) { gx_request_shot(); return; }
		if (!strcmp(line, "memory")) { h2n_write_stop_file(NULL, "asked for in the key file"); h2n_log("       h2classic_stop.bin written on request (the run goes on)\n"); return; }
		if (!strcmp(line, "trace")) { gx_request_trace(); return; }      /* as F11: the next two frames are described in the log */
		if (!strncmp(line, "slowmo ", 7)) { extern void h2n_set_slowmo(int n); h2n_set_slowmo(atoi(line + 7)); return; }   /* (testing only) */
		if (!strncmp(line, "mouse ", 6))                    /* (testing only) the mouse moved by so many counts, as if the window had it */
		{
			long dx = 0, dy = 0;
			if (sscanf(line + 6, "%ld %ld", &dx, &dy) >= 1) { InterlockedExchangeAdd(&g_mouse_tdx, dx); InterlockedExchangeAdd(&g_mouse_tdy, dy); g_km_last = 1; g_mouse_used = 1; }
			h2n_log("       controller: \"%s\" from the key file at frame %u\n", line, h2n_frames());
			return;
		}
		if (!strcmp(line, "sun")) { extern void __cdecl h2n_test_face_sun(void); h2n_test_face_sun(); h2n_log("       controller: \"sun\" from the key file at frame %u\n", h2n_frames()); return; }   /* (testing only) */
		if (!strcmp(line, "widefix0") || !strcmp(line, "widefix1")) { extern void __cdecl h2n_test_widefix(int on); h2n_test_widefix(line[7] == '1'); h2n_log("       controller: \"%s\" from the key file at frame %u\n", line, h2n_frames()); return; }   /* (testing only) */
		if (!strcmp(line, "tracewindow")) { extern void __cdecl h2n_trace_window_start(void); h2n_trace_window_start(); return; }   /* as F9 */
		{	/* (sg1, tests) "back+up": several names held together - each one's button; one of them may be an analog button */
			char part[16]; const char *q = line;
			while (*q)
			{
				unsigned n = 0;
				while (*q && *q != '+' && n < 15) part[n++] = *q++;
				part[n] = 0; if (*q == '+') q++;
				for (i = 0; i < sizeof(g_names) / sizeof(g_names[0]); i++) if (!strcmp(part, g_names[i].name)) { g_live_button |= g_names[i].button; if (g_names[i].analog >= 0) g_live_analog = g_names[i].analog; }
			}
		}
		for (i = 0; i < 8; i++) if (!strcmp(line, sticks[i])) g_live_stick = (int)i;
		if (!g_live_button && g_live_analog < 0 && g_live_stick < 0) { h2n_log("       controller: \"%s\" in the key file is not a name this build knows\n", line); return; }
		h2n_log("       controller: \"%s\" from the key file at frame %u\n", line, h2n_frames());
		g_live_polls = g_live_hold + 2;                     /* held, then let go for two readings before the next line is taken */
	}
	if (g_live_polls > 2 && g_live_port == 0) apply_press(pad, lx, ly, rx, ry);
	if (g_live_polls > 2 && g_live_shots) gx_request_shot();
	g_live_polls--;
}

static void read_script(void)
{
	const char *list = getenv("H2N_KEYS");
	g_script_read = 1;
	while (list && *list && g_script_count < 64)
	{
		char name[16];
		unsigned n = 0, i;
		while (*list && *list != '@' && *list != ',' && n < 15) name[n++] = *list++;
		name[n] = 0;
		if (*list != '@') break;
		for (i = 0; i < sizeof(g_names) / sizeof(g_names[0]); i++)
			if (!strcmp(name, g_names[i].name))
			{
				g_script[g_script_count].button = g_names[i].button; g_script[g_script_count].analog = g_names[i].analog;
				if (list[1] == 'f') { g_script[g_script_count].frame = (unsigned)strtoul(list + 2, NULL, 10); g_script[g_script_count].at = -1.0; }
				else { g_script[g_script_count].frame = 0; g_script[g_script_count].at = strtod(list + 1, NULL); }
				g_script_count++;
			}
		while (*list && *list != ',') list++;
		if (*list == ',') list++;
	}
	if (g_script_count) h2n_log("       controller: %u scripted presses (H2N_KEYS)\n", g_script_count);
}

static int key(int code) { return (GetAsyncKeyState(code) & 0x8000) != 0; }

/* ---- (s10v) the keyboard and the mouse as port 1's controller: every button's keys can be chosen ----
 * H2Classic Settings (Keyboard + Mouse Settings) saves two keys for each of the controller's buttons and for each way of
 * the two sticks, as key_<name>=<key>,<key> in settings.ini; main.c hands them on as H2N_KEY_<NAME>. A key is one of
 * Windows' virtual-key codes - the mouse's buttons are among them (1 left, 2 right, 4 middle, 5 and 6 the side ones) -
 * or 256 for the mouse wheel (either way: a notch holds the button for 80 ms, three readings at least); 0 is no key. With nothing saved
 * the keys are the ones the keyboard had before (s9), and on the mouse: left = the right trigger, right = the left
 * trigger, middle = the right stick's click, the wheel = Y.
 * The mouse's movement does not go through the sticks: it is added to the look where the game works it out (interp.c
 * h2n_look_end), as Halo 2 for Windows Vista does. Here are its counts (raw input, gfx9.c) and its three settings. */
enum { BIND_START, BIND_BACK, BIND_UP, BIND_DOWN, BIND_LEFT, BIND_RIGHT, BIND_A, BIND_B, BIND_X, BIND_Y, BIND_WHITE, BIND_BLACK, BIND_LT, BIND_RT,
	BIND_LTHUMB, BIND_RTHUMB, BIND_FORWARD, BIND_BACKWARD, BIND_STRAFELEFT, BIND_STRAFERIGHT, BIND_LOOKUP, BIND_LOOKDOWN, BIND_LOOKLEFT, BIND_LOOKRIGHT, BIND_COUNT };
#define KEY_WHEEL 256
static const struct { const char *name; int keys[2]; } g_bind_table[BIND_COUNT] = {
	{ "START", { VK_RETURN, 0 } }, { "BACK", { VK_BACK, VK_ESCAPE } }, { "UP", { VK_UP, 0 } }, { "DOWN", { VK_DOWN, 0 } }, { "LEFT", { VK_LEFT, 0 } }, { "RIGHT", { VK_RIGHT, 0 } },
	{ "A", { 'Z', VK_SPACE } }, { "B", { 'X', 0 } }, { "X", { 'C', 0 } }, { "Y", { 'V', KEY_WHEEL } }, { "WHITE", { '1', 0 } }, { "BLACK", { '2', 0 } },
	{ "LT", { 'Q', VK_RBUTTON } }, { "RT", { 'E', VK_LBUTTON } }, { "LTHUMB", { 'F', 0 } }, { "RTHUMB", { 'G', VK_MBUTTON } },
	{ "FORWARD", { 'W', 0 } }, { "BACKWARD", { 'S', 0 } }, { "STRAFELEFT", { 'A', 0 } }, { "STRAFERIGHT", { 'D', 0 } },
	{ "LOOKUP", { 'I', 0 } }, { "LOOKDOWN", { 'K', 0 } }, { "LOOKLEFT", { 'J', 0 } }, { "LOOKRIGHT", { 'L', 0 } } };
static int g_binds[BIND_COUNT][2], g_binds_read;
/* the wheel's "key": down for 80 ms after the wheel turned, and for three readings of the controller at least (a
   slow frame rate reads less often than that) */
static volatile DWORD g_wheel_tick; static volatile LONG g_wheel_turned, g_wheel_polls;
static int wheel_down(void) { return g_wheel_turned && (g_wheel_polls > 0 || GetTickCount() - g_wheel_tick < 80); }
static volatile LONG g_mouse_dx, g_mouse_dy, g_mouse_capture;
static int g_km_last, g_mouse_used;              /* the keyboard or mouse was used last (not the controller); the mouse has moved since */
static float g_mouse_sens = -1.0f, g_mouse_zoom = -1.0f;
static int g_mouse_invert = -1;
static void binds_read(void)
{
	unsigned i;
	g_binds_read = 1;
	for (i = 0; i < BIND_COUNT; i++)
	{
		char name[40];
		const char *v;
		int a = 0, b = 0;
		snprintf(name, sizeof(name), "H2N_KEY_%s", g_bind_table[i].name);
		v = getenv(name);
		if (v && sscanf(v, "%d,%d", &a, &b) >= 1 && a >= 0 && a <= KEY_WHEEL && b >= 0 && b <= KEY_WHEEL) { g_binds[i][0] = a; g_binds[i][1] = b; }
		else { g_binds[i][0] = g_bind_table[i].keys[0]; g_binds[i][1] = g_bind_table[i].keys[1]; }
	}
}
/* a mouse button counts while the pointer is over the game's picture (or held in it), and not the press that brought the
   window to the front: after the window becomes the active one, every mouse button has to have been up once */
static int g_mouse_buttons_ok, g_mouse_armed;
static int mouse_button(int code) { return code == VK_LBUTTON || code == VK_RBUTTON || code == VK_MBUTTON || code == VK_XBUTTON1 || code == VK_XBUTTON2; }
static int bind_down(int which)
{
	int k;
	for (k = 0; k < 2; k++)
	{
		int code = g_binds[which][k];
		if (mouse_button(code) && !g_mouse_buttons_ok) continue;
		if (code == KEY_WHEEL ? wheel_down() : code > 0 && key(code)) return 1;
	}
	return 0;
}
/* Enable Keyboard + Mouse (Keyboard + Mouse Settings; keyboard_mouse in settings.ini, H2N_KBM): off = the keyboard and the
   mouse are not port 1's controller - no key is read as a button, the mouse does not look and the pointer is left alone */
static int g_kbm = -1;
int __cdecl h2n_kbm_on(void) { if (g_kbm < 0) { const char *v = getenv("H2N_KBM"); g_kbm = !v || !*v || atoi(v) || !_stricmp(v, "on") ? 1 : 0; } return g_kbm; }
static float env_float(const char *name, float otherwise, float low, float high)
{
	const char *v = getenv(name);
	float f = v && *v ? (float)atof(v) : otherwise;
	return f < low ? low : f > high ? high : f;
}
/* the mouse's settings: degrees a count = 0.022 x Mouse Sensitivity (the scale many PC shooters use); Zoom Sensitivity
   multiplies that while zoomed, on top of the game's own rule there (1 = the game's rule alone); the mouse's own inversion */
float __cdecl h2n_mouse_sensitivity(void) { if (g_mouse_sens < 0.0f) g_mouse_sens = env_float("H2N_MOUSE_SENS", 2.5f, 0.05f, 50.0f); return g_mouse_sens; }
float __cdecl h2n_mouse_zoom(void) { if (g_mouse_zoom < 0.0f) g_mouse_zoom = env_float("H2N_MOUSE_ZOOM", 1.0f, 0.05f, 10.0f); return g_mouse_zoom; }
int __cdecl h2n_mouse_invert(void) { if (g_mouse_invert < 0) { const char *v = getenv("H2N_MOUSE_INVERT"); g_mouse_invert = v && atoi(v) ? 1 : 0; } return g_mouse_invert; }
/* (settings changed while the game runs - main.c h2n_live_poll: the keys and the mouse's settings are read again) */
void __cdecl h2n_input_live(void) { g_binds_read = 0; g_mouse_sens = g_mouse_zoom = -1.0f; g_mouse_invert = -1; g_kbm = -1; if (!h2n_kbm_on()) { g_km_last = 0; g_mouse_used = 0; } h2n_log("       controller: the keys and the mouse's settings were read again\n"); }
/* gfx9.c: the mouse moved (raw input counts) or its wheel turned, while the game's window has the pointer */
void __cdecl h2n_mouse_raw(long dx, long dy)
{
	if (!g_mouse_capture || (!dx && !dy)) return;
	InterlockedExchangeAdd(&g_mouse_dx, dx); InterlockedExchangeAdd(&g_mouse_dy, dy);
	g_km_last = 1; g_mouse_used = 1;
}
void __cdecl h2n_mouse_wheel(void) { g_wheel_tick = GetTickCount(); g_wheel_polls = 3; g_wheel_turned = 1; }
void __cdecl h2n_mouse_captured(int on) { if (!on) { InterlockedExchange(&g_mouse_dx, 0); InterlockedExchange(&g_mouse_dy, 0); } InterlockedExchange(&g_mouse_capture, on); }
/* interp.c: the counts since the last time, taken */
void __cdecl h2n_mouse_take(long *dx, long *dy) { *dx = InterlockedExchange(&g_mouse_dx, 0) + InterlockedExchange(&g_mouse_tdx, 0); *dy = InterlockedExchange(&g_mouse_dy, 0) + InterlockedExchange(&g_mouse_tdy, 0); }
/* the player on port 1 is playing with the mouse now: the keyboard or mouse was used after the controller, and the mouse has moved */
int __cdecl h2n_mouse_player(void) { return g_km_last && g_mouse_used; }
/* gfx9.c: the keyboard or mouse is what port 1's player used last (a key, a mouse button, the wheel): the pointer may be
   held for the look. A player on a controller keeps a free pointer, as before this build. */
int __cdecl h2n_km_player(void) { return h2n_kbm_on() && g_km_last; }

void h2n_pad_read(h2n_pad *pad);
/* one of the PC's controllers (XInput user index 0..3) into a pad; 1 if it is connected */
static int pc_read(unsigned index, h2n_pad *pad, int *lx, int *ly, int *rx, int *ry)
{
	pc_state s;
	if (!g_pc_get_state) return 0;
	memset(&s, 0, sizeof(s));
	if (g_pc_get_state(index, &s) != ERROR_SUCCESS) return 0;
	pad->buttons |= s.buttons & 0xff;
	if (s.buttons & 0x1000) pad->analog[AN_A] = 255;
	if (s.buttons & 0x2000) pad->analog[AN_B] = 255;
	if (s.buttons & 0x4000) pad->analog[AN_X] = 255;
	if (s.buttons & 0x8000) pad->analog[AN_Y] = 255;
	if (s.buttons & 0x0100) pad->analog[AN_WHITE] = 255;
	if (s.buttons & 0x0200) pad->analog[AN_BLACK] = 255;
	pad->analog[AN_LEFT_TRIGGER] = s.left_trigger; pad->analog[AN_RIGHT_TRIGGER] = s.right_trigger;
	*lx = s.lx; *ly = s.ly; *rx = s.rx; *ry = s.ry;
	return 1;
}
static void pc_library(void)
{
	unsigned i;
	if (g_pc_looked) return;
	{
		static const char *const libraries[3] = { "xinput1_4.dll", "xinput1_3.dll", "xinput9_1_0.dll" };
		g_pc_looked = 1;
		for (i = 0; i < 3 && !g_pc_get_state; i++)
		{
			HMODULE library = LoadLibraryA(libraries[i]);
			if (library) g_pc_get_state = (void *)GetProcAddress(library, "XInputGetState");
			if (g_pc_get_state)
			{
				g_pc_set_state = (void *)GetProcAddress(library, "XInputSetState");
				g_pc_library = libraries[i];
				h2n_log("       controller: real controllers are read through %s; the keyboard works as well (see README)\n", libraries[i]);
			}
		}
		if (!g_pc_get_state) h2n_log("       controller: no XInput library on this system; the keyboard is the controller (see README)\n");
	}
}
/* the console's ports 2-4: the PC's controllers 2-4 (XInput user indices 1-3), each in the port of its own number.
 * Port 1 is always there (the PC's first controller, the keyboard and the test presses). Asking XInput about a controller
 * that is not connected is slow on some systems, so an empty port is looked at again only every half second. */
static double g_port_looked[4];
static int g_port_there[4];
static int g_test_pads = -1;                      /* H2N_TESTPADS=n (tests): ports 2..n reported plugged in, pressed through the key file ("2:a") */
static int test_pad(unsigned port) { if (g_test_pads < 0) g_test_pads = getenv("H2N_TESTPADS") ? atoi(getenv("H2N_TESTPADS")) : 0; return (int)port < g_test_pads; }
int h2n_pad_connected(unsigned port)
{
	double now;
	h2n_pad pad;
	int lx, ly, rx, ry;
	if (port == 0) return 1;
	if (port > 3) return 0;
	if (test_pad(port)) return 1;
	pc_library();
	if (!g_pc_get_state) return 0;
	now = h2n_seconds();
	if (!g_port_there[port] && now - g_port_looked[port] < 0.5 && g_port_looked[port] > 0.0) return 0;
	g_port_looked[port] = now;
	memset(&pad, 0, sizeof(pad));
	g_port_there[port] = pc_read(port, &pad, &lx, &ly, &rx, &ry);
	return g_port_there[port];
}
/* s9w: a controller reads as untouched while another program has the focus (no window, as in tests: always read) */
static int window_focused(void) { void *window = gx_window_handle(); return !window || GetForegroundWindow() == (HWND)window; }
void h2n_pad_read_port(unsigned port, h2n_pad *pad)
{
	int lx = 0, ly = 0, rx = 0, ry = 0;
	memset(pad, 0, sizeof(*pad));
	if (port == 0) { h2n_pad_read(pad); return; }
	pc_library();
	if (port > 3) return;
	if (!pc_read(port, pad, &lx, &ly, &rx, &ry))
	{
		g_port_there[port] = 0;
		if (!test_pad(port)) return;
		memset(pad, 0, sizeof(*pad)); lx = ly = rx = ry = 0;
	}
	else if (!window_focused()) { memset(pad, 0, sizeof(*pad)); lx = ly = rx = ry = 0; }
	live_presses(pad, &lx, &ly, &rx, &ry, port);
	pad->lx = (short)(lx > 32767 ? 32767 : lx < -32768 ? -32768 : lx); pad->ly = (short)(ly > 32767 ? 32767 : ly < -32768 ? -32768 : ly);
	pad->rx = (short)(rx > 32767 ? 32767 : rx < -32768 ? -32768 : rx); pad->ry = (short)(ry > 32767 ? 32767 : ry < -32768 ? -32768 : ry);
}

void h2n_pad_read(h2n_pad *pad)
{
	double now = h2n_seconds();
	unsigned i;
	int lx = 0, ly = 0, rx = 0, ry = 0;
	void *window = gx_window_handle();
	memset(pad, 0, sizeof(*pad));
	if (!g_script_read) read_script();
	pc_library();
	g_port_there[0] = pc_read(0, pad, &lx, &ly, &rx, &ry);
	if (g_port_there[0])
	{
		if (!g_pc_seen) { g_pc_seen = 1; h2n_log("       controller: a controller is connected\n"); }
		if (!window_focused()) { memset(pad, 0, sizeof(*pad)); lx = ly = rx = ry = 0; }
		/* (s10v) the controller in use: the mouse is then not "the player's" until the keyboard or mouse is used again */
		else if (pad->buttons || pad->analog[AN_A] || pad->analog[AN_B] || pad->analog[AN_X] || pad->analog[AN_Y] || pad->analog[AN_BLACK] || pad->analog[AN_WHITE]
			|| pad->analog[AN_LEFT_TRIGGER] > 40 || pad->analog[AN_RIGHT_TRIGGER] > 40 || lx > 12000 || lx < -12000 || ly > 12000 || ly < -12000 || rx > 12000 || rx < -12000 || ry > 12000 || ry < -12000)
			{ g_km_last = 0; g_mouse_used = 0; }
	}
	if (!window || GetForegroundWindow() != (HWND)window) g_mouse_armed = 0;
	else if (h2n_kbm_on())
	{
		/* (s10v) the keys by the table (H2Classic Settings' Keyboard + Mouse Settings); the keys of before are its defaults */
		static const struct { int bind; unsigned button; int analog; } map[16] = {
			{ BIND_START, PAD_START, -1 }, { BIND_BACK, PAD_BACK, -1 }, { BIND_UP, PAD_UP, -1 }, { BIND_DOWN, PAD_DOWN, -1 }, { BIND_LEFT, PAD_LEFT, -1 }, { BIND_RIGHT, PAD_RIGHT, -1 },
			{ BIND_A, 0, AN_A }, { BIND_B, 0, AN_B }, { BIND_X, 0, AN_X }, { BIND_Y, 0, AN_Y }, { BIND_WHITE, 0, AN_WHITE }, { BIND_BLACK, 0, AN_BLACK },
			{ BIND_LT, 0, AN_LEFT_TRIGGER }, { BIND_RT, 0, AN_RIGHT_TRIGGER }, { BIND_LTHUMB, PAD_LEFT_THUMB, -1 }, { BIND_RTHUMB, PAD_RIGHT_THUMB, -1 } };
		int any = 0;
		if (!g_binds_read) binds_read();
		if (!key(VK_LBUTTON) && !key(VK_RBUTTON) && !key(VK_MBUTTON) && !key(VK_XBUTTON1) && !key(VK_XBUTTON2)) g_mouse_armed = 1;
		g_mouse_buttons_ok = g_mouse_armed && gx_pointer_over();
		for (i = 0; i < 16; i++)
			if (bind_down(map[i].bind)) { any = 1; pad->buttons |= map[i].button; if (map[i].analog >= 0) pad->analog[map[i].analog] = 255; }
		if (bind_down(BIND_STRAFELEFT)) { lx -= 32767; any = 1; }
		if (bind_down(BIND_STRAFERIGHT)) { lx += 32767; any = 1; }
		if (bind_down(BIND_FORWARD)) { ly += 32767; any = 1; }
		if (bind_down(BIND_BACKWARD)) { ly -= 32767; any = 1; }
		if (bind_down(BIND_LOOKLEFT)) { rx -= 32767; any = 1; }
		if (bind_down(BIND_LOOKRIGHT)) { rx += 32767; any = 1; }
		if (bind_down(BIND_LOOKUP)) { ry += 32767; any = 1; }
		if (bind_down(BIND_LOOKDOWN)) { ry -= 32767; any = 1; }
		if (any) g_km_last = 1;
		{	/* (tests: H2N_MOUSE_TEST) which of the table's entries are down, when that changes */
			static int test = -1, lines;
			static unsigned was;
			unsigned now_down = 0;
			if (test < 0) test = getenv("H2N_MOUSE_TEST") ? 1 : 0;
			if (test && lines < 60)
			{
				for (i = 0; i < BIND_COUNT; i++) if (bind_down((int)i)) now_down |= 1u << i;
				if (now_down != was) { was = now_down; lines++; h2n_log("       mouse test: keys down %06x (bit = the table's entry: 6 A, 9 Y, 12 LT, 13 RT, 15 RTHUMB, 16 FORWARD); mouse buttons count: %d\n", now_down, g_mouse_buttons_ok); }
			}
		}
	}
	if (g_wheel_polls > 0) InterlockedDecrement(&g_wheel_polls);
	live_presses(pad, &lx, &ly, &rx, &ry, 0);
	for (i = 0; i < g_script_count; i++)
		if (g_script[i].at < 0.0 ? (h2n_frames() >= g_script[i].frame && g_script_polls[i] < 2 && ++g_script_polls[i]) : (now >= g_script[i].at && now < g_script[i].at + 0.12))     /* a frame-timed press lasts two readings */
		{
			pad->buttons |= g_script[i].button;
			if (g_script[i].analog >= 0) pad->analog[g_script[i].analog] = 255;
		}
	pad->lx = (short)(lx > 32767 ? 32767 : lx < -32768 ? -32768 : lx); pad->ly = (short)(ly > 32767 ? 32767 : ly < -32768 ? -32768 : ly);
	pad->rx = (short)(rx > 32767 ? 32767 : rx < -32768 ? -32768 : rx); pad->ry = (short)(ry > 32767 ? 32767 : ry < -32768 ? -32768 : ry);
}

/* s10l: the vibration. The game's rumble code sets the two motor speeds of each gamepad (function_124a40, 0 when the
 * player's profile has vibration off) and sends them with the console's XInputSetState each time it reads the controllers
 * (function_124ee0: 0 outside a game and while it is paused). xpad.cpp hands them here, and they go to the PC's controller
 * in the same port through XInput's XInputSetState: XInput's left motor is the heavy, slow one and its right motor the
 * light, fast one, as the console's controller's, both 0-65535, so the speeds go across unchanged. A controller is given
 * a speed only when it changes (the console sends every reading; the motors keep the last one either way), only while it
 * is connected, and stop while another program has the focus (the controller reads as untouched then as well) and when
 * the program closes. H2N_RUMBLE=0 (tests): nothing is sent. */
static int g_rumble_on = -1;
static int g_rumble_known[4];                    /* the PC's controller in the port holds g_rumble_left / right */
static WORD g_rumble_left[4], g_rumble_right[4];
static unsigned g_rumble_sends[4], g_rumble_failures[4];
void h2n_pad_rumble(unsigned port, unsigned left, unsigned right)
{
	pc_vibration v;
	DWORD result;
	if (port > 3) return;
	if (g_rumble_on < 0)
	{
		const char *e = getenv("H2N_RUMBLE");
		g_rumble_on = !(e && *e == '0');
		if (!g_rumble_on) h2n_log("       controller: the game's vibration is not sent to the controllers (H2N_RUMBLE=0)\n");
	}
	if (!g_rumble_on) return;
	pc_library();
	if (!g_pc_set_state || !g_port_there[port]) { g_rumble_known[port] = 0; return; }
	if (!window_focused()) left = right = 0;
	if (g_rumble_known[port] && g_rumble_left[port] == left && g_rumble_right[port] == right) return;
	v.left = (WORD)left; v.right = (WORD)right;
	result = g_pc_set_state(port, &v);
	if (result != ERROR_SUCCESS)
	{
		g_rumble_known[port] = 0;
		if (g_rumble_failures[port]++ < 3) h2n_log("       controller %u: the vibration (left motor %u, right motor %u) was not taken: XInputSetState gave %lu\n", port + 1, left, right, (unsigned long)result);
		return;
	}
	g_rumble_known[port] = 1; g_rumble_left[port] = (WORD)left; g_rumble_right[port] = (WORD)right;
	if ((left || right) && g_rumble_sends[port]++ < 3)
		h2n_log("       controller %u: vibration, left motor %u, right motor %u (through %s)\n", port + 1, left, right, g_pc_library);
}
/* the program is closing (window closed, EXIT, a test's end, a stop): the motors are stopped */
void h2n_pad_rumble_stop(void)
{
	unsigned port;
	for (port = 0; port < 4; port++)
		if (g_pc_set_state && g_rumble_known[port] && (g_rumble_left[port] || g_rumble_right[port]))
		{
			pc_vibration v = { 0, 0 };
			g_pc_set_state(port, &v);
			g_rumble_left[port] = g_rumble_right[port] = 0;
		}
}

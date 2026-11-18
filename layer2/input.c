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
		if (!strcmp(line, "tracewindow")) { extern void __cdecl h2n_trace_window_start(void); h2n_trace_window_start(); return; }   /* as F9 */
		for (i = 0; i < sizeof(g_names) / sizeof(g_names[0]); i++) if (!strcmp(line, g_names[i].name)) { g_live_button = g_names[i].button; g_live_analog = g_names[i].analog; }
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
	}
	if (window && GetForegroundWindow() == (HWND)window)
	{
		if (key(VK_RETURN)) pad->buttons |= PAD_START;
		if (key(VK_BACK) || key(VK_ESCAPE)) pad->buttons |= PAD_BACK;
		if (key(VK_UP)) pad->buttons |= PAD_UP;
		if (key(VK_DOWN)) pad->buttons |= PAD_DOWN;
		if (key(VK_LEFT)) pad->buttons |= PAD_LEFT;
		if (key(VK_RIGHT)) pad->buttons |= PAD_RIGHT;
		if (key('Z') || key(VK_SPACE)) pad->analog[AN_A] = 255;
		if (key('X')) pad->analog[AN_B] = 255;
		if (key('C')) pad->analog[AN_X] = 255;
		if (key('V')) pad->analog[AN_Y] = 255;
		if (key('1')) pad->analog[AN_WHITE] = 255;
		if (key('2')) pad->analog[AN_BLACK] = 255;
		if (key('Q')) pad->analog[AN_LEFT_TRIGGER] = 255;
		if (key('E')) pad->analog[AN_RIGHT_TRIGGER] = 255;
		if (key('F')) pad->buttons |= PAD_LEFT_THUMB;
		if (key('G')) pad->buttons |= PAD_RIGHT_THUMB;
		if (key('A')) lx -= 32767;
		if (key('D')) lx += 32767;
		if (key('W')) ly += 32767;
		if (key('S')) ly -= 32767;
		if (key('J')) rx -= 32767;
		if (key('L')) rx += 32767;
		if (key('I')) ry += 32767;
		if (key('K')) ry -= 32767;
	}
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

/* H2Classic: the vertical blank (60 times a second, on its own thread, as the console delivers it between the game's
   instructions), presenting, and the test build's time limit */
#include "h2n.h"
#include <math.h>
#include <dwmapi.h>
#include "apu.h"
void h2n_pad_rumble_stop(void);

int h2n_env_int(const char *name, int otherwise);
struct vblank_data { DWORD vblank, swap, flags; };
static void (__cdecl *volatile g_callback)(struct vblank_data *);
static volatile LONG g_vblanks, g_swaps_asked, g_swaps_shown;
static HANDLE g_tick;
static LARGE_INTEGER g_frequency, g_started;
static unsigned g_frames, g_run_seconds = 180, g_last_report;
static int g_window_open;
unsigned __cdecl h2n_frames(void) { return g_frames; }
void gx_report(void);
void h2n_log(const char *format, ...);
void __cdecl h2n_window_open(void)
{
	g_window_open = 1;
	if (!getenv("H2N_SECONDS")) { g_run_seconds = 0; h2n_log("       a window is open: the run lasts until the window is closed\n"); }
}
static volatile LONG g_last_activity_ms, g_reporting;
#define QUIET_AFTER 30.0       /* a run may end early once this many seconds have passed ... */
#define QUIET_FOR   20.0       /* ... and nothing new has happened for this long */
static unsigned g_totals[32];
static HANDLE g_main_thread;
static volatile LONG g_last_frame_ms;
void h2n_report_thread(HANDLE thread, const char *why);
double h2n_seconds(void);
void h2n_files_report(void);
HANDLE h2n_start_thread(LPTHREAD_START_ROUTINE start, void *parameter);
extern const char *const h2n_d3d_counter_names;

/* ---- H2N_UNLOCK=1: frames as soon as they are ready, up to the refresh rate; the game's ticks stay 30 a second ----
 * The game's frame pacing (retail 0x12b0e0) waits a number of refreshes for each frame: the display setting's 1, raised to 2
 * while a game is in progress ("if N <= 2, N = 2") - 30 frames a second. The byte that raise is tested on (the game
 * options' +0x1120, read at retail 0x12b172) is read from h2n_pace_clamp instead when this is on (main.c re-aims that one
 * instruction in our copy of the function): 1 is the game's own pacing exactly, 0 skips the raise. The game's own time
 * update turns each frame's seconds into ticks and carries the rest, so at 60 frames there is a tick every other frame.
 * This is the Spooky Ghost mod's "Unlock FPS", byte for byte, with its conditions: only in a game (the game options'
 * +8 is 3 at the main menu) and not while a cutscene plays (the cinematic globals' +5). The mod's other half - drawing
 * the frames between two ticks with everything blended between them, and its fix for particle systems on frames
 * without a tick - is not here: without it the world still moves 30 times a second and only what the game does per
 * frame (turning the view) gains. */
unsigned char h2n_pace_clamp = 1;
/* H2N_VSYNC=off (s9v): H2N_UNLOCK's frames, with the vertical blank at 1000 a second (unless H2N_HZ says otherwise), so a frame is
   drawn as soon as the last is done; the game's ticks stay 30 a second */
static int vsync_off(void) { static int off = -1; if (off < 0) { const char *v = getenv("H2N_VSYNC"); off = v && (!_stricmp(v, "off") || !strcmp(v, "0")); } return off; }
int h2n_unlock_wanted(void) { static int on = -1; if (on < 0) on = h2n_env_int("H2N_UNLOCK", 0) || vsync_off() ? 1 : 0; return on; }
static int game_pointer(DWORD p) { return p >= 0x80000000u && p < 0x88000000u && !(p & 3); }
/* ---- H2N_UNLOCK: the vertical blank at the PC display's own refresh rate (s9o) ----
 * The console's picture refreshes 59.94 times a second and the game keeps time in refreshes: a frame's seconds are the
 * vblanks it took / the refresh rate it holds at 0x485ac0 (60, a figure it never measures). Unlocked, a frame is shown at
 * each vblank, so nothing could pass 60 frames a second. Now, with H2N_UNLOCK, the vblank follows the display the window
 * is on: its refresh rate (Windows' figure for that display, made exact by the desktop compositor's where the two
 * agree; H2N_HZ=<rate> sets one), each vblank placed just after the display's own where the compositor reports them.
 * The game is given the rate the way Spooky Ghost gives it on the xemu Halo 2 build's "Game refresh rate" (v2.0.8,
 * v2.0.9, v2.0.27), byte for byte: its own figure is never written (it also sets the tick rate when a game starts -
 * figure / 2 = 30 - the network's frame rate and Direct3D's), only the one read that turns vblanks into a frame's
 * seconds (retail 0x12b1f6) reads h2n_refresh, and its 30 Hz hold ("if N <= 2, N = 2", retail 0x12b191) the vblanks of a
 * 30 Hz frame at this rate, h2n_pace_min: the rate / 30 rounded, 2 at least - 2 at 60, 4 at 120, 5 at 144 (28.8 a
 * second), 6 at 165, 8 at 240. main.c re-aims those two instructions with the clamp's; this runs only if all three were
 * found. Without H2N_UNLOCK, everything is the console's 59.94. */
static short g_refresh_base = 60; static int g_slowmo;   /* (H2N_SLOWMO: testing only) */
extern short h2n_refresh;
void h2n_set_slowmo(int n)                               /* (testing only: the key file's "slowmo N" line) */
{
	g_slowmo = n > 1 && n <= 64 ? n : 0;
	if (!g_slowmo) h2n_refresh = g_refresh_base;
	h2n_log("       slow motion %d (from the key file, testing only)\n", g_slowmo);
}
short h2n_refresh = 60;                       /* read by the game's frame pacing: vblanks a second (H2N_UNLOCK) */
unsigned short h2n_pace_min = 2;              /* read by the game's frame pacing: the vblanks of a 30 Hz frame (H2N_UNLOCK) */
int h2n_follow_display;                       /* main.c: the pacing's instructions were re-aimed - the vblank may follow the display */
static volatile LONGLONG g_vb_period;         /* the vertical blank's period (performance-counter counts) */
static double g_vb_hz = 59.94;
static HRESULT (WINAPI *g_dwm_timing)(HWND, DWM_TIMING_INFO *);
static volatile LONG g_vb_lock;               /* the vblanks follow the compositor's (its rate agrees with ours) */
static int g_vb_lock_n = 1;                   /* (H2N_HZ a whole part of the display's rate: one vblank every n of the display's) */
static unsigned g_lock_moves;
static int dwm_timing(DWM_TIMING_INFO *t)
{
	if (!g_dwm_timing) return 0;
	memset(t, 0, sizeof(*t));
	t->cbSize = sizeof(*t);
	return g_dwm_timing(NULL, t) == S_OK && t->qpcRefreshPeriod > 0 && t->rateRefresh.uiDenominator && t->rateRefresh.uiNumerator;
}
void h2n_display_rate(HWND window)
{
	const char *forced = getenv("H2N_HZ");
	double hz = 0.0, dwm_hz = 0.0;
	int monitor_hz = 0;
	HMODULE dwm;
	DWM_TIMING_INFO t;
	char from[160] = "";
	if (!h2n_unlock_wanted() || !h2n_follow_display) return;
	dwm = LoadLibraryA("dwmapi.dll");
	g_dwm_timing = dwm ? (HRESULT (WINAPI *)(HWND, DWM_TIMING_INFO *))GetProcAddress(dwm, "DwmGetCompositionTimingInfo") : NULL;
	{
		HMONITOR monitor = window ? MonitorFromWindow(window, MONITOR_DEFAULTTOPRIMARY) : NULL;
		MONITORINFOEXA info;
		DEVMODEA mode;
		memset(&info, 0, sizeof(info)); info.cbSize = sizeof(info);
		memset(&mode, 0, sizeof(mode)); mode.dmSize = sizeof(mode);
		if (monitor && GetMonitorInfoA(monitor, (MONITORINFO *)&info) && EnumDisplaySettingsA(info.szDevice, ENUM_CURRENT_SETTINGS, &mode) && mode.dmDisplayFrequency > 1)
			monitor_hz = (int)mode.dmDisplayFrequency;
	}
	if (dwm_timing(&t)) dwm_hz = (double)t.rateRefresh.uiNumerator / (double)t.rateRefresh.uiDenominator;
	if (forced && atof(forced) >= 24.0 && atof(forced) <= 1000.0) { hz = atof(forced); snprintf(from, sizeof(from), "H2N_HZ"); }
	else if (vsync_off()) { hz = 1000.0; snprintf(from, sizeof(from), "H2N_VSYNC=off (frames as fast as they are made)"); }
	else if (dwm_hz >= 24.0 && monitor_hz && fabs(dwm_hz - monitor_hz) < 1.5) { hz = dwm_hz; snprintf(from, sizeof(from), "the display (%d Hz; the compositor's %.3f)", monitor_hz, dwm_hz); }
	else if (monitor_hz >= 24) { hz = monitor_hz == 59 || monitor_hz == 119 || monitor_hz == 143 || monitor_hz == 239 ? (monitor_hz + 1) * 1000.0 / 1001.0 : monitor_hz; snprintf(from, sizeof(from), "the display (%d Hz)", monitor_hz); }
	if (!(hz >= 24.0 && hz <= 1000.0)) { h2n_log("       refresh: the display's refresh rate is not known; the vertical blank stays the console's 59.94 a second\n"); return; }
	g_vb_lock_n = 1;
	if (forced && dwm_hz > 0.0)                       /* H2N_HZ a whole part of the display's (120 of 240): every Nth of its refreshes */
	{
		double ratio = dwm_hz / hz;
		int n = (int)(ratio + 0.5);
		if (n >= 1 && n <= 8 && fabs(ratio - n) < 0.01 * n) { hz = dwm_hz / n; g_vb_lock_n = n; }
	}
	g_vb_hz = hz;
	h2n_refresh = (short)(hz + 0.5);
	{
		int slow = h2n_env_int("H2N_SLOWMO", 1);      /* (for testing on a slow machine only: the game counts each refresh as 1/n of
		                                                  itself, so it runs at 1/n speed and frames fall between its ticks) */
		g_refresh_base = h2n_refresh;
		if (slow > 1 && slow <= 16) { g_slowmo = slow; h2n_log("       H2N_SLOWMO=%d: in a game (not the menus) the game runs at 1/%d speed (testing only)\n", slow, slow); }
	}
	h2n_pace_min = (unsigned short)((h2n_refresh + 15) / 30 < 2 ? 2 : (h2n_refresh + 15) / 30);
	g_vb_period = (LONGLONG)((double)g_frequency.QuadPart / hz + 0.5);
	g_vb_lock = dwm_hz > 0.0 && fabs(dwm_hz / g_vb_lock_n - hz) < hz * 0.005;
	h2n_log("       refresh: the vertical blank follows %s: %.3f a second%s; a frame's seconds are vblanks / %d, a 30 Hz frame %u vblanks (%.1f a second)\n",
		from, hz, !g_vb_lock ? "" : g_vb_lock_n > 1 ? ", each just after every so many of the display's own" : ", each just after the display's own", (int)h2n_refresh, (unsigned)h2n_pace_min, hz / h2n_pace_min);
}
/* the next vblank moved a quarter of the way to just after the display's own (the compositor's last vblank and period) */
static LONGLONG vb_lock(LONGLONG next, LONGLONG period)
{
	DWM_TIMING_INFO t;
	LONGLONG p, e, lead;
	if (!dwm_timing(&t)) return next;
	p = (LONGLONG)t.qpcRefreshPeriod * g_vb_lock_n;
	if (p < period - period / 200 || p > period + period / 200) return next;     /* (another rate now: left alone) */
	lead = g_frequency.QuadPart / 2000;                                           /* half a millisecond after it */
	e = (next - ((LONGLONG)t.qpcVBlank + lead)) % p;
	if (e < 0) e += p;
	if (e > p / 2) e -= p;
	if (e > p / 64 || e < -p / 64) g_lock_moves++;
	return next - e / 4;
}
/* s9o: the clamp is off everywhere once a game's options exist - in cutscenes (as Spooky Ghost's console build since
 * v2.0.26: its render-only nodes, v2.0.25, and its scripted camera between ticks, v2.0.31, are in interp.c now) and on
 * the main menu (your request; the mod keeps the menu's own pacing, which holds its first screens - the main menu and
 * "press start" - to 30 and runs the others at every refresh: now all of them at every refresh, with the in-between
 * frames there too) */
static void h2n_pace_update(void)
{
	DWORD opt;
	if (!h2n_unlock_wanted()) return;
	opt = *(volatile DWORD *)0x4E6948;
	h2n_pace_clamp = game_pointer(opt) ? 0 : 1;
	if (g_slowmo > 1)                                 /* (H2N_SLOWMO, testing only: slowed in a game, not on the main menu) */
		h2n_refresh = (short)(game_pointer(opt) && *(volatile DWORD *)(opt + 8) != 3 ? g_refresh_base * g_slowmo : g_refresh_base);
}

/* ---- H2N_WATCH=<ticks>: every so many game ticks, one log line per AI actor with where its unit is and how far it
 * went since the last line (0.00 = standing), and the player's own place. For telling what the AI does without a video.
 * Read from the game's own tables: actors *0x4F55F0 (0x888 bytes each, unit at +0x18, team at +0x24), objects
 * *0x4E0300 (12-byte headers, the object at +8, its place at +0x64), game time *0x510C54 (+8 the tick). ---- */
static void h2n_watch(void)
{
	static int every = -1;
	static DWORD last_tick;
	static float was[256][3];
	static DWORD was_unit[256];
	DWORD time, pool, objects, data, object_data, count, tick, moving = 0, standing = 0, players, local;
	if (every < 0) every = h2n_env_int("H2N_WATCH", 0);
	if (!every) return;
	time = *(volatile DWORD *)0x510C54; pool = *(volatile DWORD *)0x4F55F0; objects = *(volatile DWORD *)0x4E0300;
	if (!game_pointer(time) || !game_pointer(pool) || !game_pointer(objects)) return;
	tick = *(volatile DWORD *)(time + 8);
	if (tick / every == last_tick / every) return;
	last_tick = tick;
	{
		/* the game's options once per map: what kind of game it is and the campaign's difficulty (0 easy .. 3 legendary) */
		static DWORD told_options;
		DWORD options = *(volatile DWORD *)0x4E6948;
		if (game_pointer(options) && tick < told_options) told_options = 0;
		if (game_pointer(options) && !told_options)
			h2n_log("watch %6lu game options: kind %ld, difficulty %d, map %.60s\n", tick, *(long *)(options + 8), (int)*(short *)(options + 0x132), (const char *)(options + 0x24));
		told_options = tick + 1;
	}
	data = *(DWORD *)(pool + 0x44); count = *(DWORD *)(pool + 0x20); object_data = *(DWORD *)(objects + 0x44);
	if (!game_pointer(data) || !game_pointer(object_data) || count > 256) return;
	local = *(volatile DWORD *)0x4E8C20; players = *(volatile DWORD *)0x4E8C24;
	if (game_pointer(local) && game_pointer(players))
	{
		DWORD pd = *(DWORD *)(local + 0xC), pdata = *(DWORD *)(players + 0x44);
		if (pd != 0xffffffffu && game_pointer(pdata))
		{
			DWORD ud = *(DWORD *)(pdata + (pd & 0xffff) * *(DWORD *)(players + 0x24) + 0x2C);
			if (ud != 0xffffffffu && (ud & 0xffff) < *(DWORD *)(objects + 0x20))
			{
				DWORD o = *(DWORD *)(object_data + (ud & 0xffff) * 12 + 8);
				if (game_pointer(o))
				{
					/* and the controller as the game counts it (gamepad 0: frames each button has been down, *0x4E61B8 + 0x24 +
					 * 0x18; 6 = left stick click, 7 = right) and the player-control record's zoom request (*0x4ED284 + 0x14:
					 * +0x2E the zoom level asked for, +0x89 held, +0x8C count) */
					DWORD control = *(volatile DWORD *)0x4ED284;
					const BYTE *pad = (const BYTE *)0x4E61B8 + 0x24 + 0x18;
					h2n_log("watch %6lu player at %8.2f %8.2f %10.4f  speed %7.4f %7.4f %8.4f  pad %u %u %u %u %u %u %u %u  zoom %d %u %u\n", tick,
						*(float *)(o + 0x64), *(float *)(o + 0x68), *(float *)(o + 0x6C), *(float *)(o + 0x88), *(float *)(o + 0x8C), *(float *)(o + 0x90),
						pad[0], pad[1], pad[2], pad[3], pad[4], pad[5], pad[6], pad[7],
						game_pointer(control) ? (int)*(short *)(control + 0x14 + 0x2E) : -9, game_pointer(control) ? *(BYTE *)(control + 0x14 + 0x89) : 0,
						game_pointer(control) ? *(BYTE *)(control + 0x14 + 0x8C) : 0);
				}
			}
		}
	}
	if (h2n_env_int("H2N_WATCHLIGHT", 0))      /* a test: every light that is on (pool *0x4E030C, 0x110 bytes each): what it hangs on and where it is */
	{
		DWORD lights = *(volatile DWORD *)0x4E030C, n;
		if (game_pointer(lights) && game_pointer(*(DWORD *)(lights + 0x44)))
			for (n = 0; n < *(DWORD *)(lights + 0x20) && n < 512; n++)
			{
				BYTE *l = (BYTE *)(ULONG_PTR)(*(DWORD *)(lights + 0x44) + n * 0x110);
				if (*(short *)l && (*(WORD *)(l + 2) & 2) && (*(DWORD *)(l + 0x4c) != 0xffffffff))
					h2n_log("watch %6u light %u flags %04x tag %08x object %08x marker %d at %9.3f %9.3f %9.3f forward %6.3f %6.3f %6.3f\n", tick, n, *(WORD *)(l + 2), *(DWORD *)(l + 4),
						*(DWORD *)(l + 0x4c), (int)*(short *)(l + 0x54), *(float *)(l + 0x84), *(float *)(l + 0x88), *(float *)(l + 0x8c), *(float *)(l + 0xac), *(float *)(l + 0xb0), *(float *)(l + 0xb4));
			}
	}
	if (h2n_env_int("H2N_WATCHOBJ", 0))
	{
		/* H2N_WATCHOBJ=1: every object whose height changed since the last line (at most 8 a line): for watching things fall */
		static float was_z[2048];
		DWORD total = *(DWORD *)(objects + 0x20), shown = 0;
		for (DWORD k = 0; k < total && k < 2048; k++)
		{
			DWORD h = object_data + k * 12, o2;
			if (!(*(DWORD *)h & 0xffff)) { was_z[k] = 0; continue; }
			o2 = *(DWORD *)(h + 8);
			if (!game_pointer(o2)) continue;
			if (*(float *)(o2 + 0x6C) != was_z[k] && was_z[k] != 0 && shown < 8 && fabsf(*(float *)(o2 + 0x64)) > 20.f)
			{
				h2n_log("watch %6lu object %4lu kind %u tag %04lx z %10.4f vz %8.4f\n", tick, k, *(BYTE *)(h + 3), *(DWORD *)o2 & 0xffff, *(float *)(o2 + 0x6C), *(float *)(o2 + 0x90));
				shown++;
			}
			was_z[k] = *(float *)(o2 + 0x6C);
		}
	}
	for (DWORD i = 0; i < count; i++)
	{
		DWORD a = data + i * *(DWORD *)(pool + 0x24), unit, o;
		float *at, d;
		if (!(*(DWORD *)a & 0xffff)) continue;
		unit = *(DWORD *)(a + 0x18);
		if (unit == 0xffffffffu || (unit & 0xffff) >= *(DWORD *)(objects + 0x20)) continue;
		o = *(DWORD *)(object_data + (unit & 0xffff) * 12 + 8);
		if (!game_pointer(o)) continue;
		at = (float *)(o + 0x64);
		d = was_unit[i] == unit ? sqrtf((at[0] - was[i][0]) * (at[0] - was[i][0]) + (at[1] - was[i][1]) * (at[1] - was[i][1]) + (at[2] - was[i][2]) * (at[2] - was[i][2])) : -1.f;
		h2n_log("watch %6lu actor %3lu team %2ld unit %08lx at %8.2f %8.2f %8.2f went %6.2f\n", tick, i, (long)*(short *)(a + 0x24), unit, at[0], at[1], at[2], d);
		if (was_unit[i] != unit)
		{
			/* once per actor: the tags its record names, each with its class, for telling which character it is. A value is a tag's
			 * handle when the tag table (*0x547F00: +8 the entries of 16 bytes - class, handle, address, size - and +0x18 their
			 * count) holds that very value at the index in its low word. */
			char text[600]; int n = 0;
			DWORD table = *(volatile DWORD *)0x547F00, entries = game_pointer(table) ? *(DWORD *)(table + 8) : 0, tags = entries ? *(DWORD *)(table + 0x18) : 0;
			for (DWORD k = 0; game_pointer(entries) && k + 4 <= *(DWORD *)(pool + 0x24) && n < 560; k += 4)
			{
				DWORD v = *(DWORD *)(a + k), index = v & 0xffff;
				if (v != 0xffffffffu && index < tags && *(DWORD *)(entries + index * 16 + 4) == v)
				{
					const char *c = (const char *)(entries + index * 16);
					n += sprintf(text + n, " +%lx:%c%c%c%c:%04lx", k, c[3], c[2], c[1], c[0], index);
				}
			}
			text[n] = 0;
			h2n_log("watch %6lu actor %3lu is unit %08lx, object tag %04lx, tags in the actor:%s\n", tick, i, unit, *(DWORD *)o & 0xffff, text);
		}
		if (d > 0.02f) moving++; else if (d >= 0.f) standing++;
		was_unit[i] = unit; was[i][0] = at[0]; was[i][1] = at[1]; was[i][2] = at[2];
	}
	h2n_log("watch %6lu: %lu moving, %lu standing\n", tick, moving, standing);
}

static DWORD WINAPI vblank_thread(void *unused)
{
	LARGE_INTEGER now;
	LONGLONG period, next;
	(void)unused;
	QueryPerformanceCounter(&now);
	period = g_vb_period;                                  /* 59.94 Hz, or (H2N_UNLOCK) the display's */
	next = now.QuadPart + period;
	for (;;)
	{
		struct vblank_data data;
		if (g_vb_period != period) { next += g_vb_period - period; period = g_vb_period; }
		for (;;)
		{
			QueryPerformanceCounter(&now);
			if (now.QuadPart >= next) break;
			if ((next - now.QuadPart) * 1000 / g_frequency.QuadPart > 2) Sleep(1); else SwitchToThread();
		}
		next += period;
		if (now.QuadPart - next > period * 4) next = now.QuadPart + period;      /* fell far behind (a debugger, a stall): start over */
		data.vblank = (DWORD)InterlockedIncrement(&g_vblanks);
		if (g_vb_lock && (data.vblank % 30) == 0) next = vb_lock(next, period);
		data.flags = 0;
		if (g_swaps_shown != g_swaps_asked) { g_swaps_shown = g_swaps_asked; data.flags = 1; }   /* D3DVBLANK_SWAPDONE */
		data.swap = (DWORD)g_swaps_shown;
		h2n_scrub_stack(0x4000);
		if (g_callback) g_callback(&data);
		SetEvent(g_tick);
		if (g_main_thread && (data.vblank % 60) == 0)
		{
			/* the watchdog: if the game stops presenting frames, say where its main thread is */
			static unsigned reported;
			LONG idle = (LONG)GetTickCount() - g_last_frame_ms;
			if (g_last_frame_ms && idle > 5000 && idle / 5000 != (LONG)reported)
			{
				InterlockedIncrement(&g_reporting);
				char why[120];
				reported = (unsigned)(idle / 5000);
				snprintf(why, sizeof(why), "no frame presented for %ld s; the main thread is here", idle / 1000);
				h2n_report_thread(g_main_thread, why);
				h2n_files_report();
				InterlockedDecrement(&g_reporting);
			}
			if (g_window_open && g_last_frame_ms && idle > 90000)
			{
				h2n_report_thread(g_main_thread, "no frame presented for 90 seconds; the main thread is here");
				h2n_write_stop_file(NULL, "no frame presented");
				h2n_log("\n=== stopping: the game has not presented a frame for 90 seconds; %u frames presented in all ===\n", g_frames);
				h2n_pad_rumble_stop();
				ExitProcess(0);
			}
			if (g_run_seconds && h2n_seconds() > g_run_seconds + 30.0)
			{
				h2n_report_thread(g_main_thread, "the time limit passed with no frame being presented; the main thread is here");
				h2n_write_stop_file(NULL, "no frame presented");
				h2n_log("\n=== test build: stopping, %u frames presented in all ===\n", g_frames);
				h2n_pad_rumble_stop();
				ExitProcess(0);
			}
		}
	}
	return 0;
}
static void __cdecl timing_init(void)
{
	HMODULE winmm = LoadLibraryA("winmm.dll");
	FARPROC begin = winmm ? GetProcAddress(winmm, "timeBeginPeriod") : NULL;
	const char *seconds = getenv("H2N_SECONDS");
	if (begin) ((UINT (WINAPI *)(UINT))begin)(1);
	if (seconds) g_run_seconds = (unsigned)atoi(seconds);
	QueryPerformanceFrequency(&g_frequency);
	QueryPerformanceCounter(&g_started);
	g_vb_period = g_frequency.QuadPart * 1001 / 60000;     /* the console's 59.94 Hz */
	g_tick = CreateEventA(NULL, FALSE, FALSE, NULL);
	CloseHandle(h2n_start_thread(vblank_thread, NULL));
}
__attribute__((section(".CRT$XCD"), used)) static void (__cdecl *const timing_init_entry)(void) = timing_init;

void __cdecl h2n_vblank_set_callback(void *callback)
{
	g_callback = (void (__cdecl *)(struct vblank_data *))callback;
	h2n_log("[%4u] the game registered its vertical-blank callback\n", h2n_next_sequence());
}
unsigned h2n_run_seconds(void)
{
	return g_run_seconds;
}
void h2n_watch_main_thread(void)
{
	DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &g_main_thread, 0, FALSE, DUPLICATE_SAME_ACCESS);
	g_last_frame_ms = (LONG)GetTickCount();
}
/* something new happened (a line was logged, a file was read or written): a quiet run can end early, a busy one
   runs on to the limit */
void h2n_note_activity(void)
{
	if (!g_reporting) InterlockedExchange(&g_last_activity_ms, (LONG)GetTickCount());
}

double h2n_seconds(void)
{
	LARGE_INTEGER now;
	QueryPerformanceCounter(&now);
	return (double)(now.QuadPart - g_started.QuadPart) / (double)g_frequency.QuadPart;
}
static void print_counters(const char *label, const unsigned *counters, unsigned count, unsigned divide)
{
	char names[400], *name, *context = NULL;
	unsigned i = 0;
	snprintf(names, sizeof(names), "%s", h2n_d3d_counter_names);
	h2n_log("%s", label);
	for (name = strtok_r(names, ",", &context); name && i < count; name = strtok_r(NULL, ",", &context), i++)
		if (counters[i]) h2n_log(" %s %u", name, divide ? counters[i] / divide : counters[i]);
	h2n_log("\n");
}
/* ---- s9q: where a frame's time goes (every 5 s in the log: "frame time ..."). Points the game's main loop passes, in
 * order (h2n_perf, from the source's and the layer's own hooks); a segment is counted when both its ends were passed in
 * the frame. Performance counter reads only - nothing is waited for or changed. ---- */
enum { PF_START, PF_UPDATE, PF_TICKS_END, PF_CAMERAS, PF_DRAW, PF_WAIT0, PF_WAIT1, PF_SWAP, PF_PRESENT0, PF_PRESENT1, PF_VBLANK, PF_END, PF_POINTS };
static LONGLONG g_pf_at[PF_POINTS];
static unsigned g_pf_seen;
static double g_pf_sum[PF_POINTS], g_pf_max[PF_POINTS], g_pf_frame_sum, g_pf_frame_max;
static unsigned g_pf_frames, g_pf_ticks, g_pf_refresh[5];
static LONG g_pf_last_vblanks;
void __cdecl h2n_perf(int point)
{
	LARGE_INTEGER now;
	if (point < 0 || point >= PF_POINTS) return;
	QueryPerformanceCounter(&now);
	g_pf_at[point] = now.QuadPart; g_pf_seen |= 1u << point;
}
void __cdecl h2n_perf_tick(void) { g_pf_ticks++; }
double h2n_ip_cost_ms(int reset);
static void perf_frame_end(void)
{
	static const int seg[][2] = { { PF_START, PF_UPDATE }, { PF_UPDATE, PF_TICKS_END }, { PF_TICKS_END, PF_CAMERAS }, { PF_CAMERAS, PF_DRAW },
		{ PF_DRAW, PF_WAIT0 }, { PF_WAIT0, PF_WAIT1 }, { PF_WAIT1, PF_SWAP }, { PF_SWAP, PF_PRESENT0 }, { PF_PRESENT0, PF_PRESENT1 },
		{ PF_PRESENT1, PF_VBLANK }, { PF_VBLANK, PF_END } };
	LONG vb = g_vblanks, d = vb - g_pf_last_vblanks;
	unsigned i;
	double f = 1000.0 / (double)g_frequency.QuadPart, total;
	h2n_perf(PF_END);
	g_pf_last_vblanks = vb;
	if (g_pf_seen & 1u)
	{
		for (i = 0; i < sizeof(seg) / sizeof(seg[0]); i++)
			if ((g_pf_seen >> seg[i][0] & 1u) && (g_pf_seen >> seg[i][1] & 1u) && g_pf_at[seg[i][1]] >= g_pf_at[seg[i][0]])
			{
				double ms = (double)(g_pf_at[seg[i][1]] - g_pf_at[seg[i][0]]) * f;
				g_pf_sum[i] += ms; if (ms > g_pf_max[i]) g_pf_max[i] = ms;
			}
		total = (double)(g_pf_at[PF_END] - g_pf_at[PF_START]) * f;
		g_pf_frame_sum += total; if (total > g_pf_frame_max) g_pf_frame_max = total;
		g_pf_frames++;
		if (d >= 1) g_pf_refresh[d >= 5 ? 4 : d - 1]++;
	}
	g_pf_seen = 0;
	g_pf_at[PF_START] = g_pf_at[PF_END]; g_pf_seen = 1u;                      /* the next frame starts here */
}
static void perf_report(void)
{
	unsigned n = g_pf_frames ? g_pf_frames : 1, i;
	if (!g_pf_frames) return;
	h2n_log("       frame time (ms, average / slowest of %u frames): all %.2f / %.2f = before the game's update %.2f / %.2f, ticks %.2f / %.2f (%u ticks), "
		"frame update %.2f / %.2f, cameras %.2f / %.2f, draw %.2f / %.2f, the game's own wait %.2f / %.2f, after it %.2f / %.2f, our picture's copy %.2f / %.2f, "
		"Direct3D 9's Present %.2f / %.2f, to the refresh %.2f / %.2f, the refresh wait %.2f / %.2f; in-between frames %.2f a frame; "
		"frames lasting 1 refresh %u, 2 %u, 3 %u, 4 %u, more %u\n",
		g_pf_frames, g_pf_frame_sum / n, g_pf_frame_max, g_pf_sum[0] / n, g_pf_max[0], g_pf_sum[1] / n, g_pf_max[1], g_pf_ticks,
		g_pf_sum[2] / n, g_pf_max[2], g_pf_sum[3] / n, g_pf_max[3], g_pf_sum[4] / n, g_pf_max[4], g_pf_sum[5] / n, g_pf_max[5],
		g_pf_sum[6] / n, g_pf_max[6], g_pf_sum[7] / n, g_pf_max[7], g_pf_sum[8] / n, g_pf_max[8], g_pf_sum[9] / n, g_pf_max[9],
		g_pf_sum[10] / n, g_pf_max[10], h2n_ip_cost_ms(1) / n, g_pf_refresh[0], g_pf_refresh[1], g_pf_refresh[2], g_pf_refresh[3], g_pf_refresh[4]);
	{
		extern void __cdecl h2n_gfx_perf_report(unsigned frames, double draw_ms);
		h2n_gfx_perf_report(g_pf_frames, g_pf_sum[4] / n);      /* (s9r: the draw's own parts, gfx9.c) */
	}
	for (i = 0; i < PF_POINTS; i++) { g_pf_sum[i] = 0; g_pf_max[i] = 0; }
	g_pf_frame_sum = g_pf_frame_max = 0; g_pf_frames = 0; g_pf_ticks = 0;
	for (i = 0; i < 5; i++) g_pf_refresh[i] = 0;
}

/* called for every frame the game presents; waits for the vertical blank the way the console's swap does */
/* H2N_POKE=<address>:<byte>@<frame>[,...] (hex address and byte, decimal frame): when that frame has been presented,
   the byte is stored at the address. For experiments: setting one of the game's own request flags (they live in the
   retail data, at retail's addresses) to see a path the run would not otherwise take. */
static struct poke { DWORD address; BYTE value; unsigned frame; } g_pokes[8];
static int g_poke_count = -1;
static void pokes_apply(unsigned frame)
{
	if (g_poke_count < 0)
	{
		const char *list = getenv("H2N_POKE");
		g_poke_count = 0;
		while (list && *list && g_poke_count < 8)
		{
			char *end;
			struct poke *p = &g_pokes[g_poke_count];
			p->address = strtoul(list, &end, 16);
			if (*end != ':') break;
			p->value = (BYTE)strtoul(end + 1, &end, 16);
			p->frame = *end == '@' ? strtoul(end + 1, &end, 10) : 1;
			g_poke_count++;
			list = *end ? end + 1 : end;
		}
	}
	for (int i = 0; i < g_poke_count; i++)
		if (g_pokes[i].frame == frame && !IsBadWritePtr((void *)(ULONG_PTR)g_pokes[i].address, 1))
		{
			h2n_log("H2N_POKE: frame %u, storing %02x at %08lx (it held %02x)\n", frame, g_pokes[i].value, g_pokes[i].address, *(BYTE *)(ULONG_PTR)g_pokes[i].address);
			*(volatile BYTE *)(ULONG_PTR)g_pokes[i].address = g_pokes[i].value;
		}
}

static unsigned g_counter_count;
/* s9s: the movies. The game's movie player keeps its state at 0x4e9188 (s_bink_globals, unknown_01e930.cpp; +0xc the
   movie, +8 the flags it was started with, +0x10/+0x12 its size) and the movie is retail's Bink library's (s_bink_movie: +8
   frames, +0xc the frame it is on, +0x14/+0x18 the frame rate). Read only, on the main thread (the game opens and closes
   movies there too): a line when one starts, every 5 s while it plays, and when it is closed. */
static double g_movie_seconds;
static void movie_watch(void)
{
	static const unsigned *shown;
	static unsigned started, frames, frame, rate, divisor, flags;
	const unsigned char *m = (const unsigned char *)0x4e9188;
	const unsigned *movie = *(const unsigned *const *)(m + 0xc);
	if (movie != shown)
	{
		if (shown) h2n_log("[%4u] movie: closed at %.2f s on its frame %u of %u, after %u frames presented (%.1f s)\n", h2n_next_sequence(), h2n_seconds(),
			frame, frames, g_frames - started, h2n_seconds() - g_movie_seconds);
		shown = movie;
		started = g_frames;
		g_movie_seconds = h2n_seconds();
		if (movie)
		{
			flags = *(const unsigned *)(m + 8); frames = movie[2]; frame = movie[3]; rate = movie[5]; divisor = movie[6];
			h2n_log("[%4u] movie: playing at %.2f s, %ux%u, %u frames at %.2f a second (%.1f s), the player's flags %03x%s\n", h2n_next_sequence(), g_movie_seconds,
				movie[0], movie[1], frames, divisor ? (double)rate / divisor : 0.0, rate ? (double)frames * divisor / rate : 0.0, flags,
				/* function_155f80: a button ends it when its flags have 2 and either lack 0x100 or the player's +3 is set */
				(flags & 2) && (!(flags & 0x100) || m[3]) ? " (a button ends it)" : " (no button ends it)");
		}
		return;
	}
	if (!movie) return;
	frame = movie[3]; frames = movie[2];
	if ((g_frames - started) % 300 == 299) h2n_log("       movie: on its frame %u of %u after %u frames presented\n", frame, frames, g_frames - started + 1);
}
unsigned __cdecl h2n_present(unsigned interval, const unsigned *counters, unsigned count)
{
	unsigned waits = interval & 0x7fffffffu;
	double now;
	if (waits > 4) waits = 1;
	h2n_perf(PF_VBLANK);
	InterlockedIncrement(&g_swaps_asked);
	while (waits && g_swaps_shown != g_swaps_asked) WaitForSingleObject(g_tick, 100);
	for (unsigned i = 1; i < waits; i++) WaitForSingleObject(g_tick, 100);
	perf_frame_end();
	g_frames++;
	h2n_pace_update();
	h2n_watch();
	movie_watch();
	g_last_frame_ms = (LONG)GetTickCount();
	pokes_apply(g_frames);
	for (unsigned i = 0; i < count && i < 32; i++) g_totals[i] += counters[i];
	now = h2n_seconds();
	if (g_frames <= 3 || g_frames == 10 || g_frames == 30 || (unsigned)now / 5 != g_last_report)
	{
		InterlockedIncrement(&g_reporting);         /* the periodic lines are not news */
		char label[80];
		snprintf(label, sizeof(label), "frame %u at %.2f s (%ld vertical blanks):", g_frames, now, (long)g_vblanks);
		print_counters(label, counters, count, 0);
		g_last_report = (unsigned)now / 5;
		perf_report();
		h2n_files_report();
		if (g_last_report && g_last_report % 6 == 0) apu_report();   /* every 30 s (the sound session's s9i) */
		InterlockedDecrement(&g_reporting);
	}
	{
		double quiet = ((LONG)GetTickCount() - g_last_activity_ms) / 1000.0;
		int limit = g_run_seconds && now >= g_run_seconds;
		if (limit || (g_run_seconds && now >= QUIET_AFTER && quiet >= QUIET_FOR))
		{
			InterlockedIncrement(&g_reporting);
			if (limit) h2n_log("\n=== test build: stopping at the %u-second limit ===\n", g_run_seconds);
			else h2n_log("\n=== test build: stopping after %.0f seconds: nothing new has happened for %.0f seconds ===\n", now, quiet);
			h2n_log("frames presented: %u (%.1f a second), vertical blanks: %ld\n", g_frames, g_frames / now, (long)g_vblanks);
			print_counters("average per frame:", g_totals, count, g_frames ? g_frames : 1);
			h2n_files_report();
			gx_report();
			apu_report();
			h2n_log("the game's main loop was still running when the test stopped it\n");
			h2n_pad_rumble_stop();
			ExitProcess(0);
		}
	}
	g_counter_count = count;
	h2n_scrub_stack(0x40000);       /* the frame is over: nothing of the game's is below this call */
	return g_frames;
}

void __cdecl h2n_window_closed(void)
{
	double now = h2n_seconds();
	InterlockedIncrement(&g_reporting);
	h2n_log("\n=== the window was closed after %.0f seconds ===\n", now);
	h2n_log("frames presented: %u (%.1f a second), vertical blanks: %ld\n", g_frames, now > 0 ? g_frames / now : 0.0, (long)g_vblanks);
	print_counters("average per frame:", g_totals, g_counter_count, g_frames ? g_frames : 1);
	h2n_files_report();
	gx_report();
	apu_report();
	h2n_log("the game's main loop was still running when the window was closed\n");
	h2n_pad_rumble_stop();
	ExitProcess(0);
}

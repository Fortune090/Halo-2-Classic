/* H2Classic: the picture. A window and a Direct3D 9 device behind the console's Direct3D calls.
 *
 * The console's GPU is programmed with a vertex program (its own instruction set), register combiners (its own
 * pixel pipeline) and textures in its own layouts. This file turns each of those into the Direct3D 9 equivalent as
 * it is first used: vertex programs and combiner settings become shader model 3 byte code (written directly, so no
 * shader compiler is needed on the PC), textures are unpacked into ordinary ones, and every surface the game draws
 * into is a render-target texture, so that it can be drawn from again. */
#define COBJMACROS
#include <windows.h>
#ifndef H2N_VA                           /* (s10r) one program for both game versions: the address is picked at run time */
extern int h2n_v15;                     /* main.c: 0 = game version 1.0 (the disc's default.xbe), 1 = 1.5 (the title update's) */
#define H2N_VA(v10, v15) (h2n_v15 ? (v15) : (v10))
#define H2N_VA2(v10, v15) { (v10), (v15) }   /* in a table: both, picked with [h2n_v15] */
#endif
#include <d3d9.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "gfx.h"
#include "gfx_aa.h"
#include "gfx_dlss.h"

void h2n_log(const char *format, ...);
int h2n_env_int(const char *name, int otherwise);
FILE *h2n_real_fopen(const char *name, const char *mode);
void h2n_window_open(void);
void h2n_display_rate(HWND window);
void __cdecl h2n_perf(int point);

static HWND g_window;
static IDirect3D9 *g_d3d;
static int g_ex;                 /* the device is a Direct3D 9Ex one (H2N_AA=dlss: its textures can be shared with the DLSS helper) */
static IDirect3DDevice9 *g_device;
static D3DPRESENT_PARAMETERS g_pp;
static unsigned g_frame, g_width, g_height;
static int g_closed, g_lost, g_shot_request, g_trace;
float __cdecl h2n_ws_factor(void);                /* (xapi.c) */
static int g_new_window, g_display_mode;           /* (s9w: the window from the settings; 0 windowed, 1 borderless, 2 full screen) */
static RECT g_picture_rect;                       /* (s9w: where the game's picture goes in the window's picture) */
static unsigned g_trace_frame;     /* H2N_GFXFRAME: describe every draw of this frame, and save every surface drawn into at its end */
static unsigned g_trace_left;      /* (key file "trace"; H2N_GFXFRAME) describe this many frames, starting with the next one */
static int g_trace_now;            /* the frame being drawn is one of those */
#define FRAME_TRACE (g_trace_now)
#define SCALE_LIMIT 20             /* (s10z) Render Scale goes to x20: 12800x9600 (Direct3D 9's largest surface is 16384 a side on today's cards) */
static unsigned g_scale_most = SCALE_LIMIT; /* (s10u) the largest g_scale this device allows: by its largest texture, and (s10z) by what it had memory for */
static int g_scale_lower;          /* (s10z) a surface could not be made at this scale: one lower from the next frame on (picture_follow) */
#define SCALED_FAILED(hr) do { if (g_scale > 1) g_scale_lower = 1; (void)(hr); } while (0)
#define NO_MEMORY(hr) ((hr) == (HRESULT)0x8876017CL || (hr) == (HRESULT)0x8007000EL)   /* D3DERR_OUTOFVIDEOMEMORY, E_OUTOFMEMORY */
#ifdef H2N_SCALE_TEST              /* (test builds here only, never sent) as if the card had no room for surfaces above this scale */
#define SCALE_REFUSED (g_scale > (H2N_SCALE_TEST))
#else
#define SCALE_REFUSED 0
#endif
static unsigned g_scale = 1;       /* every surface the game draws into is this many times its size in each direction (H2N_RENDER) */
static IDirect3DSurface9 *g_half_surface[4]; static unsigned g_half_size[4];   /* the steps of halving a picture drawn larger than the window */
static unsigned g_ssaa;            /* H2N_SSAA: the picture drawn this many times the window in each direction, then averaged down */
static unsigned g_window_width, g_window_height;  /* the window's picture: the game's, times H2N_SCALE; 16:9 when the console is set to widescreen (H2N_WIDE) */
static unsigned g_window_scale = 1;/* the window, and the Direct3D 9 back buffer behind it, is this many times the game's picture (H2N_SCALE) */
static int g_lazy, g_lazy_draw = 1;/* H2N_GFXLAZY: draw only the frames that are saved as pictures (to run a level fast without a graphics card) */
static unsigned g_lazy_run;        /* frames still to draw before the picture that was asked for is saved */
/* where a pixel is: the console's GPU takes the middle of the first pixel to be at (0.5, 0.5), Direct3D 9 at (0, 0). The
   game's vertex programs write positions for the console, so half a pixel is taken off (H2N_GFXHALF, in hundredths of a pixel) */
static float g_half = -0.5f;
static int g_depth_mode = 1;       /* H2N_GFXDEPTH=0: depth as the stage 8 build made it (the program's z, cut off at the depth buffer's range) */
static unsigned g_notes;

static void note(const char *format, ...)       /* things worth one line in the log, but not thousands */
{
	char text[400];
	va_list a;
	if (g_notes >= 200) return;
	va_start(a, format); vsnprintf(text, sizeof(text), format, a); va_end(a);
	h2n_log("       picture: %s\n", text);
	if (++g_notes == 200) h2n_log("       picture: (no more notes will be written)\n");
}
static int note_once(unsigned *seen, unsigned capacity, unsigned value)
{
	unsigned i;
	for (i = 0; i < capacity && seen[i] != 0xffffffffu; i++) if (seen[i] == value) return 0;
	if (i < capacity) seen[i] = value;
	return 1;
}

/* ================================================================ window and device ==== */
/* (s10v) the mouse. Its movement comes as raw input (WM_INPUT: the mouse's own counts, not the pointer's, so Windows'
 * pointer speed and acceleration play no part) and is handed to input.c; interp.c adds it to the look. While the game is
 * taking the player's look (in a game, no menu up, this window the active one - h2n_mouse_wanted, interp.c) the pointer
 * is hidden and kept inside the window; in the menus, when paused, and whenever another window is active it is free -
 * and so it is for a player on a controller (h2n_km_player, input.c: the keyboard or mouse was what port 1 used last). */
void __cdecl h2n_mouse_raw(long dx, long dy);
void __cdecl h2n_mouse_wheel(void);
void __cdecl h2n_mouse_captured(int on);
int __cdecl h2n_mouse_wanted(void);
int __cdecl h2n_km_player(void);
static int g_mouse_held, g_mouse_raw_ok;
static void mouse_capture(void)
{
	int want = g_window && !g_closed && g_mouse_raw_ok && GetForegroundWindow() == g_window && h2n_km_player() && h2n_mouse_wanted();
	if (want)
	{
		RECT r;
		POINT a, b;
		if (GetClientRect(g_window, &r))
		{	/* (each frame: the window may have been moved or resized) */
			a.x = r.left; a.y = r.top; b.x = r.right; b.y = r.bottom;
			ClientToScreen(g_window, &a); ClientToScreen(g_window, &b);
			r.left = a.x; r.top = a.y; r.right = b.x; r.bottom = b.y;
			ClipCursor(&r);
		}
	}
	if (want == g_mouse_held) return;
	g_mouse_held = want;
	h2n_mouse_captured(want);
	{ static int told; if (told < 12) { told++; h2n_log("       mouse: the pointer is %s%s\n", want ? "held in the window (the game is taking the look)" : "free again", told == 12 ? " (said twelve times; not again)" : ""); } }
	if (want) SetCursor(NULL);
	else { ClipCursor(NULL); if (g_display_mode != 2) SetCursor(LoadCursorA(NULL, (LPCSTR)IDC_ARROW)); }
}
/* (s10w) The window's picture is always as large as the window itself is - its inside, in the screen's own pixels - whatever
 * Window Size asked for. Until now the picture was Window Size, and each frame was fitted to the window by Direct3D's
 * Present: a second resizing whenever the two differed - a window Windows made smaller because it did not fit the screen,
 * a maximized or dragged window, and every borderless or full-screen window whose Window Size was not the screen's (the
 * user: 3440x1440 shrunk and 2560x1080 enlarged into the same window looked different). Now the device's back buffer is
 * the window's size: the game's drawing (640x480 times Render Scale) is resized once, straight onto the pixels shown, and
 * Present copies one to one. Window Size is the size a WINDOWED window is given; when the window's size is another one
 * (or changes), the device is reset to it between two frames. */
static int g_want_w, g_want_h;                    /* Window Size as asked for (what a windowed window is given) */
static int g_refused_w, g_refused_h;              /* a window size the device did not take: not tried again while the window has it */
static int client_size(int *w, int *h)
{
	RECT r;
	if (!g_window || IsIconic(g_window) || !GetClientRect(g_window, &r)) return 0;
	*w = r.right - r.left; *h = r.bottom - r.top;
	return *w >= 64 && *h >= 48;
}
/* a framed window's outside for the inside in r: with the frame's size on a screen Windows scales (the program counts in
   the screen's own pixels since s10w, main.c dpi_aware; AdjustWindowRect alone gives the unscaled frame, and the inside
   would come out a little smaller than asked). Windows 10 1607 and later have the call; before that, the plain one. */
static void frame_rect(RECT *r, DWORD style)
{
	HMODULE user = GetModuleHandleA("user32.dll");
	BOOL (WINAPI *for_dpi)(RECT *, DWORD, BOOL, DWORD, UINT) = user ? (void *)GetProcAddress(user, "AdjustWindowRectExForDpi") : NULL;
	UINT (WINAPI *system_dpi)(void) = user ? (void *)GetProcAddress(user, "GetDpiForSystem") : NULL;
	UINT (WINAPI *window_dpi)(HWND) = user ? (void *)GetProcAddress(user, "GetDpiForWindow") : NULL;
	UINT dpi = g_window && window_dpi ? window_dpi(g_window) : system_dpi ? system_dpi() : 0;
	if (!for_dpi || dpi < 96 || !for_dpi(r, style, FALSE, 0, dpi)) AdjustWindowRect(r, style, FALSE);
}
/* the window's picture w x h, and the game's picture inside it: as large as fits with its shape kept, in the middle */
static void picture_fit(int w, int h)
{
	double aspect = h2n_env_int("H2N_WIDE", 0) ? 4.0 / 3.0 * h2n_ws_factor() : 4.0 / 3.0;   /* 16:9, or 64:27 for 21:9 (s9y) */
	int pw, ph;
	if ((double)w / h > aspect) { ph = h; pw = (int)(h * aspect + 0.5); }
	else { pw = w; ph = (int)(w / aspect + 0.5); }
	if (pw > w) pw = w;
	if (ph > h) ph = h;
	g_window_width = (unsigned)w; g_window_height = (unsigned)h;
	g_picture_rect.left = (w - pw) / 2; g_picture_rect.top = (h - ph) / 2;
	g_picture_rect.right = g_picture_rect.left + pw; g_picture_rect.bottom = g_picture_rect.top + ph;
}
/* input.c: the pointer is over the picture's window (its client area), or held in it - a mouse button then counts */
int gx_pointer_over(void)
{
	POINT p;
	RECT r;
	if (!g_window) return 0;
	if (g_mouse_held) return 1;
	if (!GetCursorPos(&p) || !ScreenToClient(g_window, &p) || !GetClientRect(g_window, &r)) return 0;
	return p.x >= r.left && p.x < r.right && p.y >= r.top && p.y < r.bottom;
}
static LRESULT CALLBACK window_proc(HWND w, UINT message, WPARAM wp, LPARAM lp)
{
	switch (message)
	{
	case WM_CLOSE: g_closed = 1; return 0;
	case WM_INPUT:
	{
		RAWINPUT in;
		UINT size = sizeof(in);
		if (GetRawInputData((HRAWINPUT)lp, RID_INPUT, &in, &size, sizeof(RAWINPUTHEADER)) != (UINT)-1 && in.header.dwType == RIM_TYPEMOUSE
			&& !(in.data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE))
			h2n_mouse_raw(in.data.mouse.lLastX, in.data.mouse.lLastY);
		break;
	}
	case WM_MOUSEWHEEL: h2n_mouse_wheel(); return 0;
	case WM_KILLFOCUS: case WM_ACTIVATEAPP:
		if (message == WM_KILLFOCUS || !wp) { if (g_mouse_held) { g_mouse_held = 0; h2n_mouse_captured(0); ClipCursor(NULL); } }
		break;
	case WM_KEYDOWN:
		/* (s10x) F11 (two frames described in the log) and F12 (a picture of the frame saved) are gone from the keyboard: the
		   test launcher's key file still asks for them ("trace", "shot": input.c) */
		if (wp == VK_F9) { extern void __cdecl h2n_trace_window_start(void); h2n_trace_window_start(); }   /* (H2N_INTERP=3: a cutscene, frame by frame, for 4 s) */
		break;
	case WM_ERASEBKGND:
	{	/* black around the picture (gdi32's stock brush, looked up: the layer does not link gdi32) */
		static HBRUSH black;
		RECT all;
		if (!black) { HGDIOBJ (WINAPI *stock)(int) = (void *)GetProcAddress(LoadLibraryA("gdi32.dll"), "GetStockObject"); black = stock ? (HBRUSH)stock(BLACK_BRUSH) : NULL; }
		GetClientRect(w, &all);
		if (black) FillRect((HDC)wp, &all, black);
		return 1;
	}
	case WM_SIZE: InvalidateRect(w, NULL, TRUE); break;
	case WM_SETCURSOR: if ((g_display_mode == 2 || g_mouse_held) && LOWORD(lp) == HTCLIENT) { SetCursor(NULL); return TRUE; } break;   /* (s9w: no pointer over the game in full screen; s10v: nor while the mouse looks) */
	}
	return DefWindowProcA(w, message, wp, lp);
}

static void pump(void)
{
	MSG m;
	while (PeekMessageA(&m, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessageA(&m); }
}

int gx_running(void) { return g_device != NULL; }
void gx_request_shot(void) { g_shot_request = 1; }
void gx_request_trace(void) { if (!g_trace_left) g_trace_left = 2; }
void *gx_window_handle(void) { return g_window; }
int gx_closed(void) { return g_closed; }
void __cdecl h2n_menu_exit(void)             /* (s10j) the main menu's EXIT: closes as the window's close button does */
{
	h2n_log("       the main menu's EXIT was chosen: the program closes\n");
	g_closed = 1;
}

/* ---- s9r: Direct3D 9 is called only when what a call sets differs from what was set last (H2N_GFXCACHE=0: every call
 * made, as before). Each draw used to set every state, sampler, texture, shader and all 197 vertex and 39 pixel
 * constants, whether or not they had changed - some 60 calls and 4 KB of constants a draw. What the device holds is kept
 * here as it is set; a call is left out when it would set the same value again. All of this file's calls that set the
 * device's state go through these, so the copy is always the device's; it is forgotten when the device is created or
 * reset (Direct3D 9 then starts from its defaults). Nothing the game sees changes: the same values reach the device. ---- */
static int g_cache_on = 1;
static unsigned g_calls_made, g_calls_saved;      /* (for the log: since the last report) */
static struct
{
	DWORD rs[256]; unsigned char rs_known[256];
	DWORD sampler[4][14]; unsigned char sampler_known[4][14];
	IDirect3DBaseTexture9 *texture[4]; unsigned char texture_known[4];
	IDirect3DVertexShader9 *vs; IDirect3DVertexDeclaration9 *declaration; IDirect3DPixelShader9 *ps; unsigned char vs_known, declaration_known, ps_known;
	IDirect3DSurface9 *target, *depth; unsigned char target_known, depth_known;
	D3DVIEWPORT9 viewport; unsigned char viewport_known;
	RECT scissor; unsigned char scissor_known;
	float vc[256][4]; unsigned char vc_known[256];
	float pc[64][4]; unsigned char pc_known[64];
} g_ss;
static void ss_forget(void) { memset(&g_ss, 0, sizeof(g_ss)); }
static unsigned g_ss_wrong;                        /* (H2N_GFXCACHE=2, a test: each call left out is checked against the device's own state) */
static void ss_wrong(const char *what, unsigned a, unsigned b)
{
	if (++g_ss_wrong <= 20) h2n_log("       picture: H2N_GFXCACHE=2 check: %s is %08x on the device, %08x here\n", what, a, b);
}
static void ss_rs(D3DRENDERSTATETYPE t, DWORD v)
{
	if (g_cache_on && (unsigned)t < 256 && g_ss.rs_known[t] && g_ss.rs[t] == v)
	{
		g_calls_saved++;
		if (g_cache_on == 2) { DWORD x = 0; IDirect3DDevice9_GetRenderState(g_device, t, &x); if (x != v) ss_wrong("a render state", x, v); }
		return;
	}
	IDirect3DDevice9_SetRenderState(g_device, t, v); g_calls_made++;
	if ((unsigned)t < 256) { g_ss.rs[t] = v; g_ss.rs_known[t] = 1; }
}
static void ss_sampler(unsigned stage, D3DSAMPLERSTATETYPE t, DWORD v)
{
	if (g_cache_on && stage < 4 && (unsigned)t < 14 && g_ss.sampler_known[stage][t] && g_ss.sampler[stage][t] == v)
	{
		g_calls_saved++;
		if (g_cache_on == 2) { DWORD x = 0; IDirect3DDevice9_GetSamplerState(g_device, stage, t, &x); if (x != v) ss_wrong("a sampler state", x, v); }
		return;
	}
	IDirect3DDevice9_SetSamplerState(g_device, stage, t, v); g_calls_made++;
	if (stage < 4 && (unsigned)t < 14) { g_ss.sampler[stage][t] = v; g_ss.sampler_known[stage][t] = 1; }
}
static void ss_texture(unsigned stage, IDirect3DBaseTexture9 *t)       /* (the device holds a reference to what is bound: an address is not reused while bound) */
{
	if (g_cache_on && stage < 4 && g_ss.texture_known[stage] && g_ss.texture[stage] == t)
	{
		g_calls_saved++;
		if (g_cache_on == 2) { IDirect3DBaseTexture9 *x = NULL; IDirect3DDevice9_GetTexture(g_device, stage, &x); if (x) IDirect3DBaseTexture9_Release(x); if (x != t) ss_wrong("a texture", (unsigned)(ULONG_PTR)x, (unsigned)(ULONG_PTR)t); }
		return;
	}
	IDirect3DDevice9_SetTexture(g_device, stage, t); g_calls_made++;
	if (stage < 4) { g_ss.texture[stage] = t; g_ss.texture_known[stage] = 1; }
}
static void ss_vs(IDirect3DVertexShader9 *v)
{
	if (g_cache_on && g_ss.vs_known && g_ss.vs == v)
	{
		g_calls_saved++;
		if (g_cache_on == 2) { IDirect3DVertexShader9 *x = NULL; IDirect3DDevice9_GetVertexShader(g_device, &x); if (x) IDirect3DVertexShader9_Release(x); if (x != v) ss_wrong("the vertex shader", (unsigned)(ULONG_PTR)x, (unsigned)(ULONG_PTR)v); }
		return;
	}
	IDirect3DDevice9_SetVertexShader(g_device, v); g_calls_made++; g_ss.vs = v; g_ss.vs_known = 1;
}
static void ss_declaration(IDirect3DVertexDeclaration9 *v)
{
	if (g_cache_on && g_ss.declaration_known && g_ss.declaration == v) { g_calls_saved++; return; }
	IDirect3DDevice9_SetVertexDeclaration(g_device, v); g_calls_made++; g_ss.declaration = v; g_ss.declaration_known = 1;
}
static void ss_ps(IDirect3DPixelShader9 *v)
{
	if (g_cache_on && g_ss.ps_known && g_ss.ps == v)
	{
		g_calls_saved++;
		if (g_cache_on == 2) { IDirect3DPixelShader9 *x = NULL; IDirect3DDevice9_GetPixelShader(g_device, &x); if (x) IDirect3DPixelShader9_Release(x); if (x != v) ss_wrong("the pixel shader", (unsigned)(ULONG_PTR)x, (unsigned)(ULONG_PTR)v); }
		return;
	}
	IDirect3DDevice9_SetPixelShader(g_device, v); g_calls_made++; g_ss.ps = v; g_ss.ps_known = 1;
}
static void ss_target(IDirect3DSurface9 *v)
{
	if (g_cache_on && g_ss.target_known && g_ss.target == v)
	{
		g_calls_saved++;
		if (g_cache_on == 2) { IDirect3DSurface9 *x = NULL; IDirect3DDevice9_GetRenderTarget(g_device, 0, &x); if (x) IDirect3DSurface9_Release(x); if (x != v) ss_wrong("the target", (unsigned)(ULONG_PTR)x, (unsigned)(ULONG_PTR)v); }
		return;
	}
	IDirect3DDevice9_SetRenderTarget(g_device, 0, v); g_calls_made++; g_ss.target = v; g_ss.target_known = 1;
	g_ss.viewport_known = 0; g_ss.scissor_known = 0;          /* (Direct3D 9 sets the viewport and the scissor to the whole new target) */
}
static void ss_depth(IDirect3DSurface9 *v)
{
	if (g_cache_on && g_ss.depth_known && g_ss.depth == v)
	{
		g_calls_saved++;
		if (g_cache_on == 2) { IDirect3DSurface9 *x = NULL; IDirect3DDevice9_GetDepthStencilSurface(g_device, &x); if (x) IDirect3DSurface9_Release(x); if (x != v) ss_wrong("the depth buffer", (unsigned)(ULONG_PTR)x, (unsigned)(ULONG_PTR)v); }
		return;
	}
	IDirect3DDevice9_SetDepthStencilSurface(g_device, v); g_calls_made++; g_ss.depth = v; g_ss.depth_known = 1;
}
static void ss_viewport(const D3DVIEWPORT9 *v)
{
	if (g_cache_on && g_ss.viewport_known && !memcmp(&g_ss.viewport, v, sizeof(*v)))
	{
		g_calls_saved++;
		if (g_cache_on == 2) { D3DVIEWPORT9 x; memset(&x, 0, sizeof(x)); IDirect3DDevice9_GetViewport(g_device, &x); if (memcmp(&x, v, sizeof(x))) ss_wrong("the viewport (x)", x.X, v->X); }
		return;
	}
	IDirect3DDevice9_SetViewport(g_device, v); g_calls_made++; g_ss.viewport = *v; g_ss.viewport_known = 1;
}
static void ss_scissor(const RECT *r)
{
	if (g_cache_on && g_ss.scissor_known && !memcmp(&g_ss.scissor, r, sizeof(*r))) { g_calls_saved++; return; }
	IDirect3DDevice9_SetScissorRect(g_device, r); g_calls_made++; g_ss.scissor = *r; g_ss.scissor_known = 1;
}
static void ss_constants(int pixel, unsigned start, const float *data, unsigned count)   /* only the registers that differ, in runs */
{
	float (*shadow)[4] = pixel ? g_ss.pc : g_ss.vc;
	unsigned char *known = pixel ? g_ss.pc_known : g_ss.vc_known;
	unsigned limit = pixel ? 64u : 256u, i = 0, run, made = g_calls_made;
	if (!g_cache_on || start + count > limit)
	{
		if (pixel) IDirect3DDevice9_SetPixelShaderConstantF(g_device, start, data, count); else IDirect3DDevice9_SetVertexShaderConstantF(g_device, start, data, count);
		g_calls_made++;
		if (start + count <= limit) for (i = 0; i < count; i++) { memcpy(shadow[start + i], data + 4 * i, 16); known[start + i] = 1; }
		return;
	}
	while (i < count)
	{
		if (known[start + i] && !memcmp(shadow[start + i], data + 4 * i, 16))
		{
			if (g_cache_on == 2)
			{
				float x[4] = { 0, 0, 0, 0 };
				if (pixel) IDirect3DDevice9_GetPixelShaderConstantF(g_device, start + i, x, 1); else IDirect3DDevice9_GetVertexShaderConstantF(g_device, start + i, x, 1);
				if (memcmp(x, data + 4 * i, 16)) ss_wrong(pixel ? "a pixel constant" : "a vertex constant", start + i, 0);
			}
			i++; continue;
		}
		for (run = i; run < count && !(known[start + run] && !memcmp(shadow[start + run], data + 4 * run, 16)); run++)
		{
			memcpy(shadow[start + run], data + 4 * run, 16); known[start + run] = 1;
		}
		if (pixel) IDirect3DDevice9_SetPixelShaderConstantF(g_device, start + i, data + 4 * i, run - i); else IDirect3DDevice9_SetVertexShaderConstantF(g_device, start + i, data + 4 * i, run - i);
		g_calls_made++;
		i = run;
	}
	if (g_calls_made == made) g_calls_saved++;
}

/* ---- s9r: where the draw's time goes (the log's "picture time", every 5 s). The processor's time stamp counter, read at
 * the edges of each part of this file's work; turned into milliseconds against the performance counter at each report. ---- */
enum { CT_TARGETS, CT_SHADERS, CT_TEXTURES, CT_DEPTHCOPY, CT_VERTICES, CT_STATE, CT_DRAWCALL, CT_SELFREAD, CT_CLEAR, CT_QUERIES, CT_ALL_GX, CT_PUSH, CT_COUNT };
static gx_u64 g_ct[CT_COUNT], g_ct_at, g_ct_nested;
static gx_u64 tsc(void) { return __builtin_ia32_rdtsc(); }
gx_u64 __cdecl h2n_gfx_tsc(void) { return tsc(); }
gx_u64 __cdecl h2n_gfx_gx_total(void) { return g_ct[CT_ALL_GX]; }
void __cdecl h2n_gfx_push_time(gx_u64 reading) { g_ct[CT_PUSH] += reading; }
static void ct_mark(int category)               /* the time since the last mark goes to this part (less what was counted inside it) */
{
	gx_u64 now = tsc(), spent = now - g_ct_at;
	g_ct[category] += spent > g_ct_nested ? spent - g_ct_nested : 0;
	g_ct_nested = 0; g_ct_at = now;
}

static void set_defaults(void)
{
	g_cache_on = h2n_env_int("H2N_GFXCACHE", 1);
	ss_forget();                                        /* (a new or reset device: its defaults, not what was set before) */
	ss_rs(D3DRS_LIGHTING, FALSE);
	ss_rs(D3DRS_CLIPPING, TRUE);
}

int gx_open(unsigned width, unsigned height)
{
	WNDCLASSA wc;
	RECT r;
	HMODULE library;
	IDirect3D9 *(WINAPI *create)(UINT);
	HRESULT hr;
	DWORD style = WS_OVERLAPPEDWINDOW;
	unsigned scale, fit;
	static const DWORD processing[3] = { D3DCREATE_HARDWARE_VERTEXPROCESSING, D3DCREATE_MIXED_VERTEXPROCESSING, D3DCREATE_SOFTWARE_VERTEXPROCESSING };
	int i;

	if (g_device) return 1;
	if (!h2n_env_int("H2N_WINDOW", 1)) { h2n_log("       picture: H2N_WINDOW=0, so no window is opened and nothing is drawn\n"); return 0; }
	g_trace = h2n_env_int("H2N_GFXTRACE", 0);
	g_trace_frame = (unsigned)h2n_env_int("H2N_GFXFRAME", 0);
	g_scale = (unsigned)h2n_env_int("H2N_RENDER", 1);
	if (g_scale < 1 || g_scale > SCALE_LIMIT) g_scale = 1;   /* (s10t) up to 10: 6400x4800; (s10z) up to 20: 12800x9600 */
	g_lazy = h2n_env_int("H2N_GFXLAZY", 0); g_lazy_draw = !g_lazy;
	g_half = h2n_env_int("H2N_GFXHALF", -50) / 100.0f;
	g_depth_mode = h2n_env_int("H2N_GFXDEPTH", 1);
	library = LoadLibraryA("d3d9.dll");
	if (library && aa_mode() == AA_DLSS)
	{
		/* DLSS: a Direct3D 9Ex device, whose textures can be shared with the helper's Direct3D 11 device */
		HRESULT (WINAPI *create_ex)(UINT, IDirect3D9Ex **) = (void *)GetProcAddress(library, "Direct3DCreate9Ex");
		IDirect3D9Ex *ex = NULL;
		if (create_ex && SUCCEEDED(create_ex(D3D_SDK_VERSION, &ex)) && ex) { g_d3d = (IDirect3D9 *)ex; g_ex = 1; }
		else dlss_failed("Direct3D 9Ex could not be started");
	}
	create = library ? (void *)GetProcAddress(library, "Direct3DCreate9") : NULL;
	for (i = 0; create && !g_d3d && i < 5; i++) { if (i) Sleep(300); g_d3d = create(D3D_SDK_VERSION); }
	if (!g_d3d)
	{
		h2n_log("*** picture: Direct3D 9 did not start (%s); no window is opened and nothing will be drawn\n",
			!library ? "d3d9.dll did not load" : !create ? "d3d9.dll has no Direct3DCreate9" : "Direct3DCreate9 gave nothing, five times");
		return 0;
	}

	memset(&wc, 0, sizeof(wc));
	wc.lpfnWndProc = window_proc; wc.hInstance = GetModuleHandleA(NULL); wc.lpszClassName = "h2classic"; wc.hCursor = LoadCursorA(NULL, (LPCSTR)IDC_ARROW);
	RegisterClassA(&wc);
	/* s9w: the window from the settings - H2N_WINDOW_SIZE=<width>x<height> (the picture's size in pixels) and H2N_DISPLAY=windowed
	   (resizable), borderless or fullscreen (both cover the screen; fullscreen also stays on top and hides the pointer). The
	   game's picture keeps its shape (4:3, or 16:9 with H2N_WIDE) inside it, with black around, and the window can be
	   resized freely: the picture is fitted to it, never stretched. Without either setting, the window is as before. */
	{
		const char *size_text = getenv("H2N_WINDOW_SIZE"), *mode_text = getenv("H2N_DISPLAY");
		int want_w = 0, want_h = 0, screen_w = GetSystemMetrics(SM_CXSCREEN), screen_h = GetSystemMetrics(SM_CYSCREEN);
		if (size_text) sscanf(size_text, "%dx%d", &want_w, &want_h);
		g_display_mode = !mode_text ? 0 : !_stricmp(mode_text, "fullscreen") ? 2 : !_stricmp(mode_text, "borderless") ? 1 : 0;
		g_new_window = size_text || mode_text;
		if (g_new_window)
		{
			int pw, ph, pct = h2n_env_int("H2N_RENDERSCALE", 100), cw = 0, ch = 0;
			if (want_w < 320 || want_h < 240 || want_w > 16384 || want_h > 16384) { want_w = g_display_mode ? screen_w : 1280; want_h = g_display_mode ? screen_h : 960; }
			g_want_w = want_w; g_want_h = want_h;
			g_window_scale = 1; scale = 1; g_ssaa = 0;      /* (render scale takes the place of H2N_SSAA here) */
			if (g_display_mode)
			{
				style = WS_POPUP;
				g_window = CreateWindowExA(g_display_mode == 2 ? WS_EX_TOPMOST : 0, "h2classic", "H2Classic", style, 0, 0, screen_w, screen_h, NULL, NULL, wc.hInstance, NULL);
			}
			else
			{
				r.left = 0; r.top = 0; r.right = want_w; r.bottom = want_h;
				frame_rect(&r, style);
				g_window = CreateWindowExA(0, "h2classic", "H2Classic", style, CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left, r.bottom - r.top, NULL, NULL, wc.hInstance, NULL);
			}
			/* (s10w) the picture is the window's own size: the screen's for borderless and full screen, and for a window what
			   Windows gave it (a window that does not fit the screen is made smaller) */
			if (g_window) { ShowWindow(g_window, SW_SHOWNORMAL); pump(); }
			if (!client_size(&cw, &ch)) { cw = g_display_mode ? screen_w : want_w; ch = g_display_mode ? screen_h : want_h; }
			picture_fit(cw, ch);
			pw = g_picture_rect.right - g_picture_rect.left; ph = g_picture_rect.bottom - g_picture_rect.top;
			if (!getenv("H2N_RENDER"))
			{	/* render scale: the game's picture (640x480 times a whole number) against the picture's height on the screen */
				int n = (int)(ph * (pct < 25 ? 25 : pct > 400 ? 400 : pct) / 100.0 / height + 0.5);
				g_scale = (unsigned)(n < 1 ? 1 : n > 8 ? 8 : n);
			}
			/* (s10t) render scale is the launcher's multiplier of the game's 640x480 (render_multiplier -> H2N_RENDER); the
			   percentage of the picture's height is only what a settings.ini from before s10t still gives */
			if (getenv("H2N_RENDER"))
				h2n_log("       picture: %s, the picture %ux%u - the window's own size (%dx%d of it the game's), render scale x%u: drawn at %ux%u\n",
					g_display_mode == 2 ? "full screen" : g_display_mode ? "borderless window over the screen" : "a window that can be resized",
					g_window_width, g_window_height, pw, ph, g_scale, width * g_scale, height * g_scale);
			else
			h2n_log("       picture: %s, the picture %ux%u - the window's own size (%dx%d of it the game's, at %d%% render scale: drawn at %ux%u)\n",
				g_display_mode == 2 ? "full screen" : g_display_mode ? "borderless window over the screen" : "a window that can be resized",
				g_window_width, g_window_height, pw, ph, pct, width * g_scale, height * g_scale);
			if (!g_display_mode && (cw != want_w || ch != want_h))
				h2n_log("       picture: Window Size asked for %dx%d; the window Windows gave is %dx%d inside (the screen is %dx%d)\n", want_w, want_h, cw, ch, screen_w, screen_h);
			else if (g_display_mode && size_text && (cw != want_w || ch != want_h))
				h2n_log("       picture: Window Size (%dx%d) is for a windowed window; this window is the screen, %dx%d\n", want_w, want_h, cw, ch);
		}
	}
	if (!g_new_window)
	{
	/* H2N_SSAA=n (supersampling): the window as usual, the picture drawn n times as large in each direction (H2N_RENDER is not used) */
	g_ssaa = (unsigned)h2n_env_int("H2N_SSAA", 0);
	if (g_ssaa >= 2) g_scale = 1; else g_ssaa = 0;
	for (fit = 1; fit < 8 && GetSystemMetrics(SM_CXSCREEN) >= (int)(width * (fit + 1)) + 64 && GetSystemMetrics(SM_CYSCREEN) >= (int)(height * (fit + 1)) + 120; fit++) { }
	scale = g_scale > 1 ? (g_scale < fit ? g_scale : fit) : fit >= 2 ? 2 : 1;
	scale = (unsigned)h2n_env_int("H2N_SCALE", (int)scale);
	if (scale < 1 || scale > 8) scale = 1;
	g_window_scale = scale;
	if (g_ssaa)
	{
		unsigned most = 8 / scale;                    /* drawn at most 8 times the game's picture, a whole number of times the window */
		if (most < 2) { h2n_log("       picture: H2N_SSAA: the window is already %u times the picture, so it cannot be drawn larger; no supersampling\n", scale); g_ssaa = 0; }
		else { if (g_ssaa > most) { h2n_log("       picture: H2N_SSAA=%u: at most %u with this window\n", g_ssaa, most); g_ssaa = most; } g_scale = scale * g_ssaa; }
	}
	/* a console set to widescreen still draws 640x480; the television stretches that picture to 16:9, and so does this window */
	g_window_height = height * scale; g_window_width = h2n_env_int("H2N_WIDE", 0) ? (g_window_height * 16 + 4) / 9 : width * scale;
	r.left = 0; r.top = 0; r.right = (LONG)g_window_width; r.bottom = (LONG)g_window_height;
	AdjustWindowRect(&r, style, FALSE);
	g_window = CreateWindowExA(0, "h2classic", "H2Classic", style, CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left, r.bottom - r.top, NULL, NULL, wc.hInstance, NULL);
	}
	if (!g_window) { h2n_log("*** picture: the window could not be created (error %lu); nothing will be drawn\n", GetLastError()); return 0; }
	{	/* (s10v) the mouse's own movement to this window (usage page 1, usage 2: a mouse) */
		RAWINPUTDEVICE device;
		device.usUsagePage = 1; device.usUsage = 2; device.dwFlags = 0; device.hwndTarget = g_window;
		g_mouse_raw_ok = RegisterRawInputDevices(&device, 1, sizeof(device)) != 0;
		if (!g_mouse_raw_ok) h2n_log("       mouse: raw input could not be registered (error %lu); the mouse does not look\n", GetLastError());
	}
	ShowWindow(g_window, SW_SHOWNORMAL);
	pump();

	memset(&g_pp, 0, sizeof(g_pp));
	g_pp.BackBufferWidth = g_window_width; g_pp.BackBufferHeight = g_window_height; g_pp.BackBufferFormat = D3DFMT_X8R8G8B8; g_pp.BackBufferCount = 1;
	g_pp.SwapEffect = D3DSWAPEFFECT_COPY; g_pp.hDeviceWindow = g_window; g_pp.Windowed = TRUE;
	g_pp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
	hr = E_FAIL;
	for (i = 0; i < 3 && FAILED(hr); i++)
	{
		if (g_ex)
		{
			IDirect3DDevice9Ex *ex = NULL;
			hr = IDirect3D9Ex_CreateDeviceEx((IDirect3D9Ex *)g_d3d, D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, g_window, processing[i] | D3DCREATE_FPU_PRESERVE | D3DCREATE_MULTITHREADED, &g_pp, NULL, &ex);
			g_device = SUCCEEDED(hr) ? (IDirect3DDevice9 *)ex : NULL;
		}
		else hr = IDirect3D9_CreateDevice(g_d3d, D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, g_window, processing[i] | D3DCREATE_FPU_PRESERVE | D3DCREATE_MULTITHREADED, &g_pp, &g_device);
	}
	if (FAILED(hr) && g_ex)
	{
		/* no 9Ex device: the ordinary one, and TAA in place of DLSS */
		IDirect3D9 *(WINAPI *create9)(UINT) = (void *)GetProcAddress(library, "Direct3DCreate9");
		dlss_failed("the Direct3D 9Ex device could not be created");
		IDirect3D9_Release(g_d3d); g_d3d = create9 ? create9(D3D_SDK_VERSION) : NULL; g_ex = 0;
		for (i = 0; g_d3d && i < 3 && FAILED(hr); i++)
			hr = IDirect3D9_CreateDevice(g_d3d, D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, g_window, processing[i] | D3DCREATE_FPU_PRESERVE | D3DCREATE_MULTITHREADED, &g_pp, &g_device);
	}
	if (FAILED(hr)) { g_device = NULL; h2n_log("*** picture: the Direct3D 9 device could not be created (%08lx); nothing will be drawn\n", (unsigned long)hr); return 0; }
	{
		D3DADAPTER_IDENTIFIER9 id;
		D3DCAPS9 caps;
		memset(&id, 0, sizeof(id)); memset(&caps, 0, sizeof(caps));
		IDirect3D9_GetAdapterIdentifier(g_d3d, D3DADAPTER_DEFAULT, 0, &id);
		IDirect3DDevice9_GetDeviceCaps(g_device, &caps);
		while (g_scale > 1 && (caps.MaxTextureWidth < width * g_scale || caps.MaxTextureHeight < height * g_scale)) g_scale--;
		while (g_scale_most > 1 && (caps.MaxTextureWidth < width * g_scale_most || caps.MaxTextureHeight < height * g_scale_most)) g_scale_most--;
		h2n_log("       picture: window %ux%u, the game's %ux%u picture drawn at %ux%u (H2N_RENDER=%u), Direct3D 9%s on \"%s\", vertex shaders %lu.%lu, pixel shaders %lu.%lu, vertex processing %s\n",
			g_new_window ? g_window_width : width * scale, g_new_window ? g_window_height : height * scale, width, height, width * g_scale, height * g_scale, g_scale, g_ex ? "Ex" : "", id.Description, (caps.VertexShaderVersion >> 8) & 255, caps.VertexShaderVersion & 255,
			(caps.PixelShaderVersion >> 8) & 255, caps.PixelShaderVersion & 255, i == 1 ? "hardware" : i == 2 ? "mixed" : "software");
		if (g_ex) dlss_start(g_d3d);
		if (!g_depth_mode || g_half != -0.5f || g_lazy)
			h2n_log("       picture: test switches: depth %s, pixel shift %g, %s\n", g_depth_mode ? "as the console's" : "as stage 8's (H2N_GFXDEPTH=0)", g_half, g_lazy ? "only the frames that are saved are drawn (H2N_GFXLAZY)" : "every frame drawn");
		if (g_ssaa)
			h2n_log("       picture: supersampling (H2N_SSAA=%u): drawn at %ux%u, %s\n", g_ssaa, width * g_scale, height * g_scale,
				g_scale == scale * g_ssaa ? "the window's size times H2N_SSAA, averaged down" : "less than asked (the device's largest texture)");
		if (aa_mode() != AA_OFF)
			h2n_log("       picture: anti-aliasing H2N_AA=%s asked for: each player's 3D view, before its HUD is drawn\n", aa_mode() == AA_FXAA ? "fxaa" : aa_mode() == AA_SMAA ? "smaa" : aa_mode() == AA_DLSS ? "dlss" : "taa");
		if ((caps.VertexShaderVersion & 0xffff) < 0x300 || (caps.PixelShaderVersion & 0xffff) < 0x300)
			h2n_log("*** picture: this graphics device does not offer shader model 3, which the picture needs; expect a black window\n");
	}
	g_width = width; g_height = height;
	set_defaults();
	h2n_window_open();
	h2n_display_rate(g_window);       /* (H2N_UNLOCK: the vertical blank follows this window's display - timing.c) */
	return 1;
}

static void save_surface(IDirect3DSurface9 *surface, unsigned width, unsigned height, const char *name)
{
	IDirect3DSurface9 *copy = NULL;
	D3DLOCKED_RECT lock;
	FILE *f;
	D3DSURFACE_DESC description;
	/* the copy in ordinary memory has to have the surface's own format */
	if (FAILED(IDirect3DSurface9_GetDesc(surface, &description))) return;
	if (FAILED(IDirect3DDevice9_CreateOffscreenPlainSurface(g_device, width, height, description.Format, D3DPOOL_SYSTEMMEM, &copy, NULL))) return;
	if (SUCCEEDED(IDirect3DDevice9_GetRenderTargetData(g_device, surface, copy)) && SUCCEEDED(IDirect3DSurface9_LockRect(copy, &lock, NULL, D3DLOCK_READONLY)))
	{
		f = h2n_real_fopen(name, "wb");
		if (f)
		{
			unsigned char header[54];
			unsigned row = width * 4, size = 54 + row * height, y;
			memset(header, 0, sizeof(header));
			header[0] = 'B'; header[1] = 'M'; memcpy(header + 2, &size, 4); header[10] = 54; header[14] = 40;
			memcpy(header + 18, &width, 4); memcpy(header + 22, &height, 4); header[26] = 1; header[28] = 32;
			fwrite(header, 1, 54, f);
			for (y = height; y-- > 0;) fwrite((const unsigned char *)lock.pBits + y * lock.Pitch, 1, row, f);
			fclose(f);
			h2n_log("       picture: frame %u saved as %s\n", g_frame, name);
		}
		IDirect3DSurface9_UnlockRect(copy);
	}
	IDirect3DSurface9_Release(copy);
}

/* (s10r) F11 also keeps the text pages: the 16-bit textures (A4R4G4B4) the game draws its letters from - it writes each
 * letter's pixels into one of these itself, so a wrong letter on the screen is either wrong here already (the game's
 * font cache) or drawn wrongly from a right page. Saved from the game's memory at the end of a described frame as
 * h2classic_surface_<frame>_<memory>.bmp: the left half is the page's coverage (alpha) as grey, the right half its colour. */
static gx_texture g_text_pages[8];
static unsigned g_text_page_count, g_text_vertex_lines;
static void unpack_level(unsigned *out, unsigned out_pitch, const unsigned char *source, const gx_texture *t, unsigned width, unsigned height, unsigned depth, unsigned slice, unsigned level);
static int text_page_note(const gx_texture *t)
{
	unsigned i;
	if (!t->memory || (t->format != 0x04 && t->format != 0x1d) || t->cube || t->volume || t->width > 512 || t->height > 512 || !t->width || !t->height) return 0;
	for (i = 0; i < g_text_page_count; i++) if (g_text_pages[i].memory == t->memory) return 1;
	if (g_text_page_count < 8) g_text_pages[g_text_page_count++] = *t;
	return 1;
}
static void text_pages_save(void)
{
	unsigned i;
	for (i = 0; i < g_text_page_count; i++)
	{
		const gx_texture *t = &g_text_pages[i];
		unsigned w = t->width, h = t->height, x, y, size = 54 + w * 2 * 4 * h, w2 = w * 2;
		unsigned *pixels = malloc(w * h * 4), *row = malloc(w * 2 * 4);
		unsigned char header[54];
		char name[80];
		FILE *f;
		if (!pixels || !row) { free(pixels); free(row); continue; }
		unpack_level(pixels, w * 4, t->memory, t, w, h, 1, 0, 0);
		snprintf(name, sizeof(name), "h2classic_surface_%05u_%08x.bmp", g_frame, (unsigned)(size_t)t->memory);
		f = h2n_real_fopen(name, "wb");
		if (f)
		{
			memset(header, 0, sizeof(header));
			header[0] = 'B'; header[1] = 'M'; memcpy(header + 2, &size, 4); header[10] = 54; header[14] = 40;
			memcpy(header + 18, &w2, 4); memcpy(header + 22, &h, 4); header[26] = 1; header[28] = 32;
			fwrite(header, 1, 54, f);
			for (y = h; y-- > 0;)
			{
				for (x = 0; x < w; x++)
				{
					unsigned c = pixels[y * w + x];
					row[x] = 0xff000000u | (c >> 24) * 0x010101u;
					row[w + x] = 0xff000000u | (c & 0xffffffu);
				}
				fwrite(row, 4, w * 2, f);
			}
			fclose(f);
			h2n_log("       picture: frame %u: text page %p (%ux%u, the letters' own texture) saved from the game's memory as %s\n", g_frame, t->memory, w, h, name);
		}
		free(pixels); free(row);
	}
	g_text_page_count = 0; g_text_vertex_lines = 0;
}

/* ================================================================ byte code ==== */
typedef struct code { unsigned *t; unsigned n, capacity; } code;
static void put(code *c, unsigned v)
{
	if (c->n == c->capacity) { c->capacity = c->capacity ? c->capacity * 2 : 512; c->t = realloc(c->t, c->capacity * 4); }
	c->t[c->n++] = v;
}
enum { RT_TEMP = 0, RT_INPUT = 1, RT_CONST = 2, RT_ADDR = 3, RT_OUTPUT = 6, RT_COLOROUT = 8, RT_DEPTHOUT = 9, RT_SAMPLER = 10 };
enum { OP_MOV = 1, OP_ADD = 2, OP_SUB = 3, OP_MAD = 4, OP_MUL = 5, OP_RCP = 6, OP_RSQ = 7, OP_DP3 = 8, OP_DP4 = 9, OP_MIN = 10, OP_MAX = 11, OP_SLT = 12, OP_SGE = 13,
	OP_EXP = 14, OP_LOG = 15, OP_LIT = 16, OP_DST = 17, OP_LRP = 18, OP_FRC = 19, OP_DCL = 31, OP_ABS = 35, OP_MOVA = 46, OP_TEXKILL = 65, OP_TEX = 66, OP_CMP = 88,
	OP_DSX = 91, OP_DSY = 92 };
enum { USAGE_POSITION = 0, USAGE_TEXCOORD = 5, USAGE_COLOR = 10 };
#define RTYPE(t) ((((unsigned)(t) & 7u) << 28) | (((unsigned)(t) & 0x18u) << 8))
#define DST(t, n, mask) (0x80000000u | RTYPE(t) | (unsigned)(n) | ((unsigned)(mask) << 16))
#define SATURATE 0x00100000u
#define SRC(t, n, swizzle) (0x80000000u | RTYPE(t) | (unsigned)(n) | ((unsigned)(swizzle) << 16))
#define NEGATE 0x01000000u
#define ABS_NEG 0x0C000000u          /* (s10k) -|x| (shader model 3) */
#define REL_ADDRESS 0x2000u
#define SWZ(x, y, z, w) ((unsigned)(x) | ((unsigned)(y) << 2) | ((unsigned)(z) << 4) | ((unsigned)(w) << 6))
#define XYZW SWZ(0, 1, 2, 3)
#define XXXX SWZ(0, 0, 0, 0)
#define YYYY SWZ(1, 1, 1, 1)
#define ZZZZ SWZ(2, 2, 2, 2)
#define WWWW SWZ(3, 3, 3, 3)
#define M_X 1
#define M_Y 2
#define M_Z 4
#define M_W 8
#define M_XYZ 7
#define M_ALL 15
#define TEMP(n) SRC(RT_TEMP, n, XYZW)
#define CONSTANT(n) SRC(RT_CONST, n, XYZW)
static void ins(code *c, unsigned op, unsigned count, ...)
{
	va_list a;
	unsigned i;
	put(c, op | (count << 24));
	va_start(a, count);
	for (i = 0; i < count; i++) put(c, va_arg(a, unsigned));
	va_end(a);
}
static void declare(code *c, unsigned usage, unsigned index, unsigned type, unsigned number)
{
	ins(c, OP_DCL, 2, 0x80000000u | usage | (index << 16), DST(type, number, M_ALL));
}
static unsigned swizzle_of(unsigned token) { return (token >> 16) & 255; }
static unsigned with_swizzle(unsigned token, unsigned swizzle) { return (token & ~0x00ff0000u) | (swizzle << 16); }
static unsigned component(unsigned token, unsigned which)      /* the same operand, with one of its four components in all four places */
{
	unsigned s = (swizzle_of(token) >> (which * 2)) & 3;
	return with_swizzle(token, SWZ(s, s, s, s));
}
static unsigned hash_words(const void *data, unsigned bytes, unsigned h)
{
	const unsigned char *p = data;
	unsigned i;
	for (i = 0; i < bytes; i++) h = (h ^ p[i]) * 16777619u;
	return h;
}

static void dump_words(const char *what, const unsigned *words, unsigned count)       /* for a shader Direct3D 9 refused: enough to see why from the log */
{
	static unsigned dumps;
	unsigned i;
	if (dumps++ >= 24) return;
	h2n_log("       picture: %s (%u words):", what, count);
	for (i = 0; i < count && i < 1200; i++) h2n_log("%s%08x", i % 16 ? " " : "\n         ", words[i]);
	h2n_log("\n");
}

/* Windows' Direct3D 9 checks a shader before it accepts it (Wine does not), so every shader written here is
 * checked first by the documented rules that such a check enforces: an instruction may read one constant register,
 * one input register and three temporaries; a register is written before it is read; reciprocal, root, exponent and
 * logarithm take one component; inputs and samplers are declared; every declared output is written in full. */
static unsigned check_shader(const unsigned *t, unsigned count, const char *what, unsigned hash)
{
	unsigned i = 1, problems = 0, index = 0, k;
	unsigned char written[32], out_declared[16], out_written[16];
	unsigned inputs_declared = 0, samplers_declared = 0, address_written = 0, depth_written = 0;
	int pixel = (t[0] >> 16) == 0xffff;
	memset(written, 0, sizeof(written)); memset(out_declared, 0, sizeof(out_declared)); memset(out_written, 0, sizeof(out_written));
#define PROBLEM(...) do { if (problems++ < 6) { char text[200]; snprintf(text, sizeof(text), __VA_ARGS__); note("%s %08x, instruction %u (word %u): %s", what, hash, index, i, text); } } while (0)
	while (i < count && (t[i] & 0xffff) != 0xffff)
	{
		unsigned op = t[i] & 0xffff, length = (t[i] >> 24) & 15, first = i + 1, n;
		unsigned constants[4], constant_count = 0, input_registers[4], input_count = 0, temporaries[4], temporary_count = 0;
		unsigned destination, mask, type;
		if (first + length > count) { PROBLEM("runs past the end"); break; }
		if (op == OP_DCL)
		{
			unsigned reg = t[first + 1];
			type = ((reg >> 28) & 7) | ((reg >> 8) & 0x18);
			if (type == RT_INPUT) inputs_declared |= 1u << (reg & 31);
			else if (type == RT_SAMPLER) samplers_declared |= 1u << (reg & 31);
			else if (type == RT_OUTPUT && (reg & 0x7ff) < 16) out_declared[reg & 0x7ff] |= (reg >> 16) & 15;
			i = first + length;
			continue;
		}
		destination = t[first]; mask = (destination >> 16) & 15; type = ((destination >> 28) & 7) | ((destination >> 8) & 0x18);
		for (n = first + (op == OP_TEXKILL ? 0 : 1); n < first + length; n++)
		{
			unsigned source = t[n], source_type = ((source >> 28) & 7) | ((source >> 8) & 0x18), number = source & 0x7ff, swizzle = (source >> 16) & 255, read = 0, c;
			unsigned position = n - first - 1;          /* which source of the instruction */
			if (op == OP_TEXKILL) { swizzle = XYZW; read = mask; }
			else if (op == OP_DP3) read = 7;
			else if (op == OP_DP4 || op == OP_LIT || op == OP_DST) read = 15;
			else if (op == OP_RCP || op == OP_RSQ || op == OP_EXP || op == OP_LOG)
			{
				read = 1;
				if (swizzle != (swizzle & 3) * 0x55) PROBLEM("a one-component operation is given several components");
			}
			else if (op == OP_TEX) read = position == 0 ? 7 : 0;
			else if (op == OP_MOVA) read = mask;
			else read = mask;
			if (source_type == RT_CONST) { for (k = 0; k < constant_count && constants[k] != number; k++) { } if (k == constant_count && constant_count < 4) constants[constant_count++] = number; }
			else if (source_type == RT_INPUT)
			{
				for (k = 0; k < input_count && input_registers[k] != number; k++) { }
				if (k == input_count && input_count < 4) input_registers[input_count++] = number;
				if (!(inputs_declared & (1u << (number & 31)))) PROBLEM("reads input %u, which is not declared", number);
			}
			else if (source_type == RT_SAMPLER) { if (!(samplers_declared & (1u << (number & 31)))) PROBLEM("uses sampler %u, which is not declared", number); }
			else if (source_type == RT_TEMP && number < 32)
			{
				for (k = 0; k < temporary_count && temporaries[k] != number; k++) { }
				if (k == temporary_count && temporary_count < 4) temporaries[temporary_count++] = number;
				for (c = 0; c < 4; c++)
					if ((read & (1u << c)) && !(written[number] & (1u << ((swizzle >> (2 * c)) & 3)))) { PROBLEM("reads r%u.%c before it is written", number, "xyzw"[(swizzle >> (2 * c)) & 3]); break; }
			}
			else if (source_type == RT_ADDR && !pixel) { if (!address_written) PROBLEM("uses the address register before it is written"); }
			if ((source & REL_ADDRESS) && !pixel) { n++; if (!address_written) PROBLEM("indexes constants before the address register is written"); }
		}
		if (constant_count > 1) PROBLEM("reads %u different constant registers (c%u, c%u); one is allowed", constant_count, constants[0], constants[1]);
		if (input_count > 1) PROBLEM("reads %u different input registers; one is allowed", input_count);
		if (temporary_count > 3) PROBLEM("reads %u different temporaries; three are allowed", temporary_count);
		if (op != OP_TEXKILL)
		{
			if (!mask) PROBLEM("writes nothing");
			if (type == RT_TEMP && (destination & 0x7ff) < 32) written[destination & 0x7ff] |= (unsigned char)mask;
			else if ((type == RT_OUTPUT && !pixel) || (type == RT_COLOROUT && pixel)) { if ((destination & 0x7ff) < 16) out_written[destination & 0x7ff] |= (unsigned char)mask; }
			else if (type == RT_DEPTHOUT && pixel)       /* the depth: one value, from one component, written once */
			{
				unsigned swizzle = (t[first + 1] >> 16) & 255;
				if (op != OP_MOV || swizzle != (swizzle & 3) * 0x55) PROBLEM("the depth is not written by a move of one component");
				if (depth_written++) PROBLEM("the depth is written twice");
			}
			else if (type == RT_ADDR && !pixel) address_written = 1;
			else PROBLEM("writes a register of type %u", type);
		}
		index++;
		i = first + length;
	}
	if (i >= count) PROBLEM("has no end");
	if (index > 512) PROBLEM("has %u instructions; 512 are sure to be allowed", index);
	if (pixel) { if (out_written[0] != 15) PROBLEM("does not write the whole colour"); }
	else for (k = 0; k < 16; k++) if (out_declared[k] != out_written[k]) PROBLEM("output %u is declared %x and written %x", k, out_declared[k], out_written[k]);
#undef PROBLEM
	return problems;
}
static unsigned g_shaders_made, g_shaders_refused, g_shader_problems;
static int dump_wanted(unsigned hash)       /* H2N_GFXDUMP=<hash>[,<hash>...]: write these programs and combiner settings, and the shaders made from them, to the log */
{
	const char *list = getenv("H2N_GFXDUMP");
	while (list && *list)
	{
		char *end;
		unsigned long v = strtoul(list, &end, 16);
		if (end == list) break;
		if ((unsigned)v == hash) return 1;
		list = end;
		while (*list == ',' || *list == ' ') list++;
	}
	return 0;
}

/* A shader Direct3D 9 refused although the check above passed it: find out which instruction it is. Each instruction
 * is tried alone in a small shader that has the same declarations, every temporary written beforehand and every
 * output written afterwards, so the one Direct3D 9 will not take shows itself. */
static void probe_refused(const unsigned *t, unsigned count, const char *what, unsigned hash)
{
	static unsigned probed;
	unsigned pixel = (t[0] >> 16) == 0xffff, i = 1, declarations_end = 1, k, tried = 0, refused = 0, tested[256], tested_count = 0;
	unsigned outputs[16], output_count = 0;
	code base;
	if (probed++ >= 4) return;
	while (i < count && (t[i] & 0xffff) == OP_DCL)
	{
		unsigned reg = t[i + 2], type = ((reg >> 28) & 7) | ((reg >> 8) & 0x18);
		if (type == RT_OUTPUT && !pixel && output_count < 16) outputs[output_count++] = reg & 0x7ff;
		i += 1 + ((t[i] >> 24) & 15);
	}
	declarations_end = i;
	for (k = 0; k <= 1; k++)       /* first the frame alone, then each instruction in it */
	{
		unsigned j = declarations_end;
		while (j < count && (t[j] & 0xffff) != 0xffff)
		{
			unsigned length = 1 + ((t[j] >> 24) & 15), n, h = hash_words(&t[j], length * 4, 2166136261u), seen = 0;
			IUnknown *made = NULL;
			HRESULT hr;
			memset(&base, 0, sizeof(base));
			for (n = 0; n < declarations_end; n++) put(&base, t[n]);
			for (n = 0; n < 32; n++) ins(&base, OP_MOV, 2, DST(RT_TEMP, n, M_ALL), SRC(RT_CONST, 0, XYZW));
			if (!pixel) ins(&base, OP_MOVA, 2, DST(RT_ADDR, 0, M_X), SRC(RT_CONST, 0, XXXX));
			if (k) { for (n = 0; n < tested_count; n++) if (tested[n] == h) seen = 1; if (!seen) for (n = 0; n < length; n++) put(&base, t[j + n]); }
			if (pixel) ins(&base, OP_MOV, 2, DST(RT_COLOROUT, 0, M_ALL), SRC(RT_CONST, 0, XYZW));
			else for (n = 0; n < output_count; n++) ins(&base, OP_MOV, 2, DST(RT_OUTPUT, outputs[n], M_ALL), SRC(RT_CONST, 0, XYZW));
			put(&base, 0x0000ffff);
			if (!seen || !k)
			{
				hr = pixel ? IDirect3DDevice9_CreatePixelShader(g_device, (const DWORD *)base.t, (IDirect3DPixelShader9 **)&made)
					: IDirect3DDevice9_CreateVertexShader(g_device, (const DWORD *)base.t, (IDirect3DVertexShader9 **)&made);
				if (SUCCEEDED(hr) && made) IUnknown_Release(made);
				if (!k)
				{
					if (FAILED(hr)) { h2n_log("       picture: %s %08x: even its declarations with nothing else are refused (%08lx)\n", what, hash, (unsigned long)hr); dump_words("that frame", base.t, base.n); free(base.t); return; }
					free(base.t);
					break;
				}
				tried++;
				if (tested_count < 256) tested[tested_count++] = h;
				if (FAILED(hr) && refused++ < 6)
				{
					char text[160];
					unsigned used = 0;
					for (n = 0; n < length && used < sizeof(text) - 10; n++) used += (unsigned)snprintf(text + used, sizeof(text) - used, " %08x", t[j + n]);
					h2n_log("       picture: %s %08x: Direct3D 9 refuses this instruction on its own (word %u):%s\n", what, hash, j, text);
				}
			}
			free(base.t);
			j += length;
		}
	}
	h2n_log("       picture: %s %08x: %u different instructions tried on their own, %u refused%s\n", what, hash, tried, refused,
		refused ? "" : " (so it is the shader as a whole that is refused: its size, its declarations or the order of things)");
}

/* ================================================================ vertex programs ==== */
/* registers of the translated program: r0-r11 the console's, r12 its position output (which it can read back),
 * r13-r22 its other outputs, r27-r30 scratch. constants: c0-c191 the console's, c192 and c193 turn the screen
 * coordinates the program produces back into clip space, c194 = (0, 1, 0.5, 2), c195 = limits for "rcc",
 * c196 = (P, Q, R, 0): the depth Direct3D 9 cuts triangles by is (z * w) * P + w * Q + R (see gx_draw).
 * Besides the console's outputs the shader writes texture coordinate 5 = (z * w, w, 0, 1): the pixel shader makes
 * the depth of each pixel from it, as the console's GPU does from the position itself. */
enum { VR_POSITION = 12, VR_OUTPUT = 13, VR_MAC = 27, VR_ILU = 28, VR_SCRATCH = 29, VR_SCRATCH2 = 30, VR_ZERO = 31, VC_SCALE = 192, VC_OFFSET = 193, VC_NUMBERS = 194, VC_LIMITS = 195, VC_DEPTH = 196,
	VC_HELPERS = 5 };
typedef struct vertex_shader { unsigned hash, length, inputs, told; unsigned key[136][4]; IDirect3DVertexShader9 *shader; IDirect3DVertexDeclaration9 *declaration; unsigned slot[16], stride; struct vertex_shader *next; } vertex_shader;
static vertex_shader *g_vertex_shaders[256];

static unsigned field(const unsigned *w, int word, int shift, int bits) { return (w[word] >> shift) & ((1u << bits) - 1); }
static unsigned d3d_mask(unsigned m) { return ((m & 8) >> 3) | ((m & 4) >> 1) | ((m & 2) << 1) | ((m & 1) << 3); }

typedef struct operand { unsigned token[2], count; } operand;
static operand vp_source(const unsigned *w, int which, unsigned *inputs)
{
	static const int place[3][7] = {      /* word and shift of: negate, x, y, z, w, register, multiplexer */
		{ 8, 6, 4, 2, 0, 28, 26 }, { 25, 23, 21, 19, 17, 13, 11 }, { 10, 8, 6, 4, 2, 0, 28 } };
	const int *p = place[which];
	int w1 = which == 0 ? 1 : 2;
	unsigned negate = field(w, w1, p[0], 1), swizzle = SWZ(field(w, w1, p[1], 2), field(w, w1, p[2], 2), field(w, w1, p[3], 2), field(w, w1, p[4], 2));
	unsigned reg = which == 0 ? field(w, 2, 28, 4) : which == 1 ? field(w, 2, 13, 4) : (field(w, 2, 0, 2) << 2) | field(w, 3, 30, 2);
	unsigned mux = which == 0 ? field(w, 2, 26, 2) : which == 1 ? field(w, 2, 11, 2) : field(w, 3, 28, 2);
	operand o;
	o.count = 1; o.token[1] = 0;
	switch (mux)
	{
	case 1: o.token[0] = SRC(RT_TEMP, reg > 12 ? 12 : reg, swizzle); break;
	case 2: o.token[0] = SRC(RT_INPUT, field(w, 1, 9, 4), swizzle); *inputs |= 1u << field(w, 1, 9, 4); break;
	case 3:
		o.token[0] = SRC(RT_CONST, field(w, 1, 13, 8), swizzle);
		if (field(w, 3, 1, 1)) { o.token[0] |= REL_ADDRESS; o.token[1] = SRC(RT_ADDR, 0, XXXX); o.count = 2; }
		break;
	default: o.token[0] = SRC(RT_TEMP, VR_ZERO, XXXX); break;       /* a register, not a constant: an instruction may read only one constant register */
	}
	if (negate) o.token[0] |= NEGATE;
	return o;
}
static void put_operand(code *c, operand o) { put(c, o.token[0]); if (o.count == 2) put(c, o.token[1]); }
static void vp_op(code *c, unsigned op, unsigned destination, int n, operand *o)
{
	unsigned count = 1;
	int i;
	for (i = 0; i < n; i++) count += o[i].count;
	put(c, op | (count << 24)); put(c, destination);
	for (i = 0; i < n; i++) put_operand(c, o[i]);
}
static operand scalar(operand o) { o.token[0] = component(o.token[0], 0); return o; }

static const char *const vp_mac_names[16] = { "nop", "mov", "mul", "add", "mad", "dp3", "dph", "dp4", "dst", "min", "max", "slt", "sge", "arl", "?", "?" };
static const char *const vp_ilu_names[8] = { "nop", "mov", "rcp", "rcc", "rsq", "exp", "log", "lit" };

static void vp_describe(const unsigned *w, char *text, unsigned size)
{
	static const char *const mux_names = "?rvc";
	unsigned mac = field(w, 1, 21, 4), ilu = field(w, 1, 25, 3), n = 0;
	int i;
	n += snprintf(text + n, size - n, "%s/%s", vp_mac_names[mac], vp_ilu_names[ilu]);
	for (i = 0; i < 3; i++)
	{
		unsigned inputs = 0;
		operand o = vp_source(w, i, &inputs);
		unsigned mux = i == 0 ? field(w, 2, 26, 2) : i == 1 ? field(w, 2, 11, 2) : field(w, 3, 28, 2);
		unsigned s = swizzle_of(o.token[0]);
		n += snprintf(text + n, size - n, " %s%c%u%s.%c%c%c%c", o.token[0] & NEGATE ? "-" : "", mux_names[mux], o.token[0] & 0x7ff, o.count == 2 ? "[a0]" : "",
			"xyzw"[s & 3], "xyzw"[(s >> 2) & 3], "xyzw"[(s >> 4) & 3], "xyzw"[(s >> 6) & 3]);
	}
	snprintf(text + n, size - n, " -> r%u mask %x (ilu mask %x), %s%u mask %x from %s%s", field(w, 3, 20, 4), field(w, 3, 24, 4), field(w, 3, 16, 4),
		field(w, 3, 11, 1) ? "o" : "c", field(w, 3, 3, 8), field(w, 3, 12, 4), field(w, 3, 2, 1) ? "ilu" : "mac", field(w, 3, 0, 1) ? " END" : "");
}

static vertex_shader *vertex_shader_for(const unsigned (*program)[4], unsigned start)
{
	unsigned length = 0, hash, i, inputs = 0, temps_read = 0, outputs = 0;
	vertex_shader *v;
	code c;
	D3DVERTEXELEMENT9 elements[17];
	HRESULT hr;

	while (start + length < 136) { length++; if (program[start + length - 1][3] & 1) break; }
	hash = hash_words(program[start], length * 16, 2166136261u);
	for (v = g_vertex_shaders[hash & 255]; v; v = v->next)
		if (v->hash == hash && v->length == length && !memcmp(v->key, program[start], length * 16)) return v;

	v = calloc(1, sizeof(*v));
	v->hash = hash; v->length = length; memcpy(v->key, program[start], length * 16);
	v->next = g_vertex_shaders[hash & 255]; g_vertex_shaders[hash & 255] = v;

	/* what it reads, so that temporaries start at zero as they do on the console */
	for (i = 0; i < length; i++)
	{
		const unsigned *w = program[start + i];
		unsigned mac = field(w, 1, 21, 4), ilu = field(w, 1, 25, 3);
		int uses[3] = { mac != 0, mac == 2 || (mac >= 4 && mac <= 12), mac == 3 || mac == 4 || ilu != 0 };
		int k;
		for (k = 0; k < 3; k++)
			if (uses[k])
			{
				unsigned mux = k == 0 ? field(w, 2, 26, 2) : k == 1 ? field(w, 2, 11, 2) : field(w, 3, 28, 2);
				unsigned reg = k == 0 ? field(w, 2, 28, 4) : k == 1 ? field(w, 2, 13, 4) : (field(w, 2, 0, 2) << 2) | field(w, 3, 30, 2);
				if (mux == 1) temps_read |= 1u << (reg > 12 ? 12 : reg);
				if (mux == 2) inputs |= 1u << field(w, 1, 9, 4);
			}
	}
	v->inputs = inputs;

	memset(&c, 0, sizeof(c));
	put(&c, 0xfffe0300);
	for (i = 0; i < 16; i++) if (inputs & (1u << i)) declare(&c, USAGE_TEXCOORD, i, RT_INPUT, i);
	declare(&c, USAGE_POSITION, 0, RT_OUTPUT, 0);
	declare(&c, USAGE_COLOR, 0, RT_OUTPUT, 1);
	declare(&c, USAGE_COLOR, 1, RT_OUTPUT, 2);
	for (i = 0; i < 5; i++) declare(&c, USAGE_TEXCOORD, i, RT_OUTPUT, 3 + i);
	declare(&c, USAGE_TEXCOORD, 5, RT_OUTPUT, 8);
	for (i = 0; i <= 12; i++) ins(&c, OP_MOV, 2, DST(RT_TEMP, i, M_ALL), SRC(RT_CONST, VC_NUMBERS, i == 12 ? SWZ(0, 0, 0, 1) : XXXX));
	ins(&c, OP_MOV, 2, DST(RT_TEMP, VR_ZERO, M_ALL), SRC(RT_CONST, VC_NUMBERS, XXXX));
	for (i = 0; i < 10; i++) ins(&c, OP_MOV, 2, DST(RT_TEMP, VR_OUTPUT + i, M_ALL), SRC(RT_CONST, VC_NUMBERS, i == 2 ? YYYY : SWZ(0, 0, 0, 1)));
	(void)temps_read;
	for (i = 0; i < length; i++) if (field(program[start + i], 3, 1, 1)) break;
	if (i < length) ins(&c, OP_MOVA, 2, DST(RT_ADDR, 0, M_X), SRC(RT_CONST, VC_NUMBERS, XXXX));      /* the address register starts at zero */

	for (i = 0; i < length; i++)
	{
		const unsigned *w = program[start + i];
		unsigned mac = field(w, 1, 21, 4), ilu = field(w, 1, 25, 3);
		unsigned mac_mask = d3d_mask(field(w, 3, 24, 4)), ilu_mask = d3d_mask(field(w, 3, 16, 4)), out_mask = d3d_mask(field(w, 3, 12, 4));
		unsigned out_r = field(w, 3, 20, 4), out_is_register = field(w, 3, 11, 1), out_address = field(w, 3, 3, 8), out_from_ilu = field(w, 3, 2, 1);
		unsigned mac_result = DST(RT_TEMP, VR_MAC, M_ALL), ilu_result = DST(RT_TEMP, VR_ILU, M_ALL), scratch = DST(RT_TEMP, VR_SCRATCH, M_ALL);
		operand o[3], t[3];
		int have_address = 0;
		if (g_trace) { char text[200]; vp_describe(w, text, sizeof(text)); h2n_log("       vertex program %08x +%u: %s\n", hash, i, text); }
		{ unsigned unused = 0; o[0] = vp_source(w, 0, &unused); o[1] = vp_source(w, 1, &unused); o[2] = vp_source(w, 2, &unused); }
		if (out_r > 12) out_r = 12;
		switch (mac)
		{
		case 0: break;
		case 1: vp_op(&c, OP_MOV, mac_result, 1, o); break;
		case 2: vp_op(&c, OP_MUL, mac_result, 2, o); break;
		case 3: t[0] = o[0]; t[1] = o[2]; vp_op(&c, OP_ADD, mac_result, 2, t); break;      /* add takes its second operand from input C */
		case 4: vp_op(&c, OP_MAD, mac_result, 3, o); break;
		case 5: vp_op(&c, OP_DP3, mac_result, 2, o); break;
		case 6:
			vp_op(&c, OP_DP3, scratch, 2, o);
			t[0].count = 1; t[0].token[0] = TEMP(VR_SCRATCH); t[1] = o[1]; t[1].token[0] = component(o[1].token[0], 3);
			vp_op(&c, OP_ADD, mac_result, 2, t);
			break;
		case 7: vp_op(&c, OP_DP4, mac_result, 2, o); break;
		case 8: vp_op(&c, OP_DST, mac_result, 2, o); break;
		case 9: vp_op(&c, OP_MIN, mac_result, 2, o); break;
		case 10: vp_op(&c, OP_MAX, mac_result, 2, o); break;
		case 11: vp_op(&c, OP_SLT, mac_result, 2, o); break;
		case 12: vp_op(&c, OP_SGE, mac_result, 2, o); break;
		case 13:                                                                              /* the address register takes the floor */
			/* the console adds a little before taking the floor, so that an index kept as byte/255 and scaled
			 * back up (bone numbers) lands on its whole number; without it 7/255 * 765 gives bone 20, not 21 */
			t[0] = scalar(o[0]);
			vp_op(&c, OP_MOV, scratch, 1, t);
			ins(&c, OP_ADD, 3, scratch, TEMP(VR_SCRATCH), SRC(RT_CONST, VC_LIMITS, ZZZZ));
			ins(&c, OP_FRC, 2, DST(RT_TEMP, VR_SCRATCH2, M_ALL), TEMP(VR_SCRATCH));
			ins(&c, OP_ADD, 3, scratch, TEMP(VR_SCRATCH), TEMP(VR_SCRATCH2) | NEGATE);
			have_address = 1;
			break;
		default: note("vertex program %08x uses an unknown operation %u", hash, mac); break;
		}
		switch (ilu)
		{
		case 0: break;
		case 1: vp_op(&c, OP_MOV, ilu_result, 1, &o[2]); break;
		case 2: t[0] = scalar(o[2]); vp_op(&c, OP_RCP, ilu_result, 1, t); break;
		case 3:                                                                               /* reciprocal, kept within limits */
			t[0] = scalar(o[2]);
			vp_op(&c, OP_RCP, ilu_result, 1, t);
			ins(&c, OP_SGE, 3, scratch, TEMP(VR_ILU), SRC(RT_CONST, VC_NUMBERS, XXXX));                                  /* 1 where it is not negative */
			ins(&c, OP_MAD, 4, scratch, TEMP(VR_SCRATCH), SRC(RT_CONST, VC_NUMBERS, WWWW), SRC(RT_CONST, VC_NUMBERS, YYYY) | NEGATE);   /* its sign */
			ins(&c, OP_MUL, 3, DST(RT_TEMP, VR_SCRATCH2, M_ALL), TEMP(VR_ILU), TEMP(VR_SCRATCH));                    /* its size */
			ins(&c, OP_MAX, 3, DST(RT_TEMP, VR_SCRATCH2, M_ALL), TEMP(VR_SCRATCH2), SRC(RT_CONST, VC_LIMITS, XXXX));
			ins(&c, OP_MIN, 3, DST(RT_TEMP, VR_SCRATCH2, M_ALL), TEMP(VR_SCRATCH2), SRC(RT_CONST, VC_LIMITS, YYYY));
			ins(&c, OP_MUL, 3, ilu_result, TEMP(VR_SCRATCH2), TEMP(VR_SCRATCH));
			break;
		case 4: t[0] = scalar(o[2]); vp_op(&c, OP_RSQ, ilu_result, 1, t); break;
		case 5:                                                                               /* (2^floor(x), x - floor(x), 2^x, 1) */
			/* floor is taken as x - frc(x), which is NaN for an infinite x where the console's 2^floor(-inf) is 0: x is held
			   to +-FLT_MAX first (the same results for every finite x; 2^-FLT_MAX is 0 like 2^-inf) */
			t[0] = scalar(o[2]);
			vp_op(&c, OP_MOV, DST(RT_TEMP, VR_ILU, M_W), 1, t);
			ins(&c, OP_MAX, 3, DST(RT_TEMP, VR_ILU, M_W), SRC(RT_TEMP, VR_ILU, WWWW), SRC(RT_CONST, VC_LIMITS, WWWW) | NEGATE);
			ins(&c, OP_MIN, 3, DST(RT_TEMP, VR_ILU, M_W), SRC(RT_TEMP, VR_ILU, WWWW), SRC(RT_CONST, VC_LIMITS, WWWW));
			ins(&c, OP_FRC, 2, DST(RT_TEMP, VR_ILU, M_Y), SRC(RT_TEMP, VR_ILU, WWWW));
			ins(&c, OP_ADD, 3, DST(RT_TEMP, VR_SCRATCH, M_ALL), SRC(RT_TEMP, VR_ILU, WWWW), SRC(RT_TEMP, VR_ILU, YYYY) | NEGATE);
			ins(&c, OP_EXP, 2, DST(RT_TEMP, VR_ILU, M_X), SRC(RT_TEMP, VR_SCRATCH, XXXX));
			ins(&c, OP_EXP, 2, DST(RT_TEMP, VR_ILU, M_Z), SRC(RT_TEMP, VR_ILU, WWWW));
			ins(&c, OP_MOV, 2, DST(RT_TEMP, VR_ILU, M_W), SRC(RT_CONST, VC_NUMBERS, YYYY));
			break;
		case 6:                                                                               /* (floor(log2 x), x / 2^floor, log2 x, 1) */
			t[0] = scalar(o[2]);
			vp_op(&c, OP_LOG, DST(RT_TEMP, VR_ILU, M_Z), 1, t);
			/* log2(0): Direct3D 9 documents -FLT_MAX, some drivers (NVIDIA) give -inf, and frc(-inf) is NaN, which ran through the
			   active camouflage's bend into its texture coordinates (flat corner-coloured triangles on the user's PC). Held to
			   +-FLT_MAX: floor stays -FLT_MAX and every later 2^floor is 0, as the console's -inf gives (xemu: LOG(0) = -inf) */
			ins(&c, OP_MAX, 3, DST(RT_TEMP, VR_ILU, M_Z), SRC(RT_TEMP, VR_ILU, ZZZZ), SRC(RT_CONST, VC_LIMITS, WWWW) | NEGATE);
			ins(&c, OP_MIN, 3, DST(RT_TEMP, VR_ILU, M_Z), SRC(RT_TEMP, VR_ILU, ZZZZ), SRC(RT_CONST, VC_LIMITS, WWWW));
			ins(&c, OP_FRC, 2, DST(RT_TEMP, VR_SCRATCH, M_ALL), SRC(RT_TEMP, VR_ILU, ZZZZ));
			ins(&c, OP_ADD, 3, DST(RT_TEMP, VR_ILU, M_X), SRC(RT_TEMP, VR_ILU, ZZZZ), SRC(RT_TEMP, VR_SCRATCH, XXXX) | NEGATE);
			ins(&c, OP_EXP, 2, DST(RT_TEMP, VR_ILU, M_Y), SRC(RT_TEMP, VR_SCRATCH, XXXX));
			ins(&c, OP_MOV, 2, DST(RT_TEMP, VR_ILU, M_W), SRC(RT_CONST, VC_NUMBERS, YYYY));
			break;
		case 7: vp_op(&c, OP_LIT, ilu_result, 1, &o[2]); break;
		}
		if (have_address) ins(&c, OP_MOVA, 2, DST(RT_ADDR, 0, M_X), SRC(RT_TEMP, VR_SCRATCH, XXXX));
		if (mac && mac != 13 && mac_mask) ins(&c, OP_MOV, 2, DST(RT_TEMP, out_r, mac_mask), TEMP(VR_MAC));
		if (ilu && ilu_mask) ins(&c, OP_MOV, 2, DST(RT_TEMP, mac ? 1 : out_r, ilu_mask), TEMP(VR_ILU));
		if (out_mask && (out_from_ilu ? ilu != 0 : (mac != 0 && mac != 13)))
		{
			unsigned from = TEMP(out_from_ilu ? VR_ILU : VR_MAC);
			if (!out_is_register) { static unsigned seen[8] = { ~0u, ~0u, ~0u, ~0u, ~0u, ~0u, ~0u, ~0u }; if (note_once(seen, 8, hash)) note("vertex program %08x writes a constant; that write is left out", hash); }
			else
			{
				unsigned which = out_address & 15;
				if (which == 0) ins(&c, OP_MOV, 2, DST(RT_TEMP, VR_POSITION, out_mask), from);
				else if (which >= 3 && which <= 12) { ins(&c, OP_MOV, 2, DST(RT_TEMP, VR_OUTPUT + which - 3, out_mask), from); outputs |= 1u << which; }
				/* the fog register holds one number. A write puts its first masked component there (x before y before z before w),
				   whatever the mask names: the water's program writes "oFog.w" and nothing else. (xemu's reading of the console:
				   "Writes to the oFog register apply the most significant masked component to x".) The pixel shader reads x. */
				if (which == 5 && !(out_mask & M_X))
					ins(&c, OP_MOV, 2, DST(RT_TEMP, VR_OUTPUT + 2, M_X), SRC(RT_TEMP, out_from_ilu ? VR_ILU : VR_MAC, out_mask & M_Y ? YYYY : out_mask & M_Z ? ZZZZ : WWWW));
			}
		}
	}
	/* the program leaves screen coordinates in its position; Direct3D 9 wants them before the viewport and the divide.
	   x and y: scaled and moved, then times w. z stays the program's: (z * w, w) goes to the pixel shader, and the z that
	   Direct3D 9 itself gets only decides where triangles are cut off (each instruction reads one constant register). */
	ins(&c, OP_MUL, 3, DST(RT_TEMP, VR_SCRATCH, M_XYZ), TEMP(VR_POSITION), CONSTANT(VC_SCALE));
	ins(&c, OP_ADD, 3, DST(RT_TEMP, VR_SCRATCH, M_XYZ), TEMP(VR_SCRATCH), CONSTANT(VC_OFFSET));
	ins(&c, OP_MUL, 3, DST(RT_TEMP, VR_SCRATCH, M_XYZ), TEMP(VR_SCRATCH), SRC(RT_TEMP, VR_POSITION, WWWW));
	ins(&c, OP_MOV, 2, DST(RT_TEMP, VR_SCRATCH, M_W), SRC(RT_TEMP, VR_POSITION, WWWW));
	ins(&c, OP_MOV, 2, DST(RT_TEMP, VR_ILU, M_X), SRC(RT_TEMP, VR_SCRATCH, ZZZZ));
	ins(&c, OP_MOV, 2, DST(RT_TEMP, VR_ILU, M_Y), SRC(RT_TEMP, VR_POSITION, WWWW));
	/* w as a part of the depth buffer's range is worked out here, per vertex, by the same one instruction in every program,
	   and the pixel shader takes it as it comes: two passes over the same triangles then get the same depth to the bit, which
	   the passes that test for equal depth need. (Left as a multiplication in the pixel shader, a driver that works on the two
	   shaders together may move it in front of the interpolation for one pass and not for the other: Mesa does.) */
	ins(&c, OP_MUL, 3, DST(RT_TEMP, VR_ILU, M_Z), SRC(RT_TEMP, VR_POSITION, WWWW), SRC(RT_CONST, VC_DEPTH, WWWW));
	ins(&c, OP_MOV, 2, DST(RT_TEMP, VR_ILU, M_W), SRC(RT_CONST, VC_NUMBERS, SWZ(1, 1, 1, 1)));
	ins(&c, OP_MOV, 2, DST(RT_OUTPUT, 8, M_ALL), TEMP(VR_ILU));
	ins(&c, OP_MUL, 3, DST(RT_TEMP, VR_SCRATCH2, M_X), SRC(RT_TEMP, VR_SCRATCH, ZZZZ), SRC(RT_CONST, VC_DEPTH, XXXX));
	ins(&c, OP_MAD, 4, DST(RT_TEMP, VR_SCRATCH2, M_X), SRC(RT_TEMP, VR_POSITION, WWWW), SRC(RT_CONST, VC_DEPTH, YYYY), SRC(RT_TEMP, VR_SCRATCH2, XXXX));
	ins(&c, OP_ADD, 3, DST(RT_TEMP, VR_SCRATCH, M_Z), SRC(RT_TEMP, VR_SCRATCH2, XXXX), SRC(RT_CONST, VC_DEPTH, ZZZZ));
	ins(&c, OP_MOV, 2, DST(RT_OUTPUT, 0, M_ALL), TEMP(VR_SCRATCH));
	ins(&c, OP_MOV, 2, DST(RT_OUTPUT, 1, M_ALL), TEMP(VR_OUTPUT + 0));          /* oD0 */
	ins(&c, OP_MOV, 2, DST(RT_OUTPUT, 2, M_ALL), TEMP(VR_OUTPUT + 1));          /* oD1 */
	for (i = 0; i < 4; i++) ins(&c, OP_MOV, 2, DST(RT_OUTPUT, 3 + i, M_ALL), TEMP(VR_OUTPUT + 6 + i));   /* oT0-oT3 */
	ins(&c, OP_MOV, 2, DST(RT_OUTPUT, 7, M_ALL), TEMP(VR_OUTPUT + 2));          /* oFog */
	put(&c, 0x0000ffff);

	g_shader_problems += check_shader(c.t, c.n, "the shader for vertex program", hash);
	hr = IDirect3DDevice9_CreateVertexShader(g_device, (const DWORD *)c.t, &v->shader);
	g_shaders_made++; if (FAILED(hr)) g_shaders_refused++;
	if (dump_wanted(hash)) { dump_words("the console's program", (const unsigned *)program[start], length * 4); dump_words("the shader made from it", c.t, c.n); }
	if (FAILED(hr))
	{
		v->shader = NULL;
		note("vertex program %08x (%u instructions) was not accepted by Direct3D 9 (%08lx); what uses it is not drawn", hash, length, (unsigned long)hr);
		dump_words("the console's program", (const unsigned *)program[start], length * 4);
		dump_words("the shader made from it", c.t, c.n);
		probe_refused(c.t, c.n, "the shader for vertex program", hash);
	}
	else if (g_trace) h2n_log("       vertex program %08x: %u instructions -> %u words of shader, reads attributes %04x, writes outputs %04x\n", hash, length, c.n, inputs, outputs);
	free(c.t);

	v->stride = 0;
	for (i = 0; i < 16; i++)
		if (inputs & (1u << i))
		{
			D3DVERTEXELEMENT9 *e = &elements[v->stride / 16];
			e->Stream = 0; e->Offset = (WORD)v->stride; e->Type = D3DDECLTYPE_FLOAT4; e->Method = D3DDECLMETHOD_DEFAULT; e->Usage = D3DDECLUSAGE_TEXCOORD; e->UsageIndex = (BYTE)i;
			v->slot[i] = v->stride; v->stride += 16;
		}
	{
		D3DVERTEXELEMENT9 end = D3DDECL_END();
		elements[v->stride / 16] = end;
	}
	if (!v->stride) v->stride = 16;
	IDirect3DDevice9_CreateVertexDeclaration(g_device, elements, &v->declaration);
	return v;
}

/* ================================================================ register combiners ==== */
/* registers of the translated shader: r0, r1 the combiners' own; r2-r5 the four textures; r6, r7 the two colours;
 * r8 fog (colour, and the factor in alpha); r9-r12 the inputs A-D of a stage; r13-r15 its results AB, CD and
 * their sum; r16 E*F; r17 colour 1 + r0; r18 scratch. constants: c0-c7 and c8-c15 the two constants of each stage,
 * c16, c17 the final combiner's, c18 the fog colour, c19 = (0, 1, 0.5, 2) (the only register numbers are taken
 * from: an instruction may read one constant register, so -1, -2 and -0.5 are its components negated),
 * c21-c24 one over the size of each texture that is addressed in texels, c25 = (127.5, 0, 0, 0). */
enum { PR_TEXTURE = 2, PR_COLOR = 6, PR_FOG = 8, PR_A = 9, PR_AB = 13, PR_CD = 14, PR_SUM = 15, PR_EF = 16, PR_V1R0 = 17, PR_SCRATCH = 18, PR_DOT = 19, PR_WORK = 20, PR_DEPTH = 24,
	PC_CONSTANT0 = 0, PC_CONSTANT1 = 8, PC_FINAL0 = 16, PC_FINAL1 = 17, PC_FOG = 18, PC_NUMBERS = 19, PC_NUMBERS2 = 20, PC_TEXEL = 21, PC_MORE = 25, PC_BUMP = 26, PC_LUMINANCE = 30,
	PC_DEPTH = 34, PC_DEPTH_BYTES = 36, PC_SHADOW = 39, PC_COUNT = 40 };
/* r19 keeps the dot products of the dot-product texture modes (stage n in component n); r20-r22 are more scratch.
 * c26-c29 are the bump-environment matrices of the stages, c30-c33 their luminance scale and offset.
 * The depth of a pixel (r24, r25 scratch): the console's GPU keeps, for each pixel, the w of the position (the game
 * always asks for that: "w buffering") or its z, moved by the polygon offset and held to the range the game set.
 * Direct3D 9 on a PC has no w buffer, so the shader writes that depth itself, from texture coordinate 5 = (z * w, w):
 * c34 = (1 / the depth buffer's largest value, lowest depth kept, highest depth kept, the offset's units), all as
 * parts of the depth buffer's range; c35.x = the offset's slope factor. */
/* the depth as the console keeps it, in three bytes: N = the depth as a whole number of 24 bits; "top" = N / 65536, then the
   middle and the low byte. value: the register and component holding the depth as a part of the range; out: a register that
   gets (middle, low, 0, top) / 255, which read as a colour is red, green, blue, alpha. k: the first of three constants,
   (16777215, 0.5, 1/65536, 1/256), (-65536, -256, 1/255, 0), (8388608, 0, 0.5, 0); t: the first of three scratch registers.
   The rounding: N is the nearest whole number. Half is added before the fraction is dropped only below 8388608: from there
   up a 32-bit number has no room for the half, and the product is a whole number already. */
static void emit_depth_bytes(code *c, unsigned value, unsigned out, unsigned k, unsigned t)
{
	unsigned n = DST(RT_TEMP, t, M_X), self = SRC(RT_TEMP, t, XXXX);
	ins(c, OP_MUL, 3, n, value, SRC(RT_CONST, k, XXXX));
	ins(c, OP_ADD, 3, DST(RT_TEMP, t + 1, M_X), self, SRC(RT_CONST, k + 2, XXXX) | NEGATE);
	ins(c, OP_CMP, 4, DST(RT_TEMP, t + 1, M_X), SRC(RT_TEMP, t + 1, XXXX), SRC(RT_CONST, k + 2, YYYY), SRC(RT_CONST, k + 2, ZZZZ));
	ins(c, OP_ADD, 3, n, self, SRC(RT_TEMP, t + 1, XXXX));
	ins(c, OP_FRC, 2, DST(RT_TEMP, t + 1, M_X), self);
	ins(c, OP_ADD, 3, n, self, SRC(RT_TEMP, t + 1, XXXX) | NEGATE);                                              /* N */
	ins(c, OP_MUL, 3, DST(RT_TEMP, t + 2, M_X), self, SRC(RT_CONST, k, ZZZZ));
	ins(c, OP_FRC, 2, DST(RT_TEMP, t + 1, M_X), SRC(RT_TEMP, t + 2, XXXX));
	ins(c, OP_ADD, 3, DST(RT_TEMP, t + 2, M_X), SRC(RT_TEMP, t + 2, XXXX), SRC(RT_TEMP, t + 1, XXXX) | NEGATE);  /* the top byte */
	ins(c, OP_MAD, 4, n, SRC(RT_TEMP, t + 2, XXXX), SRC(RT_CONST, k + 1, XXXX), self);                           /* what is left */
	ins(c, OP_MUL, 3, DST(RT_TEMP, t + 2, M_Y), self, SRC(RT_CONST, k, WWWW));
	ins(c, OP_FRC, 2, DST(RT_TEMP, t + 1, M_X), SRC(RT_TEMP, t + 2, YYYY));
	ins(c, OP_ADD, 3, DST(RT_TEMP, t + 2, M_Y), SRC(RT_TEMP, t + 2, YYYY), SRC(RT_TEMP, t + 1, XXXX) | NEGATE);  /* the middle byte */
	ins(c, OP_MAD, 4, DST(RT_TEMP, t + 2, M_Z), SRC(RT_TEMP, t + 2, YYYY), SRC(RT_CONST, k + 1, YYYY), self);    /* the low byte */
	ins(c, OP_MUL, 3, DST(RT_TEMP, out, M_X | M_Y | M_W), SRC(RT_TEMP, t + 2, SWZ(1, 2, 2, 0)), SRC(RT_CONST, k + 1, ZZZZ));
	ins(c, OP_MOV, 2, DST(RT_TEMP, out, M_Z), SRC(RT_CONST, k + 1, WWWW));
}
static void depth_bytes_constants(float (*k)[4])
{
	k[0][0] = 16777215.0f; k[0][1] = 0.5f; k[0][2] = 1.0f / 65536.0f; k[0][3] = 1.0f / 256.0f;
	k[1][0] = -65536.0f; k[1][1] = -256.0f; k[1][2] = 1.0f / 255.0f; k[1][3] = 0.0f;
	k[2][0] = 8388608.0f; k[2][1] = 0.0f; k[2][2] = 0.5f; k[2][3] = 0.0f;
}
typedef struct pixel_key
{
	unsigned alpha_inputs[8], rgb_inputs[8], alpha_outputs[8], rgb_outputs[8];
	unsigned final_abcd, final_efg, combiner_count, texture_modes, dot_mapping, input_texture, compare_mode;
	unsigned char kind[4], texel_addressed[4], alpha_kill[4];     /* kind: 0 nothing bound, 1 2D, 2 cube, 3 volume */
	unsigned char shadow[4];                                      /* (s10k) a stage that reads a depth buffer: its compare (1 never .. 8 always), + 0x10: 16-bit depth */
	unsigned combiners;
	unsigned depth;                                               /* 0: the depth is not written; 1: it is w; 2: it is z; plus 4: with the polygon offset */
	unsigned logic;                                               /* (sg2) 1: the console's logic operation SET - the colour written is white whatever the combiners made */
} pixel_key;
typedef struct pixel_shader { pixel_key key; unsigned hash, told; IDirect3DPixelShader9 *shader; struct pixel_shader *next; } pixel_shader;
static pixel_shader *g_pixel_shaders[256];

static unsigned combiner_register(unsigned reg, unsigned stage, int final)
{
	switch (reg)
	{
	case 1: return final ? CONSTANT(PC_FINAL0) : CONSTANT(PC_CONSTANT0 + stage);
	case 2: return final ? CONSTANT(PC_FINAL1) : CONSTANT(PC_CONSTANT1 + stage);
	case 3: return TEMP(PR_FOG);
	case 4: return TEMP(PR_COLOR);
	case 5: return TEMP(PR_COLOR + 1);
	case 8: case 9: case 10: case 11: return TEMP(PR_TEXTURE + reg - 8);
	case 12: return TEMP(0);
	case 13: return TEMP(1);
	case 14: return final ? TEMP(PR_V1R0) : SRC(RT_CONST, PC_NUMBERS, XXXX);
	case 15: return final ? TEMP(PR_EF) : SRC(RT_CONST, PC_NUMBERS, XXXX);
	}
	return SRC(RT_CONST, PC_NUMBERS, XXXX);
}
/* one combiner input into a register: "input" is the console's byte (register, alpha flag, mapping) */
static void combiner_input(code *c, unsigned destination, unsigned mask, unsigned input, unsigned stage, int final, int alpha_part)
{
	unsigned reg = input & 15, alpha = (input >> 4) & 1, mapping = (input >> 5) & 7;
	unsigned source = combiner_register(reg, stage, final), d = DST(RT_TEMP, destination, mask), self = TEMP(destination);
	if (reg == 0 || ((reg == 14 || reg == 15) && !final)) source = SRC(RT_CONST, PC_NUMBERS, XXXX);
	else if (alpha_part) source = with_swizzle(source, alpha ? WWWW : ZZZZ);
	else if (alpha) source = with_swizzle(source, WWWW);
	if (((source >> 28) & 7) == RT_CONST && (source & 0x7ff) != PC_NUMBERS && mapping < 6)
	{
		/* Direct3D 9 lets an instruction read only one constant register: bring the stage's constant into the register first */
		ins(c, OP_MOV, 2, d, source);
		source = self;
	}
	switch (mapping)
	{
	case 0: ins(c, OP_MAX, 3, d, source, SRC(RT_CONST, PC_NUMBERS, XXXX)); break;
	case 1: ins(c, OP_MOV, 2, d | SATURATE, source); ins(c, OP_ADD, 3, d, SRC(RT_CONST, PC_NUMBERS, YYYY), self | NEGATE); break;
	case 2: ins(c, OP_MAX, 3, d, source, SRC(RT_CONST, PC_NUMBERS, XXXX)); ins(c, OP_MAD, 4, d, self, SRC(RT_CONST, PC_NUMBERS, WWWW), (SRC(RT_CONST, PC_NUMBERS, YYYY) | NEGATE)); break;
	case 3: ins(c, OP_MAX, 3, d, source, SRC(RT_CONST, PC_NUMBERS, XXXX)); ins(c, OP_MAD, 4, d, self, (SRC(RT_CONST, PC_NUMBERS, WWWW) | NEGATE), SRC(RT_CONST, PC_NUMBERS, YYYY)); break;
	case 4: ins(c, OP_MAX, 3, d, source, SRC(RT_CONST, PC_NUMBERS, XXXX)); ins(c, OP_ADD, 3, d, self, (SRC(RT_CONST, PC_NUMBERS, ZZZZ) | NEGATE)); break;
	case 5: ins(c, OP_MAX, 3, d, source, SRC(RT_CONST, PC_NUMBERS, XXXX)); ins(c, OP_ADD, 3, d, SRC(RT_CONST, PC_NUMBERS, ZZZZ), self | NEGATE); break;
	case 6: ins(c, OP_MOV, 2, d, source); break;
	case 7: ins(c, OP_MOV, 2, d, source | NEGATE); break;
	}
}
/* the output mapping of a stage, then the limits every combiner result is held to */
static void combiner_output_map(code *c, unsigned reg, unsigned mask, unsigned mapping)
{
	unsigned d = DST(RT_TEMP, reg, mask), self = TEMP(reg);
	switch (mapping)
	{
	case 0x08: ins(c, OP_ADD, 3, d, self, (SRC(RT_CONST, PC_NUMBERS, ZZZZ) | NEGATE)); break;
	case 0x10: ins(c, OP_ADD, 3, d, self, self); break;
	case 0x18: ins(c, OP_MAD, 4, d, self, SRC(RT_CONST, PC_NUMBERS, WWWW), (SRC(RT_CONST, PC_NUMBERS, YYYY) | NEGATE)); break;
	case 0x20: ins(c, OP_ADD, 3, d, self, self); ins(c, OP_ADD, 3, d, self, self); break;
	case 0x30: ins(c, OP_MUL, 3, d, self, SRC(RT_CONST, PC_NUMBERS, ZZZZ)); break;
	}
	ins(c, OP_MAX, 3, d, self, (SRC(RT_CONST, PC_NUMBERS, YYYY) | NEGATE));
	ins(c, OP_MIN, 3, d, self, SRC(RT_CONST, PC_NUMBERS, YYYY));
}
static int combiner_destination(unsigned reg)
{
	switch (reg) { case 4: return PR_COLOR; case 5: return PR_COLOR + 1; case 8: case 9: case 10: case 11: return PR_TEXTURE + (int)reg - 8; case 12: return 0; case 13: return 1; }
	return -1;
}
static void combiner_half(code *c, const pixel_key *k, unsigned stage, int alpha_part)
{
	unsigned inputs = alpha_part ? k->alpha_inputs[stage] : k->rgb_inputs[stage], outputs = alpha_part ? k->alpha_outputs[stage] : k->rgb_outputs[stage];
	unsigned mask = alpha_part ? M_W : M_XYZ, flags = outputs >> 12, mapping = flags & 0x38, i;
	unsigned factor_stage0 = (k->combiner_count >> 12) & 1 ? stage : 0, factor_stage1 = (k->combiner_count >> 16) & 1 ? stage : 0;
	for (i = 0; i < 4; i++)
	{
		unsigned input = (inputs >> (24 - 8 * i)) & 255, reg = input & 15;
		combiner_input(c, PR_A + i, mask, input, reg == 1 ? factor_stage0 : factor_stage1, 0, alpha_part);
	}
	if (!alpha_part && (flags & 2)) ins(c, OP_DP3, 3, DST(RT_TEMP, PR_AB, mask), TEMP(PR_A), TEMP(PR_A + 1));
	else ins(c, OP_MUL, 3, DST(RT_TEMP, PR_AB, mask), TEMP(PR_A), TEMP(PR_A + 1));
	if (!alpha_part && (flags & 1)) ins(c, OP_DP3, 3, DST(RT_TEMP, PR_CD, mask), TEMP(PR_A + 2), TEMP(PR_A + 3));
	else ins(c, OP_MUL, 3, DST(RT_TEMP, PR_CD, mask), TEMP(PR_A + 2), TEMP(PR_A + 3));
	if (flags & 4)
	{
		/* choose AB or CD by r0's alpha: its top bit, or its lowest */
		if ((k->combiner_count >> 8) & 1) ins(c, OP_ADD, 3, DST(RT_TEMP, PR_SCRATCH, M_ALL), SRC(RT_TEMP, 0, WWWW), (SRC(RT_CONST, PC_NUMBERS, ZZZZ) | NEGATE));
		else
		{
			ins(c, OP_MUL, 3, DST(RT_TEMP, PR_SCRATCH, M_ALL), SRC(RT_TEMP, 0, WWWW), SRC(RT_CONST, PC_MORE, XXXX));
			ins(c, OP_FRC, 2, DST(RT_TEMP, PR_SCRATCH, M_ALL), TEMP(PR_SCRATCH));
			ins(c, OP_ADD, 3, DST(RT_TEMP, PR_SCRATCH, M_ALL), TEMP(PR_SCRATCH), (SRC(RT_CONST, PC_NUMBERS, ZZZZ) | NEGATE));
		}
		ins(c, OP_CMP, 4, DST(RT_TEMP, PR_SUM, mask), TEMP(PR_SCRATCH), TEMP(PR_CD), TEMP(PR_AB));
	}
	else ins(c, OP_ADD, 3, DST(RT_TEMP, PR_SUM, mask), TEMP(PR_AB), TEMP(PR_CD));
	combiner_output_map(c, PR_AB, mask, mapping);
	combiner_output_map(c, PR_CD, mask, mapping);
	combiner_output_map(c, PR_SUM, mask, mapping);
}
static void combiner_write(code *c, const pixel_key *k, unsigned stage, int alpha_part)
{
	unsigned outputs = alpha_part ? k->alpha_outputs[stage] : k->rgb_outputs[stage], mask = alpha_part ? M_W : M_XYZ, flags = outputs >> 12;
	int ab = combiner_destination((outputs >> 4) & 15), cd = combiner_destination(outputs & 15), sum = combiner_destination((outputs >> 8) & 15);
	if (ab >= 0) ins(c, OP_MOV, 2, DST(RT_TEMP, ab, mask), TEMP(PR_AB));
	if (cd >= 0) ins(c, OP_MOV, 2, DST(RT_TEMP, cd, mask), TEMP(PR_CD));
	if (sum >= 0) ins(c, OP_MOV, 2, DST(RT_TEMP, sum, mask), TEMP(PR_SUM));
	if (!alpha_part)
	{
		if (ab >= 0 && (flags & 0x80)) ins(c, OP_MOV, 2, DST(RT_TEMP, ab, M_W), SRC(RT_TEMP, PR_AB, ZZZZ));
		if (cd >= 0 && (flags & 0x40)) ins(c, OP_MOV, 2, DST(RT_TEMP, cd, M_W), SRC(RT_TEMP, PR_CD, ZZZZ));
	}
}

/* the dot product of a stage's texture coordinate with an earlier texture, read as a vector: into r19's component */
static void stage_dot(code *c, const pixel_key *k, unsigned stage)
{
	unsigned input = stage == 2 ? (k->input_texture >> 16) & 15 : stage == 3 ? (k->input_texture >> 20) & 15 : 0;
	unsigned mapping = (k->dot_mapping >> (4 * (stage - 1))) & 7;
	if (input >= stage) input = 0;
	if (mapping == 0) ins(c, OP_MOV, 2, DST(RT_TEMP, PR_SCRATCH, M_XYZ), TEMP(PR_TEXTURE + input));
	else if (mapping == 4)
	{
		/* PS_DOTMAPPING_HILO_1: the texel as two 16-bit numbers and a one: (H, L, 1) with 0x0000 = 0 and 0xffff = 1 (the SDK's
		   words). H is alpha and red, L is green and blue (as xemu reads the console). This is how the game reads a depth buffer
		   finely: its fog looks a depth up in a table by H, where 8 bits would give bands. */
		ins(c, OP_MUL, 3, DST(RT_TEMP, PR_SCRATCH, M_X | M_Y), SRC(RT_TEMP, PR_TEXTURE + input, SWZ(3, 1, 1, 1)), SRC(RT_CONST, PC_MORE, ZZZZ));
		ins(c, OP_MAD, 4, DST(RT_TEMP, PR_SCRATCH, M_X | M_Y), SRC(RT_TEMP, PR_TEXTURE + input, SWZ(0, 2, 2, 2)), SRC(RT_CONST, PC_MORE, WWWW), TEMP(PR_SCRATCH));
		ins(c, OP_MOV, 2, DST(RT_TEMP, PR_SCRATCH, M_Z), SRC(RT_CONST, PC_NUMBERS, YYYY));
	}
	else
	{
		if (mapping != 1) { static unsigned seen[8] = { ~0u, ~0u, ~0u, ~0u, ~0u, ~0u, ~0u, ~0u }; if (note_once(seen, 8, mapping)) note("dot-product mapping %u of the console's pixel pipeline is drawn as mapping 1 (-1 to 1); what uses it may look wrong", mapping); }
		ins(c, OP_MAD, 4, DST(RT_TEMP, PR_SCRATCH, M_XYZ), TEMP(PR_TEXTURE + input), SRC(RT_CONST, PC_NUMBERS, WWWW), (SRC(RT_CONST, PC_NUMBERS, YYYY) | NEGATE));
	}
	ins(c, OP_DP3, 3, DST(RT_TEMP, PR_DOT, 1u << stage), SRC(RT_INPUT, 2 + stage, XYZW), TEMP(PR_SCRATCH));
}

/* (s10k) A LIGHT'S SHADOW MAP. A stage whose texture is a depth buffer (a light's shadow map, drawn in a depth-only pass)
 * does not give colours on the console: the GPU compares the depth stored there with the coordinate's r (divided by q,
 * in the depth buffer's units, 0 to 0xffffff) by the render state D3DRS_SHADOWFUNC, and the texture reads 1 where the
 * compare holds and 0 where it fails (the game sets GREATER for the lights' pass: lit where what the light sees is
 * farther than the pixel). With a projective 2D lookup the stored depth is compared with 0 (as xemu does). Here the
 * depth buffer is the INTZ texture of that memory: read, scaled to the buffer's units, and compared the same way.
 * Before, such a stage read nothing (format 0x2e "not unpacked"): 0, so the light's pass added nothing wherever
 * the game drew it with its shadow map - near the light. */
static void shadow_compare(code *c, const pixel_key *k, unsigned i, unsigned mode, unsigned coordinate, unsigned d)
{
	unsigned func = (k->shadow[i] & 15) - 1u, max = (k->shadow[i] & 0x10) ? YYYY : XXXX;
	ins(c, OP_MOV, 2, DST(RT_TEMP, PR_SCRATCH, M_ALL), coordinate);
	ins(c, OP_RCP, 2, DST(RT_TEMP, PR_SCRATCH, M_W), with_swizzle(coordinate, WWWW));
	ins(c, OP_MUL, 3, DST(RT_TEMP, PR_SCRATCH, M_XYZ), coordinate, SRC(RT_TEMP, PR_SCRATCH, WWWW));
	if (mode == 2) ins(c, OP_MAX, 3, DST(RT_TEMP, PR_WORK, M_X), SRC(RT_TEMP, PR_SCRATCH, ZZZZ), SRC(RT_CONST, PC_NUMBERS, XXXX));   /* r, at least 0 */
	else ins(c, OP_MOV, 2, DST(RT_TEMP, PR_WORK, M_X), SRC(RT_CONST, PC_NUMBERS, XXXX));                                      /* 2D: compared with 0 */
	ins(c, OP_MIN, 3, DST(RT_TEMP, PR_WORK, M_X), SRC(RT_TEMP, PR_WORK, XXXX), SRC(RT_CONST, PC_SHADOW, max));                /* at most the buffer's largest */
	if (k->texel_addressed[i]) ins(c, OP_MUL, 3, DST(RT_TEMP, PR_SCRATCH, M_X | M_Y), TEMP(PR_SCRATCH), CONSTANT(PC_TEXEL + i));
	ins(c, OP_TEX, 3, DST(RT_TEMP, PR_WORK + 1, M_ALL), TEMP(PR_SCRATCH), SRC(RT_SAMPLER, i, XYZW));                       /* the stored depth, 0 to 1 */
	ins(c, OP_MAD, 4, DST(RT_TEMP, PR_WORK, M_Y), SRC(RT_TEMP, PR_WORK + 1, XXXX), SRC(RT_CONST, PC_SHADOW, max), SRC(RT_TEMP, PR_WORK, XXXX) | NEGATE);   /* stored - r */
	ins(c, OP_MOV, 2, DST(RT_TEMP, PR_WORK, M_Z), SRC(RT_TEMP, PR_WORK, YYYY) | ABS_NEG);   /* -|stored - r| */
	switch (func)
	{
	case 1: ins(c, OP_CMP, 4, d, SRC(RT_TEMP, PR_WORK, YYYY), SRC(RT_CONST, PC_NUMBERS, XXXX), SRC(RT_CONST, PC_NUMBERS, YYYY)); break;            /* less */
	case 2: ins(c, OP_CMP, 4, d, SRC(RT_TEMP, PR_WORK, ZZZZ), SRC(RT_CONST, PC_NUMBERS, YYYY), SRC(RT_CONST, PC_NUMBERS, XXXX)); break;            /* equal */
	case 3: ins(c, OP_CMP, 4, d, SRC(RT_TEMP, PR_WORK, YYYY) | NEGATE, SRC(RT_CONST, PC_NUMBERS, YYYY), SRC(RT_CONST, PC_NUMBERS, XXXX)); break;   /* less or equal */
	case 4: ins(c, OP_CMP, 4, d, SRC(RT_TEMP, PR_WORK, YYYY) | NEGATE, SRC(RT_CONST, PC_NUMBERS, XXXX), SRC(RT_CONST, PC_NUMBERS, YYYY)); break;   /* greater */
	case 5: ins(c, OP_CMP, 4, d, SRC(RT_TEMP, PR_WORK, ZZZZ), SRC(RT_CONST, PC_NUMBERS, XXXX), SRC(RT_CONST, PC_NUMBERS, YYYY)); break;            /* not equal */
	case 6: ins(c, OP_CMP, 4, d, SRC(RT_TEMP, PR_WORK, YYYY), SRC(RT_CONST, PC_NUMBERS, YYYY), SRC(RT_CONST, PC_NUMBERS, XXXX)); break;            /* greater or equal */
	case 7: ins(c, OP_MOV, 2, d, SRC(RT_CONST, PC_NUMBERS, YYYY)); break;                                                                          /* always */
	default: ins(c, OP_MOV, 2, d, SRC(RT_CONST, PC_NUMBERS, XXXX)); break;                                                                         /* never */
	}
}
static pixel_shader *pixel_shader_for(const pixel_key *k)
{
	unsigned hash = hash_words(k, sizeof(*k), 2166136261u), i, count = k->combiner_count & 15;
	pixel_shader *p;
	code c;
	HRESULT hr;
	static unsigned seen_modes[24] = { ~0u, ~0u, ~0u, ~0u, ~0u, ~0u, ~0u, ~0u, ~0u, ~0u, ~0u, ~0u, ~0u, ~0u, ~0u, ~0u, ~0u, ~0u, ~0u, ~0u, ~0u, ~0u, ~0u, ~0u };

	for (p = g_pixel_shaders[hash & 255]; p; p = p->next) if (p->hash == hash && !memcmp(&p->key, k, sizeof(*k))) return p;
	p = calloc(1, sizeof(*p));
	p->key = *k; p->hash = hash; p->next = g_pixel_shaders[hash & 255]; g_pixel_shaders[hash & 255] = p;

	memset(&c, 0, sizeof(c));
	put(&c, 0xffff0300);
	declare(&c, USAGE_COLOR, 0, RT_INPUT, 0);
	declare(&c, USAGE_COLOR, 1, RT_INPUT, 1);
	for (i = 0; i < 5; i++) declare(&c, USAGE_TEXCOORD, i, RT_INPUT, 2 + i);
	if (k->depth) declare(&c, USAGE_TEXCOORD, 5, RT_INPUT, 7);
	for (i = 0; i < 4; i++) if (k->kind[i]) ins(&c, OP_DCL, 2, 0x80000000u | ((k->kind[i] + 1u) << 27), DST(RT_SAMPLER, i, M_ALL));

	ins(&c, OP_MOV, 2, DST(RT_TEMP, 0, M_ALL), SRC(RT_CONST, PC_NUMBERS, XXXX));
	ins(&c, OP_MOV, 2, DST(RT_TEMP, 1, M_ALL), SRC(RT_CONST, PC_NUMBERS, XXXX));
	ins(&c, OP_MOV, 2, DST(RT_TEMP, PR_COLOR, M_ALL) | SATURATE, SRC(RT_INPUT, 0, XYZW));
	ins(&c, OP_MOV, 2, DST(RT_TEMP, PR_COLOR + 1, M_ALL) | SATURATE, SRC(RT_INPUT, 1, XYZW));
	ins(&c, OP_MOV, 2, DST(RT_TEMP, PR_FOG, M_XYZ), CONSTANT(PC_FOG));
	ins(&c, OP_MOV, 2, DST(RT_TEMP, PR_FOG, M_W) | SATURATE, SRC(RT_INPUT, 6, XXXX));
	ins(&c, OP_MOV, 2, DST(RT_TEMP, PR_DOT, M_ALL), SRC(RT_CONST, PC_NUMBERS, XXXX));
	for (i = 0; i < 4; i++)
	{
		unsigned mode = k->combiners ? (k->texture_modes >> (5 * i)) & 31 : (k->kind[i] ? 1 : 0), t = PR_TEXTURE + i, coordinate = SRC(RT_INPUT, 2 + i, XYZW);
		unsigned d = DST(RT_TEMP, t, M_ALL), scratch = DST(RT_TEMP, PR_SCRATCH, M_ALL);
		unsigned input = i == 2 ? (k->input_texture >> 16) & 15 : i == 3 ? (k->input_texture >> 20) & 15 : 0;
		if (input >= i) input = 0;
		if (mode == 0x11 && i == 0) mode = 0;
		if (mode && mode != 4 && mode != 5 && mode != 0x11 && !k->kind[i]) { if (mode >= 9 && mode <= 0x12 && mode != 0xf && mode != 0x10 && i) stage_dot(&c, k, i); mode = 0; }
		if (mode >= 6 && mode != 0x11 && i == 0) mode = 0;
		switch (mode)
		{
		case 0: ins(&c, OP_MOV, 2, d, SRC(RT_CONST, PC_NUMBERS, XXXX)); break;
		case 1:                                                                               /* a 2D texture, divided by q */
			if (k->shadow[i]) { shadow_compare(&c, k, i, mode, coordinate, d); break; }     /* (s10k) a shadow map */
			if (k->kind[i] == 2) { ins(&c, OP_TEX, 3, d, coordinate, SRC(RT_SAMPLER, i, XYZW)); break; }
			ins(&c, OP_MOV, 2, scratch, coordinate);
			ins(&c, OP_RCP, 2, DST(RT_TEMP, PR_SCRATCH, M_W), with_swizzle(coordinate, WWWW));
			ins(&c, OP_MUL, 3, DST(RT_TEMP, PR_SCRATCH, M_XYZ), coordinate, SRC(RT_TEMP, PR_SCRATCH, WWWW));
			if (k->texel_addressed[i]) ins(&c, OP_MUL, 3, DST(RT_TEMP, PR_SCRATCH, M_XYZ), TEMP(PR_SCRATCH), CONSTANT(PC_TEXEL + i));
			ins(&c, OP_TEX, 3, d, TEMP(PR_SCRATCH), SRC(RT_SAMPLER, i, XYZW));
			break;
		case 2:                                                                               /* a volume texture, divided by q */
			if (k->shadow[i]) { shadow_compare(&c, k, i, mode, coordinate, d); break; }     /* (s10k) a shadow map, compared with r */
			ins(&c, OP_MOV, 2, scratch, coordinate);
			ins(&c, OP_RCP, 2, DST(RT_TEMP, PR_SCRATCH, M_W), with_swizzle(coordinate, WWWW));
			ins(&c, OP_MUL, 3, DST(RT_TEMP, PR_SCRATCH, M_XYZ), coordinate, SRC(RT_TEMP, PR_SCRATCH, WWWW));
			ins(&c, OP_TEX, 3, d, TEMP(PR_SCRATCH), SRC(RT_SAMPLER, i, XYZW));
			break;
		case 3: ins(&c, OP_TEX, 3, d, coordinate, SRC(RT_SAMPLER, i, XYZW)); break;           /* a cube map */
		case 4: ins(&c, OP_MOV, 2, d | SATURATE, coordinate); break;                          /* the coordinate itself as a colour */
		case 5:                                                                               /* clip: the pixel goes when a coordinate is on the wrong side */
		{
			unsigned compare = (k->compare_mode >> (4 * i)) & 15, n;
			ins(&c, OP_MOV, 2, scratch, coordinate);
			for (n = 0; n < 4; n++)
				if (!(compare & (1u << n)))    /* "less than": kept when negative */
					ins(&c, OP_ADD, 3, DST(RT_TEMP, PR_SCRATCH, 1u << n), with_swizzle(coordinate, XYZW) | NEGATE, SRC(RT_CONST, PC_MORE, YYYY) | NEGATE);
			put(&c, OP_TEXKILL | (1u << 24)); put(&c, DST(RT_TEMP, PR_SCRATCH, M_ALL));
			ins(&c, OP_MOV, 2, d, SRC(RT_CONST, PC_NUMBERS, XXXX));
			break;
		}
		case 6: case 7:                                                                       /* the coordinate, moved by an earlier texture through a 2x2 matrix */
			ins(&c, OP_MAD, 4, DST(RT_TEMP, PR_SCRATCH, M_XYZ), TEMP(PR_TEXTURE + input), SRC(RT_CONST, PC_NUMBERS, WWWW), (SRC(RT_CONST, PC_NUMBERS, YYYY) | NEGATE));
			ins(&c, OP_MOV, 2, DST(RT_TEMP, PR_WORK, M_ALL), coordinate);
			ins(&c, OP_MAD, 4, DST(RT_TEMP, PR_WORK, M_X | M_Y), SRC(RT_TEMP, PR_SCRATCH, ZZZZ), SRC(RT_CONST, PC_BUMP + i, SWZ(0, 1, 0, 1)), TEMP(PR_WORK));
			ins(&c, OP_MAD, 4, DST(RT_TEMP, PR_WORK, M_X | M_Y), SRC(RT_TEMP, PR_SCRATCH, YYYY), SRC(RT_CONST, PC_BUMP + i, SWZ(2, 3, 2, 3)), TEMP(PR_WORK));
			if (k->texel_addressed[i]) ins(&c, OP_MUL, 3, DST(RT_TEMP, PR_WORK, M_XYZ), TEMP(PR_WORK), CONSTANT(PC_TEXEL + i));
			ins(&c, OP_TEX, 3, d, TEMP(PR_WORK), SRC(RT_SAMPLER, i, XYZW));
			if (mode == 7)
			{
				ins(&c, OP_MAD, 4, DST(RT_TEMP, PR_SCRATCH, M_X), SRC(RT_TEMP, PR_TEXTURE + input, XXXX), SRC(RT_CONST, PC_LUMINANCE + i, XXXX), SRC(RT_CONST, PC_LUMINANCE + i, YYYY));
				ins(&c, OP_MUL, 3, DST(RT_TEMP, t, M_XYZ), TEMP(t), SRC(RT_TEMP, PR_SCRATCH, XXXX));
			}
			break;
		case 9:                                                                               /* looked up at (previous dot product, this one) */
			stage_dot(&c, k, i);
			ins(&c, OP_MOV, 2, DST(RT_TEMP, PR_WORK, M_ALL), SRC(RT_TEMP, PR_DOT, i == 3 ? SWZ(2, 3, 3, 3) : i == 2 ? SWZ(1, 2, 2, 2) : SWZ(0, 1, 1, 1)));
			if (k->texel_addressed[i]) ins(&c, OP_MUL, 3, DST(RT_TEMP, PR_WORK, M_XYZ), TEMP(PR_WORK), CONSTANT(PC_TEXEL + i));   /* a texel-addressed texture (the camouflage's copy of the screen): the two dot products are texels */
			ins(&c, OP_TEX, 3, d, TEMP(PR_WORK), SRC(RT_SAMPLER, i, XYZW));
			break;
		case 0xb:                                                                             /* a cube map looked up along the three dot products */
			stage_dot(&c, k, i);
			if (i == 2 && ((k->texture_modes >> 15) & 31) >= 9) stage_dot(&c, k, 3);
			ins(&c, OP_MOV, 2, DST(RT_TEMP, PR_WORK, M_ALL), SRC(RT_TEMP, PR_DOT, SWZ(1, 2, 3, 3)));
			ins(&c, OP_TEX, 3, d, TEMP(PR_WORK), SRC(RT_SAMPLER, i, XYZW));
			break;
		case 0xc: case 0x12:                                                                  /* a cube map looked up along the reflection of the eye about them */
			stage_dot(&c, k, i);
			ins(&c, OP_MOV, 2, DST(RT_TEMP, PR_WORK, M_ALL), SRC(RT_TEMP, PR_DOT, SWZ(1, 2, 3, 3)));
			if (mode == 0xc)
			{
				ins(&c, OP_MOV, 2, DST(RT_TEMP, PR_WORK + 1, M_X), SRC(RT_INPUT, 3, WWWW));
				ins(&c, OP_MOV, 2, DST(RT_TEMP, PR_WORK + 1, M_Y), SRC(RT_INPUT, 4, WWWW));
				ins(&c, OP_MOV, 2, DST(RT_TEMP, PR_WORK + 1, M_Z | M_W), SRC(RT_INPUT, 5, WWWW));
			}
			else ins(&c, OP_MOV, 2, DST(RT_TEMP, PR_WORK + 1, M_ALL), SRC(RT_CONST, PC_NUMBERS, SWZ(0, 0, 1, 1)));
			ins(&c, OP_DP3, 3, DST(RT_TEMP, PR_WORK + 2, M_X), TEMP(PR_WORK), TEMP(PR_WORK + 1));
			ins(&c, OP_DP3, 3, DST(RT_TEMP, PR_WORK + 2, M_Y), TEMP(PR_WORK), TEMP(PR_WORK));
			ins(&c, OP_RCP, 2, DST(RT_TEMP, PR_WORK + 2, M_Y), SRC(RT_TEMP, PR_WORK + 2, YYYY));
			ins(&c, OP_MUL, 3, DST(RT_TEMP, PR_WORK + 2, M_X), SRC(RT_TEMP, PR_WORK + 2, XXXX), SRC(RT_TEMP, PR_WORK + 2, YYYY));
			ins(&c, OP_ADD, 3, DST(RT_TEMP, PR_WORK + 2, M_X), SRC(RT_TEMP, PR_WORK + 2, XXXX), SRC(RT_TEMP, PR_WORK + 2, XXXX));
			ins(&c, OP_MAD, 4, DST(RT_TEMP, PR_SCRATCH, M_ALL), TEMP(PR_WORK), SRC(RT_TEMP, PR_WORK + 2, XXXX), TEMP(PR_WORK + 1) | NEGATE);
			ins(&c, OP_TEX, 3, d, TEMP(PR_SCRATCH), SRC(RT_SAMPLER, i, XYZW));
			break;
		case 0xd: case 0xe:                                                                   /* a volume or cube map looked up at the three dot products */
			stage_dot(&c, k, i);
			ins(&c, OP_MOV, 2, DST(RT_TEMP, PR_WORK, M_ALL), SRC(RT_TEMP, PR_DOT, SWZ(1, 2, 3, 3)));
			ins(&c, OP_TEX, 3, d, TEMP(PR_WORK), SRC(RT_SAMPLER, i, XYZW));
			break;
		case 0xf: case 0x10:                                                                  /* looked up at two channels of an earlier texture */
			ins(&c, OP_MOV, 2, DST(RT_TEMP, PR_WORK, M_ALL), SRC(RT_TEMP, PR_TEXTURE + input, mode == 0xf ? SWZ(3, 0, 0, 0) : SWZ(1, 2, 2, 2)));
			ins(&c, OP_TEX, 3, d, TEMP(PR_WORK), SRC(RT_SAMPLER, i, XYZW));
			break;
		case 0x11:                                                                            /* only the dot product, for a later stage */
			stage_dot(&c, k, i);
			ins(&c, OP_MOV, 2, d, SRC(RT_CONST, PC_NUMBERS, XXXX));
			break;
		default:
			if (i && mode >= 9) stage_dot(&c, k, i);
			if (note_once(seen_modes, 24, mode)) note("texture mode %u of the console's pixel pipeline is not drawn yet; that texture reads as black", mode);
			ins(&c, OP_MOV, 2, d, SRC(RT_CONST, PC_NUMBERS, XXXX));
			break;
		}
		if (k->alpha_kill[i] && mode)
		{
			ins(&c, OP_ADD, 3, scratch, SRC(RT_TEMP, t, WWWW), SRC(RT_CONST, PC_MORE, YYYY) | NEGATE);
			put(&c, OP_TEXKILL | (1u << 24)); put(&c, DST(RT_TEMP, PR_SCRATCH, M_ALL));
		}
	}
	if (!k->combiners)
	{
		/* no pixel shader set: the first texture times the colour, or the colour alone */
		if (k->kind[0]) ins(&c, OP_MUL, 3, DST(RT_COLOROUT, 0, M_ALL), TEMP(PR_TEXTURE), TEMP(PR_COLOR));
		else ins(&c, OP_MOV, 2, DST(RT_COLOROUT, 0, M_ALL), TEMP(PR_COLOR));
	}
	else
	{
		ins(&c, OP_MOV, 2, DST(RT_TEMP, 0, M_W), SRC(RT_TEMP, PR_TEXTURE, WWWW));
		if (count > 8) count = 8;
		for (i = 0; i < count; i++)
		{
			/* both halves read the registers as they were before the stage; rgb goes through r13-r15, alpha through their alpha */
			combiner_half(&c, k, i, 0);
			combiner_half(&c, k, i, 1);
			combiner_write(&c, k, i, 0);
			combiner_write(&c, k, i, 1);
		}
		if (!k->final_abcd && !k->final_efg) ins(&c, OP_MOV, 2, DST(RT_COLOROUT, 0, M_ALL) | SATURATE, TEMP(0));
		else
		{
			unsigned settings = k->final_efg & 255;
			combiner_input(&c, PR_A, M_XYZ, (k->final_efg >> 24) & 255, 0, 1, 0);
			combiner_input(&c, PR_A + 1, M_XYZ, (k->final_efg >> 16) & 255, 0, 1, 0);
			ins(&c, OP_MUL, 3, DST(RT_TEMP, PR_EF, M_XYZ), TEMP(PR_A), TEMP(PR_A + 1));
			ins(&c, OP_MOV, 2, DST(RT_TEMP, PR_EF, M_W), SRC(RT_CONST, PC_NUMBERS, XXXX));
			if (settings & 0x40) ins(&c, OP_ADD, 3, DST(RT_TEMP, PR_A, M_XYZ), SRC(RT_CONST, PC_NUMBERS, YYYY), TEMP(PR_COLOR + 1) | NEGATE);
			else ins(&c, OP_MOV, 2, DST(RT_TEMP, PR_A, M_XYZ), TEMP(PR_COLOR + 1));
			if (settings & 0x20) ins(&c, OP_ADD, 3, DST(RT_TEMP, PR_A + 1, M_XYZ), SRC(RT_CONST, PC_NUMBERS, YYYY), TEMP(0) | NEGATE);
			else ins(&c, OP_MOV, 2, DST(RT_TEMP, PR_A + 1, M_XYZ), TEMP(0));
			ins(&c, OP_ADD, 3, DST(RT_TEMP, PR_V1R0, M_XYZ) | (settings & 0x80 ? SATURATE : 0), TEMP(PR_A), TEMP(PR_A + 1));
			ins(&c, OP_MOV, 2, DST(RT_TEMP, PR_V1R0, M_W), SRC(RT_CONST, PC_NUMBERS, XXXX));
			for (i = 0; i < 4; i++) combiner_input(&c, PR_A + i, M_XYZ, (k->final_abcd >> (24 - 8 * i)) & 255, 0, 1, 0);
			combiner_input(&c, PR_SUM, M_W, (k->final_efg >> 8) & 255, 0, 1, 1);
			ins(&c, OP_LRP, 4, DST(RT_TEMP, PR_SUM, M_XYZ), TEMP(PR_A), TEMP(PR_A + 1), TEMP(PR_A + 2));
			ins(&c, OP_ADD, 3, DST(RT_TEMP, PR_SUM, M_XYZ), TEMP(PR_SUM), TEMP(PR_A + 3));
			ins(&c, OP_MOV, 2, DST(RT_COLOROUT, 0, M_ALL) | SATURATE, TEMP(PR_SUM));
		}
	}
	/* (sg2) the console's logic operation SET (D3DRS_LOGICOP 0x150f): every bit of the colour written is set, so the pixel
	   is white whatever was computed. Direct3D 9 has no logic operations; the shader's last word on the colour is 1.
	   (The Spooky Ghost Menu's Wireframe draws its lines this way, as the console build does.) */
	if (k->logic) ins(&c, OP_MOV, 2, DST(RT_COLOROUT, 0, M_ALL), SRC(RT_CONST, PC_NUMBERS, YYYY));
	if (k->depth & 3)
	{
		unsigned d = DST(RT_TEMP, PR_DEPTH, M_X), self = SRC(RT_TEMP, PR_DEPTH, XXXX);
		if ((k->depth & 3) == 1) ins(&c, OP_MOV, 2, d, SRC(RT_INPUT, 7, ZZZZ));                                   /* w, as a part of the depth buffer's range (the vertex shader's ending) */
		else
		{
			ins(&c, OP_RCP, 2, DST(RT_TEMP, PR_DEPTH, M_Y), SRC(RT_INPUT, 7, YYYY));                                /* (z * w) / w: z as it runs across the screen */
			ins(&c, OP_MUL, 3, d, SRC(RT_INPUT, 7, XXXX), SRC(RT_TEMP, PR_DEPTH, YYYY));
			ins(&c, OP_MUL, 3, d, self, SRC(RT_CONST, PC_DEPTH, XXXX));
		}
		if (k->depth & 4)
		{
			ins(&c, OP_DSX, 2, DST(RT_TEMP, PR_DEPTH + 1, M_X), self);
			ins(&c, OP_DSY, 2, DST(RT_TEMP, PR_DEPTH + 1, M_Y), self);
			ins(&c, OP_ABS, 2, DST(RT_TEMP, PR_DEPTH + 1, M_X | M_Y), SRC(RT_TEMP, PR_DEPTH + 1, SWZ(0, 1, 1, 1)));
			ins(&c, OP_MAX, 3, DST(RT_TEMP, PR_DEPTH + 1, M_X), SRC(RT_TEMP, PR_DEPTH + 1, XXXX), SRC(RT_TEMP, PR_DEPTH + 1, YYYY));
			ins(&c, OP_MAD, 4, d, SRC(RT_TEMP, PR_DEPTH + 1, XXXX), SRC(RT_CONST, PC_DEPTH + 1, XXXX), self);
			ins(&c, OP_ADD, 3, d, self, SRC(RT_CONST, PC_DEPTH, WWWW));
		}
		ins(&c, OP_MAX, 3, d, self, SRC(RT_CONST, PC_DEPTH, YYYY));
		ins(&c, OP_MIN, 3, d, self, SRC(RT_CONST, PC_DEPTH, ZZZZ));
		ins(&c, OP_MOV, 2, DST(RT_DEPTHOUT, 0, M_ALL), self);
	}
	put(&c, 0x0000ffff);
	g_shader_problems += check_shader(c.t, c.n, "the shader for combiner setting", hash);
	hr = IDirect3DDevice9_CreatePixelShader(g_device, (const DWORD *)c.t, &p->shader);
	g_shaders_made++; if (FAILED(hr)) g_shaders_refused++;
	if (dump_wanted(hash)) { dump_words("the console's combiner setting", (const unsigned *)k, sizeof(*k) / 4); dump_words("the shader made from it", c.t, c.n); }
	if (FAILED(hr) && k->depth)
	{
		/* Direct3D 9 here will not take a shader that writes the depth. Then no draw gets one: the depth goes back to what the
		   stage 8 build made (the program's z, cut off at the depth buffer's range: no sky, and distant things less exact). */
		pixel_key plain = *k;
		plain.depth = 0;
		note("combiner setting %08x was refused by Direct3D 9 (%08lx) with the depth written; from here on the depth is made the way the stage 8 build made it (H2N_GFXDEPTH=0)", hash, (unsigned long)hr);
		dump_words("the shader that was refused", c.t, c.n); probe_refused(c.t, c.n, "the shader for combiner setting", hash);
		free(c.t);
		g_depth_mode = 0;
		p->shader = pixel_shader_for(&plain)->shader;
		return p;
	}
	if (FAILED(hr))
	{
		p->shader = NULL;
		note("combiner setting %08x (%u stages) was not accepted by Direct3D 9 (%08lx); what uses it is not drawn", hash, count, (unsigned long)hr);
		dump_words("the console's setting", (const unsigned *)k, sizeof(*k) / 4);
		dump_words("the shader made from it", c.t, c.n);
		probe_refused(c.t, c.n, "the shader for combiner setting", hash);
	}
	else if (g_trace) h2n_log("       combiner setting %08x: %u stages, texture modes %05x, final %08x %08x -> %u words of shader\n", hash, count, k->texture_modes, k->final_abcd, k->final_efg, c.n);
	free(c.t);
	return p;
}

/* ================================================================ textures ==== */
typedef struct texture_entry
{
	const unsigned char *memory;
	unsigned format, width, height, depth, levels, cube, volume, content, checked_frame, used_frame, bytes;
	IDirect3DBaseTexture9 *texture;
	IDirect3DBaseTexture9 *staging;     /* 9Ex device (H2N_AA=dlss), where a format cannot be dynamic: filled here, then copied in */
} texture_entry;
static void texture_drop(IDirect3DBaseTexture9 **texture, IDirect3DBaseTexture9 **staging)
{
	if (*texture) IDirect3DBaseTexture9_Release(*texture);
	if (*staging) IDirect3DBaseTexture9_Release(*staging);
	*texture = *staging = NULL;
}
#define TEXTURE_SLOTS 16384
static texture_entry g_textures[TEXTURE_SLOTS];
static unsigned g_texture_count, g_texture_uploads, g_texture_sweeps, g_texture_limit, g_texture_sweep_wanted;

/* The table is keyed by where the game keeps a texture, and a slot once taken stayed taken: after enough levels (s9e:
 * three in a row, 12 minutes) all 3,072 slots allowed were in use, new textures got none and were drawn black. When the
 * table holds 3,072 textures (the most s9e ever had alive, so no more memory than before; H2N_GFXTEXLIMIT for tests)
 * fills up, at the end of that frame the entries not used for ten seconds of frames give their Direct3D 9 texture back
 * (and if that is not enough, the least recently used, down to half the limit) and the rest are placed afresh. A texture the game uses again is simply unpacked again. */
static int texture_older(const void *x, const void *y)
{
	const texture_entry *a = (const texture_entry *)x, *b = (const texture_entry *)y;
	return (int)(g_frame - b->used_frame) - (int)(g_frame - a->used_frame);       /* most recently used first */
}
static void texture_sweep(void)
{
	static texture_entry kept[TEXTURE_SLOTS];
	unsigned n = 0, released = 0, i;
	for (i = 0; i < TEXTURE_SLOTS; i++)
	{
		texture_entry *e = &g_textures[i];
		if (!e->memory) continue;
		if (e->texture && g_frame - e->used_frame < 600) kept[n++] = *e;
		else if (e->texture) { texture_drop(&e->texture, &e->staging); released++; }
	}
	/* still more than three quarters of the limit: keep the most recently used half, so that sweeps stay rare */
	if (n > g_texture_limit * 3 / 4)
	{
		qsort(kept, n, sizeof(kept[0]), texture_older);
		while (n > g_texture_limit / 2) { n--; texture_drop(&kept[n].texture, &kept[n].staging); released++; }   /* s9s: the macro names its argument twice (n went down by two a call, and half the textures were never released) */
	}
	memset(g_textures, 0, sizeof(g_textures));
	for (i = 0; i < n; i++)
	{
		unsigned slot = (unsigned)(((size_t)kept[i].memory >> 4) * 2654435761u) % TEXTURE_SLOTS;
		while (g_textures[slot].memory) slot = (slot + 1) % TEXTURE_SLOTS;
		g_textures[slot] = kept[i];
	}
	g_texture_count = n;
	if (g_texture_sweeps++ < 20) note("the texture table was full: %u textures let go (not used lately first), %u kept", released, n);
}

typedef struct target_entry
{
	const unsigned char *memory;
	unsigned width, height, depth, real_width, real_height;      /* width, height: the game's; real: ours (times the render scale) */
	IDirect3DTexture9 *texture; IDirect3DSurface9 *surface;
	/* a depth buffer the game also reads as a picture (see "reading the depth buffer"): the copy it reads, whether the copy is behind, the stencil values that can be in the buffer */
	IDirect3DTexture9 *copy; IDirect3DSurface9 *copy_surface;
	unsigned char stale, stencil_any, stencil_values[256];
	unsigned stencil_count;
	unsigned stamp;                                        /* (s10g) when it was last drawn into or cleared: g_target_stamp then */
} target_entry;
#define TARGET_SLOTS 64
static target_entry g_targets[TARGET_SLOTS];
static unsigned g_target_stamp, g_frame_stamp, g_targets_made;   /* (s10g) uses so far; g_target_stamp when this frame began; surfaces made */
static target_entry *g_scene_color, *g_scene_depth;   /* (declared again with the anti-aliasing below: what the view's 3D was drawn into) */
static target_entry *g_color_target, *g_depth_target;      /* what the last draw or clear went into, and the depth buffer it used */

static unsigned format_bytes(unsigned f)       /* bytes per texel; 0 for the block-compressed formats and the ones not handled */
{
	switch (f)
	{
	case 0x00: case 0x01: case 0x0b: case 0x13: case 0x19: case 0x1b: case 0x1f: return 1;
	case 0x02: case 0x03: case 0x04: case 0x05: case 0x10: case 0x11: case 0x1a: case 0x1c: case 0x1d: case 0x20: case 0x16: case 0x17: case 0x27: case 0x28: case 0x29: return 2;
	case 0x06: case 0x07: case 0x12: case 0x1e: case 0x3a: case 0x3b: case 0x3c: case 0x3f: case 0x40: case 0x41: case 0x33: return 4;
	}
	return 0;
}
static D3DFORMAT format_compressed(unsigned f) { return f == 0x0c ? D3DFMT_DXT1 : f == 0x0e ? D3DFMT_DXT3 : f == 0x0f ? D3DFMT_DXT5 : D3DFMT_UNKNOWN; }

static unsigned texel_color(const unsigned char *p, unsigned f, const unsigned *palette)
{
	unsigned v, a, r, g, b;
	switch (f)
	{
	case 0x00: case 0x13: return 0xff000000u | p[0] * 0x010101u;                              /* luminance */
	case 0x01: case 0x1b: return p[0] * 0x01010101u;                                          /* alpha = luminance */
	case 0x19: case 0x1f: return ((unsigned)p[0] << 24) | 0xffffffu;                          /* alpha */
	case 0x0b: return palette ? palette[p[0]] : 0xff000000u | p[0] * 0x010101u;
	case 0x1a: case 0x20: return ((unsigned)p[1] << 24) | p[0] * 0x010101u;                   /* alpha, luminance */
	case 0x02: case 0x10: v = p[0] | (p[1] << 8); a = v & 0x8000 ? 255 : 0; goto five_five_five;
	case 0x03: case 0x1c: v = p[0] | (p[1] << 8); a = 255;
	five_five_five:
		r = (v >> 10) & 31; g = (v >> 5) & 31; b = v & 31;
		return (a << 24) | (((r << 3) | (r >> 2)) << 16) | (((g << 3) | (g >> 2)) << 8) | ((b << 3) | (b >> 2));
	case 0x04: case 0x1d: v = p[0] | (p[1] << 8); return (((v >> 12) & 15) * 17u << 24) | (((v >> 8) & 15) * 17u << 16) | (((v >> 4) & 15) * 17u << 8) | ((v & 15) * 17u);
	case 0x05: case 0x11:
		v = p[0] | (p[1] << 8); r = (v >> 11) & 31; g = (v >> 5) & 63; b = v & 31;
		return 0xff000000u | (((r << 3) | (r >> 2)) << 16) | (((g << 2) | (g >> 4)) << 8) | ((b << 3) | (b >> 2));
	case 0x06: case 0x12: memcpy(&v, p, 4); return v;
	case 0x07: case 0x1e: memcpy(&v, p, 4); return v | 0xff000000u;
	case 0x3a: case 0x3f: memcpy(&v, p, 4); return (v & 0xff00ff00u) | ((v >> 16) & 255) | ((v & 255) << 16);
	case 0x3b: case 0x40: memcpy(&v, p, 4); return ((v & 255) << 24) | (((v >> 8) & 255) << 16) | (((v >> 16) & 255) << 8) | (v >> 24);
	case 0x3c: case 0x41: memcpy(&v, p, 4); return ((v & 255) << 24) | (v >> 8);
	case 0x28: return 0xff000000u | ((unsigned)(unsigned char)(p[1] + 128) << 8) | (unsigned char)(p[0] + 128) | 0x800000;    /* signed pair, stored biased: U in blue, V in green */
	case 0x16: case 0x17: case 0x29: return 0xff000000u | ((unsigned)p[1] << 16) | ((unsigned)p[0] << 8) | p[0];
	}
	return 0xffff00ffu;
}
static void swizzle_masks(unsigned width, unsigned height, unsigned depth, unsigned *mx, unsigned *my, unsigned *mz)
{
	unsigned x = 0, y = 0, z = 0, bit = 1, mask_bit = 1;
	int done;
	do
	{
		done = 1;
		if (bit < width) { x |= mask_bit; mask_bit <<= 1; done = 0; }
		if (bit < height) { y |= mask_bit; mask_bit <<= 1; done = 0; }
		if (bit < depth) { z |= mask_bit; mask_bit <<= 1; done = 0; }
		bit <<= 1;
	} while (!done);
	*mx = x; *my = y; *mz = z;
}
/* one level (one slice of it) into 32-bit colours */
static void unpack_level(unsigned *out, unsigned out_pitch, const unsigned char *source, const gx_texture *t, unsigned width, unsigned height, unsigned depth, unsigned slice, unsigned level)
{
	unsigned bytes = format_bytes(t->format), x, y;
	if (t->linear)
	{
		unsigned pitch = level == 0 && t->pitch ? t->pitch : width * bytes;
		for (y = 0; y < height; y++)
			for (x = 0; x < width; x++)
				((unsigned *)((unsigned char *)out + y * out_pitch))[x] = texel_color(source + y * pitch + x * bytes, t->format, t->palette);
	}
	else
	{
		unsigned mx, my, mz, ox, oy, oz = 0, i;
		swizzle_masks(width, height, depth, &mx, &my, &mz);
		for (i = 0; i < slice; i++) oz = (oz - mz) & mz;
		for (y = 0, oy = 0; y < height; y++, oy = (oy - my) & my)
			for (x = 0, ox = 0; x < width; x++, ox = (ox - mx) & mx)
				((unsigned *)((unsigned char *)out + y * out_pitch))[x] = texel_color(source + (ox | oy | oz) * bytes, t->format, t->palette);
	}
}
static unsigned content_hash(const gx_texture *t)
{
	unsigned faces = t->cube ? 6 : 1, bytes = t->level_bytes[0], h = 2166136261u, f;
	for (f = 0; f < faces; f++)
	{
		const unsigned char *p = t->memory + f * t->face_bytes;
		if (bytes <= 8192) h = hash_words(p, bytes, h);
		else
		{
			unsigned step = (bytes - 64) / 96, i;
			for (i = 0; i < 96; i++) h = hash_words(p + i * step, 64, h);
			h = hash_words(p + bytes - 64, 64, h);
		}
	}
	if (t->palette) h = hash_words(t->palette, 1024, h);
	return h;
}
static void unpack_blocks(unsigned *out, unsigned out_pitch, const unsigned char *source, unsigned format, unsigned width, unsigned height);
static HRESULT make_texture(const gx_texture *t, unsigned levels, D3DFORMAT format, DWORD usage, D3DPOOL pool, IDirect3DBaseTexture9 **out)
{
	HRESULT hr;
	*out = NULL;
	if (t->cube) hr = IDirect3DDevice9_CreateCubeTexture(g_device, t->width, levels, usage, format, pool, (IDirect3DCubeTexture9 **)out, NULL);
	else if (t->volume) hr = IDirect3DDevice9_CreateVolumeTexture(g_device, t->width, t->height, t->depth, levels, usage, format, pool, (IDirect3DVolumeTexture9 **)out, NULL);
	else hr = IDirect3DDevice9_CreateTexture(g_device, t->width, t->height, levels, usage, format, pool, (IDirect3DTexture9 **)out, NULL);
	if (FAILED(hr)) *out = NULL;
	return hr;
}
static void upload(texture_entry *e, const gx_texture *t)
{
	unsigned faces = t->cube ? 6 : 1, face, level;
	D3DFORMAT compressed = format_compressed(t->format);
	int by_hand = compressed != D3DFMT_UNKNOWN && ((t->width | t->height) & 3);
	IDirect3DBaseTexture9 *into = e->staging ? e->staging : e->texture;
	g_texture_uploads++;
	for (face = 0; face < faces; face++)
		for (level = 0; level < e->levels; level++)
		{
			unsigned width = t->width >> level, height = t->height >> level, depth = t->depth >> level;
			const unsigned char *source = t->memory + face * t->face_bytes + t->level_offset[level];
			D3DLOCKED_RECT lock;
			HRESULT hr;
			if (!width) width = 1;
			if (!height) height = 1;
			if (!depth) depth = 1;
			if (t->volume)
			{
				D3DLOCKED_BOX box;
				unsigned z;
				if (FAILED(IDirect3DVolumeTexture9_LockBox((IDirect3DVolumeTexture9 *)into, level, &box, NULL, 0))) continue;
				for (z = 0; z < depth; z++) unpack_level((unsigned *)((unsigned char *)box.pBits + z * box.SlicePitch), (unsigned)box.RowPitch, source, t, width, height, depth, z, level);
				IDirect3DVolumeTexture9_UnlockBox((IDirect3DVolumeTexture9 *)into, level);
				continue;
			}
			hr = t->cube ? IDirect3DCubeTexture9_LockRect((IDirect3DCubeTexture9 *)into, (D3DCUBEMAP_FACES)face, level, &lock, NULL, 0)
				: IDirect3DTexture9_LockRect((IDirect3DTexture9 *)into, level, &lock, NULL, 0);
			if (FAILED(hr)) continue;
			if (by_hand) unpack_blocks(lock.pBits, (unsigned)lock.Pitch, source, t->format, width, height);
			else if (compressed != D3DFMT_UNKNOWN)
			{
				unsigned block = t->format == 0x0c ? 8 : 16, across = (width + 3) / 4, down = (height + 3) / 4, y;
				for (y = 0; y < down; y++) memcpy((unsigned char *)lock.pBits + y * lock.Pitch, source + y * across * block, across * block);
			}
			else unpack_level(lock.pBits, (unsigned)lock.Pitch, source, t, width, height, 1, 0, level);
			if (t->cube) IDirect3DCubeTexture9_UnlockRect((IDirect3DCubeTexture9 *)into, (D3DCUBEMAP_FACES)face, level);
			else IDirect3DTexture9_UnlockRect((IDirect3DTexture9 *)into, level);
		}
	if (e->staging) IDirect3DDevice9_UpdateTexture(g_device, e->staging, e->texture);
}
/* block-compressed textures whose size is not a multiple of four (Direct3D 9 will not create those) are unpacked here */
static void unpack_blocks(unsigned *out, unsigned out_pitch, const unsigned char *source, unsigned format, unsigned width, unsigned height)
{
	unsigned across = (width + 3) / 4, down = (height + 3) / 4, bx, by, i, x, y, block = format == 0x0c ? 8 : 16;
	for (by = 0; by < down; by++)
		for (bx = 0; bx < across; bx++)
		{
			const unsigned char *b = source + (by * across + bx) * block, *cb = format == 0x0c ? b : b + 8;
			unsigned c0 = cb[0] | (cb[1] << 8), c1 = cb[2] | (cb[3] << 8), bits, colors[4], alpha[16], r[4], g[4], bl[4];
			memcpy(&bits, cb + 4, 4);
			r[0] = ((c0 >> 11) & 31) * 255 / 31; g[0] = ((c0 >> 5) & 63) * 255 / 63; bl[0] = (c0 & 31) * 255 / 31;
			r[1] = ((c1 >> 11) & 31) * 255 / 31; g[1] = ((c1 >> 5) & 63) * 255 / 63; bl[1] = (c1 & 31) * 255 / 31;
			if (c0 > c1 || format != 0x0c)
			{
				r[2] = (2 * r[0] + r[1]) / 3; g[2] = (2 * g[0] + g[1]) / 3; bl[2] = (2 * bl[0] + bl[1]) / 3;
				r[3] = (r[0] + 2 * r[1]) / 3; g[3] = (g[0] + 2 * g[1]) / 3; bl[3] = (bl[0] + 2 * bl[1]) / 3;
				for (i = 0; i < 4; i++) colors[i] = 0xff000000u | (r[i] << 16) | (g[i] << 8) | bl[i];
			}
			else
			{
				r[2] = (r[0] + r[1]) / 2; g[2] = (g[0] + g[1]) / 2; bl[2] = (bl[0] + bl[1]) / 2;
				for (i = 0; i < 3; i++) colors[i] = 0xff000000u | (r[i] << 16) | (g[i] << 8) | bl[i];
				colors[3] = 0;
			}
			for (i = 0; i < 16; i++) alpha[i] = 255;
			if (format == 0x0e) for (i = 0; i < 16; i++) alpha[i] = ((b[i / 2] >> (4 * (i & 1))) & 15) * 17;
			else if (format == 0x0f)
			{
				unsigned a[8], n;
				unsigned long long codes = 0;
				a[0] = b[0]; a[1] = b[1];
				for (n = 0; n < 6; n++) codes |= (unsigned long long)b[2 + n] << (8 * n);
				if (a[0] > a[1]) for (n = 1; n < 7; n++) a[n + 1] = ((7 - n) * a[0] + n * a[1]) / 7;
				else { for (n = 1; n < 5; n++) a[n + 1] = ((5 - n) * a[0] + n * a[1]) / 5; a[6] = 0; a[7] = 255; }
				for (i = 0; i < 16; i++) alpha[i] = a[(codes >> (3 * i)) & 7];
			}
			for (i = 0; i < 16; i++)
			{
				x = bx * 4 + (i & 3); y = by * 4 + (i >> 2);
				if (x < width && y < height)
				{
					unsigned color = colors[(bits >> (2 * i)) & 3];
					if (format != 0x0c) color = (color & 0xffffffu) | (alpha[i] << 24);
					((unsigned *)((unsigned char *)out + y * out_pitch))[x] = color;
				}
			}
		}
}

static void target_release(struct target_entry *e);
static IDirect3DTexture9 *depth_copy(struct target_entry *e);
/* (s10g) One piece of the console's memory can be drawn into at several sizes, and as a colour picture and as a depth
 * buffer, in one frame - your Metropolis capture: 0x82b1b000 is a 128x128 picture, then a light's 512x512 shadow map
 * (its depth), then a 160x120 picture. Each use has its own surface here, kept from frame to frame. (Until s10f one
 * memory had one surface, made again whenever the size or kind changed: in the shadow map's passes that was twice a
 * draw, 4096x4096 each at render scale 8 - about 95 ms a frame.) What reads the memory gets the one of the size it
 * reads, the last drawn into if there are several. */
static target_entry *target_find(const unsigned char *memory)       /* the one last drawn into */
{
	unsigned i;
	target_entry *best = NULL;
	if (!memory) return NULL;
	for (i = 0; i < TARGET_SLOTS; i++) if (g_targets[i].memory == memory && (!best || g_targets[i].stamp > best->stamp)) best = &g_targets[i];
	return best;
}
static target_entry *target_find_sized(const unsigned char *memory, unsigned width, unsigned height)
{
	unsigned i;
	target_entry *best = NULL;
	if (!memory) return NULL;
	for (i = 0; i < TARGET_SLOTS; i++)
		if (g_targets[i].memory == memory && g_targets[i].width == width && g_targets[i].height == height && (!best || g_targets[i].stamp > best->stamp)) best = &g_targets[i];
	return best ? best : target_find(memory);
}
static target_entry *target_find_depth(const unsigned char *memory, unsigned width, unsigned height)   /* (s10k) the depth surface drawn at that memory, that size first */
{
	unsigned i;
	target_entry *best = NULL, *any = NULL;
	if (!memory) return NULL;
	for (i = 0; i < TARGET_SLOTS; i++)
		if (g_targets[i].memory == memory && g_targets[i].depth && g_targets[i].texture)
		{
			if (g_targets[i].width == width && g_targets[i].height == height && (!best || g_targets[i].stamp > best->stamp)) best = &g_targets[i];
			if (!any || g_targets[i].stamp > any->stamp) any = &g_targets[i];
		}
	return best ? best : any;
}
static unsigned depth_format_bits(unsigned f)    /* (s10k) the console's depth buffer formats read as a shadow map: 24 or 16 bits (0: not one, or a float one) */
{
	switch (f)
	{
	case 0x2a: case 0x2e: return 24;               /* D24S8, LIN_D24S8 */
	case 0x2c: case 0x30: return 16;               /* D16, LIN_D16 */
	}
	return 0;
}
static IDirect3DBaseTexture9 *texture_for(const gx_texture *t, int *kind)
{
	unsigned slot = (unsigned)(((size_t)t->memory >> 4) * 2654435761u) % TEXTURE_SLOTS, probe;
	texture_entry *e = NULL;
	target_entry *target = target_find_sized(t->memory, t->width, t->height);
	static unsigned seen_formats[24] = { ~0u, ~0u, ~0u, ~0u, ~0u, ~0u, ~0u, ~0u, ~0u, ~0u, ~0u, ~0u, ~0u, ~0u, ~0u, ~0u, ~0u, ~0u, ~0u, ~0u, ~0u, ~0u, ~0u, ~0u };
	HRESULT hr;
	unsigned levels = t->levels ? t->levels : 1;

	*kind = t->cube ? 2 : t->volume ? 3 : 1;
	if (target && target->texture && !target->depth) { *kind = 1; return (IDirect3DBaseTexture9 *)target->texture; }     /* something the game drew */
	static int shadow_maps = -1;                                                                                         /* (s10k) H2N_SHADOWMAP=0: as before (a shadow map reads as nothing) */
	if (shadow_maps < 0) shadow_maps = h2n_env_int("H2N_SHADOWMAP", 1) ? 1 : 0;
	if (shadow_maps && depth_format_bits(t->format) && g_depth_mode)                                                     /* (s10k) a shadow map: its depth, read and compared by the pixel shader */
	{
		target_entry *depth = target_find_depth(t->memory, t->width, t->height);
		if (depth) { *kind = 1; return (IDirect3DBaseTexture9 *)depth->texture; }
	}
	if (target && target->depth && format_bytes(t->format) == 4 && g_depth_mode)                                         /* its depth buffer, read as colours (not with stage 8's depth: that is not what the game expects to read) */
	{
		IDirect3DTexture9 *copy = depth_copy(target);
		*kind = copy ? 1 : 0;
		return (IDirect3DBaseTexture9 *)copy;
	}
	if (!format_bytes(t->format) && format_compressed(t->format) == D3DFMT_UNKNOWN)
	{
		if (note_once(seen_formats, 24, t->format)) note("texture format %#x of the console is not unpacked yet; such textures are left out", t->format);
		*kind = 0;
		return NULL;
	}
	if (levels > 12) levels = 12;
	if (!g_texture_limit) { g_texture_limit = (unsigned)h2n_env_int("H2N_GFXTEXLIMIT", 3072); if (g_texture_limit < 64 || g_texture_limit > TEXTURE_SLOTS / 2) g_texture_limit = 3072; }
	if (g_texture_count >= g_texture_limit) g_texture_sweep_wanted = 1;     /* done between two frames, when no texture is waiting to be bound */
	for (probe = 0; probe < TEXTURE_SLOTS; probe++)
	{
		texture_entry *candidate = &g_textures[(slot + probe) % TEXTURE_SLOTS];
		if (candidate->memory == t->memory) { e = candidate; break; }
		if (!candidate->memory) { if (g_texture_count < TEXTURE_SLOTS * 3 / 4) { e = candidate; g_texture_count++; } break; }
	}
	if (!e) { *kind = 0; return NULL; }
	if (e->texture && (e->format != t->format || e->width != t->width || e->height != t->height || e->depth != t->depth || e->levels != levels || e->cube != t->cube || e->volume != t->volume))
		texture_drop(&e->texture, &e->staging);
	e->memory = t->memory;
	if (!e->texture)
	{
		D3DFORMAT format = format_compressed(t->format) != D3DFMT_UNKNOWN && !((t->width | t->height) & 3) ? format_compressed(t->format) : D3DFMT_A8R8G8B8;
		e->format = t->format; e->width = t->width; e->height = t->height; e->depth = t->depth; e->levels = levels; e->cube = t->cube; e->volume = t->volume;
		/* a 9Ex device (H2N_AA=dlss) has no managed pool: dynamic textures in the default pool, filled the same way; where a
		   format cannot be dynamic, a default-pool texture filled from a system-memory twin (UpdateTexture) */
		static int staging_test = -1;          /* H2N_GFXSTAGING=1 (a test): every texture of a 9Ex device through the twin */
		if (staging_test < 0) staging_test = h2n_env_int("H2N_GFXSTAGING", 0);
		hr = g_ex && staging_test ? E_FAIL : make_texture(t, levels, format, g_ex ? D3DUSAGE_DYNAMIC : 0, g_ex ? D3DPOOL_DEFAULT : D3DPOOL_MANAGED, &e->texture);
		if (FAILED(hr) && g_ex)
		{
			hr = make_texture(t, levels, format, 0, D3DPOOL_DEFAULT, &e->texture);
			if (SUCCEEDED(hr) && FAILED(hr = make_texture(t, levels, format, 0, D3DPOOL_SYSTEMMEM, &e->staging))) texture_drop(&e->texture, &e->staging);
			if (SUCCEEDED(hr)) { static unsigned told; if (told++ < 4) note("a %ux%ux%u texture (format %#x) cannot be dynamic here: it is filled through system memory", t->width, t->height, t->depth, t->format); }
		}
		if (FAILED(hr))
		{
			e->texture = NULL;
			note("a %ux%ux%u texture (format %#x, %u levels) could not be created (%08lx)", t->width, t->height, t->depth, t->format, levels, (unsigned long)hr);
			*kind = 0;
			return NULL;
		}
		e->content = content_hash(t);
		e->checked_frame = g_frame;
		upload(e, t);
	}
	else if (e->checked_frame != g_frame)
	{
		unsigned content = content_hash(t);
		e->checked_frame = g_frame;
		if (content != e->content) { e->content = content; upload(e, t); }
	}
	e->used_frame = g_frame;
	return e->texture;
}
void gx_memory_changed(const unsigned char *memory)       /* the game locked it: look at it again the next time it is used */
{
	unsigned slot = (unsigned)(((size_t)memory >> 4) * 2654435761u) % TEXTURE_SLOTS, probe;
	for (probe = 0; probe < TEXTURE_SLOTS; probe++)
	{
		texture_entry *e = &g_textures[(slot + probe) % TEXTURE_SLOTS];
		if (!e->memory) return;
		if (e->memory == memory) { e->checked_frame = ~0u; e->content = ~e->content; return; }
	}
}
void gx_memory_gone(const unsigned char *memory)
{
	unsigned slot = (unsigned)(((size_t)memory >> 4) * 2654435761u) % TEXTURE_SLOTS, probe;
	target_entry *target;
	if (!g_device) return;
	while ((target = target_find(memory)) != NULL) target_release(target);
	for (probe = 0; probe < TEXTURE_SLOTS; probe++)
	{
		texture_entry *e = &g_textures[(slot + probe) % TEXTURE_SLOTS];
		if (!e->memory) return;
		if (e->memory == memory)
		{
			/* the slot stays taken (other entries may have been placed past it); only the Direct3D 9 texture goes */
			if (e->texture) texture_drop(&e->texture, &e->staging);
			return;
		}
	}
}

static void target_release(target_entry *e)
{
	if (e->copy_surface) IDirect3DSurface9_Release(e->copy_surface);
	if (e->copy) IDirect3DTexture9_Release(e->copy);
	if (e->surface) IDirect3DSurface9_Release(e->surface);
	if (e->texture) IDirect3DTexture9_Release(e->texture);
	memset(e, 0, sizeof(*e));
}

/* ================================================================ reading the depth buffer ==== */
/* The game reads its depth buffer as a picture: it binds the buffer's memory as a 32-bit colour texture (its fog pass
 * does, to know how far away each pixel is and whether it is sky). On the console that is the same memory, read as
 * colours: of each 32-bit word the top 24 bits are the depth and the low 8 the stencil value, so alpha, red and green
 * are the three bytes of the depth and blue is the stencil. Direct3D 9 cannot show a depth buffer as colours, so the
 * depth buffer is made as a texture whose depth can be read ("INTZ"), and before a draw that reads it a copy in
 * colours is filled in: one pass writes the three depth bytes, and one pass for each stencil value that can be in
 * the buffer writes that value into blue where the stencil test finds it. The copy is what the game's draw reads. */
static IDirect3DVertexShader9 *g_copy_vs;
static IDirect3DPixelShader9 *g_copy_ps_depth, *g_copy_ps_flat;
static IDirect3DVertexDeclaration9 *g_copy_declaration;
static unsigned g_depth_copies, g_depth_copy_passes;
static int g_targets_changed;       /* a copy was made: the render target and all the state are not the game's any more */
#define FORMAT_INTZ ((D3DFORMAT)MAKEFOURCC('I', 'N', 'T', 'Z'))

static void stencil_value_add(target_entry *e, unsigned value)
{
	unsigned i;
	value &= 255;
	for (i = 0; i < e->stencil_count; i++) if (e->stencil_values[i] == value) return;
	if (e->stencil_count < 256) e->stencil_values[e->stencil_count++] = (unsigned char)value;
}
/* what a draw can leave in the stencil buffer */
static void stencil_track(target_entry *e, const gx_state *s)
{
	unsigned ops[3], i;
	if (!e || !s->stencil || !(s->stencil_write_mask & 255)) return;
	ops[0] = s->stencil_fail; ops[1] = s->stencil_zfail; ops[2] = s->stencil_pass;
	for (i = 0; i < 3; i++)
	{
		if (ops[i] == 0x1e00) continue;                                               /* keep */
		e->stale = 1;
		if (ops[i] == 0) stencil_value_add(e, 0);                                     /* zero */
		else if (ops[i] == 0x1e01 && (s->stencil_write_mask & 255) == 255) stencil_value_add(e, s->stencil_ref);   /* replace */
		else e->stencil_any = 1;                                                      /* counted up or down, inverted, or written in part: it can be anything */
	}
}
static int copy_shaders(void)
{
	static int tried;
	static const D3DVERTEXELEMENT9 elements[] = { { 0, 0, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0 }, { 0, 16, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0 }, D3DDECL_END() };
	code c;
	if (tried) return g_copy_vs && g_copy_ps_depth && g_copy_ps_flat && g_copy_declaration;
	tried = 1;
	memset(&c, 0, sizeof(c));
	put(&c, 0xfffe0300);
	declare(&c, USAGE_POSITION, 0, RT_INPUT, 0); declare(&c, USAGE_TEXCOORD, 0, RT_INPUT, 1);
	declare(&c, USAGE_POSITION, 0, RT_OUTPUT, 0); declare(&c, USAGE_TEXCOORD, 0, RT_OUTPUT, 1);
	ins(&c, OP_MOV, 2, DST(RT_OUTPUT, 0, M_ALL), SRC(RT_INPUT, 0, XYZW));
	ins(&c, OP_MOV, 2, DST(RT_OUTPUT, 1, M_ALL), SRC(RT_INPUT, 1, XYZW));
	put(&c, 0x0000ffff);
	g_shader_problems += check_shader(c.t, c.n, "the shader that copies a depth buffer (vertices)", 0);
	if (FAILED(IDirect3DDevice9_CreateVertexShader(g_device, (const DWORD *)c.t, &g_copy_vs))) g_copy_vs = NULL;
	free(c.t);
	/* the depth, read from the depth buffer's texture, as its three bytes */
	memset(&c, 0, sizeof(c));
	put(&c, 0xffff0300);
	declare(&c, USAGE_TEXCOORD, 0, RT_INPUT, 0);
	ins(&c, OP_DCL, 2, 0x80000000u | (2u << 27), DST(RT_SAMPLER, 0, M_ALL));
	ins(&c, OP_TEX, 3, DST(RT_TEMP, 4, M_ALL), SRC(RT_INPUT, 0, XYZW), SRC(RT_SAMPLER, 0, XYZW));
	emit_depth_bytes(&c, SRC(RT_TEMP, 4, XXXX), 3, 0, 0);
	ins(&c, OP_MOV, 2, DST(RT_COLOROUT, 0, M_ALL), TEMP(3));
	put(&c, 0x0000ffff);
	g_shader_problems += check_shader(c.t, c.n, "the shader that copies a depth buffer (depth)", 0);
	if (FAILED(IDirect3DDevice9_CreatePixelShader(g_device, (const DWORD *)c.t, &g_copy_ps_depth))) { g_copy_ps_depth = NULL; dump_words("the shader that copies a depth buffer", c.t, c.n); }
	free(c.t);
	memset(&c, 0, sizeof(c));
	put(&c, 0xffff0300);
	ins(&c, OP_MOV, 2, DST(RT_COLOROUT, 0, M_ALL), SRC(RT_CONST, 0, XYZW));
	put(&c, 0x0000ffff);
	if (FAILED(IDirect3DDevice9_CreatePixelShader(g_device, (const DWORD *)c.t, &g_copy_ps_flat))) g_copy_ps_flat = NULL;
	free(c.t);
	if (FAILED(IDirect3DDevice9_CreateVertexDeclaration(g_device, elements, &g_copy_declaration))) g_copy_declaration = NULL;
	if (!(g_copy_vs && g_copy_ps_depth && g_copy_ps_flat && g_copy_declaration)) note("the shaders that copy a depth buffer for reading were not accepted by Direct3D 9; what reads the depth buffer (fog) is not drawn right");
	return g_copy_vs && g_copy_ps_depth && g_copy_ps_flat && g_copy_declaration;
}
static IDirect3DTexture9 *depth_copy_inner(target_entry *e);
static IDirect3DTexture9 *depth_copy(target_entry *e)
{
	gx_u64 t0 = tsc(), spent;
	IDirect3DTexture9 *r = depth_copy_inner(e);
	spent = tsc() - t0;
	g_ct[CT_DEPTHCOPY] += spent; g_ct_nested += spent;
	return r;
}
static IDirect3DTexture9 *depth_copy_inner(target_entry *e)
{
	IDirect3DDevice9 *d = g_device;
	D3DVIEWPORT9 viewport;
	float quad[4][8], constants[3][4], dx, dy;
	unsigned i, values;
	if (!e->texture || !copy_shaders()) return NULL;
	if (!e->copy)
	{
		if (FAILED(IDirect3DDevice9_CreateTexture(d, e->real_width, e->real_height, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &e->copy, NULL))) { e->copy = NULL; SCALED_FAILED(0); return NULL; }
		if (FAILED(IDirect3DTexture9_GetSurfaceLevel(e->copy, 0, &e->copy_surface))) { IDirect3DTexture9_Release(e->copy); e->copy = NULL; e->copy_surface = NULL; return NULL; }
		e->stale = 1;
	}
	if (!e->stale) return e->copy;
	/* a quad over the whole surface, each pixel reading its own texel (Direct3D 9 puts the middle of a pixel half a pixel from where its texel is) */
	dx = 1.0f / e->real_width; dy = 1.0f / e->real_height;
	for (i = 0; i < 4; i++)
	{
		float u = (float)(i & 1), v = (float)(i >> 1);
		quad[i][0] = u * 2.0f - 1.0f - dx; quad[i][1] = 1.0f - v * 2.0f + dy; quad[i][2] = 0.5f; quad[i][3] = 1.0f;
		quad[i][4] = u; quad[i][5] = v; quad[i][6] = 0.0f; quad[i][7] = 1.0f;
	}
	viewport.X = 0; viewport.Y = 0; viewport.Width = e->real_width; viewport.Height = e->real_height; viewport.MinZ = 0.0f; viewport.MaxZ = 1.0f;
	ss_target(e->copy_surface);
	ss_depth(NULL);
	ss_viewport(&viewport);
	ss_rs(D3DRS_ALPHATESTENABLE, FALSE); ss_rs(D3DRS_ALPHABLENDENABLE, FALSE);
	ss_rs(D3DRS_ZENABLE, D3DZB_FALSE); ss_rs(D3DRS_ZWRITEENABLE, FALSE);
	ss_rs(D3DRS_STENCILENABLE, FALSE); ss_rs(D3DRS_CULLMODE, D3DCULL_NONE);
	ss_rs(D3DRS_FILLMODE, D3DFILL_SOLID); ss_rs(D3DRS_SCISSORTESTENABLE, FALSE);
	ss_rs(D3DRS_COLORWRITEENABLE, D3DCOLORWRITEENABLE_RED | D3DCOLORWRITEENABLE_GREEN | D3DCOLORWRITEENABLE_BLUE | D3DCOLORWRITEENABLE_ALPHA);
	ss_vs(g_copy_vs); ss_declaration(g_copy_declaration); ss_ps(g_copy_ps_depth);
	depth_bytes_constants(constants);
	ss_constants(1, 0, &constants[0][0], 3);
	ss_texture(0, (IDirect3DBaseTexture9 *)e->texture);
	ss_sampler(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP); ss_sampler(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
	ss_sampler(0, D3DSAMP_MAGFILTER, D3DTEXF_POINT); ss_sampler(0, D3DSAMP_MINFILTER, D3DTEXF_POINT); ss_sampler(0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
	for (i = 1; i < 4; i++) ss_texture(i, NULL);
	IDirect3DDevice9_DrawPrimitiveUP(d, D3DPT_TRIANGLESTRIP, 2, quad, sizeof(quad[0]));
	g_depth_copy_passes++;
	/* the stencil values, into blue: only where the stencil buffer holds the value */
	ss_texture(0, NULL);
	ss_depth(e->surface);
	ss_ps(g_copy_ps_flat);
	ss_rs(D3DRS_COLORWRITEENABLE, D3DCOLORWRITEENABLE_BLUE);
	ss_rs(D3DRS_STENCILENABLE, TRUE); ss_rs(D3DRS_STENCILFUNC, D3DCMP_EQUAL);
	ss_rs(D3DRS_STENCILMASK, 255); ss_rs(D3DRS_STENCILWRITEMASK, 0);
	ss_rs(D3DRS_STENCILFAIL, D3DSTENCILOP_KEEP); ss_rs(D3DRS_STENCILZFAIL, D3DSTENCILOP_KEEP); ss_rs(D3DRS_STENCILPASS, D3DSTENCILOP_KEEP);
	values = e->stencil_any ? 256 : e->stencil_count;
	if (e->stencil_any) { static int told; if (!told) { told = 1; note("the stencil buffer is read back after being counted up or down: all 256 values are looked for, which is slow"); } }
	for (i = 0; i < values; i++)
	{
		unsigned value = e->stencil_any ? i : e->stencil_values[i];
		if (!value) continue;                                 /* blue is zero already */
		constants[0][0] = 0.0f; constants[0][1] = 0.0f; constants[0][2] = value / 255.0f; constants[0][3] = 0.0f;
		ss_constants(1, 0, &constants[0][0], 1);
		ss_rs(D3DRS_STENCILREF, value);
		IDirect3DDevice9_DrawPrimitiveUP(d, D3DPT_TRIANGLESTRIP, 2, quad, sizeof(quad[0]));
		g_depth_copy_passes++;
	}
	ss_rs(D3DRS_COLORWRITEENABLE, D3DCOLORWRITEENABLE_RED | D3DCOLORWRITEENABLE_GREEN | D3DCOLORWRITEENABLE_BLUE | D3DCOLORWRITEENABLE_ALPHA);
	e->stale = 0;
	g_depth_copies++; g_targets_changed = 1;
	if (FRAME_TRACE) h2n_log("       the depth buffer %p is copied for reading as a picture (%u stencil value%s looked for)\n", e->memory, values, values == 1 ? "" : "s");
	return e->copy;
}

/* ================================================================ what the game draws into ==== */
static target_entry *target_for(const gx_surface *s, int depth)
{
	target_entry *e = NULL, *oldest = NULL;
	unsigned i, others = 0;
	HRESULT hr;
	/* (s10g) the surface of this memory, size and kind, if there is one; four kinds or sizes of one memory at most are kept */
	for (i = 0; i < TARGET_SLOTS; i++)
	{
		target_entry *c = &g_targets[i];
		if (c->memory != s->memory) continue;
		if (c->width == s->width && c->height == s->height && c->depth == (unsigned)depth) { e = c; break; }
		others++;
		if (!oldest || c->stamp < oldest->stamp) oldest = c;
	}
	if (e) { e->stamp = ++g_target_stamp; return e; }
	if (others >= 4 && oldest && oldest->stamp <= g_frame_stamp && oldest != g_scene_color && oldest != g_scene_depth) target_release(oldest);
	for (i = 0; i < TARGET_SLOTS && g_targets[i].memory; i++) { }
	if (i == TARGET_SLOTS)
	{
		/* all taken: the one drawn into longest ago goes, if that was before this frame */
		target_entry *lru = NULL;
		for (i = 0; i < TARGET_SLOTS; i++) if (!lru || g_targets[i].stamp < lru->stamp) lru = &g_targets[i];
		if (!lru || lru->stamp > g_frame_stamp || lru == g_scene_color || lru == g_scene_depth) { note("more than %u surfaces are being drawn into; the newest is left out", TARGET_SLOTS); return NULL; }
		target_release(lru);
		i = (unsigned)(lru - g_targets);
	}
	e = &g_targets[i];
	e->stamp = ++g_target_stamp;
	g_targets_made++;
	e->memory = s->memory; e->width = s->width; e->height = s->height; e->depth = (unsigned)depth;
	e->real_width = s->width * g_scale; e->real_height = s->height * g_scale;
	if (depth)
	{
		static int plain;
		hr = plain ? E_FAIL : SCALE_REFUSED ? (HRESULT)0x8876017CL : IDirect3DDevice9_CreateTexture(g_device, e->real_width, e->real_height, 1, D3DUSAGE_DEPTHSTENCIL, FORMAT_INTZ, D3DPOOL_DEFAULT, &e->texture, NULL);
		if (SUCCEEDED(hr)) hr = IDirect3DTexture9_GetSurfaceLevel(e->texture, 0, &e->surface);
		if (FAILED(hr))
		{
			if (e->texture) { IDirect3DTexture9_Release(e->texture); e->texture = NULL; }
			if (g_scale > 1 && NO_MEMORY(hr)) SCALED_FAILED(hr);            /* (s10z) no memory for one this large is not "this device has none" */
			else if (!plain) { plain = 1; note("this graphics device has no depth buffer that can be read as a texture (INTZ, %08lx); what reads the depth buffer (fog) is not drawn right", (unsigned long)hr); }
			hr = SCALE_REFUSED ? (HRESULT)0x8876017CL : IDirect3DDevice9_CreateDepthStencilSurface(g_device, e->real_width, e->real_height, D3DFMT_D24S8, D3DMULTISAMPLE_NONE, 0, FALSE, &e->surface, NULL);
		}
		e->stale = 1; e->stencil_count = 0; e->stencil_any = 1;      /* nothing is known of what a new buffer holds */
	}
	else
	{
		hr = SCALE_REFUSED ? (HRESULT)0x8876017CL : IDirect3DDevice9_CreateTexture(g_device, e->real_width, e->real_height, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &e->texture, NULL);
		if (SUCCEEDED(hr)) hr = IDirect3DTexture9_GetSurfaceLevel(e->texture, 0, &e->surface);
		if (SUCCEEDED(hr)) IDirect3DDevice9_ColorFill(g_device, e->surface, NULL, 0);
	}
	if (FAILED(hr)) { SCALED_FAILED(hr); note("a %ux%u %s surface could not be created at %ux%u (%08lx)", s->width, s->height, depth ? "depth" : "colour", s->width * g_scale, s->height * g_scale, (unsigned long)hr); memset(e, 0, sizeof(*e)); return NULL; }
	if (g_trace) h2n_log("       picture: new %s surface %ux%u for the game's memory %p\n", depth ? "depth" : "colour", s->width, s->height, s->memory);
	return e;
}
/* (s10g) A depth-only pass - a light's shadow map: the console's colour surface set at the depth buffer's own memory, with
 * colour writes off, so only the depth is written there. That memory's surface is the depth buffer; Direct3D 9 still wants
 * a colour target, which gets a scratch surface of the same size, kept and never read. (Until s10f the colour and the depth
 * of one memory each took the other's place, so both were made again for every draw of the pass, and its depth was lost
 * from one draw to the next.) */
static target_entry g_scratch_color;
static target_entry *scratch_color(unsigned real_width, unsigned real_height)
{
	target_entry *e = &g_scratch_color;
	if (e->surface && e->real_width == real_width && e->real_height == real_height) return e;
	if (e->surface) { IDirect3DSurface9_Release(e->surface); e->surface = NULL; }
	{
		static const D3DFORMAT formats[3] = { (D3DFORMAT)MAKEFOURCC('N', 'U', 'L', 'L'), D3DFMT_R5G6B5, D3DFMT_A8R8G8B8 };   /* the drivers' "no colour" surface first */
		unsigned f;
		for (f = 0; f < 3 && !e->surface; f++)
			if (FAILED(IDirect3DDevice9_CreateRenderTarget(g_device, real_width, real_height, formats[f], D3DMULTISAMPLE_NONE, 0, FALSE, &e->surface, NULL))) e->surface = NULL;
	}
	if (!e->surface) { note("a %ux%u scratch colour surface for a depth-only pass could not be made", real_width, real_height); return NULL; }
	e->real_width = real_width; e->real_height = real_height;
	g_targets_made++;
	return e;
}
static int set_targets(const gx_state *s)
{
	target_entry *color, *depth = NULL;
	if (s->target.memory && s->target.memory == s->depth.memory)
	{
		gx_surface d = s->depth;
		if (d.width < s->target.width) d.width = s->target.width;
		if (d.height < s->target.height) d.height = s->target.height;
		depth = target_for(&d, 1);
		color = depth && depth->surface ? scratch_color(depth->real_width, depth->real_height) : NULL;
		g_color_target = color; g_depth_target = depth;
		if (!color || !depth) return 0;
		ss_target(color->surface);
		ss_depth(depth->surface);
		return 1;
	}
	color = s->target.memory ? target_for(&s->target, 0) : NULL;
	g_color_target = color; g_depth_target = NULL;
	if (!color || !color->surface) return 0;
	if (s->depth.memory)
	{
		gx_surface d = s->depth;
		if (d.width < s->target.width) d.width = s->target.width;
		if (d.height < s->target.height) d.height = s->target.height;
		depth = target_for(&d, 1);
	}
	g_depth_target = depth;
	ss_target(color->surface);
	ss_depth(depth ? depth->surface : NULL);
	return 1;
}

/* ================================================================ state ==== */
static DWORD blend_factor(unsigned v)
{
	switch (v)
	{
	case 0: return D3DBLEND_ZERO; case 1: return D3DBLEND_ONE;
	case 0x300: return D3DBLEND_SRCCOLOR; case 0x301: return D3DBLEND_INVSRCCOLOR; case 0x302: return D3DBLEND_SRCALPHA; case 0x303: return D3DBLEND_INVSRCALPHA;
	case 0x304: return D3DBLEND_DESTALPHA; case 0x305: return D3DBLEND_INVDESTALPHA; case 0x306: return D3DBLEND_DESTCOLOR; case 0x307: return D3DBLEND_INVDESTCOLOR;
	case 0x308: return D3DBLEND_SRCALPHASAT;
	case 0x8001: case 0x8003: return D3DBLEND_BLENDFACTOR; case 0x8002: case 0x8004: return D3DBLEND_INVBLENDFACTOR;
	}
	return D3DBLEND_ONE;
}
static DWORD blend_operation(unsigned v)
{
	switch (v) { case 0x800a: case 0xf005: return D3DBLENDOP_SUBTRACT; case 0x800b: case 0xf004: return D3DBLENDOP_REVSUBTRACT; case 0x8007: return D3DBLENDOP_MIN; case 0x8008: return D3DBLENDOP_MAX; }
	return D3DBLENDOP_ADD;
}
static DWORD compare(unsigned v) { return v >= 0x200 && v <= 0x207 ? v - 0x200 + 1 : D3DCMP_ALWAYS; }
static DWORD stencil_operation(unsigned v)
{
	switch (v) { case 0: return D3DSTENCILOP_ZERO; case 0x1e01: return D3DSTENCILOP_REPLACE; case 0x1e02: return D3DSTENCILOP_INCRSAT; case 0x1e03: return D3DSTENCILOP_DECRSAT;
		case 0x150a: return D3DSTENCILOP_INVERT; case 0x8507: return D3DSTENCILOP_INCR; case 0x8508: return D3DSTENCILOP_DECR; }
	return D3DSTENCILOP_KEEP;
}
static DWORD address_mode(unsigned v) { return v == 1 ? D3DTADDRESS_WRAP : v == 2 ? D3DTADDRESS_MIRROR : v == 4 ? D3DTADDRESS_BORDER : D3DTADDRESS_CLAMP; }
static DWORD filter_mode(unsigned v, int mip) { return v == 0 ? (mip ? D3DTEXF_NONE : D3DTEXF_POINT) : v == 1 ? D3DTEXF_POINT : mip ? D3DTEXF_LINEAR : v == 3 ? D3DTEXF_ANISOTROPIC : D3DTEXF_LINEAR; }
static void color_vector(unsigned color, float *v)
{
	v[0] = ((color >> 16) & 255) / 255.0f; v[1] = ((color >> 8) & 255) / 255.0f; v[2] = (color & 255) / 255.0f; v[3] = (color >> 24) / 255.0f;
}

static void apply_state(const gx_state *s)
{
	IDirect3DDevice9 *d = g_device;
	D3DVIEWPORT9 viewport;
	DWORD mask = ((s->color_mask >> 16) & 1 ? D3DCOLORWRITEENABLE_RED : 0) | ((s->color_mask >> 8) & 1 ? D3DCOLORWRITEENABLE_GREEN : 0)
		| (s->color_mask & 1 ? D3DCOLORWRITEENABLE_BLUE : 0) | ((s->color_mask >> 24) & 1 ? D3DCOLORWRITEENABLE_ALPHA : 0);
	viewport.X = s->viewport_x; viewport.Y = s->viewport_y; viewport.Width = s->viewport_width; viewport.Height = s->viewport_height; viewport.MinZ = 0.0f; viewport.MaxZ = 1.0f;
	if (viewport.X + viewport.Width > s->target.width) viewport.Width = s->target.width > viewport.X ? s->target.width - viewport.X : 1;
	if (viewport.Y + viewport.Height > s->target.height) viewport.Height = s->target.height > viewport.Y ? s->target.height - viewport.Y : 1;
	viewport.X *= g_scale; viewport.Y *= g_scale; viewport.Width *= g_scale; viewport.Height *= g_scale;
	ss_viewport(&viewport);
	ss_rs(D3DRS_ALPHATESTENABLE, s->alpha_test != 0);
	ss_rs(D3DRS_ALPHAFUNC, compare(s->alpha_func));
	ss_rs(D3DRS_ALPHAREF, s->alpha_ref & 255);
	ss_rs(D3DRS_ALPHABLENDENABLE, s->blend != 0 && s->logic_op != 0x150f);     /* (sg2: with a logic operation the console does not blend) */
	ss_rs(D3DRS_SRCBLEND, blend_factor(s->blend_source));
	ss_rs(D3DRS_DESTBLEND, blend_factor(s->blend_destination));
	ss_rs(D3DRS_BLENDOP, blend_operation(s->blend_op));
	ss_rs(D3DRS_BLENDFACTOR, s->blend_color);
	ss_rs(D3DRS_COLORWRITEENABLE, mask);
	ss_rs(D3DRS_ZENABLE, s->z_enable && s->depth.memory ? D3DZB_TRUE : D3DZB_FALSE);
	ss_rs(D3DRS_ZWRITEENABLE, s->z_write != 0);
	ss_rs(D3DRS_ZFUNC, compare(s->z_func));
	ss_rs(D3DRS_STENCILENABLE, s->stencil != 0 && s->depth.memory);
	ss_rs(D3DRS_STENCILFUNC, compare(s->stencil_func));
	ss_rs(D3DRS_STENCILREF, s->stencil_ref);
	ss_rs(D3DRS_STENCILMASK, s->stencil_mask);
	ss_rs(D3DRS_STENCILWRITEMASK, s->stencil_write_mask);
	ss_rs(D3DRS_STENCILFAIL, stencil_operation(s->stencil_fail));
	ss_rs(D3DRS_STENCILZFAIL, stencil_operation(s->stencil_zfail));
	ss_rs(D3DRS_STENCILPASS, stencil_operation(s->stencil_pass));
	ss_rs(D3DRS_CULLMODE, s->cull == 0x900 ? D3DCULL_CW : s->cull == 0x901 ? D3DCULL_CCW : D3DCULL_NONE);
	{
		static int test = -1;
		if (test < 0) test = h2n_env_int("H2N_GFXTEST", 0);
		if (test & 1) ss_rs(D3DRS_ZENABLE, D3DZB_FALSE);
		if (test & 2) ss_rs(D3DRS_STENCILENABLE, FALSE);
		if (test & 4) ss_rs(D3DRS_CULLMODE, D3DCULL_NONE);
	}
	ss_rs(D3DRS_FILLMODE, s->fill == 0x1b00 ? D3DFILL_POINT : s->fill == 0x1b01 ? D3DFILL_WIREFRAME : D3DFILL_SOLID);
	ss_rs(D3DRS_SHADEMODE, s->shade == 0x1d00 ? D3DSHADE_FLAT : D3DSHADE_GOURAUD);
	if (s->scissor_on)
	{
		RECT r;
		r.left = s->scissor[0] * (LONG)g_scale; r.top = s->scissor[1] * (LONG)g_scale; r.right = s->scissor[2] * (LONG)g_scale; r.bottom = s->scissor[3] * (LONG)g_scale;
		ss_scissor(&r);
	}
	ss_rs(D3DRS_SCISSORTESTENABLE, s->scissor_on != 0);
}

/* ================================================================ drawing ==== */
static float *g_vertices;
static unsigned g_remap_stamp[65536], g_remap_generation, g_remap_capacity, g_remapped_draws;   /* (s10f) see gx_draw */
static double g_converted_vertices, g_range_vertices;                 /* (s10f) a frame's vertices converted, and what the draws' index ranges span */
static unsigned short g_remap_index[65536], *g_remap_order;
static unsigned g_vertex_capacity;
static unsigned short *g_indices;
static unsigned g_index_capacity;
static unsigned g_draws, g_draws_skipped;

static void decode_attribute(float *out, const unsigned char *p, unsigned type)
{
	unsigned count = type >> 4, kind = type & 15, i;
	float f[4];
	short h[4];
	out[0] = 0.0f; out[1] = 0.0f; out[2] = 0.0f; out[3] = 1.0f;
	switch (kind)
	{
	case 2:
		if (count == 7) { memcpy(f, p, 12); out[0] = f[0]; out[1] = f[1]; out[3] = f[2]; }
		else { if (count > 4) count = 4; memcpy(f, p, count * 4); for (i = 0; i < count; i++) out[i] = f[i]; }
		break;
	case 0: out[0] = p[2] / 255.0f; out[1] = p[1] / 255.0f; out[2] = p[0] / 255.0f; out[3] = p[3] / 255.0f; break;
	case 5: if (count > 4) count = 4; memcpy(h, p, count * 2); for (i = 0; i < count; i++) out[i] = (float)h[i]; break;
	case 1: if (count > 4) count = 4; memcpy(h, p, count * 2); for (i = 0; i < count; i++) out[i] = h[i] < -32767 ? -1.0f : h[i] / 32767.0f; break;
	case 4: if (count > 4) count = 4; for (i = 0; i < count; i++) out[i] = p[i] / 255.0f; break;
	case 6:
	{
		unsigned v;
		int x, y, z;
		memcpy(&v, p, 4);
		x = (int)(v << 21) >> 21; y = (int)(v << 10) >> 21; z = (int)v >> 22;
		out[0] = x < -1023 ? -1.0f : x / 1023.0f; out[1] = y < -1023 ? -1.0f : y / 1023.0f; out[2] = z < -511 ? -1.0f : z / 511.0f;
		break;
	}
	}
}

/* for a frame that is being described (F11, H2N_GFXFRAME): what a draw changed in the surface it went into. The surface is
   read back before and after; the count of changed pixels, the box they lie in and one of them go to the log, so that two
   frames that should be the same can be compared draw by draw. */
static IDirect3DSurface9 *g_read_surface[2];
static unsigned g_read_width, g_read_height;
static int read_target(int which)
{
	target_entry *e = g_color_target;
	if (!e || !e->surface) return 0;
	if (g_read_width != e->real_width || g_read_height != e->real_height)
	{
		int i;
		for (i = 0; i < 2; i++) { if (g_read_surface[i]) IDirect3DSurface9_Release(g_read_surface[i]); g_read_surface[i] = NULL; }
		g_read_width = e->real_width; g_read_height = e->real_height;
		for (i = 0; i < 2; i++)
			if (FAILED(IDirect3DDevice9_CreateOffscreenPlainSurface(g_device, g_read_width, g_read_height, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &g_read_surface[i], NULL))) { g_read_surface[i] = NULL; g_read_width = 0; return 0; }
	}
	return SUCCEEDED(IDirect3DDevice9_GetRenderTargetData(g_device, e->surface, g_read_surface[which]));
}
static void describe_change(void)
{
	D3DLOCKED_RECT before, after;
	unsigned x, y, changed = 0, x0 = ~0u, y0 = ~0u, x1 = 0, y1 = 0, sample_before = 0, sample_after = 0, sample_x = 0, sample_y = 0, upper = 0, lower = 0;
	if (!g_read_surface[0] || !g_read_surface[1]) return;
	if (FAILED(IDirect3DSurface9_LockRect(g_read_surface[0], &before, NULL, D3DLOCK_READONLY))) return;
	if (FAILED(IDirect3DSurface9_LockRect(g_read_surface[1], &after, NULL, D3DLOCK_READONLY))) { IDirect3DSurface9_UnlockRect(g_read_surface[0]); return; }
	for (y = 0; y < g_read_height; y++)
	{
		const unsigned *a = (const unsigned *)((const unsigned char *)before.pBits + y * before.Pitch), *b = (const unsigned *)((const unsigned char *)after.pBits + y * after.Pitch);
		for (x = 0; x < g_read_width; x++)
			if (a[x] != b[x])
			{
				if (!changed || (x >= g_read_width / 3 && x < g_read_width * 2 / 3 && y >= g_read_height / 3 && y < g_read_height * 2 / 3 && !(sample_x >= g_read_width / 3 && sample_x < g_read_width * 2 / 3 && sample_y >= g_read_height / 3 && sample_y < g_read_height * 2 / 3)))
					{ sample_before = a[x]; sample_after = b[x]; sample_x = x; sample_y = y; }
				changed++;
				if (x < x0) x0 = x;
				if (x > x1) x1 = x;
				if (y < y0) y0 = y;
				if (y > y1) y1 = y;
				/* which side of the diagonal from the top left to the bottom right corner: a draw made of two triangles that changes one side only shows here */
				if ((unsigned long long)x * g_read_height > (unsigned long long)y * g_read_width) upper++; else lower++;
			}
	}
	IDirect3DSurface9_UnlockRect(g_read_surface[1]); IDirect3DSurface9_UnlockRect(g_read_surface[0]);
	if (!changed) h2n_log("            changed nothing in the surface\n");
	else h2n_log("            changed %u pixels in (%u,%u)-(%u,%u) of %ux%u; above the diagonal %u, below %u; at (%u,%u) %08x -> %08x\n", changed, x0, y0, x1, y1, g_read_width, g_read_height, upper, lower,
		sample_x, sample_y, sample_before, sample_after);
}

static unsigned g_draws_lazy, g_self_reads;
/* H2Classic, the Widescreen Fix (xd3d.cpp, h2n_hud_*): what is drawn into this surface is narrowed by k about the x pivot,
   in the console's screen coordinates - folded into the constants that map them back to clip space (c192 / c193) */
static const unsigned char *g_sq_target;
static float g_sq_pivot, g_sq_k = 1.0f;
static unsigned g_sq_draws;
void __cdecl gx_squeeze(const unsigned char *target, float pivot, float k) { g_sq_target = target; g_sq_pivot = pivot; g_sq_k = k; }
/* ================================================================ a player's view: its 3D, then its HUD ==== */
/* H2Classic, anti-aliasing (gfx_aa.c): the game draws each player's view with function_2ba10 (unknown_023540.cpp): its 3D by
   function_13680, then its HUD and windows. h2n_scene_13680 stands in that one call (scripts/scene_hook.py points the call
   in function_2ba10 at it) and runs it between gx_scene_begin and gx_scene_end. The view's 3D draws are recognised in
   between (w-buffered and depth-tested, into a surface of the console's whole picture), shifted by the frame's sub-pixel
   offset when the anti-aliasing is temporal, and the view is anti-aliased once they are done, before the HUD. */
static int g_scene_on;                     /* between the two */
static target_entry *g_scene_color, *g_scene_depth;   /* what the view's 3D was drawn into (its first 3D draw) */
static unsigned g_scene_viewport[4];       /* that draw's viewport, in the console's pixels */
static int g_scene_first_person;           /* the depth was copied before the first-person draws in this view (taa) */
static unsigned g_scene_jittered;          /* 3D draws shifted, all views */
static const unsigned char *g_presented[4];/* the surfaces the game has shown (its back buffers): a view's 3D is drawn straight into one */
static unsigned g_scene_first_draw;       /* (for the log) */
static int presented(const unsigned char *memory)
{
	unsigned i;
	for (i = 0; i < 4; i++) if (g_presented[i] == memory) return 1;
	return 0;
}

static int scene_view(aa_view *v)
{
	target_entry *c = g_scene_color, *d = g_scene_depth;
	unsigned x0 = g_scene_viewport[0], y0 = g_scene_viewport[1], w = g_scene_viewport[2], h = g_scene_viewport[3];
	if (!c || !c->texture || !c->surface || !w || !h || x0 >= c->width || y0 >= c->height) return 0;
	if (x0 + w > c->width) w = c->width - x0;
	if (y0 + h > c->height) h = c->height - y0;
	memset(v, 0, sizeof(*v));
	v->color = c->texture; v->color_surface = c->surface;
	v->full_width = c->real_width; v->full_height = c->real_height;
	if (d && d->texture) { v->depth = d->texture; v->depth_width = d->real_width; v->depth_height = d->real_height; }
	v->x = x0 * g_scale; v->y = y0 * g_scale; v->width = w * g_scale; v->height = h * g_scale;
	v->half_width = w * 0.5f; v->half_height = h * 0.5f;
	return 1;
}
void gx_scene_begin(void)
{
	g_scene_on = 1; g_scene_color = NULL; g_scene_depth = NULL; g_scene_first_person = 0; g_scene_first_draw = g_draws + 1;
	aa_scene_begin();
}
static unsigned g_scene_notes;              /* the first views, described in the log (where their 3D went) */
void gx_scene_end(void)
{
	aa_view v;
	if (g_scene_on && g_scene_notes < 4 && aa_mode() != AA_OFF)
	{
		g_scene_notes++;
		if (!g_scene_color) note("anti-aliasing: view %u had no 3D draw", g_scene_notes);
		else note("anti-aliasing: view %u: its 3D was drawn into the game's memory %p (%ux%u), viewport %u %u %u %u, depth %p%s, frame %u, draws %u-%u",
			g_scene_notes, g_scene_color->memory, g_scene_color->width, g_scene_color->height, g_scene_viewport[0], g_scene_viewport[1], g_scene_viewport[2], g_scene_viewport[3],
			g_scene_depth ? g_scene_depth->memory : NULL, g_scene_depth && g_scene_depth->texture ? " (readable)" : "", g_frame + 1, g_scene_first_draw, g_draws);
	}
	if (g_scene_on && g_scene_color && g_device && !g_lost && g_lazy_draw && aa_mode() != AA_OFF && scene_view(&v))
	{
		if (aa_scene(g_device, &v)) g_targets_changed = 1;
		ss_forget();          /* (gfx_aa.c sets the device's state itself: what the state cache holds is not the device's any more) */
	}
	g_scene_on = 0; g_scene_color = NULL; g_scene_depth = NULL;
}
/* the game's function_13680 (unknown_01cf50.cpp: void __fastcall function_13680(bool)) and the call that stands in it */
extern void __fastcall h2n_real_13680(int enabled) __asm__("?function_13680@@YIX_N@Z");
void __fastcall h2n_scene_13680(int enabled)
{
	gx_scene_begin();
	h2n_real_13680(enabled & 0xff);
	gx_scene_end();
}

/* ---- (sg2) wireframe as outlines ----
 * The console draws wireframe by outlining every triangle it is given. The game's meshes are triangle strips, joined
 * into one strip by triangles with no area (two corners the same, or three in a line), and its flat faces are two or
 * more triangles. So the console's wireframe has lines that are not edges of anything - the joins, running from the end
 * of one strip to the start of the next, often right across the model or back towards the camera (the user's example:
 * the lines off the SMG's sights) - and a diagonal across every flat face.
 * Here a wireframe draw is turned into the lines of its real edges:
 *  - corners are told apart by position (a mesh repeats a corner where its texture or shading splits), so an edge two
 *    triangles share is one edge however its corners are numbered, and is drawn once;
 *  - a triangle with two corners at the same place, or with no area, is left out: that is every join;
 *  - an edge between exactly two triangles that lie in one plane, on opposite sides of it, is left out: the diagonal
 *    of a flat face. Every other edge is drawn - a mesh's border, a fold, and an edge more than two triangles meet at.
 * "In one plane": their normals within about two degrees of each other, measured in the mesh's own (packed)
 * coordinates. H2N_WIREFRAME=triangles gives the console's every-triangle wireframe back. */
typedef struct { unsigned a, b, va, vb, count, draw; float n[3], d[3]; } wire_edge;
static wire_edge *g_wire_edges; static unsigned *g_wire_slots, *g_wire_points, *g_wire_same; static unsigned short *g_wire_lines;
static unsigned g_wire_edge_room, g_wire_slot_room, g_wire_point_room, g_wire_same_room, g_wire_line_room;
static int g_wire_outlines = -1;
static unsigned wire_outline(D3DPRIMITIVETYPE type, unsigned primitives, const unsigned short *index, const float *position, unsigned step, unsigned vertices)
{
	unsigned i, t, slots, points, edges = 0, lines = 0;
	if (g_wire_outlines < 0) { const char *w = getenv("H2N_WIREFRAME"); g_wire_outlines = !(w && !_stricmp(w, "triangles")); }
	if (!g_wire_outlines || !primitives || primitives > 400000 || !vertices || vertices > 65536) return ~0u;
	for (points = 16; points < vertices * 2; points *= 2) ;
	for (slots = 16; slots < primitives * 6; slots *= 2) ;
	if (points > g_wire_point_room) { g_wire_point_room = points; g_wire_points = realloc(g_wire_points, points * sizeof(unsigned)); }
	if (slots > g_wire_slot_room) { g_wire_slot_room = slots; g_wire_slots = realloc(g_wire_slots, slots * sizeof(unsigned)); }
	if (vertices > g_wire_same_room) { g_wire_same_room = vertices + 1024; g_wire_same = realloc(g_wire_same, g_wire_same_room * sizeof(unsigned)); }
	if (primitives * 3 > g_wire_edge_room) { g_wire_edge_room = primitives * 3 + 1024; g_wire_edges = realloc(g_wire_edges, g_wire_edge_room * sizeof(wire_edge)); }
	if (!g_wire_points || !g_wire_slots || !g_wire_same || !g_wire_edges) return ~0u;
	memset(g_wire_points, 0, points * sizeof(unsigned)); memset(g_wire_slots, 0, slots * sizeof(unsigned));
	/* corners at the same place are one corner */
	for (i = 0; i < vertices; i++)
	{
		const unsigned *w = (const unsigned *)(position + (size_t)i * step);
		unsigned h = ((w[0] * 2654435761u) ^ (w[1] * 2246822519u) ^ (w[2] * 3266489917u)) & (points - 1);
		for (;;)
		{
			unsigned other = g_wire_points[h];
			if (!other) { g_wire_points[h] = i + 1; g_wire_same[i] = i; break; }
			if (!memcmp(position + (size_t)(other - 1) * step, w, 12)) { g_wire_same[i] = other - 1; break; }
			h = (h + 1) & (points - 1);
		}
	}
	for (t = 0; t < primitives; t++)
	{
		unsigned v[3], c[3], k;
		double p[3][3], e1[3], e2[3], n[3], length;
		if (type == D3DPT_TRIANGLELIST) { v[0] = t * 3; v[1] = t * 3 + 1; v[2] = t * 3 + 2; }
		else if (type == D3DPT_TRIANGLESTRIP) { v[0] = t; v[1] = t + 1; v[2] = t + 2; }
		else { v[0] = 0; v[1] = t + 1; v[2] = t + 2; }
		for (k = 0; k < 3; k++) { if (index) v[k] = index[v[k]]; if (v[k] >= vertices) return ~0u; c[k] = g_wire_same[v[k]]; }
		if (c[0] == c[1] || c[1] == c[2] || c[0] == c[2]) continue;                       /* a join of two strips: no triangle */
		for (k = 0; k < 3; k++) { const float *q = position + (size_t)c[k] * step; p[k][0] = q[0]; p[k][1] = q[1]; p[k][2] = q[2]; }
		for (k = 0; k < 3; k++) { e1[k] = p[1][k] - p[0][k]; e2[k] = p[2][k] - p[0][k]; }
		n[0] = e1[1] * e2[2] - e1[2] * e2[1]; n[1] = e1[2] * e2[0] - e1[0] * e2[2]; n[2] = e1[0] * e2[1] - e1[1] * e2[0];
		length = n[0] * n[0] + n[1] * n[1] + n[2] * n[2];
		if (!(length > 0.0)) continue;                                                    /* three corners in a line: no area */
		length = 1.0 / __builtin_sqrt(length);
		for (k = 0; k < 3; k++) n[k] *= length;
		for (k = 0; k < 3; k++)
		{
			unsigned a = c[k], b = c[(k + 1) % 3], third = (k + 2) % 3, h, j;
			double along[3], out[3], squared = 0.0, dot = 0.0;
			wire_edge *e;
			/* the way from the edge to the triangle's third corner, square to the edge: which side of it the triangle is on */
			for (j = 0; j < 3; j++) { along[j] = p[(k + 1) % 3][j] - p[k][j]; squared += along[j] * along[j]; }
			for (j = 0; j < 3; j++) dot += (p[third][j] - p[k][j]) * along[j];
			for (j = 0; j < 3; j++) out[j] = (p[third][j] - p[k][j]) - along[j] * (squared > 0.0 ? dot / squared : 0.0);
			if (a > b) { unsigned swap = a; a = b; b = swap; }
			h = ((a * 2654435761u) ^ (b * 2246822519u)) & (slots - 1);
			for (;;)
			{
				unsigned other = g_wire_slots[h];
				if (!other)
				{
					e = &g_wire_edges[edges]; g_wire_slots[h] = ++edges;
					e->a = a; e->b = b; e->va = v[k]; e->vb = v[(k + 1) % 3]; e->count = 1; e->draw = 0;
					for (j = 0; j < 3; j++) { e->n[j] = (float)n[j]; e->d[j] = (float)out[j]; }
					break;
				}
				e = &g_wire_edges[other - 1];
				if (e->a == a && e->b == b)
				{
					double same = e->n[0] * n[0] + e->n[1] * n[1] + e->n[2] * n[2], sides = e->d[0] * out[0] + e->d[1] * out[1] + e->d[2] * out[2];
					if (++e->count > 2 || (same < 0.9994 && same > -0.9994) || !(sides < 0.0)) e->draw = 1;   /* a third triangle; a fold; two on the same side */
					break;
				}
				h = (h + 1) & (slots - 1);
			}
		}
	}
	if (edges * 2 > g_wire_line_room) { g_wire_line_room = edges * 2 + 4096; g_wire_lines = realloc(g_wire_lines, g_wire_line_room * sizeof(unsigned short)); }
	if (!g_wire_lines) return ~0u;
	for (i = 0; i < edges; i++)
		if (g_wire_edges[i].count == 1 || g_wire_edges[i].draw) { g_wire_lines[lines++] = (unsigned short)g_wire_edges[i].va; g_wire_lines[lines++] = (unsigned short)g_wire_edges[i].vb; }
	return lines;
}

static void gx_draw_inner(const gx_state *s, unsigned primitive, unsigned count, const unsigned short *indices, unsigned first);
void gx_draw(const gx_state *s, unsigned primitive, unsigned count, const unsigned short *indices, unsigned first)
{
	gx_u64 t0 = tsc();
	g_ct_at = t0; g_ct_nested = 0;
	gx_draw_inner(s, primitive, count, indices, first);
	g_ct[CT_ALL_GX] += tsc() - t0;
}
static void gx_draw_inner(const gx_state *s, unsigned primitive, unsigned count, const unsigned short *indices, unsigned first)
{
	vertex_shader *vs;
	pixel_shader *ps;
	pixel_key key;
	IDirect3DBaseTexture9 *textures[4];
	unsigned i, low = first, high = first + count - 1, vertices, a, primitives, index_count = 0, remap;
	float helper[VC_HELPERS][4], pixel_constants[PC_COUNT][4];
	D3DPRIMITIVETYPE type;
	int use_indices = indices != NULL, depth_tested, w_buffer, clamp, read_before = 0, scene3d;
	float jitter_x, jitter_y;
	HRESULT hr;
	static unsigned seen_primitives[12] = { ~0u, ~0u, ~0u, ~0u, ~0u, ~0u, ~0u, ~0u, ~0u, ~0u, ~0u, ~0u };

	if (!g_device || g_lost || !count) return;
	g_draws++;
	if (!g_lazy_draw) { g_draws_lazy++; return; }
	if (!s->program_selected)
	{
		static int told;
		if (!told) { told = 1; note("something is drawn with the console's fixed vertex pipeline (vertex format %#x); that is not drawn yet", s->fvf); }
		g_draws_skipped++;
		return;
	}
	if (!set_targets(s)) { g_draws_skipped++; return; }
	ct_mark(CT_TARGETS);
	vs = vertex_shader_for(s->program, s->program_start);
	ct_mark(CT_SHADERS);
	if (!vs->shader) { g_draws_skipped++; return; }

	/* the pixel side */
	memset(&key, 0, sizeof(key));
	key.combiners = s->combiners != 0;
	for (i = 0; i < 4; i++)
	{
		int kind = 0;
		textures[i] = s->texture[i].memory ? texture_for(&s->texture[i], &kind) : NULL;
		key.kind[i] = (unsigned char)(textures[i] ? kind : 0);
		key.texel_addressed[i] = (unsigned char)(textures[i] && s->texture[i].linear);
		key.alpha_kill[i] = (unsigned char)(textures[i] && s->sampler[i].alpha_kill);
		if (textures[i] && depth_format_bits(s->texture[i].format) && s->shadow_func >= 0x200 && s->shadow_func <= 0x207)
		{
			static unsigned told;
			key.shadow[i] = (unsigned char)((s->shadow_func - 0x200 + 1) | (depth_format_bits(s->texture[i].format) == 16 ? 0x10 : 0));   /* (s10k) */
			if (told++ < 3) note("a light's shadow map (format %#x, %ux%u) read by stage %u and compared (%#x) in frame %u", s->texture[i].format, s->texture[i].width, s->texture[i].height, i, s->shadow_func, g_frame);
		}
	}
	ct_mark(CT_TEXTURES);
	if (s->combiners)
	{
		unsigned stages = s->combiner_count & 15;
		if (stages > 8) stages = 8;
		for (i = 0; i < stages; i++) { key.alpha_inputs[i] = s->alpha_inputs[i]; key.rgb_inputs[i] = s->rgb_inputs[i]; key.alpha_outputs[i] = s->alpha_outputs[i]; key.rgb_outputs[i] = s->rgb_outputs[i]; }
		key.final_abcd = s->final_abcd; key.final_efg = s->final_efg; key.combiner_count = s->combiner_count; key.texture_modes = s->texture_modes;
		key.dot_mapping = s->dot_mapping; key.input_texture = s->input_texture; key.compare_mode = s->compare_mode;
	}
	/* A draw that reads the surface it is drawing into (the menus tint the picture behind them that way, each pixel from itself).
	   On the console that is the same memory read and written; in Direct3D 9 a texture may not be drawn into while it is read,
	   and what a graphics card then gives is not defined. So the draw reads a copy of the surface as it was just before it. */
	for (i = 0; i < 4; i++)
		if (textures[i] && g_color_target && s->texture[i].memory == s->target.memory && (IDirect3DBaseTexture9 *)g_color_target->texture == textures[i])
		{
			target_entry *e = g_color_target;
			unsigned k;
			if (!e->copy)
			{
				if (FAILED(IDirect3DDevice9_CreateTexture(g_device, e->real_width, e->real_height, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &e->copy, NULL))) { e->copy = NULL; SCALED_FAILED(0); break; }
				if (FAILED(IDirect3DTexture9_GetSurfaceLevel(e->copy, 0, &e->copy_surface))) { IDirect3DTexture9_Release(e->copy); e->copy = NULL; e->copy_surface = NULL; break; }
			}
			if (FAILED(IDirect3DDevice9_StretchRect(g_device, e->surface, NULL, e->copy_surface, NULL, D3DTEXF_NONE))) break;
			for (k = i; k < 4; k++) if (textures[k] == (IDirect3DBaseTexture9 *)e->texture) textures[k] = (IDirect3DBaseTexture9 *)e->copy;
			g_self_reads++;
			if (FRAME_TRACE) h2n_log("       the next draw reads the surface it draws into: it is given a copy of the surface as it is now\n");
			break;
		}
	ct_mark(CT_SELFREAD);
	/* anti-aliasing: a 3D draw of the player's view being drawn (w-buffered, depth-tested, into a surface of the whole picture:
	   the game's back buffer, where the view's 3D goes, or a surface it draws first with the same depth buffer; all of them are
	   shifted by the same offset so that their depths agree). The view is the part of the back buffer its 3D was drawn into. */
	scene3d = g_scene_on && s->z_enable == 2 && s->depth.memory && s->target.width == g_width && s->target.height == g_height && g_color_target;
	{ static int told; if (s->z_enable && told < 30 && getenv("H2N_AADIAG")) { told++; h2n_log("       AADIAG: scene %d z %u depth %p target %p %ux%u (want %ux%u) colour %p presented %d\n", g_scene_on, (unsigned)s->z_enable, (void *)s->depth.memory, (void *)s->target.memory, (unsigned)s->target.width, (unsigned)s->target.height, (unsigned)g_width, (unsigned)g_height, (void *)g_color_target, presented(s->target.memory)); } }
	if (scene3d)
	{
		if (!g_scene_color && presented(s->target.memory))
		{
			g_scene_color = g_color_target; g_scene_depth = g_depth_target;
			g_scene_viewport[0] = s->viewport_x; g_scene_viewport[1] = s->viewport_y; g_scene_viewport[2] = s->viewport_width; g_scene_viewport[3] = s->viewport_height;
			aa_scene_constants(s->constants);
		}
		/* the game draws the first-person arms and weapon between g_485a76 = 1 and 0 (unknown_0350e0.cpp, function_2bcd0): the depth
		   as it is just before them tells their pixels apart afterwards (they move with the camera) */
		if (!g_scene_first_person && (aa_mode() == AA_TAA || aa_mode() == AA_DLSS) && *(volatile const unsigned char *)H2N_VA(0x485a76, 0x492296) && g_scene_color && g_color_target == g_scene_color)
		{
			aa_view v;
			g_scene_first_person = 1;
			if (scene_view(&v)) { aa_first_person(g_device, &v); g_targets_changed = 1; ss_forget(); }
		}
	}
	if (g_targets_changed) { g_targets_changed = 0; if (!set_targets(s)) { g_draws_skipped++; return; } }     /* a depth buffer was copied for reading on the way */
	ct_mark(CT_TARGETS);
	/* the depth: tested at all? the w of the position (the game's choice everywhere) or its z? clamped to the range, or cut off at it? */
	depth_tested = s->z_enable && s->depth.memory;
	w_buffer = s->z_enable == 2;
	clamp = (s->depth_clip & 0x10) != 0;
	if (s->z_enable == 1) { static int told; if (!told) { told = 1; note("something is drawn with the plain z buffer; everything so far used the w buffer"); } }
	if (s->depth_clip & ~0x10u) { static unsigned seen[6] = { ~0u, ~0u, ~0u, ~0u, ~0u, ~0u }; if (note_once(seen, 6, s->depth_clip)) note("depth clip control %#x: only its clamp bit (0x10) is acted on", s->depth_clip); }
	key.depth = g_depth_mode && depth_tested ? (w_buffer ? 1u : 2u) | (s->offset_on ? 4u : 0u) : 0;
	key.logic = s->logic_op == 0x150f;
	if (s->logic_op && s->logic_op != 0x150f) { static unsigned seen[4] = { ~0u, ~0u, ~0u, ~0u }; if (note_once(seen, 4, s->logic_op)) note("logic operation %#x: only SET (0x150f) is acted on", s->logic_op); }
	ps = pixel_shader_for(&key);
	ct_mark(CT_SHADERS);
	if (!ps->shader) { g_draws_skipped++; return; }

	/* the vertices: every attribute the program reads, as four floats */
	if (use_indices)
	{
		low = 0xffff; high = 0;
		for (i = 0; i < count; i++) { if (indices[i] < low) low = indices[i]; if (indices[i] > high) high = indices[i]; }
	}
	vertices = high - low + 1;
	/* (s10f) a draw whose indices use only some of the vertices between their lowest and highest (a level's large shared
	   vertex buffers: the user's Outskirts rooftops, 1,285 draws and 20 ms a frame here) gets only the vertices it uses
	   converted and handed over, numbered in the order they first appear; what is drawn is the same */
	g_range_vertices += vertices;
	remap = use_indices && vertices > count + 64;
	if (remap)
	{
		if (++g_remap_generation == 0) { memset(g_remap_stamp, 0, sizeof(g_remap_stamp)); g_remap_generation = 1; }
		if (count > g_remap_capacity) { g_remap_capacity = count + 4096; g_remap_order = realloc(g_remap_order, g_remap_capacity * sizeof(unsigned short)); }
		vertices = 0;
		for (i = 0; i < count; i++)
		{
			unsigned v = indices[i];
			if (g_remap_stamp[v] != g_remap_generation)
			{
				g_remap_stamp[v] = g_remap_generation; g_remap_index[v] = (unsigned short)vertices; g_remap_order[vertices++] = (unsigned short)v;
			}
		}
		g_remapped_draws++;
	}
	g_converted_vertices += vertices;
	if (vertices * vs->stride / 4 > g_vertex_capacity) { g_vertex_capacity = vertices * vs->stride / 4 + 4096; g_vertices = realloc(g_vertices, g_vertex_capacity * sizeof(float)); }
	for (a = 0; a < 16; a++)
	{
		const gx_attribute *attribute = &s->attribute[a];
		float *out;
		if (!(vs->inputs & (1u << a))) continue;
		out = g_vertices + vs->slot[a] / 4;
		if (!attribute->data)
			for (i = 0; i < vertices; i++, out += vs->stride / 4) memcpy(out, s->current[a], 16);
		else if (attribute->type == 0x42)
			for (i = 0; i < vertices; i++, out += vs->stride / 4) memcpy(out, attribute->data + (size_t)(remap ? g_remap_order[i] : low + i) * attribute->stride, 16);
		else
			for (i = 0; i < vertices; i++, out += vs->stride / 4) decode_attribute(out, attribute->data + (size_t)(remap ? g_remap_order[i] : low + i) * attribute->stride, attribute->type);
	}

	/* the primitives: quads become triangles, everything else has a Direct3D 9 equivalent */
	switch (primitive)
	{
	case 1: type = D3DPT_POINTLIST; primitives = count; break;
	case 2: type = D3DPT_LINELIST; primitives = count / 2; break;
	case 3: case 4: type = D3DPT_LINESTRIP; primitives = count - 1; break;
	case 5: type = D3DPT_TRIANGLELIST; primitives = count / 3; break;
	case 6: case 9: type = D3DPT_TRIANGLESTRIP; primitives = count - 2; break;
	case 7: case 10: type = D3DPT_TRIANGLEFAN; primitives = count - 2; break;
	case 8: type = D3DPT_TRIANGLELIST; primitives = count / 4 * 2; break;
	default:
		if (note_once(seen_primitives, 12, primitive)) note("primitive type %u is not drawn", primitive);
		g_draws_skipped++;
		return;
	}
	if (count < (type == D3DPT_POINTLIST ? 1u : type == D3DPT_LINELIST || type == D3DPT_LINESTRIP ? 2u : 3u) || !primitives) return;
	if (use_indices || primitive == 8 || primitive == 3)
	{
		unsigned needed = primitive == 8 ? count / 4 * 6 : count + 1;
		if (needed > g_index_capacity) { g_index_capacity = needed + 4096; g_indices = realloc(g_indices, g_index_capacity * 2); }
#define INDEX(n) ((unsigned short)(remap ? g_remap_index[indices[n]] : (use_indices ? indices[n] : first + (n)) - low))
		if (primitive == 8)
			for (i = 0; i + 3 < count; i += 4)
			{
				g_indices[index_count++] = INDEX(i); g_indices[index_count++] = INDEX(i + 1); g_indices[index_count++] = INDEX(i + 2);
				g_indices[index_count++] = INDEX(i); g_indices[index_count++] = INDEX(i + 2); g_indices[index_count++] = INDEX(i + 3);
			}
		else
		{
			for (i = 0; i < count; i++) g_indices[index_count++] = INDEX(i);
			if (primitive == 3) { g_indices[index_count++] = INDEX(0); primitives = count; }
		}
#undef INDEX
		use_indices = 1;
	}

	ct_mark(CT_VERTICES);
	apply_state(s);
	ss_vs(vs->shader);
	ss_declaration(vs->declaration);
	ss_ps(ps->shader);
	ss_constants(0, 0, &s->constants[0][0], 192);
	memset(pixel_constants, 0, sizeof(pixel_constants));
	{
		/* the program ends in the console's screen coordinates: x and y in pixels, z in the depth buffer's units, w as it was.
		   x and y go back to what Direct3D 9 takes (the viewport undone; half a pixel taken off, see g_half). */
		float zmax = s->depth_max > 0.0f ? s->depth_max : 16777215.0f, low_depth = s->clip_min, high_depth = s->clip_max;
		float half_width = s->viewport_width * 0.5f, half_height = s->viewport_height * 0.5f, p, q, r;
		const float tolerance = 1.0f / 2097152.0f;      /* 2^-21: eight steps of a 24-bit depth, for the rounding of a few operations on 32-bit numbers */
		helper[0][0] = 1.0f / half_width; helper[0][1] = -1.0f / half_height; helper[0][2] = 1.0f; helper[0][3] = 0.0f;
		helper[1][0] = -(s->viewport_x + half_width) / half_width + g_half / (g_scale * half_width);
		helper[1][1] = (s->viewport_y + half_height) / half_height - g_half / (g_scale * half_height);
		helper[1][2] = 0.0f; helper[1][3] = 0.0f;
		if (g_sq_target && s->target.memory == g_sq_target && g_sq_k != 1.0f)
		{
			/* x -> pivot + (x - pivot) k before the mapping x * c192.x + c193.x */
			helper[1][0] += g_sq_pivot * (1.0f - g_sq_k) / half_width;
			helper[0][0] *= g_sq_k;
			g_sq_draws++;
		}
		/* anti-aliasing (taa): the view's 3D moved by this frame's sub-pixel offset (Playdead's FrustumJitter moves the frustum by
		   the sample, which moves the picture the other way: x by -sample, y by -sample in texture coordinates, which run down) */
		if (scene3d && aa_jitter(&jitter_x, &jitter_y))
		{
			helper[1][0] -= jitter_x / (g_scale * half_width);
			helper[1][1] += jitter_y / (g_scale * half_height);
			g_scene_jittered++;
		}
		helper[2][0] = 0.0f; helper[2][1] = 1.0f; helper[2][2] = 0.5f; helper[2][3] = 2.0f;
		helper[3][0] = 5.42101e-20f; helper[3][1] = 1.884467e19f; helper[3][2] = 0.001f; helper[3][3] = 3.402823466e38f;   /* .w: FLT_MAX, for LOG and EXP */
		/* The console's GPU does not cut triangles at a near and a far plane. For each pixel it takes the depth (w, or z) and
		   either drops the pixel when that is outside the range the game set (SetDepthClipPlanes; otherwise all of the depth
		   buffer's range), or, with the clamp bit of the depth clip control, holds it to the range. It drops what is behind the
		   eye (w below zero) in both cases. Direct3D 9 cuts triangles where its own z leaves 0..w, so that z is made to say the
		   same thing: z = (z*w)*P + w*Q + R. The depth each pixel is given comes from the pixel shader (c34), not from this z. */
		if (!g_depth_mode) { p = 1.0f / zmax; q = 0.0f; r = 0.0f; }                 /* stage 8: the program's z over the depth buffer's range */
		else if (clamp) { p = 0.0f; q = 0.5f; r = -tolerance * (w_buffer ? zmax : 1.0f); } /* nothing is cut off but what is behind the eye (or all but touching it) */
		else
		{
			float low = low_depth * (1.0f - tolerance), high = high_depth * (1.0f + tolerance);
			if (w_buffer && low < tolerance * zmax) low = tolerance * zmax;        /* Direct3D 9 cannot cut at w = 0 itself */
			if (!(high > low)) { p = 0.0f; q = 0.0f; r = -1.0f; }                   /* an empty range: nothing is drawn */
			else if (w_buffer) { p = 0.0f; q = high / (high - low); r = -low * high / (high - low); }    /* 0 at w = low, w at w = high */
			else { p = 1.0f / (high - low); q = -low / (high - low); r = 0.0f; }                        /* 0 at z = low, w at z = high */
		}
		helper[4][0] = p; helper[4][1] = q; helper[4][2] = r; helper[4][3] = 1.0f / zmax;       /* .w: what turns w into a part of the depth buffer's range */
		ss_constants(0, VC_SCALE, &helper[0][0], VC_HELPERS);
		pixel_constants[PC_DEPTH][0] = 1.0f / zmax; pixel_constants[PC_DEPTH][1] = low_depth / zmax; pixel_constants[PC_DEPTH][2] = high_depth / zmax;
		pixel_constants[PC_DEPTH][3] = s->offset_units / zmax;
		pixel_constants[PC_DEPTH + 1][0] = s->offset_slope * (float)g_scale;        /* the slope is per pixel of the console */
		depth_bytes_constants(&pixel_constants[PC_DEPTH_BYTES]);
	}
	for (i = 0; i < 8; i++) { color_vector(s->constant0[i], pixel_constants[PC_CONSTANT0 + i]); color_vector(s->constant1[i], pixel_constants[PC_CONSTANT1 + i]); }
	color_vector(s->final_constant0, pixel_constants[PC_FINAL0]); color_vector(s->final_constant1, pixel_constants[PC_FINAL1]); color_vector(s->fog_color, pixel_constants[PC_FOG]);
	pixel_constants[PC_NUMBERS][0] = 0.0f; pixel_constants[PC_NUMBERS][1] = 1.0f; pixel_constants[PC_NUMBERS][2] = 0.5f; pixel_constants[PC_NUMBERS][3] = 2.0f;
	pixel_constants[PC_MORE][0] = 127.5f; pixel_constants[PC_MORE][1] = 1.0f / 512.0f; pixel_constants[PC_MORE][2] = 65280.0f / 65535.0f; pixel_constants[PC_MORE][3] = 255.0f / 65535.0f;
	pixel_constants[PC_SHADOW][0] = 16777215.0f; pixel_constants[PC_SHADOW][1] = 65535.0f; pixel_constants[PC_SHADOW][2] = 0.0f; pixel_constants[PC_SHADOW][3] = 0.0f;   /* (s10k) a depth buffer's largest value */
	for (i = 0; i < 4; i++)
	{
		const gx_sampler *sampler = &s->sampler[i];
		pixel_constants[PC_TEXEL + i][0] = s->texture[i].width ? 1.0f / s->texture[i].width : 1.0f;
		pixel_constants[PC_TEXEL + i][1] = s->texture[i].height ? 1.0f / s->texture[i].height : 1.0f;
		pixel_constants[PC_TEXEL + i][2] = 1.0f; pixel_constants[PC_TEXEL + i][3] = 1.0f;
		memcpy(pixel_constants[PC_BUMP + i], sampler->bump, 16);
		pixel_constants[PC_LUMINANCE + i][0] = sampler->luminance_scale; pixel_constants[PC_LUMINANCE + i][1] = sampler->luminance_offset;
		ss_texture(i, textures[i]);
		if (!textures[i]) continue;
		ss_sampler(i, D3DSAMP_ADDRESSU, s->texture[i].linear ? D3DTADDRESS_CLAMP : address_mode(sampler->address_u));
		ss_sampler(i, D3DSAMP_ADDRESSV, s->texture[i].linear ? D3DTADDRESS_CLAMP : address_mode(sampler->address_v));
		ss_sampler(i, D3DSAMP_ADDRESSW, address_mode(sampler->address_w));
		if (key.shadow[i])                                                       /* (s10k) a shadow map: each compare on one stored depth */
		{
			ss_sampler(i, D3DSAMP_MAGFILTER, D3DTEXF_POINT); ss_sampler(i, D3DSAMP_MINFILTER, D3DTEXF_POINT); ss_sampler(i, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
		}
		else
		{
		ss_sampler(i, D3DSAMP_MAGFILTER, filter_mode(sampler->mag, 0) == D3DTEXF_ANISOTROPIC ? D3DTEXF_LINEAR : filter_mode(sampler->mag, 0));
		ss_sampler(i, D3DSAMP_MINFILTER, filter_mode(sampler->min, 0));
		ss_sampler(i, D3DSAMP_MIPFILTER, s->texture[i].levels > 1 ? filter_mode(sampler->mip, 1) : D3DTEXF_NONE);
		}
		ss_sampler(i, D3DSAMP_BORDERCOLOR, sampler->border_color);
		ss_sampler(i, D3DSAMP_MAXANISOTROPY, sampler->max_anisotropy ? sampler->max_anisotropy : 1);
	}
	ss_constants(1, 0, &pixel_constants[0][0], PC_COUNT);

	if (FRAME_TRACE) read_before = read_target(0);
	ct_mark(CT_STATE);
	{
		/* (sg2) wireframe: the real edges as lines (wire_outline) */
		const unsigned short *draw_indices = use_indices ? g_indices : NULL;
		unsigned outline = ~0u;
		if (s->fill == 0x1b01 && (type == D3DPT_TRIANGLELIST || type == D3DPT_TRIANGLESTRIP || type == D3DPT_TRIANGLEFAN) && (vs->inputs & 1))
			outline = wire_outline(type, primitives, draw_indices, g_vertices + vs->slot[0] / 4, vs->stride / 4, vertices);
		if (outline != ~0u) { type = D3DPT_LINELIST; primitives = outline / 2; draw_indices = g_wire_lines; }
#define ISSUE() (!primitives ? D3D_OK : draw_indices ? IDirect3DDevice9_DrawIndexedPrimitiveUP(g_device, type, 0, vertices, primitives, draw_indices, D3DFMT_INDEX16, g_vertices, vs->stride) \
		: IDirect3DDevice9_DrawPrimitiveUP(g_device, type, primitives, g_vertices, vs->stride))
		hr = ISSUE();
		/* (sg2) wireframe: a line is one pixel of the console's 640x480 picture wide. Direct3D 9 draws lines one pixel of
		   OUR picture wide, which at Render Scale x2 and up is a fraction of that (at x4 a quarter: hard to see). So the
		   same lines are drawn again moved one of our pixels at a time, to the right and down, until a line covers a
		   console pixel each way (the scale; 8 at most). Only the offset the vertex shader ends with changes; it is put
		   back after. At x1 nothing more is drawn. */
		if (SUCCEEDED(hr) && primitives && s->fill == 0x1b01 && g_scale > 1 && type != D3DPT_POINTLIST && s->viewport_width && s->viewport_height)
		{
			unsigned wide = g_scale > 8 ? 8 : (unsigned)g_scale, k, axis;
			float x0 = helper[1][0], y0 = helper[1][1];
			float px = 2.0f / ((float)s->viewport_width * (float)g_scale), py = 2.0f / ((float)s->viewport_height * (float)g_scale);
			for (axis = 0; axis < 2; axis++)
				for (k = 1; k < wide; k++)
				{
					helper[1][0] = x0 + (axis == 0 ? px * (float)k : 0.0f);
					helper[1][1] = y0 - (axis == 1 ? py * (float)k : 0.0f);
					ss_constants(0, VC_OFFSET, &helper[1][0], 1);
					ISSUE();
				}
			helper[1][0] = x0; helper[1][1] = y0;
			ss_constants(0, VC_OFFSET, &helper[1][0], 1);
		}
#undef ISSUE
	}
	ct_mark(CT_DRAWCALL);
	if (FAILED(hr)) { static int told; if (!told) { told = 1; note("a draw call was refused by Direct3D 9 (%08lx)", (unsigned long)hr); } g_draws_skipped++; }
	if (g_depth_target)
	{
		if (depth_tested && s->z_write) g_depth_target->stale = 1;
		stencil_track(g_depth_target, s);
	}
	if (FRAME_TRACE)
	{
		h2n_log("       draw %u: primitive %u, %u vertices%s, program %08x (start %u), combiners %08x (%u stages, modes %05x), target %p %ux%u depth %p, viewport %u %u %u %u\n", g_draws, primitive, count,
			indices ? " indexed" : "", vs->hash, s->program_start, ps->hash, s->combiner_count & 15, s->texture_modes, s->target.memory, s->target.width, s->target.height, s->depth.memory,
			s->viewport_x, s->viewport_y, s->viewport_width, s->viewport_height);
		h2n_log("            blend %u %x %x op %x, alpha test %u %x %u, z %u write %u func %x, cull %x, mask %x, stencil %u, c10 %g %g %g %g, c11 %g %g %g %g\n", s->blend, s->blend_source, s->blend_destination,
			s->blend_op, s->alpha_test, s->alpha_func, s->alpha_ref, s->z_enable, s->z_write, s->z_func, s->cull, s->color_mask, s->stencil,
			s->constants[10][0], s->constants[10][1], s->constants[10][2], s->constants[10][3], s->constants[11][0], s->constants[11][1], s->constants[11][2], s->constants[11][3]);
		h2n_log("            stencil func %x ref %x mask %x write %x fail %x zfail %x pass %x, z bias %u\n", s->stencil_func, s->stencil_ref, s->stencil_mask, s->stencil_write_mask, s->stencil_fail, s->stencil_zfail, s->stencil_pass, s->z_bias);
		h2n_log("            depth: %s, range %g..%g of %g, clip control %#x, offset %s slope %g units %g; scissor %s %d %d %d %d\n", !s->z_enable ? "not tested" : w_buffer ? "w" : "z", s->clip_min, s->clip_max, s->depth_max,
			s->depth_clip, s->offset_on ? "on" : "off", s->offset_slope, s->offset_units, s->scissor_on ? "on" : "off", s->scissor[0], s->scissor[1], s->scissor[2], s->scissor[3]);
		if (read_before && read_target(1)) describe_change();
		/* the console's own program and combiner setting, once for each in a run: with these a wrong draw can be worked out from the log alone */
		if (!vs->told)
		{
			unsigned n;
			vs->told = 1;
			h2n_log("            program %08x, %u instructions:", vs->hash, vs->length);
			for (n = 0; n < vs->length * 4 && n < 136 * 4; n++) h2n_log(" %08x", vs->key[n / 4][n % 4]);
			h2n_log("\n");
		}
		if (!ps->told)
		{
			unsigned n; const unsigned *w = (const unsigned *)&ps->key;
			ps->told = 1;
			h2n_log("            combiners %08x:", ps->hash);
			for (n = 0; n < sizeof(ps->key) / 4; n++) h2n_log(" %08x", w[n]);
			h2n_log("\n");
		}
		if (h2n_env_int("H2N_GFXCONST", 0))      /* a test: the vertex constants this draw is given (bones are 50 and up) */
		{
			int k;
			for (k = 0; k < 192; k++)
			{
				const float *v = s->constants[k];
				if ((k < 12 || (k >= 50 && k < 110) || k >= 168 || h2n_env_int("H2N_GFXCONST", 0) > 1) && (v[0] != 0 || v[1] != 0 || v[2] != 0 || v[3] != 0))
				{
					unsigned bits[4];
					memcpy(bits, v, 16);
					h2n_log("            c%d: %g %g %g %g  (%08x %08x %08x %08x)\n", k, v[0], v[1], v[2], v[3], bits[0], bits[1], bits[2], bits[3]);
				}
			}
		}
		for (i = 0; i < 4; i++)
			if (s->texture[i].memory)
				h2n_log("            texture %u: %p format %#x %ux%ux%u levels %u%s%s%s -> %s\n", i, s->texture[i].memory, s->texture[i].format, s->texture[i].width, s->texture[i].height, s->texture[i].depth,
					s->texture[i].levels, s->texture[i].linear ? " texel-addressed" : "", s->texture[i].cube ? " cube" : "", s->texture[i].volume ? " volume" : "",
					!textures[i] ? "not available" : target_find(s->texture[i].memory) ? "a surface the game drew" : "unpacked");
		for (a = 0; a < 16; a++)
			if (vs->inputs & (1u << a))
			{
				const float *f = g_vertices + vs->slot[a] / 4;
				h2n_log("            attribute %u (%s type %#x): %g %g %g %g | %g %g %g %g\n", a, s->attribute[a].data ? "stream" : "constant", s->attribute[a].type, f[0], f[1], f[2], f[3],
					f[vs->stride / 4], f[vs->stride / 4 + 1], f[vs->stride / 4 + 2], f[vs->stride / 4 + 3]);
			}
		{
			/* a test: H2N_GFXVERTS=<vertex program hash>: every vertex of the described draws that use that program, as the
			   program gets it (scripts/vpemu.py runs the program on them).
			   (s10r) And every vertex of a draw of letters (one that reads a text page), up to 6000 a frame: where each
			   letter's quad lies and which part of the page it shows */
			const char *want = getenv("H2N_GFXVERTS");
			int letters = 0;
			for (i = 0; i < 4; i++) if (text_page_note(&s->texture[i])) letters = 1;
			if (letters && g_text_vertex_lines + vertices > 6000) letters = 0;
			if (letters) g_text_vertex_lines += vertices;
			if (letters || (want && (unsigned)strtoul(want, NULL, 16) == vs->hash))
			{
				unsigned v;
				for (v = 0; v < vertices; v++)
				{
					h2n_log("            vertex %u:", v);
					for (a = 0; a < 16; a++)
						if (vs->inputs & (1u << a))
						{
							const float *f = g_vertices + v * (vs->stride / 4) + vs->slot[a] / 4;
							h2n_log(" v%u %.9g %.9g %.9g %.9g", a, f[0], f[1], f[2], f[3]);
						}
					h2n_log("\n");
				}
			}
		}
	}
}

/* some of a surface's four channels set to a colour: a rectangle over the surface (or the part named), drawn with the
 * colour write mask of those channels and nothing else in the way */
static void clear_channels(unsigned channels, unsigned color, const D3DRECT *r, const D3DVIEWPORT9 *whole)
{
	static const float corner[4][2] = { { -1.0f, 1.0f }, { 1.0f, 1.0f }, { -1.0f, -1.0f }, { 1.0f, -1.0f } };
	IDirect3DDevice9 *d = g_device;
	D3DVIEWPORT9 viewport = *whole;
	float quad[4][8], constant[4];
	unsigned i;
	if (!copy_shaders()) { static int told; if (!told) { told = 1; note("a clear of some colour channels only could not be drawn; it is left out"); } return; }
	if (r)
	{
		LONG x1 = r->x1 < 0 ? 0 : r->x1, y1 = r->y1 < 0 ? 0 : r->y1, x2 = r->x2 > (LONG)whole->Width ? (LONG)whole->Width : r->x2, y2 = r->y2 > (LONG)whole->Height ? (LONG)whole->Height : r->y2;
		if (x2 <= x1 || y2 <= y1) return;
		viewport.X = (DWORD)x1; viewport.Y = (DWORD)y1; viewport.Width = (DWORD)(x2 - x1); viewport.Height = (DWORD)(y2 - y1);
	}
	for (i = 0; i < 4; i++) { quad[i][0] = corner[i][0]; quad[i][1] = corner[i][1]; quad[i][2] = 0.5f; quad[i][3] = 1.0f; quad[i][4] = 0.0f; quad[i][5] = 0.0f; quad[i][6] = 0.0f; quad[i][7] = 1.0f; }
	color_vector(color, constant);
	ss_viewport(&viewport);
	ss_rs(D3DRS_ALPHATESTENABLE, FALSE); ss_rs(D3DRS_ALPHABLENDENABLE, FALSE);
	ss_rs(D3DRS_ZENABLE, D3DZB_FALSE); ss_rs(D3DRS_ZWRITEENABLE, FALSE);
	ss_rs(D3DRS_STENCILENABLE, FALSE); ss_rs(D3DRS_CULLMODE, D3DCULL_NONE);
	ss_rs(D3DRS_FILLMODE, D3DFILL_SOLID); ss_rs(D3DRS_SCISSORTESTENABLE, FALSE);
	ss_rs(D3DRS_COLORWRITEENABLE, (channels & 1 ? D3DCOLORWRITEENABLE_RED : 0) | (channels & 2 ? D3DCOLORWRITEENABLE_GREEN : 0) | (channels & 4 ? D3DCOLORWRITEENABLE_BLUE : 0) | (channels & 8 ? D3DCOLORWRITEENABLE_ALPHA : 0));
	ss_vs(g_copy_vs); ss_declaration(g_copy_declaration); ss_ps(g_copy_ps_flat);
	ss_constants(1, 0, constant, 1);
	for (i = 0; i < 4; i++) ss_texture(i, NULL);
	IDirect3DDevice9_DrawPrimitiveUP(d, D3DPT_TRIANGLESTRIP, 2, quad, sizeof(quad[0]));
	ss_rs(D3DRS_COLORWRITEENABLE, D3DCOLORWRITEENABLE_RED | D3DCOLORWRITEENABLE_GREEN | D3DCOLORWRITEENABLE_BLUE | D3DCOLORWRITEENABLE_ALPHA);
}

static void gx_clear_inner(const gx_state *s, unsigned flags, unsigned color, float z, unsigned stencil, const int *rectangle);
void gx_clear(const gx_state *s, unsigned flags, unsigned color, float z, unsigned stencil, const int *rectangle)
{
	gx_u64 t0 = tsc(), spent;
	g_ct_nested = 0;
	gx_clear_inner(s, flags, color, z, stencil, rectangle);
	spent = tsc() - t0;
	g_ct[CT_CLEAR] += spent; g_ct[CT_ALL_GX] += spent;
}
static void gx_clear_inner(const gx_state *s, unsigned flags, unsigned color, float z, unsigned stencil, const int *rectangle)
{
	/* the console clears red, green, blue and alpha each by its own flag (D3DCLEAR_TARGET_R, _G, _B, _A: 0x10 to 0x80); Direct3D 9
	   clears all four or none, whatever the colour write mask says, so a clear of some of them is drawn as a rectangle instead */
	unsigned channels = (flags >> 4) & 15;
	DWORD what = (channels == 15 ? D3DCLEAR_TARGET : 0) | (flags & 1 && s->depth.memory ? D3DCLEAR_ZBUFFER : 0) | (flags & 2 && s->depth.memory ? D3DCLEAR_STENCIL : 0);
	D3DVIEWPORT9 viewport;
	D3DRECT r;
	if (!g_device || g_lost || !(what || channels) || !g_lazy_draw || !set_targets(s)) return;
	if (FRAME_TRACE)
	{
		h2n_log("       clear: flags %x colour %08x z %g stencil %u, target %p %ux%u depth %p", flags, color, z, stencil, s->target.memory, s->target.width, s->target.height, s->depth.memory);
		if (rectangle) h2n_log(", rectangle %d %d %d %d", rectangle[0], rectangle[1], rectangle[2], rectangle[3]);
		h2n_log("\n");
	}
	viewport.X = 0; viewport.Y = 0; viewport.Width = s->target.width * g_scale; viewport.Height = s->target.height * g_scale; viewport.MinZ = 0.0f; viewport.MaxZ = 1.0f;
	ss_viewport(&viewport);
	ss_rs(D3DRS_SCISSORTESTENABLE, FALSE);
	int use_rect = rectangle != NULL;
	if (rectangle) { r.x1 = rectangle[0] * (LONG)g_scale; r.y1 = rectangle[1] * (LONG)g_scale; r.x2 = rectangle[2] * (LONG)g_scale; r.y2 = rectangle[3] * (LONG)g_scale; }
	else
	{
		/* s9l: no rectangle - the console's Direct3D clears the viewport, not the whole surface (as Direct3D 8 and 9 do). With
		   two players the game clears each player's view before drawing it; clearing the whole surface wiped the first
		   player's half (your co-op picture: player 1 all the clear colour, 0x475b63). */
		unsigned x0 = s->viewport_x, y0 = s->viewport_y, x1 = s->viewport_x + s->viewport_width, y1 = s->viewport_y + s->viewport_height;
		if (x1 > s->target.width) x1 = s->target.width;
		if (y1 > s->target.height) y1 = s->target.height;
		if (x0 < x1 && y0 < y1 && (x0 || y0 || x1 < s->target.width || y1 < s->target.height))
		{
			static int told;
			r.x1 = (LONG)(x0 * g_scale); r.y1 = (LONG)(y0 * g_scale); r.x2 = (LONG)(x1 * g_scale); r.y2 = (LONG)(y1 * g_scale);
			use_rect = 1;
			if (!told) { told = 1; note("a clear of part of a surface (the viewport %u %u %u %u): only that part is cleared, as on the console", x0, y0, x1 - x0, y1 - y0); }
		}
	}
	{ static int test = -1; if (test < 0) test = h2n_env_int("H2N_GFXTEST", 0); if (!color && (test & 8)) color = 0x00606060; }      /* a test: what is drawn black on black shows on grey */
	if (what) IDirect3DDevice9_Clear(g_device, use_rect ? 1 : 0, use_rect ? &r : NULL, what, color, z, stencil);
	if (channels && channels != 15) clear_channels(channels, color, use_rect ? &r : NULL, &viewport);
	if (g_depth_target && (what & (D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL)))
	{
		g_depth_target->stale = 1;
		if (what & D3DCLEAR_STENCIL)
		{
			if (!use_rect) { g_depth_target->stencil_count = 0; g_depth_target->stencil_any = 0; }
			stencil_value_add(g_depth_target, stencil);
		}
	}
}

/* ================================================================ the visibility test ==== */
/* The console counts the pixels that pass the depth test between a begin and an end, and gives the count for the
 * end's number some time later (the game draws a lens flare by how much of its light source came through). Direct3D 9
 * has the same thing as an occlusion query. The count is in the console's pixels: ours, over the render scale squared. */
#define VISIBILITY_SLOTS 16384
static struct { IDirect3DQuery9 *query; unsigned char state; unsigned pixels; } g_visibility[VISIBILITY_SLOTS];     /* state: 0 never asked, 1 waiting, 2 known */
static IDirect3DQuery9 *g_query_open, *g_query_spare[64];
static unsigned g_query_spares, g_visibility_begun, g_visibility_known, g_visibility_seen, g_visibility_unsupported;

static void query_put_back(IDirect3DQuery9 *q)
{
	if (g_query_spares < 64) g_query_spare[g_query_spares++] = q; else IDirect3DQuery9_Release(q);
}
static void gx_visibility_begin_inner(void);
void gx_visibility_begin(void)
{
	gx_u64 t0 = tsc(), spent;
	gx_visibility_begin_inner();
	spent = tsc() - t0; g_ct[CT_QUERIES] += spent; g_ct[CT_ALL_GX] += spent;
}
static void gx_visibility_begin_inner(void)
{
	g_visibility_begun++;
	if (!g_device || g_lost || g_visibility_unsupported) return;
	if (g_query_open) { IDirect3DQuery9_Issue(g_query_open, D3DISSUE_END); query_put_back(g_query_open); g_query_open = NULL; }     /* begun again: the count starts over */
	if (g_query_spares) g_query_open = g_query_spare[--g_query_spares];
	else if (FAILED(IDirect3DDevice9_CreateQuery(g_device, D3DQUERYTYPE_OCCLUSION, &g_query_open)))
	{
		g_query_open = NULL; g_visibility_unsupported = 1;
		note("this graphics device has no occlusion queries; every visibility test will say that nothing was visible (no lens flares)");
		return;
	}
	IDirect3DQuery9_Issue(g_query_open, D3DISSUE_BEGIN);
	if (FRAME_TRACE) h2n_log("       visibility test: begin\n");
}
static void gx_visibility_end_inner(unsigned index);
void gx_visibility_end(unsigned index)
{
	gx_u64 t0 = tsc(), spent;
	gx_visibility_end_inner(index);
	spent = tsc() - t0; g_ct[CT_QUERIES] += spent; g_ct[CT_ALL_GX] += spent;
}
static void gx_visibility_end_inner(unsigned index)
{
	if (FRAME_TRACE) h2n_log("       visibility test: end, number %u\n", index);
	if (index >= VISIBILITY_SLOTS) { if (g_query_open) { IDirect3DQuery9_Issue(g_query_open, D3DISSUE_END); query_put_back(g_query_open); g_query_open = NULL; } return; }
	if (g_visibility[index].query) { query_put_back(g_visibility[index].query); g_visibility[index].query = NULL; }      /* an older test of this number that nobody read */
	if (!g_query_open) { g_visibility[index].state = 2; g_visibility[index].pixels = 0; return; }
	IDirect3DQuery9_Issue(g_query_open, D3DISSUE_END);
	g_visibility[index].query = g_query_open; g_visibility[index].state = 1;
	g_query_open = NULL;
}
static int gx_visibility_result_inner(unsigned index, unsigned *pixels);
int gx_visibility_result(unsigned index, unsigned *pixels)
{
	gx_u64 t0 = tsc(), spent;
	int r = gx_visibility_result_inner(index, pixels);
	spent = tsc() - t0; g_ct[CT_QUERIES] += spent; g_ct[CT_ALL_GX] += spent;
	return r;
}
static int gx_visibility_result_inner(unsigned index, unsigned *pixels)
{
	DWORD count = 0;
	HRESULT hr;
	*pixels = 0;
	if (index >= VISIBILITY_SLOTS || !g_visibility[index].state) return -1;
	if (g_visibility[index].state == 1)
	{
		hr = g_visibility[index].query ? IDirect3DQuery9_GetData(g_visibility[index].query, &count, sizeof(count), D3DGETDATA_FLUSH) : E_FAIL;
		if (hr == S_FALSE) return 0;                         /* the graphics card has not got that far */
		if (FAILED(hr)) count = 0;
		if (g_visibility[index].query) { query_put_back(g_visibility[index].query); g_visibility[index].query = NULL; }
		g_visibility[index].pixels = (count + g_scale * g_scale / 2) / (g_scale * g_scale);
		g_visibility[index].state = 2;
		g_visibility_known++; if (g_visibility[index].pixels) g_visibility_seen++;
		if (FRAME_TRACE) h2n_log("       visibility test: number %u is known: %u pixels\n", index, g_visibility[index].pixels);
	}
	*pixels = g_visibility[index].pixels;
	return 1;
}
static void release_queries(void)       /* the device is being reset: queries do not outlive that */
{
	unsigned i;
	if (g_query_open) { IDirect3DQuery9_Release(g_query_open); g_query_open = NULL; }
	for (i = 0; i < g_query_spares; i++) IDirect3DQuery9_Release(g_query_spare[i]);
	g_query_spares = 0;
	for (i = 0; i < VISIBILITY_SLOTS; i++)
		if (g_visibility[i].query) { IDirect3DQuery9_Release(g_visibility[i].query); g_visibility[i].query = NULL; g_visibility[i].state = 2; g_visibility[i].pixels = 0; }
}

/* ================================================================ each frame ==== */
static void release_targets(void)
{
	unsigned i;
	for (i = 0; i < TARGET_SLOTS; i++) target_release(&g_targets[i]);
	if (g_scratch_color.surface) IDirect3DSurface9_Release(g_scratch_color.surface);
	memset(&g_scratch_color, 0, sizeof(g_scratch_color));
	g_color_target = NULL; g_depth_target = NULL;
	release_queries();
	for (i = 0; i < 4; i++) if (g_half_surface[i]) { IDirect3DSurface9_Release(g_half_surface[i]); g_half_surface[i] = NULL; }
	aa_release();
}

/* s9r: every 5 s with the frame time (timing.c): this file's part of the draw, by part */
void __cdecl h2n_gfx_perf_report(unsigned frames, double draw_ms)
{
	static LARGE_INTEGER q0;
	static gx_u64 t0;
	LARGE_INTEGER q1, f;
	gx_u64 t1 = tsc();
	double per_ms, ours, gx, n = frames ? (double)frames : 1.0;
	QueryPerformanceCounter(&q1); QueryPerformanceFrequency(&f);
	if (!q0.QuadPart || !frames || q1.QuadPart <= q0.QuadPart || t1 <= t0) { q0 = q1; t0 = t1; memset(g_ct, 0, sizeof(g_ct)); g_calls_made = g_calls_saved = 0; g_remapped_draws = 0; g_converted_vertices = g_range_vertices = 0; g_targets_made = 0; return; }
	per_ms = (double)(t1 - t0) / ((double)(q1.QuadPart - q0.QuadPart) * 1000.0 / (double)f.QuadPart);
	q0 = q1; t0 = t1;
#define CT_MS(c) ((double)g_ct[c] / per_ms / n)
	gx = CT_MS(CT_ALL_GX); ours = gx + CT_MS(CT_PUSH);
	h2n_log("       picture time (ms a frame, of the draw's %.2f): ours %.2f = reading the game's GPU commands %.2f, draws %.2f (surfaces %.2f, depth copies for reading %.2f, "
		"shaders %.2f, textures %.2f, vertices %.2f, state and constants %.2f, Direct3D 9's draw calls %.2f, copies for draws that read what they draw into %.2f), clears %.2f, visibility tests %.2f; "
		"the game's own work and the rest %.2f; Direct3D 9 state calls %.0f a frame, %.0f left out as unchanged%s; vertices converted %.0f a frame (the draws' index ranges span %.0f), %.0f draws given only the vertices they use; surfaces made %.2f a frame\n",
		draw_ms, ours, CT_MS(CT_PUSH), gx - CT_MS(CT_CLEAR) - CT_MS(CT_QUERIES), CT_MS(CT_TARGETS), CT_MS(CT_DEPTHCOPY), CT_MS(CT_SHADERS), CT_MS(CT_TEXTURES),
		CT_MS(CT_VERTICES), CT_MS(CT_STATE), CT_MS(CT_DRAWCALL), CT_MS(CT_SELFREAD), CT_MS(CT_CLEAR), CT_MS(CT_QUERIES),
		draw_ms > ours ? draw_ms - ours : 0.0, g_calls_made / n, g_calls_saved / n, g_cache_on ? "" : " (H2N_GFXCACHE=0: none left out)", g_converted_vertices / n, g_range_vertices / n, g_remapped_draws / n, g_targets_made / n);
#undef CT_MS
	memset(g_ct, 0, sizeof(g_ct)); g_calls_made = g_calls_saved = 0; g_remapped_draws = 0; g_converted_vertices = g_range_vertices = 0; g_targets_made = 0;
}
void gx_report(void)
{
	if (g_device) h2n_log("picture: %u shaders made, %u refused by Direct3D 9, %u faults found by this build's own check of them; visibility tests: %u begun, %u answered, %u of those saw something; the depth buffer was copied for reading %u times (%u passes); %u draws read the surface they drew into\n",
		g_shaders_made, g_shaders_refused, g_shader_problems, g_visibility_begun, g_visibility_known, g_visibility_seen, g_depth_copies, g_depth_copy_passes, g_self_reads);
	if (g_device) { aa_report(); if (aa_mode() == AA_TAA || aa_mode() == AA_DLSS) h2n_log("picture: anti-aliasing: %u 3D draws moved by the sub-pixel offsets\n", g_scene_jittered); }
}

void gx_frame_counts(unsigned *draws, unsigned *skipped, unsigned *uploads)
{
	*draws = g_draws; *skipped = g_draws_skipped; *uploads = g_texture_uploads;
	g_draws = 0; g_draws_skipped = 0; g_texture_uploads = 0;
}
/* ---- H2N_FPS=1 (s9v): the frames a second the window shows, counted over half a second, in its top left corner ----
 * Drawn on the window's own picture after the game's (dots of a 3x5 digit font, no texture), with every Direct3D 9 state
 * put back afterwards (a state block, and the target and depth surfaces, which a state block does not hold). */
static int g_fps_wanted = -1;                 /* the FPS counter (H2N_FPS; s10u: settable while the game runs) */
static IDirect3DStateBlock9 *g_fps_block;
static void fps_overlay(void)
{
	static const unsigned short glyph[10] = { 0x7b6f, 0x2c97, 0x73e7, 0x73cf, 0x5bc9, 0x79cf, 0x79ef, 0x7249, 0x7bef, 0x7bcf };  /* 3x5, row by row */
	static DWORD start, frames, shown;
	struct dot { float x, y, z, rhw; DWORD colour; } v[6 * 96];
	IDirect3DSurface9 *back = NULL, *target = NULL, *depth = NULL;
	unsigned n = 0, digits;
	float d, x0, y0;
	char text[12];
	DWORD now = GetTickCount();
	if (g_fps_wanted < 0) g_fps_wanted = h2n_env_int("H2N_FPS", 0) ? 1 : 0;
	if (!g_fps_wanted || !g_device) return;
	frames++;
	if (!start) start = now;
	if (now - start >= 500) { shown = (DWORD)(frames * 1000.0 / (now - start) + 0.5); frames = 0; start = now; }
	if (!g_fps_block && FAILED(IDirect3DDevice9_CreateStateBlock(g_device, D3DSBT_ALL, &g_fps_block))) { g_fps_wanted = 0; h2n_log("       picture: H2N_FPS: no state block, no counter\n"); return; }
	snprintf(text, sizeof(text), "%lu", (unsigned long)(shown > 99999 ? 99999 : shown));
	digits = (unsigned)strlen(text);
	d = (float)(g_window_height / 180 > 2 ? g_window_height / 180 : 2);
	x0 = d * 2; y0 = d * 2;
	if (g_new_window) { x0 += (float)g_picture_rect.left; y0 += (float)g_picture_rect.top; }
#define H2N_QUAD(l, t, r, b, c) do { struct dot q[6] = { { l, t, 0, 1, c }, { r, t, 0, 1, c }, { l, b, 0, 1, c }, { r, t, 0, 1, c }, { r, b, 0, 1, c }, { l, b, 0, 1, c } }; \
		memcpy(v + n, q, sizeof(q)); n += 6; } while (0)
	H2N_QUAD(x0 - d, y0 - d, x0 + digits * 4 * d, y0 + 6 * d, 0xff000000u);
	for (unsigned c = 0; c < digits; c++)
		for (int bit = 0; bit < 15; bit++)
			if (glyph[text[c] - '0'] >> (14 - bit) & 1)
			{
				float l = x0 + c * 4 * d + (bit % 3) * d, t = y0 + (bit / 3) * d;
				H2N_QUAD(l, t, l + d, t + d, 0xffffffffu);
			}
#undef H2N_QUAD
	IDirect3DStateBlock9_Capture(g_fps_block);
	IDirect3DDevice9_GetRenderTarget(g_device, 0, &target);
	IDirect3DDevice9_GetDepthStencilSurface(g_device, &depth);
	if (SUCCEEDED(IDirect3DDevice9_GetBackBuffer(g_device, 0, 0, D3DBACKBUFFER_TYPE_MONO, &back)))
	{
		IDirect3DDevice9_SetRenderTarget(g_device, 0, back);
		IDirect3DDevice9_SetDepthStencilSurface(g_device, NULL);
		IDirect3DDevice9_SetRenderState(g_device, D3DRS_ZENABLE, FALSE);
		IDirect3DDevice9_SetRenderState(g_device, D3DRS_ZWRITEENABLE, FALSE);
		IDirect3DDevice9_SetRenderState(g_device, D3DRS_ALPHABLENDENABLE, FALSE);
		IDirect3DDevice9_SetRenderState(g_device, D3DRS_ALPHATESTENABLE, FALSE);
		IDirect3DDevice9_SetRenderState(g_device, D3DRS_STENCILENABLE, FALSE);
		IDirect3DDevice9_SetRenderState(g_device, D3DRS_SCISSORTESTENABLE, FALSE);
		IDirect3DDevice9_SetRenderState(g_device, D3DRS_CULLMODE, D3DCULL_NONE);
		IDirect3DDevice9_SetRenderState(g_device, D3DRS_FILLMODE, D3DFILL_SOLID);
		IDirect3DDevice9_SetRenderState(g_device, D3DRS_LIGHTING, FALSE);
		IDirect3DDevice9_SetRenderState(g_device, D3DRS_FOGENABLE, FALSE);
		IDirect3DDevice9_SetRenderState(g_device, D3DRS_COLORWRITEENABLE, 0xf);
		IDirect3DDevice9_SetRenderState(g_device, D3DRS_SRGBWRITEENABLE, FALSE);
		IDirect3DDevice9_SetTexture(g_device, 0, NULL);
		IDirect3DDevice9_SetTextureStageState(g_device, 0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
		IDirect3DDevice9_SetTextureStageState(g_device, 0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
		IDirect3DDevice9_SetTextureStageState(g_device, 0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
		IDirect3DDevice9_SetTextureStageState(g_device, 0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
		IDirect3DDevice9_SetTextureStageState(g_device, 1, D3DTSS_COLOROP, D3DTOP_DISABLE);
		IDirect3DDevice9_SetPixelShader(g_device, NULL);
		IDirect3DDevice9_SetVertexShader(g_device, NULL);
		IDirect3DDevice9_SetFVF(g_device, D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
		IDirect3DDevice9_DrawPrimitiveUP(g_device, D3DPT_TRIANGLELIST, n / 3, v, sizeof(v[0]));
		IDirect3DDevice9_SetRenderTarget(g_device, 0, target);
		IDirect3DDevice9_SetDepthStencilSurface(g_device, depth);
		IDirect3DSurface9_Release(back);
	}
	if (target) IDirect3DSurface9_Release(target);
	if (depth) IDirect3DSurface9_Release(depth);
	IDirect3DStateBlock9_Apply(g_fps_block);
}

/* ================================================================ (s10u) the settings, changed while the game runs ====
 * H2Classic Settings can stay open (or be opened) while the game runs; it saves h2classic\settings.ini at every change
 * and tells the game, which reads the file again between two frames (main.c h2n_live_poll) and calls these for what
 * changed. Only the window from the settings is changed (the test window stays as it was started).
 *   Display Mode: the same window, given the other frame and place - no new device.
 *   Window Size: the back buffer is the window's picture, so the device is reset to the new size (as after a lost
 *     device: every surface drawn into is let go first and made again by the next draws).
 *   Render Scale: the surfaces are let go and made again at the new multiple; the device stays.
 *   Aspect Ratio: xapi.c has given the game its widescreen setting and the factor; here the picture's place in the window. */
void __cdecl gx_fps_live(void) { g_fps_wanted = h2n_env_int("H2N_FPS", 0) ? 1 : 0; }
void __cdecl gx_aa_live(void) { if (g_device && !g_lost) { aa_live(); ss_forget(); } }
static HRESULT reset_device(void)
{
	return g_ex ? IDirect3DDevice9Ex_ResetEx((IDirect3DDevice9Ex *)g_device, &g_pp, NULL) : IDirect3DDevice9_Reset(g_device, &g_pp);
}
/* (s10w) the picture made the window's size, and the surfaces made again for another render scale: the device is reset to
 * the window's inside when that is not the back buffer's size, as after a lost device. Returns what was done (1 the size,
 * 2 the scale). Between two frames only. */
static int picture_refit(unsigned scale)
{
	int cw = 0, ch = 0, size_changed, scale_changed = scale != g_scale;
	unsigned i;
	if (!client_size(&cw, &ch) || (cw == g_refused_w && ch == g_refused_h)) { cw = (int)g_window_width; ch = (int)g_window_height; }
	size_changed = (unsigned)cw != g_window_width || (unsigned)ch != g_window_height;
	if (size_changed || scale_changed)
	{
		/* nothing of the old sizes stays bound or kept, as when the device was lost */
		IDirect3DSurface9 *back = NULL;
		if (SUCCEEDED(IDirect3DDevice9_GetBackBuffer(g_device, 0, 0, D3DBACKBUFFER_TYPE_MONO, &back)) && back) { IDirect3DDevice9_SetRenderTarget(g_device, 0, back); IDirect3DSurface9_Release(back); }
		IDirect3DDevice9_SetDepthStencilSurface(g_device, NULL);
		for (i = 0; i < 16; i++) IDirect3DDevice9_SetTexture(g_device, i, NULL);
		release_targets();
		for (i = 0; i < 2; i++) { if (g_read_surface[i]) IDirect3DSurface9_Release(g_read_surface[i]); g_read_surface[i] = NULL; }
		g_read_width = 0;
		for (i = 0; i < 4; i++) { if (g_half_surface[i]) { IDirect3DSurface9_Release(g_half_surface[i]); g_half_surface[i] = NULL; } g_half_size[i] = 0; }
		if (g_fps_block) { IDirect3DStateBlock9_Release(g_fps_block); g_fps_block = NULL; }
	}
	if (size_changed)
	{
		D3DPRESENT_PARAMETERS was = g_pp;
		HRESULT hr;
		g_pp.BackBufferWidth = (UINT)cw; g_pp.BackBufferHeight = (UINT)ch;
		hr = reset_device();
		if (FAILED(hr))
		{
			h2n_log("*** picture: the device did not take the window's size %dx%d (%08lx); the picture stays %ux%u and is fitted to the window\n", cw, ch, (unsigned long)hr, g_window_width, g_window_height);
			g_refused_w = cw; g_refused_h = ch;
			g_pp = was;
			cw = (int)g_window_width; ch = (int)g_window_height; size_changed = 0;
			if (FAILED(reset_device())) { g_lost = 1; h2n_log("       picture: the graphics device has to be reset again (frame %u)\n", g_frame); }
		}
		else { g_refused_w = g_refused_h = 0; }
	}
	if (size_changed || scale_changed) set_defaults();
	picture_fit(cw, ch);
	g_scale = scale;
	if (!g_lost)
	{	/* black over the whole picture once: the game's picture covers its own rectangle only */
		IDirect3DSurface9 *back = NULL;
		if (SUCCEEDED(IDirect3DDevice9_GetBackBuffer(g_device, 0, 0, D3DBACKBUFFER_TYPE_MONO, &back)) && back)
		{
			IDirect3DDevice9_ColorFill(g_device, back, NULL, 0);
			IDirect3DSurface9_Release(back);
			IDirect3DDevice9_Present(g_device, NULL, NULL, NULL, NULL);
		}
	}
	InvalidateRect(g_window, NULL, TRUE);
	return (size_changed ? 1 : 0) | (scale_changed ? 2 : 0);
}
void __cdecl gx_live(void)
{
	const char *size_text = getenv("H2N_WINDOW_SIZE"), *mode_text = getenv("H2N_DISPLAY");
	int want_w = 0, want_h = 0, screen_w = GetSystemMetrics(SM_CXSCREEN), screen_h = GetSystemMetrics(SM_CYSCREEN), mode, done;
	int want_changed, mode_changed;
	unsigned scale;
	if (!g_device || g_lost) return;
	if (!g_new_window) { h2n_log("       picture: the settings changed, but this is the test window (no window size or display mode at the start): it stays as it is\n"); return; }
	/* the same reading of the settings as when the window was opened (gx_open) */
	if (size_text) sscanf(size_text, "%dx%d", &want_w, &want_h);
	mode = !mode_text ? 0 : !_stricmp(mode_text, "fullscreen") ? 2 : !_stricmp(mode_text, "borderless") ? 1 : 0;
	if (want_w < 320 || want_h < 240 || want_w > 16384 || want_h > 16384) { want_w = mode ? screen_w : 1280; want_h = mode ? screen_h : 960; }
	if (getenv("H2N_RENDER")) { scale = (unsigned)h2n_env_int("H2N_RENDER", 1); if (scale < 1 || scale > SCALE_LIMIT) scale = 1; }
	else
	{	/* (a settings.ini from before s10t: a percentage of the picture's height as it is now) */
		int pct = h2n_env_int("H2N_RENDERSCALE", 100), n = (int)((g_picture_rect.bottom - g_picture_rect.top) * (pct < 25 ? 25 : pct > 400 ? 400 : pct) / 100.0 / g_height + 0.5);
		scale = (unsigned)(n < 1 ? 1 : n > 8 ? 8 : n);
	}
	if (scale > g_scale_most) scale = g_scale_most;
	want_changed = want_w != g_want_w || want_h != g_want_h;
	mode_changed = mode != g_display_mode;
	g_want_w = want_w; g_want_h = want_h;
	if (mode_changed || (want_changed && !mode))
	{
		/* the same window with the other frame and place: over the screen without a frame (borderless, full screen - that
		   one on top of every window, as at the start), or a framed window whose inside is Window Size (or what of it
		   Windows lets a window on this screen have) */
		DWORD style = mode ? WS_POPUP : WS_OVERLAPPEDWINDOW;
		RECT r, at;
		int x = 0, y = 0, w = screen_w, h = screen_h;
		if (!mode)
		{
			r.left = 0; r.top = 0; r.right = want_w; r.bottom = want_h;
			frame_rect(&r, style);
			w = r.right - r.left; h = r.bottom - r.top;
			if (g_display_mode == 0 && !IsZoomed(g_window) && GetWindowRect(g_window, &at)) { x = at.left; y = at.top; }   /* a window already: it stays where it is */
			else { x = (screen_w - w) / 2; y = (screen_h - h) / 2; if (x < 0) x = 0; if (y < 0) y = 0; }
		}
		if (IsZoomed(g_window)) ShowWindow(g_window, SW_RESTORE);        /* (a maximized window is given its new size as a plain one) */
		SetWindowLongA(g_window, GWL_STYLE, (LONG)(style | WS_VISIBLE));
		SetWindowPos(g_window, mode == 2 ? HWND_TOPMOST : HWND_NOTOPMOST, x, y, w, h, SWP_FRAMECHANGED | SWP_NOACTIVATE | SWP_SHOWWINDOW);
		g_display_mode = mode;
	}
	/* the picture for the window as it is now (also: the game's picture for another shape - Aspect Ratio) */
	done = picture_refit(scale);
	h2n_log("       picture: settings changed while the game runs: %s, the picture %ux%u - the window's own size (%ldx%ld of it the game's), render scale x%u: drawn at %ux%u%s%s%s\n",
		g_display_mode == 2 ? "full screen" : g_display_mode ? "borderless window over the screen" : "a window that can be resized",
		g_window_width, g_window_height, (long)(g_picture_rect.right - g_picture_rect.left), (long)(g_picture_rect.bottom - g_picture_rect.top), g_scale, g_width * g_scale, g_height * g_scale,
		done & 1 ? "; the device was reset to the window's size" : "", done & 2 ? "; the surfaces are made again at the new scale" : "", mode_changed ? "; the window was changed" : "");
	if (g_display_mode && want_changed)
		h2n_log("       picture: Window Size (%dx%d) is for a windowed window; this window is the screen\n", want_w, want_h);
}
/* (s10w) once a frame, between two frames: has the window another size than the picture (maximized, dragged, restored)? */
static void picture_follow(void)
{
	int cw, ch;
	if (g_scale_lower)
	{	/* (s10z) the card had no room for the game's surfaces at this Render Scale: one step lower, and no higher again
		   in this run (each step that does not fit costs a frame) */
		g_scale_lower = 0;
		if (g_new_window && g_device && !g_lost && g_scale > 1)
		{
			unsigned was = g_scale;
			g_scale_most = g_scale - 1;
			picture_refit(g_scale - 1);
			h2n_log("*** picture: the graphics card could not make the game's surfaces at Render Scale x%u (%ux%u each; not enough graphics memory): x%u is used until the next start\n",
				was, g_width * was, g_height * was, g_scale);
		}
	}
	if (!g_new_window || !g_device || g_lost || !client_size(&cw, &ch)) return;
	if (((unsigned)cw == g_window_width && (unsigned)ch == g_window_height) || (cw == g_refused_w && ch == g_refused_h)) return;
	if (picture_refit(g_scale) & 1)
		h2n_log("       picture: the window is now %ux%u inside: the picture is drawn for that size (%ldx%ld of it the game's); the device was reset\n",
			g_window_width, g_window_height, (long)(g_picture_rect.right - g_picture_rect.left), (long)(g_picture_rect.bottom - g_picture_rect.top));
}


void gx_present(const gx_surface *back)
{
	static int began;
	static unsigned shots[16], shot_count, shot_every;
	static int shots_read;
	IDirect3DSurface9 *window_surface = NULL;
	target_entry *source;
	HRESULT hr;
	unsigned i;
	int shot, drawn = g_lazy_draw, soon = 0;

	if (!g_device) return;
	pump();
	mouse_capture();                  /* (s10v) the pointer held or let go, as the game takes the look or not */
	if (!shots_read)
	{
		const char *list = getenv("H2N_SHOT");
		shots_read = 1;
		shot_every = (unsigned)h2n_env_int("H2N_SHOT_EVERY", 0);
		while (list && *list && shot_count < 16) { shots[shot_count++] = (unsigned)strtoul(list, (char **)&list, 10); while (*list == ',' || *list == ' ') list++; }
	}
	if (g_texture_sweep_wanted) { g_texture_sweep_wanted = 0; texture_sweep(); }
	g_frame++;
	g_frame_stamp = g_target_stamp;      /* (s10g) surfaces used before now were used in an earlier frame */
	aa_frame();               /* anti-aliasing (taa): the next frame's sub-pixel offset */
	shot = 0;
	for (i = 0; i < shot_count; i++) { if (shots[i] == g_frame) shot = 1; if (shots[i] > g_frame && shots[i] <= g_frame + 3) soon = 1; }
	if (shot_every && g_frame % shot_every == 0) shot = 1;
	if (shot_every && (g_frame + 3) / shot_every != g_frame / shot_every) soon = 1;
	if (g_lazy && g_shot_request)
	{
		/* a picture was asked for while frames are not being drawn: draw the next three (effects that build on the frame before need that) and save the third */
		if (!g_lazy_run) { g_lazy_run = 3; soon = 1; }
		else if (!--g_lazy_run) { shot = 1; g_shot_request = 0; }
		else soon = 1;
	}
	else { if (g_shot_request) shot = 1; g_shot_request = 0; }

	if (g_lost)
	{
		hr = IDirect3DDevice9_TestCooperativeLevel(g_device);
		if (hr == D3DERR_DEVICENOTRESET)
		{
			release_targets();
			for (i = 0; i < 2; i++) { if (g_read_surface[i]) IDirect3DSurface9_Release(g_read_surface[i]); g_read_surface[i] = NULL; }
			g_read_width = 0;
			if (SUCCEEDED(IDirect3DDevice9_Reset(g_device, &g_pp))) { g_lost = 0; began = 0; set_defaults(); h2n_log("       picture: the graphics device was lost and is back\n"); }
		}
		if (g_lost) { Sleep(10); return; }
	}
	if (!began) { IDirect3DDevice9_BeginScene(g_device); began = 1; }

	if (g_trace_now)       /* the frame that was described: every surface the game drew into, as it is at the end of the frame */
	{
		for (i = 0; i < TARGET_SLOTS; i++)
			if (g_targets[i].memory && g_targets[i].texture && !g_targets[i].depth)
			{
				char name[80];
				snprintf(name, sizeof(name), "h2classic_surface_%05u_%08x.bmp", g_frame, (unsigned)(size_t)g_targets[i].memory);
				save_surface(g_targets[i].surface, g_targets[i].real_width, g_targets[i].real_height, name);
			}
		text_pages_save();
		h2n_log("       picture: frame %u: the back buffer is the game's memory %p; %u draws\n", g_frame, back ? back->memory : NULL, g_draws);
		shot = 1;
	}
	source = back ? target_find(back->memory) : NULL;
	if (back && back->memory && !presented(back->memory)) { for (i = 0; i < 4 && g_presented[i]; i++) { } g_presented[i < 4 ? i : 3] = back->memory; }
	if (g_scene_notes && g_scene_notes <= 4 && aa_mode() != AA_OFF) { static unsigned told; if (told < 4) { told++; note("anti-aliasing: frame %u shows the game's memory %p", g_frame, back ? back->memory : NULL); } }
	if (drawn && SUCCEEDED(IDirect3DDevice9_GetBackBuffer(g_device, 0, 0, D3DBACKBUFFER_TYPE_MONO, &window_surface)))
	{
		if (source && source->surface)
		{
			/* whole pixels where the window is a whole number of times the picture (or the same size); a smooth fit otherwise, as when the picture is drawn larger than the window */
			unsigned window_width = g_new_window ? (unsigned)(g_picture_rect.right - g_picture_rect.left) : g_window_width;
			unsigned window_height = g_new_window ? (unsigned)(g_picture_rect.bottom - g_picture_rect.top) : g_window_height;
			int whole = source->real_width <= window_width && source->real_height <= window_height && window_width % source->real_width == 0 && window_height % source->real_height == 0;
			IDirect3DSurface9 *from = source->surface;
			unsigned w = source->real_width, h = source->real_height, step = 0;
			/* drawn larger than the window (H2N_RENDER above H2N_SCALE): halved, four pixels into one, until one smooth step is left; that is what takes the stairs off edges */
			while (w >= window_width * 2 && h >= window_height * 2 && w >= 4 && h >= 4 && step < 4 && (w > window_width * 2 || h > window_height * 2))
			{
				if (!g_half_surface[step] || g_half_size[step] != ((w / 2) << 16 | (h / 2)))
				{
					if (g_half_surface[step]) { IDirect3DSurface9_Release(g_half_surface[step]); g_half_surface[step] = NULL; }
					if (FAILED(IDirect3DDevice9_CreateRenderTarget(g_device, w / 2, h / 2, D3DFMT_X8R8G8B8, D3DMULTISAMPLE_NONE, 0, FALSE, &g_half_surface[step], NULL))) { g_half_surface[step] = NULL; break; }
					g_half_size[step] = (w / 2) << 16 | (h / 2);
				}
				if (FAILED(IDirect3DDevice9_StretchRect(g_device, from, NULL, g_half_surface[step], NULL, D3DTEXF_LINEAR))) break;
				from = g_half_surface[step]; w /= 2; h /= 2; step++;
			}
			IDirect3DDevice9_StretchRect(g_device, from, NULL, window_surface, g_new_window ? &g_picture_rect : NULL, whole ? D3DTEXF_NONE : D3DTEXF_LINEAR);
			if (g_new_window)
			{	/* (s10w) black beside or above and below the game's picture: the back buffer is shown whole now */
				RECT bar[4] = { { 0, 0, g_picture_rect.left, (LONG)g_window_height }, { g_picture_rect.right, 0, (LONG)g_window_width, (LONG)g_window_height },
					{ 0, 0, (LONG)g_window_width, g_picture_rect.top }, { 0, g_picture_rect.bottom, (LONG)g_window_width, (LONG)g_window_height } };
				for (i = 0; i < 4; i++) if (bar[i].right > bar[i].left && bar[i].bottom > bar[i].top) IDirect3DDevice9_ColorFill(g_device, window_surface, &bar[i], 0);
			}
		}
		else IDirect3DDevice9_ColorFill(g_device, window_surface, NULL, 0);
		fps_overlay();                           /* (s9v: H2N_FPS; before a shot, so the shot has it too) */
		if (shot)
		{
			char name[64];
			snprintf(name, sizeof(name), "h2classic_frame_%05u.bmp", g_frame);
			save_surface(window_surface, g_window_width, g_window_height, name);
		}
		IDirect3DSurface9_Release(window_surface);
	}
	IDirect3DDevice9_EndScene(g_device);
	h2n_perf(8);                                 /* (s9q: Direct3D 9's Present, timed) */
	if (g_new_window && drawn)
	{	/* (s10w) the picture is the window's size: shown pixel for pixel. Only while it is not - the frame in which the
		   window changed size, a window too small to draw for, a size the device refused - is the game's picture fitted to
		   the window as it is, its shape kept (s9w) */
		RECT client, to;
		LONG pw = g_picture_rect.right - g_picture_rect.left, ph = g_picture_rect.bottom - g_picture_rect.top, cw, ch;
		GetClientRect(g_window, &client);
		cw = client.right - client.left; ch = client.bottom - client.top;
		if (cw < 1 || ch < 1) cw = pw, ch = ph;
		if ((unsigned)cw == g_window_width && (unsigned)ch == g_window_height) hr = IDirect3DDevice9_Present(g_device, NULL, NULL, NULL, NULL);
		else {
		if ((LONGLONG)cw * ph > (LONGLONG)ch * pw) { to.top = 0; to.bottom = ch; to.left = (cw - (LONG)((LONGLONG)ch * pw / ph)) / 2; to.right = to.left + (LONG)((LONGLONG)ch * pw / ph); }
		else { to.left = 0; to.right = cw; to.top = (ch - (LONG)((LONGLONG)cw * ph / pw)) / 2; to.bottom = to.top + (LONG)((LONGLONG)cw * ph / pw); }
		hr = IDirect3DDevice9_Present(g_device, &g_picture_rect, &to, NULL, NULL);
		}
	}
	else hr = drawn ? IDirect3DDevice9_Present(g_device, NULL, NULL, NULL, NULL) : D3D_OK;
	h2n_perf(9);
	/* the next frame: is it one to describe (F11: the next two; H2N_GFXFRAME: that one), and, where only the frames that are saved are drawn, one to draw? */
	if (g_trace_now && g_trace_left) g_trace_left--;
	g_trace_now = (g_trace_frame && g_frame + 1 == g_trace_frame) || g_trace_left > 0;
	if (g_trace_now && g_trace_left) h2n_log("       picture: frame %u is described draw by draw\n", g_frame + 1);
	g_lazy_draw = !g_lazy || soon || g_trace_now;
	if (hr == D3DERR_DEVICELOST) { g_lost = 1; began = 0; h2n_log("       picture: the graphics device was lost (frame %u)\n", g_frame); return; }
	{	/* (s10u) settings changed in H2Classic Settings while the game runs are taken here, between two frames (main.c) */
		void __cdecl h2n_live_poll(void);
		h2n_live_poll();
		if (g_lost) { began = 0; return; }
		picture_follow();                 /* (s10w) the window has another size now: the picture is made that size */
		if (g_lost) { began = 0; return; }
	}
	IDirect3DDevice9_BeginScene(g_device);
	began = 1;
}

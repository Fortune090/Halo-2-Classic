/* H2Classic Settings (s10g: was "H2Classic Launcher"; s9w; s9y-dlss2: TAA and DLSS in Anti-Aliasing). Plain Win32, drawn
 * in the manner of Halo 2's menus - deep navy, light-blue lines and text, frames with cut corners - in Windows' own
 * Bahnschrift where it is installed (Windows 10 1709 and later), Segoe UI otherwise. The controls are this program's own
 * (a choice with arrows, a toggle, a button), so that every part of the window is drawn alike. Every change is saved at
 * once to h2classic\settings.ini (keys this program does not know are kept as they are), which H2Classic.exe reads when it
 * starts, started from here or not. Start Game runs H2Classic.exe from this folder. */
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <commdlg.h>

enum { ID_SCALE = 100, ID_SIZE, ID_WIDE, ID_MODE, ID_UNLOCK, ID_VSYNC, ID_FPS, ID_AA, ID_SKIPINTRO, ID_SKIPATTRACT, ID_AAQ, ID_VERSION,
	ID_START = 200, ID_RESET, ID_EXIT, ID_KEYS, ID_MINIMIZE, ID_CLOSE, ID_KM,
	ID_KEYS_BIOS = 300, ID_KEYS_BIOS_BROWSE, ID_KEYS_MCPX, ID_KEYS_MCPX_BROWSE, ID_KEYS_GENERATE, ID_KEYS_CANCEL,
	ID_KM_ENABLE = 400, ID_KM_SENS, ID_KM_ZOOM, ID_KM_INVERT, ID_KM_RESET, ID_KM_DONE, ID_KM_BIND = 500 };   /* (s10v) ID_KM_BIND + 2 x row + 0 or 1 */
/* the controls' own messages: a choice's items and selection, a toggle's state (the test tools use them too) */
enum { CH_ADD = WM_USER + 1, CH_SET, CH_GET, CH_CLEAR, TG_SET, TG_GET, SL_SET, SL_GET, BD_SET, BD_GET, BD_WAITING };   /* (s10v) a slider's position, a key field's key */

static HWND g_main, g_scale, g_size, g_wide, g_mode, g_unlock, g_vsync, g_fps, g_aa, g_aaq, g_skipintro, g_skipattract, g_version;
static HWND g_keys, g_keys_bios, g_keys_mcpx;
static HWND g_km;                             /* (s10v) the Keyboard + Mouse Settings window, while it is open */
/* (s10u) the game running while this window is open: found by the event the game makes; the GAME part (which only a
   start of the game takes) is greyed out, Start Game too, and each save is passed on to the game */
static HWND g_start;
static int g_running, g_running_dlss, g_topmost, g_game_part_first, g_game_part_last, g_game_title_part;
static DWORD g_started_at;
static HFONT g_font, g_title_font, g_note_font, g_note_narrow_font, g_section_font, g_button_font;
static HBRUSH g_field_brush;
static int g_loading;
static char g_aa_note_text[200];
static RECT g_aa_note_rect;
#define SCALE_MOST 20                                             /* (s10t) Render Scale: 1 to 10 times the game's 640x480; (s10z) to 20 (12800x9600) */
static int g_sizes[32][2], g_size_count;

/* ---- the look ---- */
#define C_BG        RGB(6, 17, 31)
#define C_BG_TOP    RGB(12, 32, 56)
#define C_PANEL     RGB(9, 25, 45)
#define C_FIELD     RGB(13, 36, 64)
#define C_FIELD_HOT RGB(22, 54, 92)
#define C_LINE      RGB(124, 196, 255)
#define C_LINE_DIM  RGB(48, 94, 142)
#define C_TEXT      RGB(205, 238, 255)
#define C_LABEL     RGB(150, 192, 228)
#define C_NOTE      RGB(112, 146, 180)
#define C_OFF       RGB(72, 98, 126)
#define C_SCAN      RGB(11, 29, 51)



/* ---- settings.ini: lines kept in order; known keys replaced ---- */
static char g_lines[200][256];
static int g_line_count;
static const char *ini_path(void) { return "h2classic\\settings.ini"; }
static void ini_load(void)
{
	FILE *f = fopen(ini_path(), "r");
	g_line_count = 0;
	if (!f) return;
	while (g_line_count < 200 && fgets(g_lines[g_line_count], sizeof(g_lines[0]), f))
	{
		char *e = g_lines[g_line_count] + strlen(g_lines[g_line_count]);
		while (e > g_lines[g_line_count] && (e[-1] == '\n' || e[-1] == '\r')) *--e = 0;
		g_line_count++;
	}
	fclose(f);
}
static const char *ini_get(const char *key, const char *otherwise)
{
	static char value[256];
	size_t n = strlen(key);
	for (int i = 0; i < g_line_count; i++)
		if (!_strnicmp(g_lines[i], key, n) && g_lines[i][n] == '=') { snprintf(value, sizeof(value), "%s", g_lines[i] + n + 1); return value; }
	return otherwise;
}
static void ini_set(const char *key, const char *value)
{
	size_t n = strlen(key);
	for (int i = 0; i < g_line_count; i++)
		if (!_strnicmp(g_lines[i], key, n) && g_lines[i][n] == '=') { snprintf(g_lines[i], sizeof(g_lines[0]), "%s=%s", key, value); return; }
	if (g_line_count < 200) snprintf(g_lines[g_line_count++], sizeof(g_lines[0]), "%s=%s", key, value);
}
static void ini_remove(const char *key)
{
	size_t n = strlen(key);
	for (int i = 0; i < g_line_count; i++)
		if (!_strnicmp(g_lines[i], key, n) && g_lines[i][n] == '=') { memmove(g_lines[i], g_lines[i + 1], (size_t)(g_line_count - i - 1) * sizeof(g_lines[0])); g_line_count--; i--; }
}
static void ini_save(void)
{
	FILE *f;
	CreateDirectoryA("h2classic", NULL);
	f = fopen(ini_path(), "w");
	if (!f) return;
	for (int i = 0; i < g_line_count; i++) fprintf(f, "%s\n", g_lines[i]);
	fclose(f);
}


/* ---- drawing ---- */
static void fill(HDC dc, const RECT *r, COLORREF c) { HBRUSH b = CreateSolidBrush(c); FillRect(dc, r, b); DeleteObject(b); }
/* a frame with its top-left and bottom-right corners cut, as Halo 2's menu panels have */
static void cut_frame(HDC dc, const RECT *r, int cut_tl, int cut_br, COLORREF inside, COLORREF line, int width)
{
	POINT p[6];
	HBRUSH b = CreateSolidBrush(inside);
	HPEN pen = CreatePen(PS_SOLID, width, line), old_pen = SelectObject(dc, pen);
	HBRUSH old_brush = SelectObject(dc, b);
	int l = r->left, t = r->top, rr = r->right - 1, bb = r->bottom - 1;
	p[0].x = l + cut_tl; p[0].y = t; p[1].x = rr; p[1].y = t; p[2].x = rr; p[2].y = bb - cut_br;
	p[3].x = rr - cut_br; p[3].y = bb; p[4].x = l; p[4].y = bb; p[5].x = l; p[5].y = t + cut_tl;
	Polygon(dc, p, 6);
	SelectObject(dc, old_pen); SelectObject(dc, old_brush); DeleteObject(pen); DeleteObject(b);
}
static void triangle(HDC dc, int x, int y, int size, int right, COLORREF c)
{
	POINT p[3];
	HBRUSH b = CreateSolidBrush(c), old_brush = SelectObject(dc, b);
	HPEN pen = CreatePen(PS_SOLID, 1, c), old_pen = SelectObject(dc, pen);
	if (right) { p[0].x = x; p[0].y = y - size; p[1].x = x + size; p[1].y = y; p[2].x = x; p[2].y = y + size; }
	else { p[0].x = x + size; p[0].y = y - size; p[1].x = x; p[1].y = y; p[2].x = x + size; p[2].y = y + size; }
	Polygon(dc, p, 3);
	SelectObject(dc, old_pen); SelectObject(dc, old_brush); DeleteObject(pen); DeleteObject(b);
}
static void text_at(HDC dc, const char *s, RECT *r, HFONT f, COLORREF c, UINT format, int spacing)
{
	HFONT old = SelectObject(dc, f);
	SetBkMode(dc, TRANSPARENT); SetTextColor(dc, c); SetTextCharacterExtra(dc, spacing);
	DrawTextA(dc, s, -1, r, format);
	SetTextCharacterExtra(dc, 0);
	SelectObject(dc, old);
}
/* paint into a bitmap first, so nothing flickers */
typedef struct { HDC dc, screen; HBITMAP bitmap, old; PAINTSTRUCT ps; RECT r; HWND w; } paint_buffer;
static HDC paint_begin(HWND w, paint_buffer *p)
{
	p->w = w;
	p->screen = BeginPaint(w, &p->ps);
	GetClientRect(w, &p->r);
	p->dc = CreateCompatibleDC(p->screen);
	p->bitmap = CreateCompatibleBitmap(p->screen, p->r.right > 0 ? p->r.right : 1, p->r.bottom > 0 ? p->r.bottom : 1);
	p->old = SelectObject(p->dc, p->bitmap);
	return p->dc;
}
static void paint_end(paint_buffer *p)
{
	BitBlt(p->screen, 0, 0, p->r.right, p->r.bottom, p->dc, 0, 0, SRCCOPY);
	SelectObject(p->dc, p->old); DeleteObject(p->bitmap); DeleteDC(p->dc);
	EndPaint(p->w, &p->ps);
}

/* ---- the controls: a choice (its value between two arrows: click, the wheel, Left / Right), a toggle, a button ---- */
typedef struct { char items[32][48]; int count, sel, hot, primary, checked, pressed, wrap;     /* wrap: a choice that goes round (s10h) */
	int steps, pos, decimals, drag; double base, unit;       /* (s10v) a slider: positions 0..steps, its value base + pos x unit */
	int code, waiting, skip_char; DWORD since; } ctl;       /* (s10v) a key field: the key it holds; waiting for the next key */
static ctl *ctl_of(HWND w) { return (ctl *)GetWindowLongPtrA(w, GWLP_USERDATA); }
static void notify(HWND w, int code) { SendMessageA(GetParent(w), WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(w), code), (LPARAM)w); }
static void track_leave(HWND w) { TRACKMOUSEEVENT t; t.cbSize = sizeof(t); t.dwFlags = TME_LEAVE; t.hwndTrack = w; t.dwHoverTime = 0; TrackMouseEvent(&t); }
static LRESULT common(HWND w, UINT m, WPARAM wp, LPARAM lp, int *handled)
{
	ctl *c = ctl_of(w);
	*handled = 1;
	switch (m)
	{
	case WM_NCCREATE: SetWindowLongPtrA(w, GWLP_USERDATA, (LONG_PTR)calloc(1, sizeof(ctl))); *handled = 0; return 0;
	case WM_NCDESTROY: free(c); SetWindowLongPtrA(w, GWLP_USERDATA, 0); *handled = 0; return 0;
	case WM_ERASEBKGND: return 1;
	case WM_SETFOCUS: case WM_KILLFOCUS: case WM_ENABLE: InvalidateRect(w, NULL, FALSE); return 0;
	case WM_MOUSEMOVE: if (c && !c->hot) { c->hot = 1; track_leave(w); InvalidateRect(w, NULL, FALSE); } return 0;
	case WM_MOUSELEAVE: if (c) { c->hot = 0; c->pressed = 0; InvalidateRect(w, NULL, FALSE); } return 0;
	}
	*handled = 0;
	return 0;
}
static void choice_step(HWND w, int by)
{
	ctl *c = ctl_of(w);
	if (!c || c->count < 2 || !IsWindowEnabled(w)) return;
	if (c->wrap) c->sel = (c->sel + by + c->count) % c->count;
	else if (c->sel + by < 0 || c->sel + by >= c->count) return;     /* (s10h) at its lowest or highest: stays */
	else c->sel += by;
	InvalidateRect(w, NULL, FALSE);
	notify(w, CBN_SELCHANGE);
}
static LRESULT CALLBACK choice_proc(HWND w, UINT m, WPARAM wp, LPARAM lp)
{
	ctl *c = ctl_of(w);
	int handled;
	LRESULT r = common(w, m, wp, lp, &handled);
	if (handled) return r;
	switch (m)
	{
	case CH_ADD: if (c && c->count < 32) {   /* (s10z) 32 entries (it was 12): Render Scale has 20, Window Size up to 32 */ snprintf(c->items[c->count++], 48, "%s", (const char *)lp); InvalidateRect(w, NULL, FALSE); } return 0;
	case CH_CLEAR: if (c) { c->count = 0; c->sel = 0; InvalidateRect(w, NULL, FALSE); } return 0;
	case CH_SET: if (c && (int)wp >= 0 && (int)wp < c->count) { c->sel = (int)wp; InvalidateRect(w, NULL, FALSE); } return 0;
	case CH_GET: return c && c->count ? c->sel : -1;
	case WM_GETDLGCODE: return DLGC_WANTARROWS | DLGC_WANTCHARS;
	case WM_LBUTTONDOWN: case WM_LBUTTONDBLCLK:      /* (s10i) a quick second click steps too */
		if (GetFocus() != w) SetFocus(w);
		choice_step(w, (short)LOWORD(lp) < 30 ? -1 : 1);
		return 0;
	case WM_MOUSEWHEEL: choice_step(w, (short)HIWORD(wp) > 0 ? -1 : 1); return 0;
	case WM_KEYDOWN:
		if (wp == VK_LEFT || wp == VK_UP) choice_step(w, -1);
		else if (wp == VK_RIGHT || wp == VK_DOWN) choice_step(w, 1);
		return 0;
	case WM_CHAR: if (wp == ' ') choice_step(w, 1); return 0;
	case WM_PAINT:
	{
		paint_buffer p;
		HDC dc = paint_begin(w, &p);
		int on = IsWindowEnabled(w), focus = GetFocus() == w, mid = p.r.bottom / 2;
		RECT t = p.r;
		fill(dc, &p.r, C_PANEL);
		cut_frame(dc, &p.r, 7, 7, on && (c->hot || focus) ? C_FIELD_HOT : C_FIELD, focus ? C_LINE : C_LINE_DIM, 1);
		/* (s10h) an arrow that would go past the first or last value is dimmed */
		triangle(dc, 11, mid, 5, 0, on && c && (c->wrap || c->sel > 0) ? C_LINE : C_OFF);
		triangle(dc, p.r.right - 16, mid, 5, 1, on && c && (c->wrap || c->sel < c->count - 1) ? C_LINE : C_OFF);
		t.left += 28; t.right -= 28;
		text_at(dc, c && c->count ? c->items[c->sel] : "", &t, g_font, on ? C_TEXT : C_OFF, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, 0);
		paint_end(&p);
		return 0;
	}
	}
	return DefWindowProcA(w, m, wp, lp);
}
static LRESULT CALLBACK toggle_proc(HWND w, UINT m, WPARAM wp, LPARAM lp)
{
	ctl *c = ctl_of(w);
	int handled;
	LRESULT r = common(w, m, wp, lp, &handled);
	if (handled) return r;
	switch (m)
	{
	case TG_SET: if (c) { c->checked = wp != 0; InvalidateRect(w, NULL, FALSE); } return 0;
	case TG_GET: return c ? c->checked : 0;
	case WM_GETDLGCODE: return DLGC_WANTCHARS;
	case WM_LBUTTONDOWN: SetFocus(w); /* fall through */
	case WM_LBUTTONDBLCLK: if (c) { c->checked = !c->checked; InvalidateRect(w, NULL, FALSE); notify(w, BN_CLICKED); } return 0;
	case WM_CHAR: if (wp == ' ' && c) { c->checked = !c->checked; InvalidateRect(w, NULL, FALSE); notify(w, BN_CLICKED); } return 0;
	case WM_PAINT:
	{
		paint_buffer p;
		HDC dc = paint_begin(w, &p);
		int on = IsWindowEnabled(w), focus = on && GetFocus() == w, top = (p.r.bottom - 18) / 2;     /* (s10u) greyed out when disabled */
		RECT box = { 2, top, 20, top + 18 }, t = p.r;
		char text[128];
		fill(dc, &p.r, C_PANEL);
		cut_frame(dc, &box, 5, 0, C_FIELD, !on ? C_OFF : focus || (c && c->hot) ? C_LINE : C_LINE_DIM, 1);
		if (c && c->checked) { RECT in = { box.left + 5, box.top + 5, box.right - 5, box.bottom - 5 }; fill(dc, &in, on ? C_LINE : C_OFF); }
		GetWindowTextA(w, text, sizeof(text));
		t.left = 30;
		text_at(dc, text, &t, g_font, !on ? C_OFF : focus ? C_TEXT : C_LABEL, DT_LEFT | DT_VCENTER | DT_SINGLELINE, 0);
		paint_end(&p);
		return 0;
	}
	}
	return DefWindowProcA(w, m, wp, lp);
}
static LRESULT CALLBACK button_proc(HWND w, UINT m, WPARAM wp, LPARAM lp)
{
	ctl *c = ctl_of(w);
	int handled;
	LRESULT r = common(w, m, wp, lp, &handled);
	if (handled) return r;
	switch (m)
	{
	case WM_GETDLGCODE: return DLGC_WANTCHARS;
	case WM_LBUTTONDOWN: SetFocus(w); SetCapture(w); if (c) { c->pressed = 1; InvalidateRect(w, NULL, FALSE); } return 0;
	case WM_LBUTTONUP:
	{
		POINT pt; RECT rc;
		int was = c && c->pressed;
		ReleaseCapture();
		if (c) { c->pressed = 0; InvalidateRect(w, NULL, FALSE); }
		pt.x = (short)LOWORD(lp); pt.y = (short)HIWORD(lp); GetClientRect(w, &rc);
		if (was && PtInRect(&rc, pt)) notify(w, BN_CLICKED);
		return 0;
	}
	case WM_CHAR: if (wp == ' ') notify(w, BN_CLICKED); return 0;
	case WM_PAINT:
	{
		paint_buffer p;
		HDC dc = paint_begin(w, &p);
		int on = IsWindowEnabled(w), focus = on && GetFocus() == w, hot = on && c && c->hot, down = on && c && c->pressed, primary = on && c && c->primary;   /* (s10u) greyed out when disabled */
		RECT t = p.r;
		char text[96], *s;
		COLORREF inside = primary ? (down ? RGB(96, 168, 230) : hot ? RGB(164, 220, 255) : C_LINE) : (down ? C_FIELD : hot ? C_FIELD_HOT : C_FIELD);
		fill(dc, &p.r, C_PANEL);
		cut_frame(dc, &p.r, 10, 10, inside, !on ? C_OFF : focus && !primary ? C_TEXT : C_LINE, focus ? 2 : 1);
		GetWindowTextA(w, text, sizeof(text));
		for (s = text; *s; s++) if (*s >= 'a' && *s <= 'z') *s -= 32;
		if (down) OffsetRect(&t, 1, 1);
		text_at(dc, text, &t, g_button_font, !on ? C_OFF : primary ? C_BG : C_TEXT, DT_CENTER | DT_VCENTER | DT_SINGLELINE, 1);
		paint_end(&p);
		return 0;
	}
	}
	return DefWindowProcA(w, m, wp, lp);
}

/* ---- (s10j) the settings window has no title bar: its own minimize and close buttons, at the top right ---- */
static void paint_background(HDC dc, int left, int top, int width, int height, int header);
static LRESULT CALLBACK caption_proc(HWND w, UINT m, WPARAM wp, LPARAM lp)
{
	ctl *c = ctl_of(w);
	int handled;
	LRESULT r = common(w, m, wp, lp, &handled);
	if (handled) return r;
	switch (m)
	{
	case WM_LBUTTONDOWN: SetCapture(w); if (c) { c->pressed = 1; InvalidateRect(w, NULL, FALSE); } return 0;
	case WM_LBUTTONUP:
	{
		POINT pt; RECT rc;
		int was = c && c->pressed;
		ReleaseCapture();
		if (c) { c->pressed = 0; InvalidateRect(w, NULL, FALSE); }
		pt.x = (short)LOWORD(lp); pt.y = (short)HIWORD(lp); GetClientRect(w, &rc);
		if (was && PtInRect(&rc, pt)) notify(w, BN_CLICKED);
		return 0;
	}
	case WM_PAINT:
	{
		paint_buffer p;
		HDC dc = paint_begin(w, &p);
		RECT at;
		int close = GetDlgCtrlID(w) == ID_CLOSE, hot = c && c->hot, down = c && c->pressed;
		int cx = p.r.right / 2, cy = p.r.bottom / 2;
		COLORREF glyph = hot ? (close ? RGB(255, 255, 255) : C_TEXT) : C_LINE;
		HPEN pen, old;
		GetWindowRect(w, &at); MapWindowPoints(NULL, GetParent(w), (POINT *)&at, 2);
		paint_background(dc, at.left, at.top, p.r.right, p.r.bottom, 1);
		if (hot) fill(dc, &p.r, close ? (down ? RGB(150, 32, 20) : RGB(196, 43, 28)) : (down ? C_FIELD : C_FIELD_HOT));
		pen = CreatePen(PS_SOLID, 1, glyph); old = SelectObject(dc, pen);
		if (close)
		{
			MoveToEx(dc, cx - 5, cy - 5, NULL); LineTo(dc, cx + 6, cy + 6);
			MoveToEx(dc, cx + 5, cy - 5, NULL); LineTo(dc, cx - 6, cy + 6);
		}
		else { MoveToEx(dc, cx - 5, cy, NULL); LineTo(dc, cx + 6, cy); }
		SelectObject(dc, old); DeleteObject(pen);
		paint_end(&p);
		return 0;
	}
	}
	return DefWindowProcA(w, m, wp, lp);
}

/* (s10j) Windows 11's rounded corners and border colour, and a title bar in Windows' own light or dark theme: through
 * dwmapi.dll if it has them (older Windows: nothing changes) */
static HRESULT dwm_set(HWND w, DWORD what, const void *value, DWORD size)
{
	typedef HRESULT (WINAPI *set_fn)(HWND, DWORD, LPCVOID, DWORD);
	static set_fn set;
	static int looked;
	if (!looked) { HMODULE dll = LoadLibraryA("dwmapi.dll"); looked = 1; if (dll) set = (set_fn)(void *)GetProcAddress(dll, "DwmSetWindowAttribute"); }
	return set ? set(w, what, value, size) : E_NOTIMPL;
}

/* ---- the window's own parts: labels, notes and section frames are drawn by the window ---- */
typedef struct { int x, y, w, h; const char *text; int kind; } part;     /* kind 0 label, 1 section frame (h its height), 2 note, 3 a note on one line */
static part g_parts[40];
static int g_part_count;
static void part_add(int x, int y, int w, int h, const char *text, int kind)
{
	if (g_part_count < 40) { part *p = &g_parts[g_part_count++]; p->x = x; p->y = y; p->w = w; p->h = h; p->text = text; p->kind = kind; }
}
/* the background: navy, lighter at the top, with faint lines across the header as on a Halo 2 menu screen; drawn into dc
 * as the window has it at (left, top) (a caption button draws the part behind it the same way) */
static void paint_background(HDC dc, int left, int top, int width, int height, int header)
{
	int y;
	for (y = top - top % 4; y < top + height; y += 4)
	{
		RECT band = { 0, y - top, width, y - top + 4 };
		int k = y < 240 ? 240 - y : 0;
		fill(dc, &band, RGB(6 + 6 * k / 240, 17 + 15 * k / 240, 31 + 25 * k / 240));
	}
	if (header)
		for (y = 8; y < 64; y += 4) if (y >= top && y < top + height) { RECT scan = { 0, y - top, width, y - top + 1 }; fill(dc, &scan, C_SCAN); }
	(void)left;
}
#define HEADER_DRAG 62                        /* (s10j) the settings window has no title bar: above the title's line it is moved by */
static void paint_window(HWND w, part *parts, int count, const char *title)
{
	paint_buffer p;
	HDC dc = paint_begin(w, &p);
	int i;
	paint_background(dc, 0, 0, p.r.right, p.r.bottom, title != NULL);
	if (title)
	{
		RECT t = { 26, 16, p.r.right - 20, 58 }, line;
		text_at(dc, title, &t, g_title_font, C_TEXT, DT_LEFT | DT_VCENTER | DT_SINGLELINE, 2);
		line.left = 26; line.top = 62; line.right = p.r.right - 26; line.bottom = 64; fill(dc, &line, C_LINE);
		line.left = 26; line.top = 64; line.right = 120; line.bottom = 67; fill(dc, &line, C_LINE);
	}
	for (i = 0; i < count; i++)
	{
		part *q = &parts[i];
		RECT r = { q->x, q->y, q->x + q->w, q->y + q->h };
		int off = parts == g_parts && g_running && i >= g_game_part_first && i <= g_game_part_last;   /* (s10u) the GAME part while the game runs */
		if (q->kind == 1)
		{
			RECT cap = { q->x + 14, q->y + 6, q->x + q->w - 10, q->y + 26 };
			cut_frame(dc, &r, 14, 14, C_PANEL, C_LINE_DIM, 1);
			text_at(dc, off && i == g_game_title_part ? "GAME  -  FOR THE NEXT START" : q->text, &cap, g_section_font, off ? C_OFF : C_LINE, DT_LEFT | DT_TOP | DT_SINGLELINE, 2);
		}
		else if (q->kind == 3)                                         /* (s10j) a note kept on one line */
		{
			HFONT f = g_note_font, old = SelectObject(dc, f);
			SIZE size;
			if (GetTextExtentPoint32A(dc, q->text, (int)strlen(q->text), &size) && size.cx > q->w && g_note_narrow_font) f = g_note_narrow_font;
			SelectObject(dc, old);
			text_at(dc, q->text, &r, f, C_NOTE, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOCLIP, 0);
		}
		else text_at(dc, q->text, &r, q->kind == 2 ? g_note_font : g_font, off ? C_OFF : q->kind == 2 ? C_NOTE : C_LABEL, q->kind == 2 ? DT_LEFT | DT_TOP | DT_WORDBREAK : DT_LEFT | DT_VCENTER | DT_SINGLELINE, 0);
	}
	if (title)                                                         /* (s10j) no title bar: the window's own edge */
	{
		HBRUSH b = CreateSolidBrush(C_LINE_DIM);
		FrameRect(dc, &p.r, b);
		DeleteObject(b);
	}
	paint_end(&p);
}

static HWND g_parent;                         /* the window add() puts controls in */
static HWND add(const char *cls, const char *text, DWORD style, int x, int y, int w, int h, int id)
{
	HWND c = CreateWindowExA(0, cls, text, WS_CHILD | WS_VISIBLE | style, x, y, w, h, g_parent, (HMENU)(INT_PTR)id, GetModuleHandleA(NULL), NULL);
	SendMessageA(c, WM_SETFONT, (WPARAM)g_font, TRUE);
	return c;
}
static HWND choice(const char *label, int x, int y, int id, int wrap)
{
	HWND c;
	part_add(x, y, 160, 30, label, 0);
	c = add("h2c_choice", "", WS_TABSTOP, x + 164, y, 222, 30, id);      /* (s10h) its right edge 16 inside the panel, as the labels' left */
	if (c && ctl_of(c)) ctl_of(c)->wrap = wrap;
	return c;
}
static HWND toggle(const char *label, int x, int y, int id) { return add("h2c_toggle", label, WS_TABSTOP, x, y, 300, 26, id); }
static HWND button(const char *label, int x, int y, int w, int id, int primary)
{
	HWND b = add("h2c_button", label, WS_TABSTOP, x, y, w, 34, id);
	if (b && ctl_of(b)) ctl_of(b)->primary = primary;
	return b;
}
static int pick(HWND c) { return (int)SendMessageA(c, CH_GET, 0, 0); }
static void choose(HWND c, int i) { SendMessageA(c, CH_SET, (WPARAM)i, 0); }
static void item(HWND c, const char *s) { SendMessageA(c, CH_ADD, 0, (LPARAM)s); }
static int ticked(HWND c) { return (int)SendMessageA(c, TG_GET, 0, 0); }
static void tick(HWND c, int on) { SendMessageA(c, TG_SET, (WPARAM)(on != 0), 0); }

/* the fonts: Bahnschrift (Windows 10 1709 and later, its named styles), else Segoe UI */
static int CALLBACK font_found(const LOGFONTA *lf, const TEXTMETRICA *tm, DWORD type, LPARAM found) { (void)lf; (void)tm; (void)type; *(int *)found = 1; return 0; }
static int installed(const char *face)
{
	LOGFONTA lf;
	HDC dc = GetDC(NULL);
	int found = 0;
	memset(&lf, 0, sizeof(lf)); lf.lfCharSet = DEFAULT_CHARSET; snprintf(lf.lfFaceName, sizeof(lf.lfFaceName), "%s", face);
	EnumFontFamiliesExA(dc, &lf, (FONTENUMPROCA)font_found, (LPARAM)&found, 0);
	ReleaseDC(NULL, dc);
	return found;
}
static HFONT font(int px, int weight, const char *const *faces)
{
	for (; *faces; faces++)
		if (installed(*faces)) return CreateFontA(-px, 0, 0, 0, weight, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, *faces);
	return CreateFontA(-px, 0, 0, 0, weight, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI");
}
static void make_fonts(void)
{
	static const char *const body[] = { "Bahnschrift", "Segoe UI", NULL };
	static const char *const strong[] = { "Bahnschrift SemiBold", "Bahnschrift", "Segoe UI Semibold", "Segoe UI", NULL };
	static const char *const title[] = { "Bahnschrift SemiBold Condensed", "Bahnschrift SemiBold SemiConden", "Bahnschrift SemiBold", "Bahnschrift", "Segoe UI Semibold", NULL };
	g_font = font(16, FW_NORMAL, body);
	g_note_font = font(13, FW_NORMAL, body);
	{
		static const char *const narrow[] = { "Bahnschrift SemiCondensed", "Bahnschrift Condensed", "Segoe UI", NULL };
		g_note_narrow_font = font(13, FW_NORMAL, narrow);
	}
	g_section_font = font(13, FW_SEMIBOLD, strong);
	g_button_font = font(14, FW_SEMIBOLD, strong);
	g_title_font = font(34, FW_SEMIBOLD, title);
	g_field_brush = CreateSolidBrush(C_FIELD);
}


static void make_sizes(void)
{
	static const int common[][2] = { { 1280, 720 }, { 1280, 960 }, { 1600, 900 }, { 1920, 1080 }, { 2560, 1080 }, { 2560, 1440 }, { 3440, 1440 }, { 3840, 1080 }, { 3840, 2160 }, { 5120, 1440 } };
	int dw = GetSystemMetrics(SM_CXSCREEN), dh = GetSystemMetrics(SM_CYSCREEN), have = 0;
	g_size_count = 0;
	for (unsigned i = 0; i < sizeof(common) / sizeof(common[0]); i++)
	{
		if (common[i][0] > dw || common[i][1] > dh) continue;
		g_sizes[g_size_count][0] = common[i][0]; g_sizes[g_size_count][1] = common[i][1]; g_size_count++;
		if (common[i][0] == dw && common[i][1] == dh) have = 1;
	}
	if (!have) { g_sizes[g_size_count][0] = dw; g_sizes[g_size_count][1] = dh; g_size_count++; }
}


/* ---- anti-aliasing: the method, and below it its quality where the method has levels ---- */
static const char *const k_smaa[4] = { "low", "medium", "high", "ultra" }, *const k_smaa_names[4] = { "Low", "Medium", "High", "Ultra" };
static const char *const k_dlss[3] = { "default", "k", "j" }, *const k_dlss_names[3] = { "NVIDIA Default", "Preset K", "Preset J" };
static void aa_quality_fill(void)
{
	int aa = pick(g_aa), i, sel = 0;
	char saved[32];
	SendMessageA(g_aaq, CH_CLEAR, 0, 0);
	if (aa == 2)
	{	/* SMAA's four presets (SMAA.hlsl) */
		snprintf(saved, sizeof(saved), "%s", ini_get("smaa_quality", "high"));
		for (i = 0; i < 4; i++) { item(g_aaq, k_smaa_names[i]); if (!_stricmp(saved, k_smaa[i])) sel = i + 1; }
		sel = sel ? sel - 1 : 2;
	}
	else if (aa == 4)
	{	/* DLSS's render preset for DLAA (NVIDIA's SDK: K, the default for DLAA, or J) */
		snprintf(saved, sizeof(saved), "%s", ini_get("dlss_preset", "default"));
		for (i = 0; i < 3; i++) { item(g_aaq, k_dlss_names[i]); if (!_stricmp(saved, k_dlss[i])) sel = i; }
	}
	else item(g_aaq, aa == 0 ? "-" : "Standard");
	choose(g_aaq, sel);
	EnableWindow(g_aaq, aa == 2 || aa == 4);
}
/* the line under Anti-Aliasing: what the quality means, and for DLSS what of it is missing from this folder (the game uses
   TAA in its place whenever DLSS cannot run, and its log says why) */
static void aa_note(void)
{
	int aa = pick(g_aa), q = pick(g_aaq);
	const char *text = "";
	if (g_running && (aa == 4) != g_running_dlss) text = "DLSS starts with the game: this change is taken the next time the game starts.";   /* (s10u) */
	else if (aa == 1) text = "FXAA is a single pass; it has no quality levels.";
	else if (aa == 2) text = q == 0 ? "Low: the quickest. No diagonal or corner smoothing." : q == 1 ? "Medium: finds longer edges. No diagonal or corner smoothing."
		: q == 3 ? "Ultra: the faintest and longest edges. Costs the most." : "High: the usual choice, diagonals and corners included.";
	else if (aa == 3) text = "TAA blends frames over time; it has no quality levels.";
	else if (aa == 4)
	{
		if (GetFileAttributesA("DLSS Helper.exe") == INVALID_FILE_ATTRIBUTES) text = "DLSS Helper.exe is missing here, so TAA is used.";
		else if (GetFileAttributesA("nvngx_dlss.dll") == INVALID_FILE_ATTRIBUTES) text = "nvngx_dlss.dll is missing here, so TAA is used.";
		else text = q == 2 ? "Preset J: a little less ghosting, a little more flicker. Needs an RTX card."
			: q == 1 ? "Preset K: the best picture (transformer model). Needs an RTX card."
			: "NVIDIA's own choice for DLAA (preset K today). Needs an RTX card.";
	}
	snprintf(g_aa_note_text, sizeof(g_aa_note_text), "%s", text);
	InvalidateRect(g_main, &g_aa_note_rect, FALSE);
}

/* (s10t) the multiplier that a percentage of the picture's height came to in the game before s10t (gfx9.c: the picture
   keeps its shape inside the window; its height times the percentage, over 480, to the nearest whole number). It is
   what a settings.ini without render_multiplier starts from: 100% when the file has no render_scale either. */
static int multiplier_for(int w, int h, const char *wide, int percent)
{
	double aspect = !_stricmp(wide, "4:3") || !strcmp(wide, "0") ? 4.0 / 3.0 : !strcmp(wide, "21:9") ? 64.0 / 27.0 : !strcmp(wide, "32:9") ? 32.0 / 9.0 : 16.0 / 9.0;
	double ph = w <= 0 || h <= 0 ? 480.0 : (double)w / h > aspect ? (double)h : w / aspect;
	int n;
	if (percent < 25) percent = 25;
	if (percent > 400) percent = 400;
	n = (int)(ph * percent / 100.0 / 480.0 + 0.5);
	return n < 1 ? 1 : n > SCALE_MOST ? SCALE_MOST : n;
}
/* settings -> controls */
static void show(void)
{
	char text[64];
	int scale = atoi(ini_get("render_multiplier", "0")), percent = atoi(ini_get("render_scale", "100")), w = atoi(ini_get("window_width", "0")), h = atoi(ini_get("window_height", "0"));
	char wide[64], mode[64], aa[64];                            /* (s10f) each its own copy: ini_get's value is one shared buffer */
	snprintf(wide, sizeof(wide), "%s", ini_get("widescreen", "16:9"));
	snprintf(mode, sizeof(mode), "%s", ini_get("display_mode", "borderless"));
	snprintf(aa, sizeof(aa), "%s", ini_get("anti_aliasing", "off"));
	int best;
	g_loading = 1;
	best = g_size_count - 1;                                    /* (the screen's own size when none is saved) */
	for (int i = 0; i < g_size_count; i++) if (g_sizes[i][0] == w && g_sizes[i][1] == h) best = i;
	if (w >= 320 && h >= 240 && (g_sizes[best][0] != w || g_sizes[best][1] != h) && g_size_count < 32)
	{	/* a size saved by hand that is not in the list */
		g_sizes[g_size_count][0] = w; g_sizes[g_size_count][1] = h;
		snprintf(text, sizeof(text), "%d x %d", w, h); item(g_size, text);
		best = g_size_count++;
	}
	choose(g_size, best);
	if (scale < 1) scale = multiplier_for(g_sizes[best][0], g_sizes[best][1], wide, percent);
	choose(g_scale, (scale > SCALE_MOST ? SCALE_MOST : scale) - 1);
	choose(g_wide, !_stricmp(wide, "4:3") || !strcmp(wide, "0") ? 0 : !strcmp(wide, "21:9") ? 2 : !strcmp(wide, "32:9") ? 3 : 1);
	choose(g_mode, !_stricmp(mode, "fullscreen") ? 0 : !_stricmp(mode, "windowed") ? 2 : 1);
	EnableWindow(g_size, pick(g_mode) == 2);        /* (s10w) Window Size is a windowed window's size: borderless and full screen are the screen's */
	tick(g_unlock, atoi(ini_get("unlock_performance", "1")));
	tick(g_vsync, atoi(ini_get("vsync", "1")));
	tick(g_fps, atoi(ini_get("fps_counter", "0")));
	choose(g_aa, !_stricmp(aa, "fxaa") ? 1 : !_stricmp(aa, "smaa") ? 2 : !_stricmp(aa, "taa") ? 3 : !_stricmp(aa, "dlss") ? 4 : 0);
	aa_quality_fill();
	aa_note();
	tick(g_skipintro, atoi(ini_get("skip_intro", "0")));
	tick(g_skipattract, atoi(ini_get("skip_attract", "0")));
	choose(g_version, !strcmp(ini_get("game_version", "1.0"), "1.5") ? 1 : 0);
	g_loading = 0;
}
/* ---- (s10u) the game while this window is open ----
 * H2Classic.exe makes a named event when it starts ("H2Classic_settings_changed"; it is gone when the game ends). That
 * the event is there tells this program the game is running; setting it tells the game that settings.ini was saved. */
static int game_running(void)
{
	HANDLE e = OpenEventA(SYNCHRONIZE | EVENT_MODIFY_STATE, FALSE, "H2Classic_settings_changed");
	if (!e) return 0;
	CloseHandle(e);
	return 1;
}
static void game_signal(void)
{
	HANDLE e = OpenEventA(EVENT_MODIFY_STATE, FALSE, "H2Classic_settings_changed");
	if (!e) return;
	SetEvent(e);
	CloseHandle(e);
}
/* controls -> settings, saved */
static void save(void)
{
	char text[32];
	static const char *const modes[3] = { "fullscreen", "borderless", "windowed" }, *const aas[5] = { "off", "fxaa", "smaa", "taa", "dlss" };
	int s = pick(g_size), aa = pick(g_aa), q = pick(g_aaq);
	if (g_loading) return;
	snprintf(text, sizeof(text), "%d", pick(g_scale) < 0 ? 1 : pick(g_scale) + 1); ini_set("render_multiplier", text);
	ini_remove("render_scale");                                 /* (the percentage of before s10t) */
	if (s < 0) s = g_size_count - 1;
	snprintf(text, sizeof(text), "%d", g_sizes[s][0]); ini_set("window_width", text);
	snprintf(text, sizeof(text), "%d", g_sizes[s][1]); ini_set("window_height", text);
	ini_set("widescreen", pick(g_wide) == 0 ? "4:3" : pick(g_wide) == 2 ? "21:9" : pick(g_wide) == 3 ? "32:9" : "16:9");
	ini_set("display_mode", modes[pick(g_mode) < 0 ? 1 : pick(g_mode)]);
	ini_set("unlock_performance", ticked(g_unlock) ? "1" : "0");
	ini_set("vsync", ticked(g_vsync) ? "1" : "0");
	ini_set("fps_counter", ticked(g_fps) ? "1" : "0");
	ini_set("anti_aliasing", aas[aa < 0 ? 0 : aa]);
	if (aa == 2 && q >= 0 && q < 4) ini_set("smaa_quality", k_smaa[q]);       /* (s10g) the other method's level is kept */
	if (aa == 4 && q >= 0 && q < 3) ini_set("dlss_preset", k_dlss[q]);
	ini_set("skip_intro", ticked(g_skipintro) ? "1" : "0");
	ini_set("skip_attract", ticked(g_skipattract) ? "1" : "0");
	ini_set("game_version", pick(g_version) == 1 ? "1.5" : "1.0");
	/* system link with consoles and xemu: always on (the address and port can be changed in the file) */
	ini_set("network", "retail");
	if (!strcmp(ini_get("network_peer", ""), "")) ini_set("network_peer", "127.0.0.1:9368");
	if (!strcmp(ini_get("network_port", ""), "")) ini_set("network_port", "9369");
	ini_save();
	game_signal();                          /* (s10u) a running game reads the file again and takes what it can */
}
/* (s10i) a change is written a quarter of a second after the last one, not at every click (one file write for a run of
   quick clicks); Start Game, Exit and closing the window write what is still waiting */
static int g_save_waiting;
static void save_soon(void) { g_save_waiting = 1; SetTimer(g_main, 1, 250, NULL); }
static void save_now(void) { KillTimer(g_main, 1); if (g_save_waiting) { g_save_waiting = 0; save(); } }
static void reset(void)
{
	char version[16], intro[8], attract[8];
	KillTimer(g_main, 1); g_save_waiting = 0;
	snprintf(version, sizeof(version), "%s", ini_get("game_version", "1.0"));
	snprintf(intro, sizeof(intro), "%s", ini_get("skip_intro", "0"));
	snprintf(attract, sizeof(attract), "%s", ini_get("skip_attract", "0"));
	{	/* the defaults: show() with nothing saved, then saved. (s10v) Keyboard + Mouse Settings has its own Reset to
		   Defaults: its lines stay */
		int i, kept = 0;
		for (i = 0; i < g_line_count; i++)
			if (!_strnicmp(g_lines[i], "key_", 4) || !_strnicmp(g_lines[i], "mouse_", 6) || !_strnicmp(g_lines[i], "keyboard_mouse=", 15))
				{ if (kept != i) memcpy(g_lines[kept], g_lines[i], sizeof(g_lines[0])); kept++; }
		g_line_count = kept;
	}
	if (g_running)
	{	/* (s10u) while the game runs the GAME part is not this window's to change: it stays as it is */
		ini_set("game_version", version); ini_set("skip_intro", intro); ini_set("skip_attract", attract);
	}
	show();
	save();
}
/* (s10u) every quarter second: is the game running? When that changes, the GAME part and Start Game are greyed out or
 * given back. A game shown full screen stays on top of every window, so this window could never be seen over it:
 * while this window is the one in use (the foreground window) and the game's window is on top, this one is put on top
 * as well; as soon as another window is in use - the game, when it is started from here or clicked - it is not, and
 * the game covers it again. */
static void game_watch(void)
{
	int running = game_running(), top = 0;
	if (!running && g_started_at && GetTickCount() - g_started_at < 8000) running = 1;   /* (just started from here: not there yet) */
	else g_started_at = 0;
	if (running)
	{
		HWND game = FindWindowA("h2classic", NULL);
		top = game && (GetWindowLongA(game, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0 && (GetForegroundWindow() == g_main || (g_keys && GetForegroundWindow() == g_keys) || (g_km && GetForegroundWindow() == g_km));
	}
	if (top != g_topmost)
	{
		g_topmost = top;
		SetWindowPos(g_main, top ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
	}
	if (running == g_running) return;
	g_running = running;
	if (running) g_running_dlss = pick(g_aa) == 4;
	EnableWindow(g_version, !running); EnableWindow(g_skipintro, !running); EnableWindow(g_skipattract, !running);
	EnableWindow(g_start, !running);
	SetWindowTextA(g_start, running ? "Game Running" : "Start Game");
	if (running && (GetFocus() == g_start || GetFocus() == g_version || GetFocus() == g_skipintro || GetFocus() == g_skipattract)) SetFocus(g_mode);
	aa_note();
	InvalidateRect(g_main, NULL, FALSE);
}
static void start(void)
{
	STARTUPINFOA si;
	PROCESS_INFORMATION pi;
	/* (s10r) one program runs both game versions: H2Classic.exe reads game_version from settings.ini itself (1.0 is the
	   disc's game; 1.5 is the game with its last title update, which the map packs need) */
	char exe[MAX_PATH];
	snprintf(exe, sizeof(exe), "%s", "H2Classic.exe");
	KillTimer(g_main, 1); g_save_waiting = 0;
	save();
	memset(&si, 0, sizeof(si)); si.cb = sizeof(si);
	if (!CreateProcessA(NULL, exe, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi))
	{
		MessageBoxA(g_main, "H2Classic.exe was not found next to H2Classic Settings.", "H2Classic Settings", MB_OK | MB_ICONERROR);
		return;
	}
	CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
	/* (s10u) this window stays open behind the game: what the game can take while it runs is changed from here */
	g_started_at = GetTickCount();
	game_watch();
}


/* ---- keys.bin (s10b; s10f: every BIOS layout): the key the console signs saved files with and makes the system link key
 * from (the kernel's certificate key), taken from two files of the user's own: a BIOS image and the MCPX boot ROM it is
 * made for. Nothing of the key is in this program; it is read from the files and written to h2classic\keys.bin.
 *
 * The BIOS's second-stage loader (2BL) is the 0x6000-byte block that ends 0x200 bytes before the top of the image. It is
 * stored scrambled (RC4), and the boot ROM holds the 16-byte key that undoes it:
 *   - BIOSes before 4817 (the MCPX 1.0 boot ROM, key at 0x1A5 of it): the whole block is RC4 with that key;
 *   - 4817 and later (the MCPX 1.1 boot ROM, key at 0x19C): the block ends in a flash boot loader (FBL, 0x2A00 bytes, kept
 *     plain). The FBL unscrambles the rest with RC4 keyed by SHA-1(ROM key, the FBL's 16-byte nonce at the block's end,
 *     ROM key XOR 0x5C): the first 0x3600 bytes, then (after the keystream has run on over the FBL) the block's last 0x80.
 * Unscrambled, the loader's boot parameters carry the signature 0x7854794A ("JyTx", what the boot ROM checks), and the
 * loader's entry (the first dword, or the FBL's entry record just before the FBL) points at a record whose first dword
 * points at the loader's keys: the EEPROM key, then the certificate key, then the kernel key. Both ROM key positions are
 * tried with both layouts; only a loader that comes out with its signature and in-range pointers is taken. ---- */
#define BLDR_SIZE 0x6000
#define FBL_SIZE 0x2A00
#define BOOT_SIGNATURE 0x7854794Au
static void rc4_stream(unsigned char s[256], int *pi, int *pj, unsigned char *data, int size)   /* data NULL: run on */
{
	int i = *pi, j = *pj, k;
	unsigned char t;
	for (k = 0; k < size; k++)
	{
		i = (i + 1) & 255; j = (j + s[i]) & 255; t = s[i]; s[i] = s[j]; s[j] = t;
		if (data) data[k] ^= s[(s[i] + s[j]) & 255];
	}
	*pi = i; *pj = j;
}
static void rc4_setup(unsigned char s[256], const unsigned char *key, int key_size)
{
	int i, j = 0;
	unsigned char t;
	for (i = 0; i < 256; i++) s[i] = (unsigned char)i;
	for (i = 0; i < 256; i++) { j = (j + s[i] + key[i % key_size]) & 255; t = s[i]; s[i] = s[j]; s[j] = t; }
}
/* SHA-1 (as in layer2/xsign.c) */
typedef struct { DWORD h[5], count_low, count_high; unsigned char block[64]; } sha_state;
static DWORD rol32(DWORD v, int n) { return (v << n) | (v >> (32 - n)); }
static void sha_block(sha_state *st, const unsigned char *p)
{
	DWORD w[80], a = st->h[0], b = st->h[1], c = st->h[2], d = st->h[3], e = st->h[4], t;
	int i;
	for (i = 0; i < 16; i++) w[i] = ((DWORD)p[i * 4] << 24) | ((DWORD)p[i * 4 + 1] << 16) | ((DWORD)p[i * 4 + 2] << 8) | p[i * 4 + 3];
	for (; i < 80; i++) w[i] = rol32(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
	for (i = 0; i < 80; i++)
	{
		if (i < 20) t = ((b & c) | (~b & d)) + 0x5a827999u;
		else if (i < 40) t = (b ^ c ^ d) + 0x6ed9eba1u;
		else if (i < 60) t = ((b & c) | (b & d) | (c & d)) + 0x8f1bbcdcu;
		else t = (b ^ c ^ d) + 0xca62c1d6u;
		t += rol32(a, 5) + e + w[i];
		e = d; d = c; c = rol32(b, 30); b = a; a = t;
	}
	st->h[0] += a; st->h[1] += b; st->h[2] += c; st->h[3] += d; st->h[4] += e;
	SecureZeroMemory(w, sizeof(w));
}
static void sha_init(sha_state *st)
{
	memset(st, 0, sizeof(*st));
	st->h[0] = 0x67452301u; st->h[1] = 0xefcdab89u; st->h[2] = 0x98badcfeu; st->h[3] = 0x10325476u; st->h[4] = 0xc3d2e1f0u;
}
static void sha_update(sha_state *st, const unsigned char *p, DWORD n)
{
	while (n--)
	{
		st->block[st->count_low % 64] = *p++;
		if (++st->count_low % 64 == 0) sha_block(st, st->block);
		if (!st->count_low) st->count_high++;
	}
}
static void sha_final(sha_state *st, unsigned char out[20])
{
	DWORD bits_low = st->count_low << 3, bits_high = (st->count_high << 3) | (st->count_low >> 29);
	unsigned char pad = 0x80, zero = 0, length[8];
	int i;
	sha_update(st, &pad, 1);
	while (st->count_low % 64 != 56) sha_update(st, &zero, 1);
	for (i = 0; i < 4; i++) { length[i] = (unsigned char)(bits_high >> (24 - i * 8)); length[4 + i] = (unsigned char)(bits_low >> (24 - i * 8)); }
	sha_update(st, length, 8);
	for (i = 0; i < 20; i++) out[i] = (unsigned char)(st->h[i / 4] >> (24 - (i % 4) * 8));
	SecureZeroMemory(st, sizeof(*st));
}
static DWORD dword_at(const unsigned char *p) { return (DWORD)p[0] | (DWORD)p[1] << 8 | (DWORD)p[2] << 16 | (DWORD)p[3] << 24; }
/* a plain FBL at the end of the 2BL block: a near jump at its start, and the pointer record in front of the jump's
   target pointing into the FBL's own addresses (0xFFFFD400 up) */
static int fbl_present(const unsigned char *block)
{
	const unsigned char *fbl = block + BLDR_SIZE - FBL_SIZE;
	long target;
	DWORD a, b;
	if (fbl[0] != 0xE9) return 0;
	target = (long)(int)dword_at(fbl + 1) + 5;
	if (target < 8 || target > FBL_SIZE) return 0;
	a = dword_at(fbl + target - 8); b = dword_at(fbl + target - 4);
	return a >= 0xFFFFD400u && a < 0xFFFFD400u + FBL_SIZE && b >= 0xFFFFD400u && b < 0xFFFFD400u + FBL_SIZE;
}
/* one try: the block unscrambled with this ROM key in this layout; 1 with the certificate key when it comes out right */
static int try_loader(const unsigned char *image_block, const unsigned char *rom_key, int fbl, unsigned char cert[16])
{
	static unsigned char block[BLDR_SIZE];
	unsigned char s[256], key20[20], x[16];
	int i = 0, j = 0, k, limit, ok = 0;
	DWORD signature, entry, keys;
	sha_state st;
	memcpy(block, image_block, BLDR_SIZE);
	if (!fbl)
	{
		rc4_setup(s, rom_key, 16);
		rc4_stream(s, &i, &j, block, BLDR_SIZE);
		signature = dword_at(block + 0x5FE4);
		entry = dword_at(block) & 0xFFFF;
		limit = BLDR_SIZE;
	}
	else
	{
		sha_init(&st);
		sha_update(&st, rom_key, 16);
		sha_update(&st, image_block + BLDR_SIZE - 16, 16);                  /* the FBL's nonce */
		for (k = 0; k < 16; k++) x[k] = rom_key[k] ^ 0x5C;
		sha_update(&st, x, 16);
		sha_final(&st, key20);
		rc4_setup(s, key20, 20);
		rc4_stream(s, &i, &j, block, BLDR_SIZE - FBL_SIZE);
		rc4_stream(s, &i, &j, NULL, FBL_SIZE - 0x80);
		rc4_stream(s, &i, &j, block + BLDR_SIZE - 0x80, 0x80);
		signature = dword_at(block + 0x5FE4 - 16);                           /* the boot parameters sit 16 bytes lower */
		entry = dword_at(block + BLDR_SIZE - FBL_SIZE - 8);                  /* the FBL's entry record: the 2BL's entry */
		limit = BLDR_SIZE - FBL_SIZE;
		SecureZeroMemory(key20, sizeof(key20)); SecureZeroMemory(x, sizeof(x));
	}
	if (signature == BOOT_SIGNATURE && entry >= 8 && entry <= (DWORD)limit)
	{
		keys = dword_at(block + entry - 8) & 0xFFFF;
		if (keys + 48 <= (DWORD)limit)
		{
			unsigned char seen[256];
			int distinct = 0;
			memset(seen, 0, sizeof(seen));
			for (k = 0; k < 16; k++) if (!seen[block[keys + 16 + k]]++) distinct++;
			if (distinct >= 8) { memcpy(cert, block + keys + 16, 16); ok = 1; }
		}
	}
	SecureZeroMemory(block, sizeof(block)); SecureZeroMemory(s, sizeof(s));
	return ok;
}
static unsigned char *read_all(const char *path, long *size)
{
	FILE *f = fopen(path, "rb");
	unsigned char *data = NULL;
	*size = -1;
	if (!f) return NULL;
	if (fseek(f, 0, SEEK_END) == 0 && (*size = ftell(f)) > 0 && *size <= 0x200000 && fseek(f, 0, SEEK_SET) == 0 &&
		(data = malloc((size_t)*size)) && fread(data, 1, (size_t)*size, f) != (size_t)*size) { free(data); data = NULL; }
	fclose(f);
	return data;
}
/* 0 and the certificate key in cert when it came out; otherwise the message to show */
static const char *make_keys(const char *bios_path, const char *mcpx_path, unsigned char cert[16])
{
	static char message[600];
	static const int rom_key_at[2] = { 0x1A5, 0x19C };     /* MCPX 1.0; MCPX 1.1 (and the open boot ROMs) */
	long bios_size, mcpx_size;
	unsigned char *bios = read_all(bios_path, &bios_size), *mcpx = read_all(mcpx_path, &mcpx_size);
	const char *result = NULL;
	int fbl = 0, found = 0, r;
	if (!bios) result = "The BIOS file could not be read.";
	else if (!mcpx) result = "The MCPX boot ROM file could not be read.";
	else if (bios_size != 0x40000 && bios_size != 0x80000 && bios_size != 0x100000)
	{
		snprintf(message, sizeof(message), "The BIOS file is %ld bytes. A BIOS image is 256 KB, 512 KB or 1 MB.%s", bios_size,
			bios_size == 512 ? "\n\nThis looks like the boot ROM: it goes in the MCPX Boot ROM field." : "");
		result = message;
	}
	else if (mcpx_size != 512)
	{
		snprintf(message, sizeof(message), "The MCPX boot ROM file is %ld bytes. The boot ROM is 512 bytes (mcpx_1.0.bin or mcpx_1.1.bin).%s", mcpx_size,
			mcpx_size == 0x40000 || mcpx_size == 0x80000 || mcpx_size == 0x100000 ? "\n\nThis looks like a BIOS image: it goes in the BIOS field." : "");
		result = message;
	}
	if (!result)
	{
		const unsigned char *block = bios + bios_size - 0x6200;
		fbl = fbl_present(block);
		/* the layout the BIOS has first, then the other (a BIOS whose FBL is not used, or an unusual one) */
		for (r = 0; r < 2 && !found; r++) found = try_loader(block, mcpx + rom_key_at[r], fbl, cert);
		for (r = 0; r < 2 && !found; r++) found = try_loader(block, mcpx + rom_key_at[r], !fbl, cert);
		if (!found && fbl)
			result = "This BIOS is version 4817 or later, made for the MCPX 1.1 boot ROM (mcpx_1.1.bin), and this boot ROM does "
				"not open it.\n\nEither choose the MCPX 1.1 boot ROM, or keep the 1.0 boot ROM (mcpx_1.0.bin, the one xemu uses) "
				"and choose a BIOS made for it: 3944, 4034, 4134, a 4627 debug BIOS or Complex 4627.";
		else if (!found)
			result = "This BIOS and this boot ROM do not go together: the BIOS's loader does not come out right with it.\n\n"
				"BIOSes before 4817 (3944, 4034, 4134, a 4627 debug BIOS, Complex 4627) go with the MCPX 1.0 boot ROM "
				"(mcpx_1.0.bin, the one xemu uses); 4817 and later go with the MCPX 1.1 boot ROM (mcpx_1.1.bin).";
	}
	if (bios) { SecureZeroMemory(bios, (size_t)bios_size); free(bios); }
	if (mcpx) { SecureZeroMemory(mcpx, (size_t)mcpx_size); free(mcpx); }
	return result;
}
static void browse(HWND field, const char *title)
{
	OPENFILENAMEA o;
	char path[MAX_PATH];
	GetWindowTextA(field, path, sizeof(path));
	memset(&o, 0, sizeof(o));
	o.lStructSize = sizeof(o); o.hwndOwner = g_keys; o.lpstrFile = path; o.nMaxFile = sizeof(path); o.lpstrTitle = title;
	o.lpstrFilter = "ROM images (*.bin;*.rom;*.img)\0*.bin;*.rom;*.img\0All files (*.*)\0*.*\0";
	o.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY | OFN_NOCHANGEDIR;
	if (GetOpenFileNameA(&o)) SetWindowTextA(field, path);
}
static void keys_close(void)
{
	if (!g_keys) return;
	EnableWindow(g_main, TRUE);
	DestroyWindow(g_keys);
	g_keys = NULL;
	SetForegroundWindow(g_main);
}
static void keys_generate(void)
{
	char bios[MAX_PATH], mcpx[MAX_PATH];
	unsigned char cert[16], old[16];
	const char *error;
	FILE *f;
	int have_old = 0;
	GetWindowTextA(g_keys_bios, bios, sizeof(bios));
	GetWindowTextA(g_keys_mcpx, mcpx, sizeof(mcpx));
	if (!bios[0] || !mcpx[0]) { MessageBoxA(g_keys, "Choose both files first.", "Generate keys.bin", MB_OK | MB_ICONINFORMATION); return; }
	if ((error = make_keys(bios, mcpx, cert))) { MessageBoxA(g_keys, error, "Generate keys.bin", MB_OK | MB_ICONERROR); return; }
	if ((f = fopen("h2classic\\keys.bin", "rb")))
	{
		unsigned char all[64];
		size_t n = fread(all, 1, sizeof(all), f);
		fclose(f);
		if (n == 16) { memcpy(old, all, 16); have_old = 1; }
		else if (n == 32) { memcpy(old, all + 16, 16); have_old = 1; }        /* Cxbx-Reloaded's form: EEPROM key, then this key */
		else have_old = 2;
		SecureZeroMemory(all, sizeof(all));
	}
	if (have_old == 1 && !memcmp(old, cert, 16))
	{
		SecureZeroMemory(cert, sizeof(cert)); SecureZeroMemory(old, sizeof(old));
		MessageBoxA(g_keys, "h2classic\\keys.bin is already there and holds the same key: nothing to change.", "Generate keys.bin", MB_OK | MB_ICONINFORMATION);
		keys_close();
		return;
	}
	if (have_old && MessageBoxA(g_keys, have_old == 1 ? "h2classic\\keys.bin is already there with a DIFFERENT key. Replace it?" :
		"h2classic\\keys.bin is already there. Replace it?", "Generate keys.bin", MB_YESNO | MB_ICONQUESTION) != IDYES)
	{
		SecureZeroMemory(cert, sizeof(cert)); SecureZeroMemory(old, sizeof(old));
		return;
	}
	CreateDirectoryA("h2classic", NULL);
	error = NULL;
	if (!(f = fopen("h2classic\\keys.bin", "wb"))) error = "h2classic\\keys.bin could not be written.";
	else
	{
		if (fwrite(cert, 1, 16, f) != 16) error = "h2classic\\keys.bin could not be written.";
		if (fclose(f) != 0) error = "h2classic\\keys.bin could not be written.";
	}
	SecureZeroMemory(cert, sizeof(cert)); SecureZeroMemory(old, sizeof(old));
	if (error) { MessageBoxA(g_keys, error, "Generate keys.bin", MB_OK | MB_ICONERROR); return; }
	MessageBoxA(g_keys, "keys.bin was made in the h2classic folder.", "Generate keys.bin", MB_OK | MB_ICONINFORMATION);
	keys_close();
}

/* the keys.bin window, drawn like the settings */
static part g_kparts[8];
static int g_kpart_count;
static LRESULT CALLBACK keys_proc(HWND w, UINT m, WPARAM wp, LPARAM lp)
{
	switch (m)
	{
	case WM_COMMAND:
		switch (LOWORD(wp))
		{
		case ID_KEYS_BIOS_BROWSE: browse(g_keys_bios, "Choose the BIOS image"); return 0;
		case ID_KEYS_MCPX_BROWSE: browse(g_keys_mcpx, "Choose the MCPX boot ROM"); return 0;
		case ID_KEYS_GENERATE: case IDOK: keys_generate(); return 0;
		case ID_KEYS_CANCEL: case IDCANCEL: case ID_CLOSE: keys_close(); return 0;
		}
		break;
	case WM_ERASEBKGND: return 1;
	case WM_PAINT:
	{
		int i;
		paint_window(w, g_kparts, g_kpart_count, "GENERATE KEYS.BIN");
		/* the path fields' frames */
		{
			HDC dc = GetDC(w);
			HWND fields[2] = { g_keys_bios, g_keys_mcpx };
			for (i = 0; i < 2; i++)
			{
				RECT r; GetWindowRect(fields[i], &r); MapWindowPoints(NULL, w, (POINT *)&r, 2);
				InflateRect(&r, 4, 4);
				cut_frame(dc, &r, 6, 6, C_FIELD, GetFocus() == fields[i] ? C_LINE : C_LINE_DIM, 1);
			}
			ReleaseDC(w, dc);
		}
		RedrawWindow(g_keys_bios, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW);
		RedrawWindow(g_keys_mcpx, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW);
		return 0;
	}
	case WM_CTLCOLOREDIT: SetTextColor((HDC)wp, C_TEXT); SetBkColor((HDC)wp, C_FIELD); return (LRESULT)g_field_brush;
	case WM_CLOSE: keys_close(); return 0;
	case WM_NCHITTEST:                        /* (s10z) no title bar, as the other windows: held at the top (above the title's line), the window moves */
	{
		LRESULT hit = DefWindowProcA(w, m, wp, lp);
		POINT pt;
		pt.x = (short)LOWORD(lp); pt.y = (short)HIWORD(lp);
		ScreenToClient(w, &pt);
		return hit == HTCLIENT && pt.y >= 0 && pt.y < HEADER_DRAG ? HTCAPTION : hit;
	}
	}
	return DefWindowProcA(w, m, wp, lp);
}
#define KEYS_WIDTH 540
#define KEYS_TOP 66                           /* (s10z) what the window's own header (title, line) takes: the parts below are as they were, this much lower */
#define KEYS_HEIGHT (262 + KEYS_TOP)
static void keys_open(void)
{
	RECT main_rect;
	int t = KEYS_TOP;
	if (g_keys) { SetForegroundWindow(g_keys); return; }
	GetWindowRect(g_main, &main_rect);
	/* (s10z) drawn like the settings and the Keyboard + Mouse windows: no title bar of Windows', the title and the close
	   button the program's own */
	g_keys = CreateWindowExA(0, "h2classic_keys", "Generate Xbox keys.bin file", WS_POPUP | WS_SYSMENU,
		main_rect.left + 20, main_rect.top + 80, KEYS_WIDTH, KEYS_HEIGHT, g_main, NULL, GetModuleHandleA(NULL), NULL);
	if (!g_keys) return;
	{
		DWORD round = 2, border = C_LINE_DIM;
		dwm_set(g_keys, 33, &round, sizeof(round));
		dwm_set(g_keys, 34, &border, sizeof(border));
	}
	g_parent = g_keys;
	g_kpart_count = 0;
	add("h2c_caption", "", 0, KEYS_WIDTH - 1 - 46, 1, 46, 31, ID_CLOSE);
	g_kparts[g_kpart_count++] = (part){ 14, t + 14, 512, 182, "YOUR CONSOLE'S FILES", 1 };
	g_kparts[g_kpart_count++] = (part){ 32, t + 44, 300, 22, "BIOS (Flash ROM)", 0 };
	g_kparts[g_kpart_count++] = (part){ 32, t + 112, 300, 22, "MCPX Boot ROM", 0 };
	g_keys_bios = add("EDIT", "", ES_AUTOHSCROLL | WS_TABSTOP, 38, t + 74, 362, 22, ID_KEYS_BIOS);
	button("Browse...", 414, t + 68, 96, ID_KEYS_BIOS_BROWSE, 0);
	g_keys_mcpx = add("EDIT", "", ES_AUTOHSCROLL | WS_TABSTOP, 38, t + 142, 362, 22, ID_KEYS_MCPX);
	button("Browse...", 414, t + 136, 96, ID_KEYS_MCPX_BROWSE, 0);
	button("Generate", 300, t + 212, 110, ID_KEYS_GENERATE, 1);
	button("Cancel", 416, t + 212, 110, ID_KEYS_CANCEL, 0);
	g_parent = g_main;
	EnableWindow(g_main, FALSE);
	ShowWindow(g_keys, SW_SHOW);
	SetFocus(g_keys_bios);
}

/* ---- (s10v) Keyboard + Mouse Settings: its own window, drawn as the settings window is ----
 * Enable Keyboard + Mouse; the mouse's sensitivity, its sensitivity while zoomed and its own look inversion; and the key
 * list: two keys (or mouse buttons, or the wheel) for each of the controller's buttons and each way of its two sticks.
 * Saved in settings.ini as keyboard_mouse, mouse_sensitivity, mouse_zoom_sensitivity, mouse_invert and key_<name>=a,b
 * (Windows' key numbers; 256 is the mouse wheel, 0 no key). A running game takes every one of them at once. */
#define KM_WHEEL 256
#define KM_BINDS 24
static const struct { const char *key, *label; int keys[2]; } k_binds[KM_BINDS] = {
	{ "forward", "Move Forward", { 'W', 0 } }, { "backward", "Move Backward", { 'S', 0 } }, { "strafeleft", "Move Left", { 'A', 0 } }, { "straferight", "Move Right", { 'D', 0 } },
	{ "lookup", "Look Up", { 'I', 0 } }, { "lookdown", "Look Down", { 'K', 0 } }, { "lookleft", "Look Left", { 'J', 0 } }, { "lookright", "Look Right", { 'L', 0 } },
	{ "lthumb", "Left Stick Click (Crouch)", { 'F', 0 } }, { "rthumb", "Right Stick Click (Zoom)", { 'G', VK_MBUTTON } },
	{ "lt", "Left Trigger (Grenade)", { 'Q', VK_RBUTTON } }, { "rt", "Right Trigger (Fire)", { 'E', VK_LBUTTON } },
	{ "a", "A (Jump, Select)", { 'Z', VK_SPACE } }, { "b", "B (Melee, Back)", { 'X', 0 } }, { "x", "X (Reload, Action)", { 'C', 0 } }, { "y", "Y (Switch Weapon)", { 'V', KM_WHEEL } },
	{ "white", "White (Flashlight)", { '1', 0 } }, { "black", "Black (Switch Grenade)", { '2', 0 } },
	{ "up", "D-Pad Up", { VK_UP, 0 } }, { "down", "D-Pad Down", { VK_DOWN, 0 } }, { "left", "D-Pad Left", { VK_LEFT, 0 } }, { "right", "D-Pad Right", { VK_RIGHT, 0 } },
	{ "start", "Start", { VK_RETURN, 0 } }, { "back", "Back", { VK_BACK, VK_ESCAPE } } };
#define SENS_BASE 0.1
#define SENS_UNIT 0.1
#define SENS_STEPS 99                         /* 0.1 to 10.0 */
#define SENS_DEFAULT "2.5"
#define ZOOM_BASE 0.10
#define ZOOM_UNIT 0.05
#define ZOOM_STEPS 38                         /* 0.10 to 2.00 */
#define ZOOM_DEFAULT "1.00"
static HWND g_km, g_km_enable, g_km_sens, g_km_zoom, g_km_invert, g_km_bind[KM_BINDS][2];
static part g_mparts[40];
static int g_mpart_count, g_km_loading, g_km_waiting;

static void key_name(int code, char *out, size_t size)
{
	static const struct { int code; const char *name; } names[] = {
		{ VK_LBUTTON, "Left Mouse" }, { VK_RBUTTON, "Right Mouse" }, { VK_MBUTTON, "Middle Mouse" }, { VK_XBUTTON1, "Mouse 4" }, { VK_XBUTTON2, "Mouse 5" }, { KM_WHEEL, "Mouse Wheel" },
		{ VK_SPACE, "Space" }, { VK_RETURN, "Enter" }, { VK_BACK, "Backspace" }, { VK_ESCAPE, "Esc" }, { VK_TAB, "Tab" },
		{ VK_UP, "Up Arrow" }, { VK_DOWN, "Down Arrow" }, { VK_LEFT, "Left Arrow" }, { VK_RIGHT, "Right Arrow" },
		{ VK_SHIFT, "Shift" }, { VK_CONTROL, "Ctrl" }, { VK_MENU, "Alt" }, { VK_LSHIFT, "Left Shift" }, { VK_RSHIFT, "Right Shift" },
		{ VK_LCONTROL, "Left Ctrl" }, { VK_RCONTROL, "Right Ctrl" }, { VK_LMENU, "Left Alt" }, { VK_RMENU, "Right Alt" },
		{ VK_CAPITAL, "Caps Lock" }, { VK_INSERT, "Insert" }, { VK_DELETE, "Delete" }, { VK_HOME, "Home" }, { VK_END, "End" }, { VK_PRIOR, "Page Up" }, { VK_NEXT, "Page Down" },
		{ VK_PAUSE, "Pause" }, { VK_SCROLL, "Scroll Lock" }, { VK_NUMLOCK, "Num Lock" }, { VK_CLEAR, "Num 5 (Clear)" }, { VK_APPS, "Menu Key" },
		{ VK_MULTIPLY, "Num *" }, { VK_ADD, "Num +" }, { VK_SUBTRACT, "Num -" }, { VK_DECIMAL, "Num ." }, { VK_DIVIDE, "Num /" } };
	unsigned i;
	UINT ch;
	if (code <= 0) { snprintf(out, size, "-"); return; }
	for (i = 0; i < sizeof(names) / sizeof(names[0]); i++) if (names[i].code == code) { snprintf(out, size, "%s", names[i].name); return; }
	if ((code >= '0' && code <= '9') || (code >= 'A' && code <= 'Z')) { snprintf(out, size, "%c", code); return; }
	if (code >= VK_NUMPAD0 && code <= VK_NUMPAD9) { snprintf(out, size, "Num %d", code - VK_NUMPAD0); return; }
	if (code >= VK_F1 && code <= VK_F24) { snprintf(out, size, "F%d", code - VK_F1 + 1); return; }
	ch = MapVirtualKeyA((UINT)code, 2) & 0x7fff;                  /* (MAPVK_VK_TO_CHAR: the key's character on this keyboard) */
	if (ch > 32 && ch < 127) snprintf(out, size, "%c", (int)ch);
	else snprintf(out, size, "Key %d", code);
}

/* a slider: a value on a line - a click or a drag, the wheel, Left / Right (Page Up / Down: ten steps; Home, End) */
#define SLIDER_LEFT 16
#define SLIDER_VALUE 64                       /* the value's text, at the right */
static void slider_put(HWND w, int pos, int tell)
{
	ctl *c = ctl_of(w);
	if (!c) return;
	pos = pos < 0 ? 0 : pos > c->steps ? c->steps : pos;
	if (pos == c->pos) return;
	c->pos = pos;
	InvalidateRect(w, NULL, FALSE);
	if (tell) notify(w, CBN_SELCHANGE);
}
static int slider_at(HWND w, int x)
{
	ctl *c = ctl_of(w);
	RECT r;
	int span;
	GetClientRect(w, &r);
	span = r.right - SLIDER_VALUE - SLIDER_LEFT;
	if (!c || span < 1 || x <= SLIDER_LEFT) return 0;
	return (int)((double)(x - SLIDER_LEFT) * c->steps / span + 0.5);
}
static LRESULT CALLBACK slider_proc(HWND w, UINT m, WPARAM wp, LPARAM lp)
{
	ctl *c = ctl_of(w);
	int handled;
	LRESULT r;
	if (m == WM_MOUSEMOVE && c && c->drag) slider_put(w, slider_at(w, (short)LOWORD(lp)), 1);
	r = common(w, m, wp, lp, &handled);
	if (handled) return r;
	switch (m)
	{
	case SL_SET: if (c) { int pos = (int)wp; c->pos = pos < 0 ? 0 : pos > c->steps ? c->steps : pos; InvalidateRect(w, NULL, FALSE); } return 0;
	case SL_GET: return c ? c->pos : 0;
	case WM_GETDLGCODE: return DLGC_WANTARROWS | DLGC_WANTCHARS;
	case WM_LBUTTONDOWN: case WM_LBUTTONDBLCLK:
		if (GetFocus() != w) SetFocus(w);
		SetCapture(w);
		if (c) c->drag = 1;
		slider_put(w, slider_at(w, (short)LOWORD(lp)), 1);
		return 0;
	case WM_LBUTTONUP: if (c && c->drag) { c->drag = 0; ReleaseCapture(); } return 0;
	case WM_CAPTURECHANGED: if (c) c->drag = 0; return 0;
	case WM_MOUSEWHEEL: if (c) slider_put(w, c->pos + ((short)HIWORD(wp) > 0 ? 1 : -1), 1); return 0;
	case WM_KEYDOWN:
		if (!c) return 0;
		if (wp == VK_LEFT || wp == VK_DOWN) slider_put(w, c->pos - 1, 1);
		else if (wp == VK_RIGHT || wp == VK_UP) slider_put(w, c->pos + 1, 1);
		else if (wp == VK_PRIOR) slider_put(w, c->pos + 10, 1);
		else if (wp == VK_NEXT) slider_put(w, c->pos - 10, 1);
		else if (wp == VK_HOME) slider_put(w, 0, 1);
		else if (wp == VK_END) slider_put(w, c->steps, 1);
		return 0;
	case WM_PAINT:
	{
		paint_buffer p;
		HDC dc = paint_begin(w, &p);
		int on = IsWindowEnabled(w), focus = on && GetFocus() == w, mid = p.r.bottom / 2;
		int left = SLIDER_LEFT, right = p.r.right - SLIDER_VALUE, x = c && c->steps ? left + (int)((double)(right - left) * c->pos / c->steps + 0.5) : left;
		RECT track = { left, mid - 2, right, mid + 2 }, done = { left, mid - 2, x, mid + 2 }, thumb = { x - 4, mid - 9, x + 5, mid + 9 }, t = p.r;
		char text[32];
		fill(dc, &p.r, C_PANEL);
		cut_frame(dc, &p.r, 7, 7, on && c && (c->hot || focus) ? C_FIELD_HOT : C_FIELD, focus ? C_LINE : C_LINE_DIM, 1);
		fill(dc, &track, C_LINE_DIM); fill(dc, &done, on ? C_LINE : C_OFF); fill(dc, &thumb, on ? C_TEXT : C_OFF);
		snprintf(text, sizeof(text), "%.*f", c ? c->decimals : 1, c ? c->base + c->pos * c->unit : 0.0);
		t.left = right + 8; t.right -= 14;
		text_at(dc, text, &t, g_font, on ? C_TEXT : C_OFF, DT_RIGHT | DT_VCENTER | DT_SINGLELINE, 0);
		paint_end(&p);
		return 0;
	}
	}
	return DefWindowProcA(w, m, wp, lp);
}
static HWND slider(const char *label, int x, int y, int width, int id, double base, double unit, int steps, int decimals)
{
	HWND s = add("h2c_slider", label, WS_TABSTOP, x, y, width, 30, id);
	ctl *c = s ? ctl_of(s) : NULL;
	if (c) { c->base = base; c->unit = unit; c->steps = steps; c->decimals = decimals; }
	return s;
}

/* a key: shows the key it holds. A click (or Space), and the next key, mouse button or turn of the wheel is the new one -
 * a mouse button counts when it is pressed on the field itself; a click anywhere else leaves the key as it was. A right
 * click (or Delete) on a field that is not waiting empties it. */
static void bind_put(HWND w, int code)
{
	ctl *c = ctl_of(w);
	if (!c) return;
	c->code = code; c->waiting = 0;
	InvalidateRect(w, NULL, FALSE);
	notify(w, CBN_SELCHANGE);
}
static void bind_wait(HWND w)
{
	ctl *c = ctl_of(w);
	if (!c || !IsWindowEnabled(w)) return;
	c->waiting = 1; c->since = GetTickCount();
	InvalidateRect(w, NULL, FALSE);
}
static LRESULT CALLBACK bind_proc(HWND w, UINT m, WPARAM wp, LPARAM lp)
{
	ctl *c = ctl_of(w);
	int handled, code;
	LRESULT r;
	if (m == WM_KILLFOCUS && c) c->waiting = 0;
	r = common(w, m, wp, lp, &handled);
	if (handled) return r;
	switch (m)
	{
	case BD_SET: if (c) { c->code = (int)wp; c->waiting = 0; InvalidateRect(w, NULL, FALSE); } return 0;
	case BD_GET: return c ? c->code : 0;
	case BD_WAITING: return c ? c->waiting : 0;
	case WM_GETDLGCODE: return c && c->waiting ? DLGC_WANTALLKEYS : DLGC_WANTCHARS;
	case WM_LBUTTONDOWN: case WM_LBUTTONDBLCLK:
		if (c && c->waiting && GetFocus() == w) { if (GetTickCount() - c->since > 350) bind_put(w, VK_LBUTTON); return 0; }   /* (not the second click of a double click) */
		SetFocus(w); bind_wait(w);
		return 0;
	case WM_RBUTTONDOWN: if (c && c->waiting) bind_put(w, VK_RBUTTON); else { SetFocus(w); bind_put(w, 0); } return 0;
	case WM_MBUTTONDOWN: if (c && c->waiting) bind_put(w, VK_MBUTTON); return 0;
	case WM_XBUTTONDOWN: if (c && c->waiting) bind_put(w, HIWORD(wp) == XBUTTON1 ? VK_XBUTTON1 : VK_XBUTTON2); return TRUE;
	case WM_MOUSEWHEEL: if (c && c->waiting) bind_put(w, KM_WHEEL); return 0;
	case WM_CHAR: if (c && c->skip_char) c->skip_char = 0; else if (wp == ' ' && c && !c->waiting) bind_wait(w); return 0;
	case WM_SYSCHAR: if (c && c->waiting) return 0; break;
	case WM_KEYUP: case WM_SYSKEYUP: if (c) c->skip_char = 0; if (m == WM_KEYUP || (c && c->waiting)) return 0; break;
	case WM_KEYDOWN: case WM_SYSKEYDOWN:
		if (!c) break;
		if (!c->waiting)
		{
			if (m == WM_SYSKEYDOWN) break;
			if (wp == VK_DELETE) bind_put(w, 0);
			return 0;
		}
		if (lp & 0x40000000) return 0;                                /* (a key held down, repeating) */
		code = (int)wp;
		if (code == VK_SHIFT) { code = (int)MapVirtualKeyA((UINT)(lp >> 16) & 0xff, 3); if (code != VK_LSHIFT && code != VK_RSHIFT) code = VK_SHIFT; }   /* (MAPVK_VSC_TO_VK_EX: left or right) */
		else if (code == VK_CONTROL) code = lp & 0x01000000 ? VK_RCONTROL : VK_LCONTROL;
		else if (code == VK_MENU) code = lp & 0x01000000 ? VK_RMENU : VK_LMENU;
		/* not for the game: the Windows keys, Print Screen, and the key H2Classic itself listens to (F9); (s10x) F11 and F12 are free */
		if (code <= 0 || code > 255 || code == VK_LWIN || code == VK_RWIN || code == VK_SNAPSHOT || code == VK_F9) return 0;
		c->skip_char = 1;
		bind_put(w, code);
		return 0;
	case WM_PAINT:
	{
		paint_buffer p;
		HDC dc = paint_begin(w, &p);
		int on = IsWindowEnabled(w), focus = on && GetFocus() == w, waiting = on && c && c->waiting;
		RECT t = p.r;
		char text[40];
		fill(dc, &p.r, C_PANEL);
		cut_frame(dc, &p.r, 6, 6, waiting ? C_FIELD_HOT : on && c && c->hot ? C_FIELD_HOT : C_FIELD, waiting ? C_TEXT : focus ? C_LINE : C_LINE_DIM, 1);
		if (waiting) snprintf(text, sizeof(text), "Press a key");
		else key_name(c ? c->code : 0, text, sizeof(text));
		t.left += 6; t.right -= 6;
		{	/* a name too long for the field in the usual letters: smaller ones, then the narrow ones */
			HFONT fonts[3] = { waiting ? g_note_font : g_font, g_note_font, g_note_narrow_font ? g_note_narrow_font : g_note_font }, f = fonts[0];
			int k;
			for (k = 0; k < 3; k++)
			{
				HFONT old = SelectObject(dc, fonts[k]);
				SIZE size;
				BOOL fits = GetTextExtentPoint32A(dc, text, (int)strlen(text), &size) && size.cx <= t.right - t.left;
				SelectObject(dc, old);
				f = fonts[k];
				if (fits) break;
			}
			text_at(dc, text, &t, f, !on ? C_OFF : waiting ? C_LINE : c && c->code > 0 ? C_TEXT : C_OFF, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, 0);
		}
		paint_end(&p);
		return 0;
	}
	}
	return DefWindowProcA(w, m, wp, lp);
}

/* settings -> the window's controls */
static void km_show(void)
{
	int i, on;
	double v;
	g_km_loading = 1;
	on = atoi(ini_get("keyboard_mouse", "1")) != 0;
	tick(g_km_enable, on);
	v = atof(ini_get("mouse_sensitivity", SENS_DEFAULT));
	SendMessageA(g_km_sens, SL_SET, (WPARAM)(int)((v - SENS_BASE) / SENS_UNIT + 0.5), 0);
	v = atof(ini_get("mouse_zoom_sensitivity", ZOOM_DEFAULT));
	SendMessageA(g_km_zoom, SL_SET, (WPARAM)(int)((v - ZOOM_BASE) / ZOOM_UNIT + 0.5), 0);
	tick(g_km_invert, atoi(ini_get("mouse_invert", "0")));
	for (i = 0; i < KM_BINDS; i++)
	{
		char key[40];
		const char *value;
		int a = k_binds[i].keys[0], b = k_binds[i].keys[1], ra = 0, rb = 0;
		snprintf(key, sizeof(key), "key_%s", k_binds[i].key);
		value = ini_get(key, "");
		if (value[0] && sscanf(value, "%d,%d", &ra, &rb) >= 1 && ra >= 0 && ra <= KM_WHEEL && rb >= 0 && rb <= KM_WHEEL) { a = ra; b = rb; }
		SendMessageA(g_km_bind[i][0], BD_SET, (WPARAM)a, 0);
		SendMessageA(g_km_bind[i][1], BD_SET, (WPARAM)b, 0);
	}
	EnableWindow(g_km_sens, on); EnableWindow(g_km_zoom, on); EnableWindow(g_km_invert, on);
	for (i = 0; i < KM_BINDS; i++) { EnableWindow(g_km_bind[i][0], on); EnableWindow(g_km_bind[i][1], on); }
	g_km_loading = 0;
}
/* the window's controls -> settings, saved (and passed on to a running game) */
static void km_save(void)
{
	char text[40], key[40];
	int i;
	if (g_km_loading || !g_km) return;
	ini_set("keyboard_mouse", ticked(g_km_enable) ? "1" : "0");
	snprintf(text, sizeof(text), "%.1f", SENS_BASE + SENS_UNIT * (int)SendMessageA(g_km_sens, SL_GET, 0, 0)); ini_set("mouse_sensitivity", text);
	snprintf(text, sizeof(text), "%.2f", ZOOM_BASE + ZOOM_UNIT * (int)SendMessageA(g_km_zoom, SL_GET, 0, 0)); ini_set("mouse_zoom_sensitivity", text);
	ini_set("mouse_invert", ticked(g_km_invert) ? "1" : "0");
	for (i = 0; i < KM_BINDS; i++)
	{
		snprintf(key, sizeof(key), "key_%s", k_binds[i].key);
		snprintf(text, sizeof(text), "%d,%d", (int)SendMessageA(g_km_bind[i][0], BD_GET, 0, 0), (int)SendMessageA(g_km_bind[i][1], BD_GET, 0, 0));
		ini_set(key, text);
	}
	ini_save();
	game_signal();
}
static void km_save_soon(void) { if (g_km && !g_km_loading) { g_km_waiting = 1; SetTimer(g_km, 3, 250, NULL); } }
static void km_save_now(void) { if (g_km) KillTimer(g_km, 3); if (g_km_waiting) { g_km_waiting = 0; km_save(); } }
static void km_reset(void)
{
	int i;
	char key[40];
	if (g_km) KillTimer(g_km, 3);
	g_km_waiting = 0;
	ini_remove("keyboard_mouse"); ini_remove("mouse_sensitivity"); ini_remove("mouse_zoom_sensitivity"); ini_remove("mouse_invert");
	for (i = 0; i < KM_BINDS; i++) { snprintf(key, sizeof(key), "key_%s", k_binds[i].key); ini_remove(key); }
	km_show();                              /* the defaults: shown with nothing saved, then saved */
	km_save();
	InvalidateRect(g_km, NULL, FALSE);
}
static void km_close(void)
{
	if (!g_km) return;
	km_save_now();
	EnableWindow(g_main, TRUE);
	DestroyWindow(g_km);
	g_km = NULL;
	SetForegroundWindow(g_main);
}
/* a key was given to a field: no other field keeps the same key (the wheel too) */
static void km_bind_changed(HWND field)
{
	int code = (int)SendMessageA(field, BD_GET, 0, 0), i, k;
	if (code > 0)
		for (i = 0; i < KM_BINDS; i++) for (k = 0; k < 2; k++)
			if (g_km_bind[i][k] != field && (int)SendMessageA(g_km_bind[i][k], BD_GET, 0, 0) == code) SendMessageA(g_km_bind[i][k], BD_SET, 0, 0);
	km_save_soon();
}
static LRESULT CALLBACK km_proc(HWND w, UINT m, WPARAM wp, LPARAM lp)
{
	switch (m)
	{
	case WM_COMMAND:
	{
		int id = LOWORD(wp);
		if (id >= ID_KM_BIND && id < ID_KM_BIND + KM_BINDS * 2) { if (HIWORD(wp) == CBN_SELCHANGE) km_bind_changed((HWND)lp); return 0; }
		switch (id)
		{
		case ID_KM_ENABLE:
			if (HIWORD(wp) == BN_CLICKED)
			{
				int i, on = ticked(g_km_enable);
				EnableWindow(g_km_sens, on); EnableWindow(g_km_zoom, on); EnableWindow(g_km_invert, on);
				for (i = 0; i < KM_BINDS; i++) { EnableWindow(g_km_bind[i][0], on); EnableWindow(g_km_bind[i][1], on); }
				km_save_soon();
			}
			return 0;
		case ID_KM_INVERT: if (HIWORD(wp) == BN_CLICKED) km_save_soon(); return 0;
		case ID_KM_SENS: case ID_KM_ZOOM: if (HIWORD(wp) == CBN_SELCHANGE) km_save_soon(); return 0;
		case ID_KM_RESET: km_reset(); return 0;
		case ID_KM_DONE: case ID_CLOSE: case IDOK: case IDCANCEL: km_close(); return 0;
		}
		break;
	}
	case WM_ERASEBKGND: return 1;
	case WM_PAINT: paint_window(w, g_mparts, g_mpart_count, "KEYBOARD + MOUSE SETTINGS"); return 0;
	case WM_TIMER: if (wp == 3) km_save_now(); return 0;
	case WM_CLOSE: km_close(); return 0;
	case WM_NCHITTEST:                        /* no title bar: held at the top (above the title's line), the window moves */
	{
		LRESULT hit = DefWindowProcA(w, m, wp, lp);
		POINT pt;
		pt.x = (short)LOWORD(lp); pt.y = (short)HIWORD(lp);
		ScreenToClient(w, &pt);
		return hit == HTCLIENT && pt.y >= 0 && pt.y < HEADER_DRAG ? HTCAPTION : hit;
	}
	}
	return DefWindowProcA(w, m, wp, lp);
}
#define KM_WIDTH 924
#define KM_HEIGHT 770
static void km_open(void)
{
	RECT main_rect, work;
	int x = 26, panel = KM_WIDTH - 52, y, i, left, top;
	if (g_km) { SetForegroundWindow(g_km); return; }
	save_now();                             /* (what the settings window still had to write) */
	GetWindowRect(g_main, &main_rect);
	if (!SystemParametersInfoA(SPI_GETWORKAREA, 0, &work, 0)) { work.left = work.top = 0; work.right = GetSystemMetrics(SM_CXSCREEN); work.bottom = GetSystemMetrics(SM_CYSCREEN); }
	/* over the settings window, its middle on that window's middle, kept on the screen */
	left = (main_rect.left + main_rect.right - KM_WIDTH) / 2; top = (main_rect.top + main_rect.bottom - KM_HEIGHT) / 2;
	if (left + KM_WIDTH > work.right) left = work.right - KM_WIDTH;
	if (left < work.left) left = work.left;
	if (top + KM_HEIGHT > work.bottom) top = work.bottom - KM_HEIGHT;
	if (top < work.top) top = work.top;
	g_km = CreateWindowExA(0, "h2classic_km", "Keyboard + Mouse Settings", WS_POPUP | WS_SYSMENU, left, top, KM_WIDTH, KM_HEIGHT, g_main, NULL, GetModuleHandleA(NULL), NULL);
	if (!g_km) return;
	{
		DWORD round = 2, border = C_LINE_DIM;
		dwm_set(g_km, 33, &round, sizeof(round));
		dwm_set(g_km, 34, &border, sizeof(border));
	}
	g_parent = g_km;
	g_mpart_count = 0;
	add("h2c_caption", "", 0, KM_WIDTH - 1 - 46, 1, 46, 31, ID_CLOSE);
	y = 80;
	g_km_enable = add("h2c_toggle", "Enable Keyboard + Mouse", WS_TABSTOP, x + 4, y, 300, 26, ID_KM_ENABLE);
	y += 38;
	/* MOUSE */
	g_mparts[g_mpart_count++] = (part){ x, y, panel, 144, "MOUSE", 1 };
	g_mparts[g_mpart_count++] = (part){ x + 16, y + 30, 190, 30, "Mouse Sensitivity", 0 };
	g_km_sens = slider("Mouse Sensitivity", x + 212, y + 30, 420, ID_KM_SENS, SENS_BASE, SENS_UNIT, SENS_STEPS, 1);
	g_mparts[g_mpart_count++] = (part){ x + 16, y + 66, 190, 30, "Zoom Sensitivity", 0 };
	g_km_zoom = slider("Zoom Sensitivity", x + 212, y + 66, 420, ID_KM_ZOOM, ZOOM_BASE, ZOOM_UNIT, ZOOM_STEPS, 2);
	g_km_invert = add("h2c_toggle", "Invert Look (Mouse Only)", WS_TABSTOP, x + 20, y + 106, 300, 26, ID_KM_INVERT);
	g_mparts[g_mpart_count++] = (part){ x + 652, y + 30, panel - 668, 104, "Zoom Sensitivity 1.00 is the game's own rule: the look slows down by the zoom's magnification. The mouse does not use the game's Look Inversion setting.", 2 };
	y += 156;
	/* KEY ASSIGNMENTS */
	g_mparts[g_mpart_count++] = (part){ x, y, panel, 30 + 12 * 30 + 8, "KEY ASSIGNMENTS", 1 };
	for (i = 0; i < KM_BINDS; i++)
	{
		int lx = x + 16 + (i / 12) * 430, ly = y + 30 + (i % 12) * 30;
		g_mparts[g_mpart_count++] = (part){ lx, ly, 194, 26, k_binds[i].label, 0 };
		g_km_bind[i][0] = add("h2c_bind", "", WS_TABSTOP, lx + 196, ly, 104, 26, ID_KM_BIND + i * 2);
		g_km_bind[i][1] = add("h2c_bind", "", WS_TABSTOP, lx + 306, ly, 104, 26, ID_KM_BIND + i * 2 + 1);
	}
	y += 30 + 12 * 30 + 8 + 10;
	g_mparts[g_mpart_count++] = (part){ x + 2, y, panel - 4, 18, "Click a key, then press the new key, mouse button or wheel (click the field again for the left mouse button). Right-click a key to clear it.", 3 };
	y += 30;
	button("Reset to Defaults", x, y, 180, ID_KM_RESET, 0);
	button("Close", x + panel - 110, y, 110, ID_KM_DONE, 1);
	g_parent = g_main;
	km_show();
	EnableWindow(g_main, FALSE);
	ShowWindow(g_km, SW_SHOW);
	SetFocus(g_km_enable);
}

static LRESULT CALLBACK proc(HWND w, UINT m, WPARAM wp, LPARAM lp)
{
	switch (m)
	{
	case WM_COMMAND:
		switch (LOWORD(wp))
		{
		case ID_START: case IDOK: if (!g_running) start(); return 0;
		case ID_RESET: reset(); return 0;
		case ID_EXIT: case IDCANCEL: case ID_CLOSE: save_now(); PostQuitMessage(0); return 0;
		case ID_MINIMIZE: ShowWindow(w, SW_MINIMIZE); return 0;
		case ID_KEYS: keys_open(); return 0;
		case ID_KM: km_open(); return 0;
		case ID_AA: if (HIWORD(wp) == CBN_SELCHANGE) { aa_quality_fill(); aa_note(); save_soon(); } return 0;
		case ID_AAQ: if (HIWORD(wp) == CBN_SELCHANGE) { aa_note(); save_soon(); } return 0;
		case ID_MODE: if (HIWORD(wp) == CBN_SELCHANGE) { EnableWindow(g_size, pick(g_mode) == 2); save_soon(); } return 0;   /* (s10w) */
		case ID_SCALE: case ID_SIZE: case ID_WIDE: case ID_VERSION: if (HIWORD(wp) == CBN_SELCHANGE) save_soon(); return 0;
		case ID_UNLOCK: case ID_VSYNC: case ID_FPS: case ID_SKIPINTRO: case ID_SKIPATTRACT: if (HIWORD(wp) == BN_CLICKED) save_soon(); return 0;
		}
		break;
	case WM_ERASEBKGND: return 1;
	case WM_PAINT:
	{
		/* the AA note is one of the parts, its text the current one */
		paint_window(w, g_parts, g_part_count, "H2CLASSIC SETTINGS");
		return 0;
	}
	case WM_TIMER: if (wp == 1) save_now(); else if (wp == 2) game_watch(); return 0;
	case WM_ACTIVATE: if (LOWORD(wp) != WA_INACTIVE) PostMessageA(w, WM_TIMER, 2, 0); break;   /* (s10u) brought forward: over a full-screen game at once */
	case WM_CLOSE: save_now(); PostQuitMessage(0); return 0;
	case WM_NCHITTEST:                        /* (s10j) no title bar: held at the top (above the title's line), the window moves */
	{
		LRESULT hit = DefWindowProcA(w, m, wp, lp);
		POINT pt;
		pt.x = (short)LOWORD(lp); pt.y = (short)HIWORD(lp);
		ScreenToClient(w, &pt);
		return hit == HTCLIENT && pt.y >= 0 && pt.y < HEADER_DRAG ? HTCAPTION : hit;
	}
	}
	return DefWindowProcA(w, m, wp, lp);
}

static void register_class(HINSTANCE instance, const char *name, WNDPROC fn, HICON icon, HICON small_icon, UINT style)
{
	WNDCLASSEXA wc;
	memset(&wc, 0, sizeof(wc));
	wc.cbSize = sizeof(wc); wc.lpfnWndProc = fn; wc.hInstance = instance; wc.lpszClassName = name;
	wc.hCursor = LoadCursorA(NULL, (LPCSTR)IDC_ARROW); wc.hIcon = icon; wc.hIconSm = small_icon;
	wc.style = style;
	RegisterClassExA(&wc);
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous, LPSTR command, int show_mode)
{
	MSG msg;
	RECT r = { 0, 0, 470, 878 }, work;
	char folder[MAX_PATH], *slash;
	HICON icon, small_icon;
	int y, x = 26, panel = 418;
	(void)previous; (void)command;
	{	/* (s10u) this window stays open while the game runs, so it may be open already: that one is shown, no second one */
		HWND other = FindWindowA("h2classic_settings", NULL);
		if (other) { if (IsIconic(other)) ShowWindow(other, SW_RESTORE); SetForegroundWindow(other); return 0; }
	}
	/* this folder: where H2Classic.exe and the h2classic folder are */
	GetModuleFileNameA(NULL, folder, sizeof(folder));
	if ((slash = strrchr(folder, '\\'))) { *slash = 0; SetCurrentDirectoryA(folder); }
	icon = (HICON)LoadImageA(instance, MAKEINTRESOURCEA(1), IMAGE_ICON, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), 0);
	small_icon = (HICON)LoadImageA(instance, MAKEINTRESOURCEA(1), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);
	if (!icon) icon = LoadIconA(NULL, (LPCSTR)IDI_APPLICATION);
	register_class(instance, "h2classic_settings", proc, icon, small_icon, CS_DROPSHADOW);   /* (s10j) no title bar: a shadow of its own */
	register_class(instance, "h2classic_keys", keys_proc, icon, small_icon, 0);
	register_class(instance, "h2classic_km", km_proc, icon, small_icon, CS_DROPSHADOW);
	register_class(instance, "h2c_slider", slider_proc, NULL, NULL, 0);
	register_class(instance, "h2c_bind", bind_proc, NULL, NULL, 0);
	register_class(instance, "h2c_choice", choice_proc, NULL, NULL, 0);     /* (s10i) no CS_DBLCLKS: every click steps */
	register_class(instance, "h2c_toggle", toggle_proc, NULL, NULL, 0);
	register_class(instance, "h2c_button", button_proc, NULL, NULL, 0);
	register_class(instance, "h2c_caption", caption_proc, NULL, NULL, 0);
	make_fonts();
	/* (s10j) no title bar: a popup window, in the middle of the screen's work area, with its own minimize and close
	 * buttons; Windows 11 rounds its corners and draws its border in the window's line colour */
	if (!SystemParametersInfoA(SPI_GETWORKAREA, 0, &work, 0)) { work.left = work.top = 0; work.right = GetSystemMetrics(SM_CXSCREEN); work.bottom = GetSystemMetrics(SM_CYSCREEN); }
	g_main = CreateWindowExA(WS_EX_APPWINDOW, "h2classic_settings", "H2Classic Settings", WS_POPUP | WS_SYSMENU | WS_MINIMIZEBOX,
		work.left + (work.right - work.left - r.right) / 2, work.top + (work.bottom - work.top - r.bottom > 0 ? (work.bottom - work.top - r.bottom) / 2 : 0),
		r.right, r.bottom, NULL, NULL, instance, NULL);
	{
		DWORD round = 2, border = C_LINE_DIM;  /* (DWMWCP_ROUND; DWMWA_WINDOW_CORNER_PREFERENCE 33, DWMWA_BORDER_COLOR 34) */
		dwm_set(g_main, 33, &round, sizeof(round));
		dwm_set(g_main, 34, &border, sizeof(border));
	}
	g_parent = g_main;
	add("h2c_caption", "", 0, r.right - 1 - 92, 1, 46, 31, ID_MINIMIZE);
	add("h2c_caption", "", 0, r.right - 1 - 46, 1, 46, 31, ID_CLOSE);
	y = 82;
	/* DISPLAY */
	part_add(x, y, panel, 176, "DISPLAY", 1);
	g_mode = choice("Display Mode", x + 16, y + 30, ID_MODE, 1);
	item(g_mode, "Full Screen"); item(g_mode, "Borderless Windowed"); item(g_mode, "Windowed");
	g_size = choice("Window Size", x + 16, y + 66, ID_SIZE, 0);
	make_sizes();
	for (int i = 0; i < g_size_count; i++) { char s[32]; snprintf(s, sizeof(s), "%d x %d", g_sizes[i][0], g_sizes[i][1]); item(g_size, s); }
	g_scale = choice("Render Scale", x + 16, y + 102, ID_SCALE, 0);
	for (int i = 1; i <= SCALE_MOST; i++) { char s[32]; snprintf(s, sizeof(s), "x%d  (%dp)", i, 480 * i); item(g_scale, s); }
	g_wide = choice("Aspect Ratio", x + 16, y + 138, ID_WIDE, 0);
	item(g_wide, "4:3"); item(g_wide, "16:9"); item(g_wide, "21:9"); item(g_wide, "32:9");
	y += 188;
	/* PERFORMANCE */
	part_add(x, y, panel, 118, "PERFORMANCE", 1);
	g_unlock = toggle("Unlock Performance", x + 20, y + 30, ID_UNLOCK);
	g_vsync = toggle("VSync", x + 20, y + 58, ID_VSYNC);
	g_fps = toggle("FPS Counter", x + 20, y + 86, ID_FPS);
	y += 130;
	/* ANTI-ALIASING */
	part_add(x, y, panel, 142, "ANTI-ALIASING", 1);
	g_aa = choice("Anti-Aliasing", x + 16, y + 30, ID_AA, 1);
	item(g_aa, "Off"); item(g_aa, "FXAA"); item(g_aa, "SMAA"); item(g_aa, "TAA"); item(g_aa, "DLSS (NVIDIA RTX)");
	g_aaq = choice("Quality", x + 16, y + 66, ID_AAQ, 0);
	g_aa_note_rect.left = x + 16; g_aa_note_rect.top = y + 100; g_aa_note_rect.right = x + panel - 16; g_aa_note_rect.bottom = y + 136;
	part_add(g_aa_note_rect.left, g_aa_note_rect.top, g_aa_note_rect.right - g_aa_note_rect.left, 36, g_aa_note_text, 2);
	y += 154;
	/* GAME */
	g_game_part_first = g_game_title_part = g_part_count;
	part_add(x, y, panel, 126, "GAME", 1);
	g_version = choice("Game Version", x + 16, y + 30, ID_VERSION, 1);     /* (s10q) */
	item(g_version, "1.0"); item(g_version, "1.5");
	g_skipintro = toggle("Skip Intro Video", x + 20, y + 66, ID_SKIPINTRO);
	g_skipattract = toggle("Skip Attract Idle Video", x + 20, y + 94, ID_SKIPATTRACT);
	g_game_part_last = g_part_count - 1;
	y += 138;
	/* (s10v) the keyboard and the mouse: their own window */
	button("Keyboard + Mouse Settings", x, y, 300, ID_KM, 0);
	y += 44;
	/* the keys */
	button("Generate Xbox keys.bin file", x, y, 300, ID_KEYS, 0);
	part_add(x + 2, y + 40, panel - 4, 18, "(used for Xbox save compatibility and crossplay with xemu)", 3);   /* (s10j) one line; (s10y) "Xbox/" out: system link with a console is not offered */
	y += 72;
	g_start = button("Start Game", x, y, 150, ID_START, 1);
	button("Reset to Defaults", x + 158, y, 180, ID_RESET, 0);
	button("Exit", x + 346, y, 72, ID_EXIT, 0);
	ini_load();
	show();
	if (g_line_count == 0) save();         /* (a first start: the defaults are written, so the game starts with them too) */
	game_watch();                          /* (s10u) opened while the game runs: the GAME part greyed out from the start */
	SetTimer(g_main, 2, 250, NULL);
	ShowWindow(g_main, show_mode);
	while (GetMessageA(&msg, NULL, 0, 0) > 0)
		if (!(g_keys && IsDialogMessageA(g_keys, &msg)) && !(g_km && IsDialogMessageA(g_km, &msg)) && !IsDialogMessageA(g_main, &msg)) { TranslateMessage(&msg); DispatchMessageA(&msg); }
	return 0;
}

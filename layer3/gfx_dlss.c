/* H2Classic: the game's side of DLSS (H2N_AA=dlss): starts DLSS Helper.exe, the 64-bit helper that runs NVIDIA's DLSS
 * (dlss/h2classic_dlss.c), and talks to it through shared memory (dlss_ipc.h). gfx_aa.c does the drawing: it fills the
 * shared textures for a view, asks for an evaluation here, and draws the result back before the view's HUD.
 *
 * Every failure is logged once and turns DLSS off for the rest of the run; gfx_aa.c then goes on with TAA (the same
 * jitter, motion and depth), so the picture is never left worse than with H2N_AA=taa. */
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#include <d3d9.h>
#include <stdio.h>
#include <string.h>
#include "dlss_ipc.h"
#include "gfx_dlss.h"

void h2n_log(const char *format, ...);

static dlss_ipc *g_ipc;
static HANDLE g_mapping, g_request, g_reply, g_helper;
static int g_started, g_off, g_told_ready;
static uint32_t g_log_read, g_seq;
static unsigned g_evaluations, g_failures;

static void drain(void)
{
	char line[600];
	unsigned n = 0;
	uint32_t written;
	if (!g_ipc) return;
	written = g_ipc->log_written;
	MemoryBarrier();
	if (written - g_log_read > DLSS_LOG_RING) g_log_read = written - DLSS_LOG_RING;
	while (g_log_read != written)
	{
		char c = g_ipc->log[g_log_read % DLSS_LOG_RING];
		g_log_read++;
		if (c == '\n' || n == sizeof(line) - 1) { line[n] = 0; h2n_log("       dlss helper: %s\n", line); n = 0; continue; }
		line[n++] = c;
	}
}

void dlss_failed(const char *why)
{
	if (g_off) return;
	g_off = 1;
	drain();
	h2n_log("*** picture: DLSS: %s; the anti-aliasing goes on as TAA (H2N_AA=taa)\n", why);
}

int dlss_start(IDirect3D9 *d3d)
{
	IDirect3D9Ex *ex = NULL;
	LUID luid;
	wchar_t name[96], exe[MAX_PATH], dll[MAX_PATH], dir[MAX_PATH], command[MAX_PATH + 32], *slash;
	DWORD pid = GetCurrentProcessId();
	STARTUPINFOW si;
	PROCESS_INFORMATION pi;
	if (g_started || g_off) return g_started;
	if (!d3d || FAILED(IDirect3D9_QueryInterface(d3d, &IID_IDirect3D9Ex, (void **)&ex)) || !ex) { dlss_failed("Direct3D 9Ex is not there"); return 0; }
	memset(&luid, 0, sizeof(luid));
	IDirect3D9Ex_GetAdapterLUID(ex, D3DADAPTER_DEFAULT, &luid);
	IDirect3D9Ex_Release(ex);
	_snwprintf(name, 96, L"Local\\h2classic_dlss_%lu", pid);
	g_mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, sizeof(dlss_ipc), name);
	if (g_mapping) g_ipc = (dlss_ipc *)MapViewOfFile(g_mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(dlss_ipc));
	_snwprintf(name, 96, L"Local\\h2classic_dlss_req_%lu", pid); g_request = CreateEventW(NULL, FALSE, FALSE, name);
	_snwprintf(name, 96, L"Local\\h2classic_dlss_rep_%lu", pid); g_reply = CreateEventW(NULL, FALSE, FALSE, name);
	if (!g_ipc || !g_request || !g_reply) { dlss_failed("the shared memory for the helper could not be made"); return 0; }
	memset(g_ipc, 0, sizeof(*g_ipc));
	g_ipc->magic = DLSS_IPC_MAGIC; g_ipc->version = DLSS_IPC_VERSION;
	g_ipc->game_luid_low = luid.LowPart; g_ipc->game_luid_high = luid.HighPart;
	g_ipc->state = DLSS_STATE_STARTING;
	GetModuleFileNameW(NULL, dir, MAX_PATH);
	slash = wcsrchr(dir, L'\\');
	if (slash) *slash = 0;
	_snwprintf(exe, MAX_PATH, L"%ls\\DLSS Helper.exe", dir);
	_snwprintf(dll, MAX_PATH, L"%ls\\nvngx_dlss.dll", dir);
	if (GetFileAttributesW(exe) == INVALID_FILE_ATTRIBUTES) { dlss_failed("DLSS Helper.exe is not next to H2Classic.exe"); return 0; }
	if (GetFileAttributesW(dll) == INVALID_FILE_ATTRIBUTES) { dlss_failed("nvngx_dlss.dll is not next to H2Classic.exe (it is NVIDIA's DLSS library: put your copy there)"); return 0; }
	_snwprintf(command, MAX_PATH + 32, L"\"%ls\" %lu", exe, pid);
	memset(&si, 0, sizeof(si)); si.cb = sizeof(si);
	memset(&pi, 0, sizeof(pi));
	if (!CreateProcessW(exe, command, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, dir, &si, &pi))
	{
		char why[96];
		_snprintf(why, sizeof(why), "DLSS Helper.exe could not be started (error %lu)", GetLastError());
		dlss_failed(why);
		return 0;
	}
	CloseHandle(pi.hThread);
	g_helper = pi.hProcess;
	g_started = 1;
	h2n_log("       picture: DLSS: DLSS Helper.exe started (process %lu) on the graphics card %08lx:%08lx\n", pi.dwProcessId, (unsigned long)luid.HighPart, (unsigned long)luid.LowPart);
	return 1;
}

/* where the helper is: DLSS_STATE_READY when views can go to it */
int dlss_state(void)
{
	DWORD code;
	if (!g_started || g_off) return DLSS_STATE_FAILED;
	drain();
	if (g_helper && WaitForSingleObject(g_helper, 0) == WAIT_OBJECT_0)
	{
		char why[160];
		GetExitCodeProcess(g_helper, &code);
		_snprintf(why, sizeof(why), "DLSS Helper.exe ended by itself (exit code %#lx%s)", code,
			code == 0xC0000135 ? ": a DLL it needs is missing" : code == 0xC000007B ? ": a DLL of the wrong kind (32/64-bit) was found" : "");
		dlss_failed(why);
		return DLSS_STATE_FAILED;
	}
	if (g_ipc->state == DLSS_STATE_FAILED)
	{
		char why[320];
		_snprintf(why, sizeof(why), "the helper says it is not available: %s", g_ipc->message);
		dlss_failed(why);
		return DLSS_STATE_FAILED;
	}
	if (g_ipc->state == DLSS_STATE_READY && !g_told_ready) { g_told_ready = 1; h2n_log("       picture: DLSS: the helper is ready\n"); }
	return g_ipc->state;
}

static int call(uint32_t cmd, DWORD timeout)
{
	DWORD w;
	if (!g_ipc || g_off) return DLSS_ERR_STATE;
	g_ipc->seq = ++g_seq;
	g_ipc->cmd = cmd;
	MemoryBarrier();
	SetEvent(g_request);
	w = WaitForSingleObject(g_reply, timeout);
	drain();
	if (w != WAIT_OBJECT_0 || g_ipc->reply_seq != g_seq)
	{
		char why[96];
		_snprintf(why, sizeof(why), "the helper did not answer within %lu ms (request %u)", timeout, cmd);
		dlss_failed(why);
		return DLSS_ERR_WAIT;
	}
	return g_ipc->status;
}

int dlss_setup(unsigned slot, unsigned w, unsigned h, HANDLE handles[4], int *direct)
{
	int status;
	if (dlss_state() != DLSS_STATE_READY) return 0;
	g_ipc->slot = slot; g_ipc->width = w; g_ipc->height = h;
	{
		/* (s10g) the DLAA preset (H2N_DLSS_PRESET=k or j, the settings' "DLSS preset"): NVIDIA's render preset hint for DLAA,
		   NVSDK_NGX_DLSS_Hint_Render_Preset_K (11) or _J (10); anything else leaves NVIDIA's default (0) */
		const char *p = getenv("H2N_DLSS_PRESET");
		g_ipc->flags = !p ? 0u : !_stricmp(p, "k") ? 11u : !_stricmp(p, "j") ? 10u : 0u;
	}
	status = call(DLSS_CMD_SETUP, 5000);
	if (status != DLSS_OK)
	{
		char why[128];
		_snprintf(why, sizeof(why), "the helper could not make DLSS for a %ux%u view (%d, NGX %08lx)", w, h, status, (unsigned long)g_ipc->status_ngx);
		if (!g_off) dlss_failed(why);
		return 0;
	}
	handles[0] = (HANDLE)(uintptr_t)g_ipc->color; handles[1] = (HANDLE)(uintptr_t)g_ipc->motion;
	handles[2] = (HANDLE)(uintptr_t)g_ipc->depth; handles[3] = (HANDLE)(uintptr_t)g_ipc->output;
	*direct = (int)g_ipc->uses_direct;
	return 1;
}

int dlss_evaluate(unsigned slot, unsigned w, unsigned h, float jitter_x, float jitter_y, int reset, float frame_ms)
{
	int status;
	if (g_off || !g_ipc) return 0;
	g_ipc->slot = slot; g_ipc->width = w; g_ipc->height = h; g_ipc->reset = reset ? 1 : 0;
	g_ipc->jitter_x = jitter_x; g_ipc->jitter_y = jitter_y;
	/* the motion is this frame minus the last, in the view's texture coordinates; DLSS wants pixels towards the last frame */
	g_ipc->mv_scale_x = -(float)w; g_ipc->mv_scale_y = -(float)h;
	g_ipc->frame_ms = frame_ms;
	status = call(DLSS_CMD_EVALUATE, 1000);
	if (status != DLSS_OK)
	{
		char why[96];
		g_failures++;
		_snprintf(why, sizeof(why), "an evaluation failed (%d, NGX %08lx)", status, (unsigned long)g_ipc->status_ngx);
		if (!g_off) dlss_failed(why);
		return 0;
	}
	g_evaluations++;
	return 1;
}

void dlss_release(unsigned slot)
{
	if (!g_started || g_off || !g_ipc || g_ipc->state != DLSS_STATE_READY) return;
	g_ipc->slot = slot;
	call(DLSS_CMD_RELEASE, 2000);
}

void dlss_stop(void)
{
	if (!g_started) return;
	if (!g_off && g_ipc && g_helper && WaitForSingleObject(g_helper, 0) == WAIT_TIMEOUT)
	{
		g_ipc->seq = ++g_seq; g_ipc->cmd = DLSS_CMD_QUIT; MemoryBarrier(); SetEvent(g_request);
		WaitForSingleObject(g_helper, 2000);
	}
	drain();
}

void dlss_report(void)
{
	if (!g_started && !g_off) return;
	drain();
	h2n_log("picture: DLSS: %u evaluations%s\n", g_evaluations, g_off ? " (then turned off: see above)" : "");
}

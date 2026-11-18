/* H2Classic_DLSS.exe: NVIDIA DLSS for H2Classic.exe.
 *
 * DLSS (NVIDIA's NGX) exists only for 64-bit programs; H2Classic.exe has to stay 32-bit (the XBE's code and data sit
 * at the console's addresses). So the game starts this 64-bit helper, which makes a Direct3D 11 device on the same
 * graphics card, makes the textures the two share (layer2/dlss_ipc.h), and runs DLSS when the game asks: DLAA, the
 * picture at its own size made from this frame and the ones before, with the game's sub-pixel jitter, motion and depth.
 *
 * Started by the game as: H2Classic_DLSS.exe <game pid>. Finds nvngx_dlss.dll in its own folder (the user's copy; it
 * is not part of this build). Writes h2classic\dlss_helper_log.txt (and NGX's own lines into it), and the main lines
 * into the game's log through the shared memory. Ends when the game asks it to or when the game is gone.
 *
 * This software contains source code provided by NVIDIA Corporation: it is built against the DLSS SDK
 * (github.com/NVIDIA/DLSS, its headers and nvsdk_ngx_d.lib), under the NVIDIA RTX SDKs licence. */
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#define INITGUID
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <wchar.h>
#ifndef H2N_DLSS_NO_NGX
#include "nvsdk_ngx.h"
#include "nvsdk_ngx_helpers.h"
#include "nvsdk_ngx_helpers_d3d.h"
#endif
#include "../layer2/dlss_ipc.h"
#include "shaders/dlss_inputs_cs.h"

#define H2N_DLSS_PROJECT_ID "3b6a51e0-6f2c-4d8e-9a71-5c2e8b0d4f19"   /* this program's own id for NGX (engine type "custom") */

static dlss_ipc *g_ipc;
static HANDLE g_request, g_reply, g_game;
static HANDLE g_log_file = INVALID_HANDLE_VALUE;
static CRITICAL_SECTION g_log_lock;
static wchar_t g_dir[MAX_PATH];                 /* this exe's folder (the game folder) */
static ID3D11Device *g_device;
static ID3D11DeviceContext *g_context;
static ID3D11ComputeShader *g_split;            /* shaders/dlss_inputs.cs.hlsl; NULL: motion and depth go to DLSS as the game wrote them */
static ID3D11Query *g_done;
#ifndef H2N_DLSS_NO_NGX
static int g_ngx;
#endif
static int g_force_direct;

/* ================================================================ log */
static void write_file(const char *text)
{
	DWORD n;
	if (g_log_file != INVALID_HANDLE_VALUE) WriteFile(g_log_file, text, (DWORD)strlen(text), &n, NULL);
}
/* a line into the helper's log, and (to_game) into the game's log through the ring */
static void say(int to_game, const char *format, ...)
{
	char line[1024];
	va_list a;
	size_t n, i;
	va_start(a, format);
	_vsnprintf(line, sizeof(line) - 2, format, a);
	va_end(a);
	line[sizeof(line) - 2] = 0;
	n = strlen(line);
	if (!n || line[n - 1] != '\n') { line[n++] = '\n'; line[n] = 0; }
	EnterCriticalSection(&g_log_lock);
	{
		char stamp[32];
		_snprintf(stamp, sizeof(stamp), "[%8.3f] ", GetTickCount() / 1000.0);
		write_file(stamp);
		write_file(line);
	}
	if (to_game && g_ipc)
	{
		uint32_t w = g_ipc->log_written;
		for (i = 0; i < n; i++) g_ipc->log[(w + i) % DLSS_LOG_RING] = line[i];
		MemoryBarrier();
		g_ipc->log_written = w + (uint32_t)n;
	}
	LeaveCriticalSection(&g_log_lock);
}
static void fail(int32_t ngx, const char *format, ...)
{
	va_list a;
	if (!g_ipc) return;
	va_start(a, format);
	_vsnprintf(g_ipc->message, sizeof(g_ipc->message) - 1, format, a);
	va_end(a);
	g_ipc->message[sizeof(g_ipc->message) - 1] = 0;
	g_ipc->ngx_result = ngx;
	say(1, "not available: %s", g_ipc->message);
	MemoryBarrier();
	g_ipc->state = DLSS_STATE_FAILED;
}

#ifndef H2N_DLSS_NO_NGX
static void NVSDK_CONV ngx_log(const char *message, NVSDK_NGX_Logging_Level level, NVSDK_NGX_Feature source)
{
	char line[1200];
	(void)level;
	_snprintf(line, sizeof(line) - 1, "ngx (%d): %s", (int)source, message ? message : "");
	line[sizeof(line) - 1] = 0;
	say(0, "%s", line);
}
static const char *ngx_text(NVSDK_NGX_Result r)
{
	static char text[256];
	const wchar_t *w = GetNGXResultAsString(r);
	text[0] = 0;
	if (w) WideCharToMultiByte(CP_ACP, 0, w, -1, text, sizeof(text), NULL, NULL);
	return text;
}
#endif

/* ================================================================ a view's textures and DLSS */
typedef struct slot
{
	unsigned width, height;
	ID3D11Texture2D *color, *motion, *depth, *output;       /* shared with the game */
	ID3D11Texture2D *motion2, *depth2;                      /* what DLSS reads, made from motion and depth */
	ID3D11Texture2D *result;                                /* what DLSS writes (it needs unordered access), then copied into output */
	ID3D11ShaderResourceView *motion_srv, *depth_srv;
	ID3D11UnorderedAccessView *motion_uav, *depth_uav;
	ID3D11Buffer *size_cb;
	int direct;
#ifndef H2N_DLSS_NO_NGX
	NVSDK_NGX_Handle *feature;
	NVSDK_NGX_Parameter *params;
#endif
	unsigned evaluated;
} slot;
static slot g_slots[4];

#define RELEASE(p) do { if (p) { IUnknown_Release((IUnknown *)(p)); (p) = NULL; } } while (0)
static void slot_free(slot *s)
{
#ifndef H2N_DLSS_NO_NGX
	if (s->feature) { NVSDK_NGX_D3D11_ReleaseFeature(s->feature); s->feature = NULL; }
	if (s->params) { NVSDK_NGX_D3D11_DestroyParameters(s->params); s->params = NULL; }
#endif
	RELEASE(s->motion_srv); RELEASE(s->depth_srv); RELEASE(s->motion_uav); RELEASE(s->depth_uav); RELEASE(s->size_cb);
	RELEASE(s->color); RELEASE(s->motion); RELEASE(s->depth); RELEASE(s->output); RELEASE(s->motion2); RELEASE(s->depth2); RELEASE(s->result);
	s->width = s->height = 0; s->evaluated = 0;
}

static HRESULT texture(unsigned w, unsigned h, DXGI_FORMAT f, UINT bind, int shared, ID3D11Texture2D **out, uint64_t *handle)
{
	D3D11_TEXTURE2D_DESC d;
	HRESULT hr;
	memset(&d, 0, sizeof(d));
	d.Width = w; d.Height = h; d.MipLevels = 1; d.ArraySize = 1; d.Format = f; d.SampleDesc.Count = 1;
	d.Usage = D3D11_USAGE_DEFAULT; d.BindFlags = bind; d.MiscFlags = shared ? D3D11_RESOURCE_MISC_SHARED : 0;
	hr = ID3D11Device_CreateTexture2D(g_device, &d, NULL, out);
	if (SUCCEEDED(hr) && shared)
	{
		IDXGIResource *r = NULL;
		HANDLE h = NULL;
		hr = ID3D11Texture2D_QueryInterface(*out, &IID_IDXGIResource, (void **)&r);
		if (SUCCEEDED(hr)) { hr = IDXGIResource_GetSharedHandle(r, &h); IDXGIResource_Release(r); }
		*handle = (uint64_t)(uintptr_t)h;
		if (SUCCEEDED(hr) && !h) hr = E_FAIL;
	}
	return hr;
}

static int32_t setup(unsigned index, unsigned w, unsigned h)
{
	slot *s = &g_slots[index];
	HRESULT hr;
	const char *what = "";
	slot_free(s);
	/* shared with the game's Direct3D 9Ex device: only formats both can open (B8G8R8A8 = A8R8G8B8, R16G16B16A16_FLOAT =
	   A16B16G16R16F, R8G8B8A8 = A8B8G8R8), as render target and shader resource, one level, no more */
	hr = texture(w, h, DXGI_FORMAT_B8G8R8A8_UNORM, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE, 1, &s->color, &g_ipc->color); what = "colour";
	if (SUCCEEDED(hr)) { hr = texture(w, h, DXGI_FORMAT_R16G16B16A16_FLOAT, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE, 1, &s->motion, &g_ipc->motion); what = "motion"; }
	if (SUCCEEDED(hr)) { hr = texture(w, h, DXGI_FORMAT_R16G16B16A16_FLOAT, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE, 1, &s->depth, &g_ipc->depth); what = "depth"; }
	/* (s10f) the result and the shared output in R16G16B16A16_FLOAT: Direct3D 9 has no render target in the 8-bit RGBA
	   order (its A8B8G8R8), so the game could not open an R8G8B8A8 output (the user's first run: the slot was made, nothing
	   evaluated); 16-bit floats are a render target there and a typed UAV here */
	if (SUCCEEDED(hr)) { hr = texture(w, h, DXGI_FORMAT_R16G16B16A16_FLOAT, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE, 1, &s->output, &g_ipc->output); what = "output"; }
	if (SUCCEEDED(hr)) { hr = texture(w, h, DXGI_FORMAT_R16G16B16A16_FLOAT, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS, 0, &s->result, NULL); what = "result"; }
	if (FAILED(hr))
	{
		say(1, "slot %u: the shared %s texture (%ux%u) could not be made: %08lx", index, what, w, h, (unsigned long)hr);
		slot_free(s);
		return DLSS_ERR_TEXTURES;
	}
	/* DLSS's own formats for the motion (R16G16_FLOAT) and the depth (R32_FLOAT), filled by the compute shader */
	s->direct = !g_split || g_force_direct;
	if (!s->direct)
	{
		D3D11_BUFFER_DESC b;
		D3D11_SUBRESOURCE_DATA init;
		UINT size[4];
		hr = texture(w, h, DXGI_FORMAT_R16G16_FLOAT, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS, 0, &s->motion2, NULL);
		if (SUCCEEDED(hr)) hr = texture(w, h, DXGI_FORMAT_R32_FLOAT, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS, 0, &s->depth2, NULL);
		if (SUCCEEDED(hr)) hr = ID3D11Device_CreateShaderResourceView(g_device, (ID3D11Resource *)s->motion, NULL, &s->motion_srv);
		if (SUCCEEDED(hr)) hr = ID3D11Device_CreateShaderResourceView(g_device, (ID3D11Resource *)s->depth, NULL, &s->depth_srv);
		if (SUCCEEDED(hr)) hr = ID3D11Device_CreateUnorderedAccessView(g_device, (ID3D11Resource *)s->motion2, NULL, &s->motion_uav);
		if (SUCCEEDED(hr)) hr = ID3D11Device_CreateUnorderedAccessView(g_device, (ID3D11Resource *)s->depth2, NULL, &s->depth_uav);
		size[0] = w; size[1] = h; size[2] = size[3] = 0;
		memset(&b, 0, sizeof(b)); b.ByteWidth = 16; b.Usage = D3D11_USAGE_IMMUTABLE; b.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		memset(&init, 0, sizeof(init)); init.pSysMem = size;
		if (SUCCEEDED(hr)) hr = ID3D11Device_CreateBuffer(g_device, &b, &init, &s->size_cb);
		if (FAILED(hr))
		{
			say(1, "slot %u: the textures for DLSS's own formats could not be made (%08lx): motion and depth go to DLSS as the game wrote them", index, (unsigned long)hr);
			RELEASE(s->motion_srv); RELEASE(s->depth_srv); RELEASE(s->motion_uav); RELEASE(s->depth_uav); RELEASE(s->size_cb); RELEASE(s->motion2); RELEASE(s->depth2);
			s->direct = 1;
		}
	}
	g_ipc->uses_direct = (uint32_t)s->direct;
#ifndef H2N_DLSS_NO_NGX
	{
		NVSDK_NGX_Result r = NVSDK_NGX_D3D11_AllocateParameters(&s->params);
		NVSDK_NGX_DLSS_Create_Params create;
		if (NVSDK_NGX_FAILED(r)) { say(1, "slot %u: NGX parameters: %08x %s", index, (unsigned)r, ngx_text(r)); g_ipc->status_ngx = (int32_t)r; slot_free(s); return DLSS_ERR_CREATE; }
		if (g_ipc->flags & 0xffu)
		{
			/* (s10g) the DLAA preset the game asks for (H2N_DLSS_PRESET): NVSDK_NGX_DLSS_Hint_Render_Preset_J (10) or _K (11) */
			NVSDK_NGX_Parameter_SetUI(s->params, NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_DLAA, g_ipc->flags & 0xffu);
			say(1, "slot %u: DLAA render preset %u (%s)", index, (unsigned)(g_ipc->flags & 0xffu), (g_ipc->flags & 0xffu) == 11 ? "K" : (g_ipc->flags & 0xffu) == 10 ? "J" : "?");
		}
		memset(&create, 0, sizeof(create));
		create.Feature.InWidth = w; create.Feature.InHeight = h; create.Feature.InTargetWidth = w; create.Feature.InTargetHeight = h;
		create.Feature.InPerfQualityValue = NVSDK_NGX_PerfQuality_Value_DLAA;
		/* motion at the render size, without the jitter; the picture as stored for the screen (not linear light): LDR */
		create.InFeatureCreateFlags = NVSDK_NGX_DLSS_Feature_Flags_MVLowRes | NVSDK_NGX_DLSS_Feature_Flags_AutoExposure;
		r = NGX_D3D11_CREATE_DLSS_EXT(g_context, &s->feature, s->params, &create);
		g_ipc->status_ngx = (int32_t)r;
		if (NVSDK_NGX_FAILED(r)) { say(1, "slot %u: DLSS (DLAA, %ux%u) could not be made: %08x %s", index, w, h, (unsigned)r, ngx_text(r)); slot_free(s); return DLSS_ERR_CREATE; }
	}
#endif
	s->width = w; s->height = h;
	say(1, "slot %u: %ux%u ready (DLAA; motion and depth %s)", index, w, h, s->direct ? "as the game wrote them" : "in DLSS's own formats");
	return DLSS_OK;
}

static int32_t wait_gpu(void)
{
	BOOL done = FALSE;
	DWORD start = GetTickCount();
	ID3D11DeviceContext_End(g_context, (ID3D11Asynchronous *)g_done);
	ID3D11DeviceContext_Flush(g_context);
	while (ID3D11DeviceContext_GetData(g_context, (ID3D11Asynchronous *)g_done, &done, sizeof(done), 0) == S_FALSE)
	{
		if (GetTickCount() - start > 500) return DLSS_ERR_WAIT;
		YieldProcessor();
	}
	return DLSS_OK;
}

static int32_t evaluate(unsigned index)
{
	slot *s = &g_slots[index];
	if (!s->width || s->width != g_ipc->width || s->height != g_ipc->height) return DLSS_ERR_SLOT;
	if (!s->direct)
	{
		ID3D11ShaderResourceView *srv[2] = { s->motion_srv, s->depth_srv }, *no_srv[2] = { NULL, NULL };
		ID3D11UnorderedAccessView *uav[2] = { s->motion_uav, s->depth_uav }, *no_uav[2] = { NULL, NULL };
		ID3D11DeviceContext_CSSetShader(g_context, g_split, NULL, 0);
		ID3D11DeviceContext_CSSetShaderResources(g_context, 0, 2, srv);
		ID3D11DeviceContext_CSSetUnorderedAccessViews(g_context, 0, 2, uav, NULL);
		ID3D11DeviceContext_CSSetConstantBuffers(g_context, 0, 1, &s->size_cb);
		ID3D11DeviceContext_Dispatch(g_context, (s->width + 7) / 8, (s->height + 7) / 8, 1);
		ID3D11DeviceContext_CSSetShaderResources(g_context, 0, 2, no_srv);
		ID3D11DeviceContext_CSSetUnorderedAccessViews(g_context, 0, 2, no_uav, NULL);
		ID3D11DeviceContext_CSSetShader(g_context, NULL, NULL, 0);
	}
#ifndef H2N_DLSS_NO_NGX
	{
		NVSDK_NGX_D3D11_DLSS_Eval_Params e;
		NVSDK_NGX_Result r;
		memset(&e, 0, sizeof(e));
		e.Feature.pInColor = (ID3D11Resource *)s->color;
		e.Feature.pInOutput = (ID3D11Resource *)s->result;
		e.pInDepth = (ID3D11Resource *)(s->direct ? s->depth : s->depth2);
		e.pInMotionVectors = (ID3D11Resource *)(s->direct ? s->motion : s->motion2);
		e.InJitterOffsetX = g_ipc->jitter_x; e.InJitterOffsetY = g_ipc->jitter_y;
		e.InRenderSubrectDimensions.Width = s->width; e.InRenderSubrectDimensions.Height = s->height;
		e.InReset = g_ipc->reset ? 1 : 0;
		e.InMVScaleX = g_ipc->mv_scale_x; e.InMVScaleY = g_ipc->mv_scale_y;
		e.InPreExposure = 1.0f; e.InExposureScale = 1.0f;
		e.InFrameTimeDeltaInMsec = g_ipc->frame_ms;
		r = NGX_D3D11_EVALUATE_DLSS_EXT(g_context, s->feature, s->params, &e);
		g_ipc->status_ngx = (int32_t)r;
		if (NVSDK_NGX_FAILED(r))
		{
			say(1, "slot %u: DLSS evaluation failed: %08x %s", index, (unsigned)r, ngx_text(r));
			return DLSS_ERR_EVALUATE;
		}
	}
#endif
	ID3D11DeviceContext_CopyResource(g_context, (ID3D11Resource *)s->output, (ID3D11Resource *)s->result);
	if (!s->evaluated++) say(1, "slot %u: first DLSS evaluation done", index);
	return wait_gpu();
}

/* ================================================================ start */
static int find_adapter(IDXGIAdapter1 **out)
{
	IDXGIFactory1 *factory = NULL;
	UINT i;
	*out = NULL;
	if (FAILED(CreateDXGIFactory1(&IID_IDXGIFactory1, (void **)&factory))) return 0;
	for (i = 0; ; i++)
	{
		IDXGIAdapter1 *a = NULL;
		DXGI_ADAPTER_DESC1 d;
		char name[128];
		if (IDXGIFactory1_EnumAdapters1(factory, i, &a) != S_OK) break;
		IDXGIAdapter1_GetDesc1(a, &d);
		WideCharToMultiByte(CP_ACP, 0, d.Description, -1, name, sizeof(name), NULL, NULL);
		say(0, "graphics card %u: %s, LUID %08lx:%08lx", i, name, (long)d.AdapterLuid.HighPart, (unsigned long)d.AdapterLuid.LowPart);
		if (d.AdapterLuid.LowPart == g_ipc->game_luid_low && d.AdapterLuid.HighPart == g_ipc->game_luid_high && !*out)
		{
			say(1, "on %s (the game's graphics card)", name);
			*out = a;
			continue;
		}
		IDXGIAdapter1_Release(a);
	}
	IDXGIFactory1_Release(factory);
	return *out != NULL;
}

static int start(void)
{
	IDXGIAdapter1 *adapter = NULL;
	D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0 }, got;
	D3D11_QUERY_DESC q;
	HRESULT hr;
	wchar_t dll[MAX_PATH];
	if (!find_adapter(&adapter)) { fail(0, "the game's graphics card was not found among DXGI's adapters"); return 0; }
	hr = D3D11CreateDevice((IDXGIAdapter *)adapter, D3D_DRIVER_TYPE_UNKNOWN, NULL, 0, levels, 2, D3D11_SDK_VERSION, &g_device, &got, &g_context);
	if (FAILED(hr)) hr = D3D11CreateDevice((IDXGIAdapter *)adapter, D3D_DRIVER_TYPE_UNKNOWN, NULL, 0, levels + 1, 1, D3D11_SDK_VERSION, &g_device, &got, &g_context);
	IDXGIAdapter1_Release(adapter);
	if (FAILED(hr)) { fail(0, "Direct3D 11 device: %08lx", (unsigned long)hr); return 0; }
	say(0, "Direct3D 11 device, feature level %x", (unsigned)got);
	memset(&q, 0, sizeof(q)); q.Query = D3D11_QUERY_EVENT;
	if (FAILED(ID3D11Device_CreateQuery(g_device, &q, &g_done))) { fail(0, "Direct3D 11 event query"); return 0; }
	hr = ID3D11Device_CreateComputeShader(g_device, g_dlss_inputs_cs, sizeof(g_dlss_inputs_cs), NULL, &g_split);
	if (FAILED(hr)) { g_split = NULL; say(1, "the compute shader that converts the motion and depth was not accepted (%08lx): they go to DLSS as the game wrote them", (unsigned long)hr); }
	g_force_direct = GetEnvironmentVariableA("H2N_DLSS_DIRECT", NULL, 0) > 0;
	_snwprintf(dll, MAX_PATH, L"%ls\\nvngx_dlss.dll", g_dir);
	if (GetFileAttributesW(dll) == INVALID_FILE_ATTRIBUTES)
	{
		fail(0, "nvngx_dlss.dll is not in the game folder (next to H2Classic.exe and H2Classic_DLSS.exe)");
		return 0;
	}
#ifndef H2N_DLSS_NO_NGX
	{
		NVSDK_NGX_FeatureCommonInfo info;
		const wchar_t *paths[1];
		wchar_t data[MAX_PATH];
		NVSDK_NGX_Parameter *caps = NULL;
		NVSDK_NGX_Result r;
		int available = 0, needs_driver = 0, init_result = 0;
		unsigned major = 0, minor = 0;
		_snwprintf(data, MAX_PATH, L"%ls\\h2classic\\ngx", g_dir);
		CreateDirectoryW(data, NULL);
		paths[0] = g_dir;
		memset(&info, 0, sizeof(info));
		info.PathListInfo.Path = paths; info.PathListInfo.Length = 1;
		info.LoggingInfo.LoggingCallback = ngx_log;
		info.LoggingInfo.MinimumLoggingLevel = NVSDK_NGX_LOGGING_LEVEL_ON;
		r = NVSDK_NGX_D3D11_Init_with_ProjectID(H2N_DLSS_PROJECT_ID, NVSDK_NGX_ENGINE_TYPE_CUSTOM, "1.0", data, g_device, &info, NVSDK_NGX_Version_API);
		if (NVSDK_NGX_FAILED(r)) { fail((int32_t)r, "NGX did not start: %08x %s", (unsigned)r, ngx_text(r)); return 0; }
		g_ngx = 1;
		r = NVSDK_NGX_D3D11_GetCapabilityParameters(&caps);
		if (NVSDK_NGX_FAILED(r) || !caps) { fail((int32_t)r, "NGX capabilities: %08x %s", (unsigned)r, ngx_text(r)); return 0; }
		NVSDK_NGX_Parameter_GetI(caps, NVSDK_NGX_Parameter_SuperSampling_Available, &available);
		NVSDK_NGX_Parameter_GetI(caps, NVSDK_NGX_Parameter_SuperSampling_NeedsUpdatedDriver, &needs_driver);
		NVSDK_NGX_Parameter_GetUI(caps, NVSDK_NGX_Parameter_SuperSampling_MinDriverVersionMajor, &major);
		NVSDK_NGX_Parameter_GetUI(caps, NVSDK_NGX_Parameter_SuperSampling_MinDriverVersionMinor, &minor);
		NVSDK_NGX_Parameter_GetI(caps, NVSDK_NGX_Parameter_SuperSampling_FeatureInitResult, &init_result);
		NVSDK_NGX_D3D11_DestroyParameters(caps);
		say(1, "NGX started; DLSS available %d, needs a newer driver %d (at least %u.%u), its start %08x", available, needs_driver, major, minor, (unsigned)init_result);
		if (!available) { fail(init_result, "DLSS is not available here (NGX's start result %08x %s)%s", (unsigned)init_result, ngx_text((NVSDK_NGX_Result)init_result), needs_driver ? "; it needs a newer NVIDIA driver" : ""); return 0; }
	}
#endif
	return 1;
}

static void finish(void)
{
	unsigned i;
	for (i = 0; i < 4; i++) slot_free(&g_slots[i]);
#ifndef H2N_DLSS_NO_NGX
	if (g_ngx && g_device) NVSDK_NGX_D3D11_Shutdown1(g_device);
#endif
	RELEASE(g_split); RELEASE(g_done);
	if (g_context) { ID3D11DeviceContext_ClearState(g_context); RELEASE(g_context); }
	RELEASE(g_device);
}

int main(void)
{
	wchar_t **argv;
	int argc;
	unsigned long pid;
	wchar_t name[96], path[MAX_PATH], *slash;
	HANDLE mapping;
	InitializeCriticalSection(&g_log_lock);
	GetModuleFileNameW(NULL, g_dir, MAX_PATH);
	slash = wcsrchr(g_dir, L'\\');
	if (slash) *slash = 0;
	_snwprintf(path, MAX_PATH, L"%ls\\h2classic", g_dir);
	CreateDirectoryW(path, NULL);
	_snwprintf(path, MAX_PATH, L"%ls\\h2classic\\dlss_helper_log.txt", g_dir);
	g_log_file = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	argv = CommandLineToArgvW(GetCommandLineW(), &argc);
	if (!argv || argc < 2) { say(0, "H2Classic_DLSS.exe is started by H2Classic.exe (H2N_AA=dlss), not on its own"); return 1; }
	pid = wcstoul(argv[1], NULL, 10);
	say(0, "h2classic_dlss for the game's process %lu", pid);
	_snwprintf(name, 96, L"Local\\h2classic_dlss_%lu", pid);
	mapping = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, name);
	if (mapping) g_ipc = (dlss_ipc *)MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(dlss_ipc));
	_snwprintf(name, 96, L"Local\\h2classic_dlss_req_%lu", pid); g_request = OpenEventW(SYNCHRONIZE | EVENT_MODIFY_STATE, FALSE, name);
	_snwprintf(name, 96, L"Local\\h2classic_dlss_rep_%lu", pid); g_reply = OpenEventW(SYNCHRONIZE | EVENT_MODIFY_STATE, FALSE, name);
	g_game = OpenProcess(SYNCHRONIZE, FALSE, pid);
	if (!g_ipc || !g_request || !g_reply || !g_game || g_ipc->magic != DLSS_IPC_MAGIC || g_ipc->version != DLSS_IPC_VERSION)
	{
		say(0, "the game's shared memory or events were not found (%p %p %p %p)", (void *)g_ipc, g_request, g_reply, g_game);
		return 2;
	}
	say(1, "helper started (%u-bit, process %lu)", (unsigned)(sizeof(void *) * 8), GetCurrentProcessId());
	if (start())
	{
		MemoryBarrier();
		g_ipc->state = DLSS_STATE_READY;
		say(1, "ready");
	}
	for (;;)
	{
		HANDLE wait[2] = { g_request, g_game };
		DWORD w = WaitForMultipleObjects(2, wait, FALSE, INFINITE);
		uint32_t cmd;
		int32_t status = DLSS_ERR_STATE;
		if (w != WAIT_OBJECT_0) { say(0, "the game is gone: closing"); break; }
		cmd = g_ipc->cmd;
		g_ipc->status_ngx = 0;
		if (cmd == DLSS_CMD_QUIT) { say(0, "the game asked to close"); g_ipc->reply_seq = g_ipc->seq; g_ipc->status = DLSS_OK; SetEvent(g_reply); break; }
		if (g_ipc->slot >= 4) status = DLSS_ERR_SLOT;
		else if (g_ipc->state != DLSS_STATE_READY) status = DLSS_ERR_STATE;
		else if (cmd == DLSS_CMD_SETUP) status = setup(g_ipc->slot, g_ipc->width, g_ipc->height);
		else if (cmd == DLSS_CMD_EVALUATE) status = evaluate(g_ipc->slot);
		else if (cmd == DLSS_CMD_RELEASE) { slot_free(&g_slots[g_ipc->slot]); status = DLSS_OK; }
		g_ipc->status = status;
		MemoryBarrier();
		g_ipc->reply_seq = g_ipc->seq;
		SetEvent(g_reply);
	}
	finish();
	say(0, "closed");
	if (g_log_file != INVALID_HANDLE_VALUE) CloseHandle(g_log_file);
	return 0;
}

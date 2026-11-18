/* H2Classic: anti-aliasing of the game's 3D views (H2N_AA=fxaa | smaa | taa), and the first stage of DLSS's inputs.
 *
 * gfx9.c calls aa_scene() when a player's view has been drawn and before the game draws that view's HUD (the game's own
 * order: unknown_023540.cpp, function_2ba10: function_13680 draws the view's 3D, then the HUD and the windows), so only
 * the 3D is smoothed: the HUD, its text and the menus stay exactly as the game draws them. Each method follows its
 * published reference; the shaders are written out by hand from that code (the .asm files in layer2/shaders, scripts/sm3asm.py):
 *   fxaa  this layer's own edge smoothing in the manner of FXAA (aa_edge.ps.asm), as in the test build s9aa (which
 *         smoothed the whole picture, HUD included, as it went to the window).
 *   smaa  SMAA 1x (Jimenez et al., github.com/iryoku/smaa, MIT licence): SMAA.hlsl's three passes with the DX9 demo's
 *         sampler states (Demo/DX9/Shaders/SMAA.fx): luma edges, blending weights with the precomputed area and search
 *         textures (Textures/AreaTex.h, SearchTex.h, unchanged), neighborhood blending in linear light (sRGB read and
 *         write). Preset HIGH, or LOW, MEDIUM or ULTRA with H2N_SMAA=low|medium|ultra (s10g: low and medium added). The demo's stencil (edge pixels only in the second pass)
 *         is left out: it only saves time, the weights of the other pixels are 0 either way.
 *   taa   temporal anti-aliasing: Playdead's TemporalReprojection and VelocityBuffer (github.com/playdeadgames/temporal,
 *         MIT licence) with its defaults (FrustumJitter's Halton(2,3) x16, 3x3 rounded neighbourhood, clipping,
 *         dilation, feedback 0.88-0.97) except motion blur. The jitter: gfx9.c shifts every 3D draw of the view by this
 *         frame's sub-pixel offset. The motion: from the depth buffer and the game's own camera (the projection record
 *         at 0x4b9e14 that function_2ba10 builds for the view), the first-person arms and weapon excepted (they move
 *         with the camera: found as the pixels whose depth the game's first-person draws changed).
 * The jitter, the motion vectors, the depth and the view without its HUD are what DLSS takes in; this is stage 1 of it.
 * Test switches: H2N_AADEBUG=motion shows the motion vectors (red x, green y; one fiftieth of the view a frame is full),
 * =error this frame against the history moved by the motion (what is left on still surfaces is what the motion got
 * wrong), =log the camera and a few motion vectors read back against the same sum done here; H2N_AADUMP=<frame> writes
 * the first view's inputs and outputs from that frame on as aa_*.raw (scripts/aa_check.py, taa_check.py check them). */
#define COBJMACROS
#include <windows.h>
#include <d3d9.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "gfx_aa.h"
#include "dlss_ipc.h"
#include "gfx_dlss.h"
#include "gfx_aa_shaders.h"
#include "smaa/AreaTex.h"
#include "smaa/SearchTex.h"

void h2n_log(const char *format, ...);
int h2n_env_int(const char *name, int otherwise);

static int g_mode = -1, g_smaa_preset = 2, g_failed;      /* (s10g) SMAA: 0 low, 1 medium, 2 high, 3 ultra */
static IDirect3DVertexShader9 *g_quad_vs, *g_smaa_edge_vs, *g_smaa_blend_vs, *g_smaa_neighbor_vs;
static IDirect3DPixelShader9 *g_fxaa_ps, *g_copy_ps, *g_smaa_edge_ps, *g_smaa_blend_ps, *g_smaa_neighbor_ps, *g_taa_depth_ps, *g_taa_velocity_ps, *g_taa_resolve_ps, *g_taa_debug_ps;
static int g_debug;                                   /* H2N_AADEBUG (taa, tests): 1 motion, 2 error (test views in place of the result), 3 log */
static IDirect3DVertexDeclaration9 *g_declaration;
static IDirect3DTexture9 *g_area, *g_search;
static int g_srgb_read, g_srgb_write;                 /* SMAA's last pass reads and writes in linear light where the device can */
static D3DFORMAT g_velocity_format, g_depth_copy_format;
static unsigned g_frame, g_scenes, g_skipped, g_first_person, g_resets;
static float g_jitter[2], g_jitter_prev[2];
static unsigned g_jitter_index;
static int g_dlss_flip;                               /* H2N_DLSS_JITTER=flip: the jitter handed to DLSS with the other sign (a test) */
static unsigned g_dlss_views, g_dlss_taa;             /* views done by DLSS; views done by TAA in dlss mode (helper not ready, or off) */
static float g_frame_ms;                              /* the time between the last two frames shown (DLSS's frame time) */
static IDirect3DQuery9 *g_gpu_done;
static int temporal(int m) { return m == AA_TAA || m == AA_DLSS; }    /* the modes that jitter the 3D and need its motion */

/* the game's camera for the view being drawn (read at aa_scene_begin) */
typedef struct camera { float right[3], up[3], forward[3], position[3]; float scale_x, scale_y, offset_x, offset_y, distance; int valid; } camera;
static camera g_camera;
static int g_slot;

typedef struct view_slot      /* what each player's view keeps (the history of its picture, its last camera) */
{
	unsigned width, height, x, y;
	IDirect3DTexture9 *color, *edges, *blend, *velocity, *depth_copy, *history[2];
	IDirect3DSurface9 *color_s, *edges_s, *blend_s, *velocity_s, *depth_copy_s, *history_s[2];
	int history_valid, read, depth_copy_valid;
	unsigned last_frame;
	camera previous;
	/* DLSS (H2N_AA=dlss): the helper's textures, opened by their shared handles */
	unsigned dl_width, dl_height;
	IDirect3DTexture9 *dl_color, *dl_motion, *dl_depth, *dl_output;
	IDirect3DSurface9 *dl_color_s, *dl_motion_s, *dl_depth_s;
	int dl_valid;
} view_slot;
static view_slot g_slots[4];

int aa_mode(void)
{
	if (g_mode < 0)
	{
		const char *v = getenv("H2N_AA");
		const char *p = getenv("H2N_SMAA");
		g_mode = AA_OFF;
		if (v && *v)
		{
			if (!_stricmp(v, "fxaa") || !strcmp(v, "1")) g_mode = AA_FXAA;
			else if (!_stricmp(v, "smaa")) g_mode = AA_SMAA;
			else if (!_stricmp(v, "taa")) g_mode = AA_TAA;
			else if (!_stricmp(v, "dlss")) g_mode = AA_DLSS;
			else if (_stricmp(v, "off") && strcmp(v, "0")) h2n_log("       picture: H2N_AA=%s is not known (off, fxaa, smaa, taa, dlss): no anti-aliasing\n", v);
		}
		/* (s10g) SMAA's four presets (SMAA.hlsl SMAA_PRESET_LOW / _MEDIUM / _HIGH / _ULTRA): H2N_SMAA=low|medium|high|ultra */
		g_smaa_preset = !p || !*p ? 2 : !_stricmp(p, "low") ? 0 : !_stricmp(p, "medium") ? 1 : !_stricmp(p, "ultra") ? 3 : 2;
		p = getenv("H2N_AADEBUG");
		g_debug = !p || !*p ? 0 : !_stricmp(p, "motion") ? 1 : !_stricmp(p, "error") ? 2 : !_stricmp(p, "log") ? 3 : 0;
		p = getenv("H2N_DLSS_JITTER");
		g_dlss_flip = p && !_stricmp(p, "flip");
	}
	return g_failed ? AA_OFF : g_mode;
}

/* ================================================================ the jitter (FrustumJitter.cs: Halton_2_3_X16, patternScale 1) */
static float halton(int prime, int index)             /* HaltonSeq(prime, index), index not zero-based */
{
	float r = 0.0f, f = 1.0f;
	int i = index;
	while (i > 0) { f /= prime; r += f * (i % prime); i = (int)floorf(i / (float)prime); }
	return r;
}
void aa_frame(void)
{
	static LARGE_INTEGER last, frequency;
	LARGE_INTEGER now;
	g_frame++;
	if (!temporal(aa_mode())) return;
	QueryPerformanceCounter(&now);
	if (!frequency.QuadPart) QueryPerformanceFrequency(&frequency);
	if (last.QuadPart && frequency.QuadPart) g_frame_ms = (float)((now.QuadPart - last.QuadPart) * 1000.0 / frequency.QuadPart);
	last = now;
	g_jitter_prev[0] = g_jitter[0]; g_jitter_prev[1] = g_jitter[1];
	g_jitter_index = (g_jitter_index + 1) % 16;
	g_jitter[0] = halton(2, (int)g_jitter_index + 1) - 0.5f;
	g_jitter[1] = halton(3, (int)g_jitter_index + 1) - 0.5f;
}
int aa_jitter(float *x, float *y)
{
	if (!temporal(aa_mode())) return 0;
	*x = g_jitter[0]; *y = g_jitter[1];
	return 1;
}

/* ================================================================ the game's camera */
void aa_scene_begin(void)
{
	/* the view's projection record (s_2fd90_projection, unknown_02b400.cpp), which function_2ba10 builds with function_2fd90
	   for the view before drawing it: +0x34 the view's frame (scale, forward = right, left = up, up = backward, position),
	   +0x78 the projection matrix (m[0] scale_x, m[5] scale_y, m[8] -offset_x, m[9] -offset_y) */
	const float *p = (const float *)0x4b9e14;
	int i;
	g_slot = *(const long *)0x4b9ed4 & 3;                   /* g_4b9ed4: the view's player (function_2ba10): what it keeps is its own */
	if (!temporal(aa_mode())) return;
	for (i = 0; i < 3; i++)
	{
		g_camera.right[i] = p[14 + i];
		g_camera.up[i] = p[17 + i];
		g_camera.forward[i] = -p[20 + i];
		g_camera.position[i] = p[23 + i];
	}
	g_camera.scale_x = p[30 + 0]; g_camera.scale_y = p[30 + 5];
	g_camera.offset_x = -p[30 + 8]; g_camera.offset_y = -p[30 + 9];
	g_camera.distance = *(const float *)0x48565c;          /* g_48565c: the depth buffer's value 1 is this far (function_12d50) */
	g_camera.valid = g_camera.scale_x != 0.0f && g_camera.scale_y != 0.0f && g_camera.distance > 0.0f;
}

/* the transform the game gave the view's first 3D draw (vertex constants 0-7, function_12fa0: 0-3 the view and projection
   times 16777215 / g_48565c, 4-6 the camera's right, up and backward, 7 its position): a test compares it with the record */
void aa_scene_constants(const float (*c)[4])
{
	static unsigned n;
	float k;
	if (aa_mode() != AA_TAA || g_debug != 3 || ++n % 15) return;
	k = sqrtf(c[3][0] * c[3][0] + c[3][1] * c[3][1] + c[3][2] * c[3][2]);
	h2n_log("       taa: record F %.4f %.4f %.4f P %.3f %.3f %.3f sx %.4f sy %.4f ox %.4f oy %.4f dist %.3f | constants F %.4f %.4f %.4f R %.4f %.4f %.4f P %.3f %.3f %.3f k %.4f\n",
		g_camera.forward[0], g_camera.forward[1], g_camera.forward[2], g_camera.position[0], g_camera.position[1], g_camera.position[2],
		g_camera.scale_x, g_camera.scale_y, g_camera.offset_x, g_camera.offset_y, g_camera.distance,
		k ? c[3][0] / k : 0, k ? c[3][1] / k : 0, k ? c[3][2] / k : 0, c[4][0], c[4][1], c[4][2], c[7][0], c[7][1], c[7][2], k);
}

/* ================================================================ resources */
/* A shader Windows' Direct3D 9 refuses (Wine is more lenient, so this shows on the user's PC first): which instruction.
   The shader is cut after its first k instructions (its declarations kept, open if and rep blocks closed, oC0 then written
   from c0 unless the kept part writes it) and offered again; the smallest k that is refused names the instruction. */
static int offer(IDirect3DDevice9 *d, const DWORD *code, unsigned words, unsigned k, const unsigned *starts, unsigned n, DWORD *buffer)
{
	IDirect3DPixelShader9 *ps = NULL;
	DWORD stack[32];
	unsigned depth = 0, i, m, writes_oc0 = 0;
	HRESULT hr;
	m = k < n ? starts[k] : words;
	if (m > 8000) return 1;
	memcpy(buffer, code, m * sizeof(DWORD));
	for (i = 0; i < k && i < n; i++)
	{
		DWORD token = code[starts[i]], op = token & 0xffff;
		if ((op == 38 || op == 40 || op == 41 || op == 27) && depth < 32) stack[depth++] = op == 38 ? 39 : op == 27 ? 29 : 43;
		else if ((op == 39 || op == 43 || op == 29) && depth) depth--;
		if (((token >> 24) & 15) && op != 31 && op != 81)
		{
			DWORD dst = code[starts[i] + 1];
			if ((((dst >> 28) & 7) | ((dst >> 8) & 0x18)) == 8 && (dst & 0x7ff) == 0) writes_oc0 = 1;
		}
	}
	if (k < n)
	{
		while (depth) buffer[m++] = stack[--depth];
		if (!writes_oc0) { buffer[m++] = 0x02000001; buffer[m++] = 0x800f0800; buffer[m++] = 0xa0e40000; }
		buffer[m++] = 0x0000ffff;
	}
	hr = IDirect3DDevice9_CreatePixelShader(d, buffer, &ps);
	if (SUCCEEDED(hr) && ps) IDirect3DPixelShader9_Release(ps);
	return SUCCEEDED(hr);
}
static void find_refused(IDirect3DDevice9 *d, const char *name, const DWORD *code)
{
	static DWORD buffer[8192];
	unsigned starts[2048], n = 0, i = 1, words, low, high;
	while (code[i] != 0x0000ffff && i < 8000)
	{
		DWORD op = code[i] & 0xffff, length = (code[i] >> 24) & 15;
		if (op != 31 && op != 81 && op != 48 && op != 47 && n < 2048) starts[n++] = i;
		i += 1 + length;
	}
	words = i + 1;
	if (!offer(d, code, words, 0, starts, n, buffer))
	{
		h2n_log("*** picture: anti-aliasing: %s is refused already with only its declarations (dcl, def) and oC0 = c0\n", name);
		return;
	}
	low = 0; high = n;          /* the first low instructions are taken; with all high it is refused */
	if (offer(d, code, words, n, starts, n, buffer)) { h2n_log("*** picture: anti-aliasing: %s is taken when offered again\n", name); return; }
	while (high - low > 1)
	{
		unsigned mid = (low + high) / 2;
		if (offer(d, code, words, mid, starts, n, buffer)) low = mid; else high = mid;
	}
	{
		unsigned at = starts[high - 1], length = (code[at] >> 24) & 15, j;
		char text[160];
		int used = 0;
		for (j = 0; j <= length && j < 8 && used < 140; j++) used += sprintf(text + used, " %08lx", (unsigned long)code[at + j]);
		h2n_log("*** picture: anti-aliasing: %s is refused at its instruction %u of %u (operation %lu, tokens%s)\n", name, high, n,
			(unsigned long)(code[at] & 0xffff), text);
	}
}

static int make(IDirect3DDevice9 *d)
{
	static int tried;
	const char *refused = NULL;
	const DWORD *refused_code = NULL;
	static const D3DVERTEXELEMENT9 elements[] = { { 0, 0, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0 }, { 0, 16, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0 }, D3DDECL_END() };
	IDirect3D9 *d3d = NULL;
	HRESULT hr = S_OK;
	if (tried) return !g_failed;
	tried = 1;
#define VS(var, code) if (SUCCEEDED(hr)) { hr = IDirect3DDevice9_CreateVertexShader(d, code, &var); if (FAILED(hr)) refused = #code; }
#define PS(var, code) if (SUCCEEDED(hr)) { hr = IDirect3DDevice9_CreatePixelShader(d, code, &var); if (FAILED(hr)) { refused = #code; refused_code = code; } }
	VS(g_quad_vs, g_aa_quad_vs_code);
	PS(g_copy_ps, g_aa_copy_ps_code);
	if (SUCCEEDED(hr)) hr = IDirect3DDevice9_CreateVertexDeclaration(d, elements, &g_declaration);
	if (g_mode == AA_FXAA) PS(g_fxaa_ps, g_aa_edge_ps_code);
	if (g_mode == AA_SMAA)
	{
		VS(g_smaa_edge_vs, g_smaa_edge_vs_code); VS(g_smaa_neighbor_vs, g_smaa_neighbor_vs_code); PS(g_smaa_neighbor_ps, g_smaa_neighbor_ps_code);
		if (g_smaa_preset == 3) { PS(g_smaa_edge_ps, g_smaa_edge_ps_ultra_code); VS(g_smaa_blend_vs, g_smaa_blend_vs_ultra_code); PS(g_smaa_blend_ps, g_smaa_blend_ps_ultra_code); }
		else if (g_smaa_preset == 1) { PS(g_smaa_edge_ps, g_smaa_edge_ps_medium_code); VS(g_smaa_blend_vs, g_smaa_blend_vs_medium_code); PS(g_smaa_blend_ps, g_smaa_blend_ps_medium_code); }
		else if (g_smaa_preset == 0) { PS(g_smaa_edge_ps, g_smaa_edge_ps_low_code); VS(g_smaa_blend_vs, g_smaa_blend_vs_low_code); PS(g_smaa_blend_ps, g_smaa_blend_ps_low_code); }
		else { PS(g_smaa_edge_ps, g_smaa_edge_ps_high_code); VS(g_smaa_blend_vs, g_smaa_blend_vs_high_code); PS(g_smaa_blend_ps, g_smaa_blend_ps_high_code); }
	}
	if (temporal(g_mode)) { PS(g_taa_depth_ps, g_taa_depth_ps_code); PS(g_taa_velocity_ps, g_taa_velocity_ps_code); PS(g_taa_resolve_ps, g_taa_resolve_ps_code); PS(g_taa_debug_ps, g_taa_debug_ps_code); }
#undef VS
#undef PS
	if (FAILED(hr))
	{
		g_failed = 1;
		h2n_log("*** picture: anti-aliasing (H2N_AA=%s): Direct3D 9 did not accept %s (%08lx); no anti-aliasing\n",
			g_mode == AA_FXAA ? "fxaa" : g_mode == AA_SMAA ? "smaa" : "taa", refused ? refused : "the vertex declaration", (unsigned long)hr);
		if (refused_code) find_refused(d, refused, refused_code);
		return 0;
	}
	IDirect3DDevice9_GetDirect3D(d, &d3d);
	if (d3d)
	{
		g_srgb_read = SUCCEEDED(IDirect3D9_CheckDeviceFormat(d3d, D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, D3DFMT_X8R8G8B8, D3DUSAGE_QUERY_SRGBREAD, D3DRTYPE_TEXTURE, D3DFMT_A8R8G8B8));
		g_srgb_write = SUCCEEDED(IDirect3D9_CheckDeviceFormat(d3d, D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, D3DFMT_X8R8G8B8, D3DUSAGE_QUERY_SRGBWRITE | D3DUSAGE_RENDERTARGET, D3DRTYPE_TEXTURE, D3DFMT_A8R8G8B8));
		g_velocity_format = SUCCEEDED(IDirect3D9_CheckDeviceFormat(d3d, D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, D3DFMT_X8R8G8B8, D3DUSAGE_RENDERTARGET, D3DRTYPE_TEXTURE, D3DFMT_G16R16F)) ? D3DFMT_G16R16F
			: SUCCEEDED(IDirect3D9_CheckDeviceFormat(d3d, D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, D3DFMT_X8R8G8B8, D3DUSAGE_RENDERTARGET, D3DRTYPE_TEXTURE, D3DFMT_A16B16G16R16F)) ? D3DFMT_A16B16G16R16F : D3DFMT_UNKNOWN;
		g_depth_copy_format = SUCCEEDED(IDirect3D9_CheckDeviceFormat(d3d, D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, D3DFMT_X8R8G8B8, D3DUSAGE_RENDERTARGET, D3DRTYPE_TEXTURE, D3DFMT_R32F)) ? D3DFMT_R32F : D3DFMT_UNKNOWN;
		IDirect3D9_Release(d3d);
	}
	if (temporal(g_mode) && g_velocity_format == D3DFMT_UNKNOWN)
	{
		g_failed = 1;
		h2n_log("*** picture: anti-aliasing (H2N_AA=taa): this device cannot draw into a floating-point texture (G16R16F) for the motion; no anti-aliasing\n");
		return 0;
	}
	if (g_mode == AA_SMAA)
	{
		D3DLOCKED_RECT lock;
		int i;
		/* SMAA.cpp's loadAreaTex / loadSearchTex without the DDS files: the bytes of AreaTex.h (D3DFMT_A8L8) and SearchTex.h (D3DFMT_L8) */
		hr = IDirect3DDevice9_CreateTexture(d, AREATEX_WIDTH, AREATEX_HEIGHT, 1, 0, D3DFMT_A8L8, D3DPOOL_MANAGED, &g_area, NULL);
		if (SUCCEEDED(hr) && SUCCEEDED(IDirect3DTexture9_LockRect(g_area, 0, &lock, NULL, 0)))
		{
			for (i = 0; i < AREATEX_HEIGHT; i++) memcpy((char *)lock.pBits + i * lock.Pitch, areaTexBytes + i * AREATEX_PITCH, AREATEX_PITCH);
			IDirect3DTexture9_UnlockRect(g_area, 0);
		}
		if (SUCCEEDED(hr)) hr = IDirect3DDevice9_CreateTexture(d, SEARCHTEX_WIDTH, SEARCHTEX_HEIGHT, 1, 0, D3DFMT_L8, D3DPOOL_MANAGED, &g_search, NULL);
		if (SUCCEEDED(hr) && SUCCEEDED(IDirect3DTexture9_LockRect(g_search, 0, &lock, NULL, 0)))
		{
			for (i = 0; i < SEARCHTEX_HEIGHT; i++) memcpy((char *)lock.pBits + i * lock.Pitch, searchTexBytes + i * SEARCHTEX_PITCH, SEARCHTEX_PITCH);
			IDirect3DTexture9_UnlockRect(g_search, 0);
		}
		if (FAILED(hr)) { g_failed = 1; h2n_log("*** picture: anti-aliasing (H2N_AA=smaa): its area and search textures could not be made (%08lx); no anti-aliasing\n", (unsigned long)hr); return 0; }
	}
	h2n_log("       picture: anti-aliasing H2N_AA=%s on: %s%s\n", g_mode == AA_FXAA ? "fxaa" : g_mode == AA_SMAA ? "smaa" : g_mode == AA_DLSS ? "dlss" : "taa",
		g_mode == AA_FXAA ? "edge smoothing of each player's 3D view (the HUD and menus left as they are)"
		: g_mode == AA_SMAA ? (g_smaa_preset == 3 ? "SMAA 1x, preset ULTRA, on each player's 3D view" : g_smaa_preset == 1 ? "SMAA 1x, preset MEDIUM, on each player's 3D view"
			: g_smaa_preset == 0 ? "SMAA 1x, preset LOW, on each player's 3D view" : "SMAA 1x, preset HIGH, on each player's 3D view")
		: g_mode == AA_DLSS ? "NVIDIA DLSS (DLAA) of each player's 3D view through H2Classic_DLSS.exe, TAA while it starts or if it cannot run"
		: "temporal anti-aliasing of each player's 3D view (16 sub-pixel offsets, motion from the depth and the camera)",
		g_mode == AA_SMAA ? (g_srgb_read && g_srgb_write ? "; blended in linear light (sRGB)" : "; blended as stored (this device has no sRGB read or write)")
		: temporal(g_mode) ? (g_depth_copy_format == D3DFMT_R32F ? "" : "; the first-person weapon is not told apart (no R32F texture)") : "");
	return 1;
}
static void dl_free(view_slot *s)
{
	IDirect3DSurface9 **surfaces[] = { &s->dl_color_s, &s->dl_motion_s, &s->dl_depth_s };
	IDirect3DTexture9 **textures[] = { &s->dl_color, &s->dl_motion, &s->dl_depth, &s->dl_output };
	unsigned i;
	for (i = 0; i < 3; i++) { if (*surfaces[i]) IDirect3DSurface9_Release(*surfaces[i]); *surfaces[i] = NULL; }
	for (i = 0; i < 4; i++) { if (*textures[i]) IDirect3DTexture9_Release(*textures[i]); *textures[i] = NULL; }
	s->dl_width = s->dl_height = 0; s->dl_valid = 0;
}
static void slot_free(view_slot *s)
{
	dl_free(s);
	IDirect3DSurface9 **surfaces[] = { &s->color_s, &s->edges_s, &s->blend_s, &s->velocity_s, &s->depth_copy_s, &s->history_s[0], &s->history_s[1] };
	IDirect3DTexture9 **textures[] = { &s->color, &s->edges, &s->blend, &s->velocity, &s->depth_copy, &s->history[0], &s->history[1] };
	unsigned i;
	for (i = 0; i < sizeof(surfaces) / sizeof(surfaces[0]); i++) { if (*surfaces[i]) IDirect3DSurface9_Release(*surfaces[i]); *surfaces[i] = NULL; }
	for (i = 0; i < sizeof(textures) / sizeof(textures[0]); i++) { if (*textures[i]) IDirect3DTexture9_Release(*textures[i]); *textures[i] = NULL; }
	s->width = s->height = 0; s->history_valid = 0; s->depth_copy_valid = 0;
}
void aa_release(void)
{
	unsigned i;
	for (i = 0; i < 4; i++) slot_free(&g_slots[i]);
	if (g_gpu_done) { IDirect3DQuery9_Release(g_gpu_done); g_gpu_done = NULL; }
}
static int target(IDirect3DDevice9 *d, unsigned w, unsigned h, D3DFORMAT f, IDirect3DTexture9 **t, IDirect3DSurface9 **s)
{
	if (*t) return 1;
	if (FAILED(IDirect3DDevice9_CreateTexture(d, w, h, 1, D3DUSAGE_RENDERTARGET, f, D3DPOOL_DEFAULT, t, NULL))) { *t = NULL; return 0; }
	if (FAILED(IDirect3DTexture9_GetSurfaceLevel(*t, 0, s))) { IDirect3DTexture9_Release(*t); *t = NULL; *s = NULL; return 0; }
	return 1;
}
static view_slot *slot_for(IDirect3DDevice9 *d, unsigned w, unsigned h)
{
	view_slot *s = &g_slots[g_slot];
	int ok = 1;
	if (s->width != w || s->height != h) { slot_free(s); s->width = w; s->height = h; }
	ok = target(d, w, h, D3DFMT_A8R8G8B8, &s->color, &s->color_s);
	if (ok && g_mode == AA_SMAA) ok = target(d, w, h, D3DFMT_A8R8G8B8, &s->edges, &s->edges_s) && target(d, w, h, D3DFMT_A8R8G8B8, &s->blend, &s->blend_s);
	if (ok && temporal(g_mode))
	{
		ok = target(d, w, h, g_velocity_format, &s->velocity, &s->velocity_s) && target(d, w, h, D3DFMT_A8R8G8B8, &s->history[0], &s->history_s[0])
			&& target(d, w, h, D3DFMT_A8R8G8B8, &s->history[1], &s->history_s[1]);
		if (ok && g_depth_copy_format != D3DFMT_UNKNOWN) target(d, w, h, g_depth_copy_format, &s->depth_copy, &s->depth_copy_s);
	}
	if (!ok)
	{
		static int told;
		if (!told) { told = 1; h2n_log("*** picture: anti-aliasing: its %ux%u surfaces could not be made; the view is left as it is\n", w, h); }
		slot_free(s);
		return NULL;
	}
	return s;
}

/* H2N_AADUMP=n (a test): the anti-aliasing inputs and outputs of the first view drawn from frame n on, written as raw files (aa_<what>.raw: width, height,
   format, then the rows), to be checked against the reference code outside the game (scripts/aa_check.py) */
static unsigned g_dump_at; static int g_dumped; static float g_dump_sin;
static void dump(IDirect3DDevice9 *d, IDirect3DSurface9 *from, const RECT *r, D3DFORMAT f, unsigned w, unsigned h, const char *name)
{
	IDirect3DSurface9 *whole = NULL, *mem = NULL;
	D3DSURFACE_DESC desc;
	D3DLOCKED_RECT lock;
	FILE *out;
	unsigned y, bytes = f == D3DFMT_G16R16F || f == D3DFMT_R32F ? 4 : f == D3DFMT_A16B16G16R16F ? 8 : 4;
	char path[64];
	if (FAILED(IDirect3DSurface9_GetDesc(from, &desc))) return;
	if (r)
	{
		/* a part of a surface: copied out first */
		if (FAILED(IDirect3DDevice9_CreateRenderTarget(d, w, h, desc.Format, D3DMULTISAMPLE_NONE, 0, FALSE, &whole, NULL))) return;
		if (FAILED(IDirect3DDevice9_StretchRect(d, from, r, whole, NULL, D3DTEXF_NONE))) { IDirect3DSurface9_Release(whole); return; }
		from = whole;
	}
	if (SUCCEEDED(IDirect3DDevice9_CreateOffscreenPlainSurface(d, w, h, desc.Format, D3DPOOL_SYSTEMMEM, &mem, NULL)) && SUCCEEDED(IDirect3DDevice9_GetRenderTargetData(d, from, mem))
		&& SUCCEEDED(IDirect3DSurface9_LockRect(mem, &lock, NULL, D3DLOCK_READONLY)))
	{
		snprintf(path, sizeof(path), "aa_%s.raw", name);
		out = fopen(path, "wb");
		if (out)
		{
			unsigned head[3] = { w, h, (unsigned)desc.Format };
			fwrite(head, 4, 3, out);
			for (y = 0; y < h; y++) fwrite((char *)lock.pBits + y * lock.Pitch, bytes, w, out);
			fclose(out);
			h2n_log("       picture: anti-aliasing test: %s written (%ux%u, format %u)\n", path, w, h, (unsigned)desc.Format);
		}
		IDirect3DSurface9_UnlockRect(mem);
	}
	if (mem) IDirect3DSurface9_Release(mem);
	if (whole) IDirect3DSurface9_Release(whole);
}

/* ================================================================ drawing a pass */
static void pass(IDirect3DDevice9 *d, IDirect3DSurface9 *into, unsigned x, unsigned y, unsigned w, unsigned h, DWORD mask)
{
	D3DVIEWPORT9 v;
	v.X = x; v.Y = y; v.Width = w; v.Height = h; v.MinZ = 0.0f; v.MaxZ = 1.0f;
	IDirect3DDevice9_SetRenderTarget(d, 0, into);
	IDirect3DDevice9_SetDepthStencilSurface(d, NULL);
	IDirect3DDevice9_SetViewport(d, &v);
	IDirect3DDevice9_SetRenderState(d, D3DRS_ALPHATESTENABLE, FALSE); IDirect3DDevice9_SetRenderState(d, D3DRS_ALPHABLENDENABLE, FALSE);
	IDirect3DDevice9_SetRenderState(d, D3DRS_ZENABLE, D3DZB_FALSE); IDirect3DDevice9_SetRenderState(d, D3DRS_ZWRITEENABLE, FALSE);
	IDirect3DDevice9_SetRenderState(d, D3DRS_STENCILENABLE, FALSE); IDirect3DDevice9_SetRenderState(d, D3DRS_CULLMODE, D3DCULL_NONE);
	IDirect3DDevice9_SetRenderState(d, D3DRS_FILLMODE, D3DFILL_SOLID); IDirect3DDevice9_SetRenderState(d, D3DRS_SCISSORTESTENABLE, FALSE);
	IDirect3DDevice9_SetRenderState(d, D3DRS_SRGBWRITEENABLE, FALSE);
	IDirect3DDevice9_SetRenderState(d, D3DRS_COLORWRITEENABLE, mask);
	IDirect3DDevice9_SetVertexDeclaration(d, g_declaration);
}
static void sample(IDirect3DDevice9 *d, DWORD stage, IDirect3DBaseTexture9 *t, D3DTEXTUREFILTERTYPE filter, BOOL srgb)
{
	IDirect3DDevice9_SetTexture(d, stage, t);
	IDirect3DDevice9_SetSamplerState(d, stage, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
	IDirect3DDevice9_SetSamplerState(d, stage, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
	IDirect3DDevice9_SetSamplerState(d, stage, D3DSAMP_MAGFILTER, filter);
	IDirect3DDevice9_SetSamplerState(d, stage, D3DSAMP_MINFILTER, filter);
	IDirect3DDevice9_SetSamplerState(d, stage, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
	IDirect3DDevice9_SetSamplerState(d, stage, D3DSAMP_SRGBTEXTURE, srgb);
}
static void quad(IDirect3DDevice9 *d, unsigned w, unsigned h)
{
	/* SMAA.cpp's "typical aligned fullscreen quad": Direct3D 9 puts the middle of a pixel half a pixel from where its texel is */
	float q[4][8];
	unsigned i;
	for (i = 0; i < 4; i++)
	{
		float u = (float)(i & 1), v = (float)(i >> 1);
		q[i][0] = u * 2.0f - 1.0f - 1.0f / w; q[i][1] = 1.0f - v * 2.0f + 1.0f / h; q[i][2] = 0.5f; q[i][3] = 1.0f;
		q[i][4] = u; q[i][5] = v; q[i][6] = 0.0f; q[i][7] = 1.0f;
	}
	IDirect3DDevice9_DrawPrimitiveUP(d, D3DPT_TRIANGLESTRIP, 2, q, sizeof(q[0]));
}
static void done(IDirect3DDevice9 *d)
{
	DWORD i;
	for (i = 0; i < 4; i++) { IDirect3DDevice9_SetSamplerState(d, i, D3DSAMP_SRGBTEXTURE, FALSE); IDirect3DDevice9_SetTexture(d, i, NULL); }
	IDirect3DDevice9_SetRenderState(d, D3DRS_SRGBWRITEENABLE, FALSE);
	IDirect3DDevice9_SetRenderState(d, D3DRS_COLORWRITEENABLE, D3DCOLORWRITEENABLE_RED | D3DCOLORWRITEENABLE_GREEN | D3DCOLORWRITEENABLE_BLUE | D3DCOLORWRITEENABLE_ALPHA);
}
#define RGB_ONLY (D3DCOLORWRITEENABLE_RED | D3DCOLORWRITEENABLE_GREEN | D3DCOLORWRITEENABLE_BLUE)
#define ALL (RGB_ONLY | D3DCOLORWRITEENABLE_ALPHA)

/* the depth buffer's texture coordinates for the view's: uv * (w / depth_w, h / depth_h) + (x / depth_w, y / depth_h) */
static void depth_mapping(const aa_view *v, float *c)
{
	c[0] = (float)v->width / v->depth_width; c[1] = (float)v->height / v->depth_height;
	c[2] = (float)v->x / v->depth_width; c[3] = (float)v->y / v->depth_height;
}

/* ================================================================ the first-person draws (taa) */
void aa_first_person(IDirect3DDevice9 *d, const aa_view *v)
{
	view_slot *s;
	float c[4];
	if (!temporal(aa_mode()) || !v->depth || !make(d)) return;
	s = slot_for(d, v->width, v->height);
	if (!s || !s->depth_copy) return;
	pass(d, s->depth_copy_s, 0, 0, v->width, v->height, ALL);
	IDirect3DDevice9_SetVertexShader(d, g_quad_vs); IDirect3DDevice9_SetPixelShader(d, g_taa_depth_ps);
	depth_mapping(v, c);
	IDirect3DDevice9_SetPixelShaderConstantF(d, 0, c, 1);
	sample(d, 0, (IDirect3DBaseTexture9 *)v->depth, D3DTEXF_POINT, FALSE);
	quad(d, v->width, v->height);
	done(d);
	s->depth_copy_valid = 1;
	g_first_person++;
}

/* ================================================================ the passes */
static void fxaa(IDirect3DDevice9 *d, const aa_view *v, view_slot *s)
{
	float c[4];
	c[0] = 1.0f / v->width; c[1] = 1.0f / v->height; c[2] = -c[0]; c[3] = -c[1];
	pass(d, v->color_surface, v->x, v->y, v->width, v->height, RGB_ONLY);
	IDirect3DDevice9_SetVertexShader(d, g_quad_vs); IDirect3DDevice9_SetPixelShader(d, g_fxaa_ps);
	IDirect3DDevice9_SetPixelShaderConstantF(d, 0, c, 1);
	sample(d, 0, (IDirect3DBaseTexture9 *)s->color, D3DTEXF_LINEAR, FALSE);
	quad(d, v->width, v->height);
}
static void smaa(IDirect3DDevice9 *d, const aa_view *v, view_slot *s)
{
	float c[2][4];
	c[0][0] = 1.0f / v->width; c[0][1] = 1.0f / v->height; c[0][2] = (float)v->width; c[0][3] = (float)v->height;     /* SMAA_RT_METRICS */
	c[1][0] = 2.0f * c[0][0]; c[1][1] = 2.0f * c[0][1]; c[1][2] = 0.25f * c[0][0]; c[1][3] = 0.25f * c[0][1];
	/* SMAA::edgesDetectionPass: into the edges (cleared to 0), the luma of the colour read without sRGB (colorGammaTex) */
	pass(d, s->edges_s, 0, 0, v->width, v->height, ALL);
	IDirect3DDevice9_Clear(d, 0, NULL, D3DCLEAR_TARGET, 0, 1.0f, 0);
	IDirect3DDevice9_SetVertexShader(d, g_smaa_edge_vs); IDirect3DDevice9_SetPixelShader(d, g_smaa_edge_ps);
	IDirect3DDevice9_SetVertexShaderConstantF(d, 0, c[0], 1); IDirect3DDevice9_SetPixelShaderConstantF(d, 0, c[0], 1);
	sample(d, 0, (IDirect3DBaseTexture9 *)s->color, D3DTEXF_LINEAR, FALSE);
	quad(d, v->width, v->height);
	/* SMAA::blendingWeightsCalculationPass: into the weights (cleared to 0): edgesTex linear, areaTex linear, searchTex point */
	pass(d, s->blend_s, 0, 0, v->width, v->height, ALL);
	IDirect3DDevice9_Clear(d, 0, NULL, D3DCLEAR_TARGET, 0, 1.0f, 0);
	IDirect3DDevice9_SetVertexShader(d, g_smaa_blend_vs); IDirect3DDevice9_SetPixelShader(d, g_smaa_blend_ps);
	IDirect3DDevice9_SetVertexShaderConstantF(d, 0, c[0], 1); IDirect3DDevice9_SetPixelShaderConstantF(d, 0, c[0], 2);
	sample(d, 0, (IDirect3DBaseTexture9 *)s->edges, D3DTEXF_LINEAR, FALSE);
	sample(d, 1, (IDirect3DBaseTexture9 *)g_area, D3DTEXF_LINEAR, FALSE);
	sample(d, 2, (IDirect3DBaseTexture9 *)g_search, D3DTEXF_POINT, FALSE);
	quad(d, v->width, v->height);
	IDirect3DDevice9_SetTexture(d, 2, NULL);
	/* SMAA::neighborhoodBlendingPass: into the game's picture, colorTex read and written in linear light (SRGBTexture,
	   SRGBWriteEnable), blendTex linear; only red, green and blue are written */
	pass(d, v->color_surface, v->x, v->y, v->width, v->height, RGB_ONLY);
	IDirect3DDevice9_SetRenderState(d, D3DRS_SRGBWRITEENABLE, g_srgb_read && g_srgb_write);
	IDirect3DDevice9_SetVertexShader(d, g_smaa_neighbor_vs); IDirect3DDevice9_SetPixelShader(d, g_smaa_neighbor_ps);
	IDirect3DDevice9_SetVertexShaderConstantF(d, 0, c[0], 1); IDirect3DDevice9_SetPixelShaderConstantF(d, 0, c[0], 1);
	sample(d, 0, (IDirect3DBaseTexture9 *)s->color, D3DTEXF_LINEAR, g_srgb_read && g_srgb_write);
	sample(d, 1, (IDirect3DBaseTexture9 *)s->blend, D3DTEXF_LINEAR, FALSE);
	quad(d, v->width, v->height);
}
static void copy(IDirect3DDevice9 *d, IDirect3DTexture9 *from, IDirect3DSurface9 *into, unsigned x, unsigned y, unsigned w, unsigned h, DWORD mask)
{
	pass(d, into, x, y, w, h, mask);
	IDirect3DDevice9_SetVertexShader(d, g_quad_vs); IDirect3DDevice9_SetPixelShader(d, g_copy_ps);
	sample(d, 0, (IDirect3DBaseTexture9 *)from, D3DTEXF_POINT, FALSE);
	quad(d, w, h);
}
/* H2N_AADEBUG=log: the motion at a few pixels read back, against the same sum done here (the depth read back through a copy) */
static float half_to_float(unsigned short h)
{
	unsigned sign = (h >> 15) & 1, e = (h >> 10) & 31, m = h & 1023;
	float f = e == 0 ? m / 1024.0f / 16384.0f : e == 31 ? 65504.0f : (1.0f + m / 1024.0f) * powf(2.0f, (float)e - 15.0f);
	return sign ? -f : f;
}
static void velocity_check(IDirect3DDevice9 *d, const aa_view *v, view_slot *s, const camera *now, const camera *before)
{
	static IDirect3DTexture9 *depth_t; static IDirect3DSurface9 *depth_s, *depth_mem, *vel_mem; static unsigned w, h;
	D3DLOCKED_RECT lock;
	float c[4];
	int k;
	if (w != v->width || h != v->height)
	{
		if (depth_s) IDirect3DSurface9_Release(depth_s); if (depth_t) IDirect3DTexture9_Release(depth_t);
		if (depth_mem) IDirect3DSurface9_Release(depth_mem); if (vel_mem) IDirect3DSurface9_Release(vel_mem);
		depth_t = NULL; depth_s = depth_mem = vel_mem = NULL; w = v->width; h = v->height;
		if (!target(d, w, h, D3DFMT_R32F, &depth_t, &depth_s)) return;
		IDirect3DDevice9_CreateOffscreenPlainSurface(d, w, h, D3DFMT_R32F, D3DPOOL_SYSTEMMEM, &depth_mem, NULL);
		IDirect3DDevice9_CreateOffscreenPlainSurface(d, w, h, g_velocity_format, D3DPOOL_SYSTEMMEM, &vel_mem, NULL);
	}
	if (!depth_s || !depth_mem || !vel_mem) return;
	pass(d, depth_s, 0, 0, w, h, ALL);
	IDirect3DDevice9_SetVertexShader(d, g_quad_vs); IDirect3DDevice9_SetPixelShader(d, g_taa_depth_ps);
	depth_mapping(v, c);
	IDirect3DDevice9_SetPixelShaderConstantF(d, 0, c, 1);
	sample(d, 0, (IDirect3DBaseTexture9 *)v->depth, D3DTEXF_POINT, FALSE);
	quad(d, w, h);
	if (FAILED(IDirect3DDevice9_GetRenderTargetData(d, depth_s, depth_mem)) || FAILED(IDirect3DDevice9_GetRenderTargetData(d, s->velocity_s, vel_mem))) return;
	for (k = 0; k < 4; k++)
	{
		unsigned px = k == 0 ? w / 2 : k == 1 ? w / 8 : k == 2 ? w * 7 / 8 : w * 11 / 16, py = k == 3 ? h * 13 / 16 : h / 2;
		float depth = 0, vx = 0, vy = 0, q[2], ndc[2], ray[2], world[3], rel[3], xp, yp, dp, qp[2];
		int i;
		if (SUCCEEDED(IDirect3DSurface9_LockRect(depth_mem, &lock, NULL, D3DLOCK_READONLY))) { depth = ((float *)((char *)lock.pBits + py * lock.Pitch))[px]; IDirect3DSurface9_UnlockRect(depth_mem); }
		if (SUCCEEDED(IDirect3DSurface9_LockRect(vel_mem, &lock, NULL, D3DLOCK_READONLY)))
		{
			const unsigned short *row = (const unsigned short *)((char *)lock.pBits + py * lock.Pitch);
			unsigned step = g_velocity_format == D3DFMT_G16R16F ? 2 : 4;
			vx = half_to_float(row[px * step]); vy = half_to_float(row[px * step + 1]);
			IDirect3DSurface9_UnlockRect(vel_mem);
		}
		q[0] = (px + 0.5f) / w + g_jitter[0] / w; q[1] = (py + 0.5f) / h + g_jitter[1] / h;
		ndc[0] = q[0] * 2.0f - 1.0f - 0.5f / v->half_width; ndc[1] = 1.0f - q[1] * 2.0f + 0.5f / v->half_height;
		ray[0] = (ndc[0] - now->offset_x) / now->scale_x; ray[1] = (ndc[1] - now->offset_y) / now->scale_y;
		for (i = 0; i < 3; i++) world[i] = now->position[i] + depth * now->distance * (now->forward[i] + ray[0] * now->right[i] + ray[1] * now->up[i]);
		for (i = 0; i < 3; i++) rel[i] = world[i] - before->position[i];
		xp = rel[0] * before->right[0] + rel[1] * before->right[1] + rel[2] * before->right[2];
		yp = rel[0] * before->up[0] + rel[1] * before->up[1] + rel[2] * before->up[2];
		dp = rel[0] * before->forward[0] + rel[1] * before->forward[1] + rel[2] * before->forward[2];
		qp[0] = (before->scale_x * xp / dp + before->offset_x) * 0.5f + (1.0f + 0.5f / v->half_width) * 0.5f;
		qp[1] = -(before->scale_y * yp / dp + before->offset_y) * 0.5f + (1.0f + 0.5f / v->half_height) * 0.5f;
		h2n_log("       taa check: pixel %u,%u depth %.6f (%.2f units) | read back %.5f %.5f | here %.5f %.5f | forward now %.3f %.3f before %.3f %.3f%s\n",
			px, py, depth, depth * now->distance, vx, vy, q[0] - qp[0], q[1] - qp[1], now->forward[0], now->forward[1], before->forward[0], before->forward[1],
			s->depth_copy && s->depth_copy_valid ? ", first-person copy made" : ", no first-person copy");
	}
}

/* the motion of each pixel since the last frame (VelocityBuffer.shader, blit_frag_prepass), into a float target */
static void velocity(IDirect3DDevice9 *d, const aa_view *v, view_slot *s, IDirect3DSurface9 *into, const camera *now, const camera *before)
{
	float c[16][4];
	int i;
	memset(c, 0, sizeof(c));
	pass(d, into, 0, 0, v->width, v->height, ALL);
	IDirect3DDevice9_SetVertexShader(d, g_quad_vs); IDirect3DDevice9_SetPixelShader(d, g_taa_velocity_ps);
	depth_mapping(v, c[0]);
	c[1][0] = 2.0f; c[1][1] = -2.0f; c[1][2] = -1.0f - 0.5f / v->half_width; c[1][3] = 1.0f + 0.5f / v->half_height;
	c[2][0] = g_jitter[0] / v->width; c[2][1] = g_jitter[1] / v->height;
	c[3][0] = 1.0f / now->scale_x; c[3][1] = 1.0f / now->scale_y; c[3][2] = now->offset_x; c[3][3] = now->offset_y;
	for (i = 0; i < 3; i++)
	{
		c[4][i] = now->right[i]; c[5][i] = now->up[i]; c[6][i] = now->forward[i]; c[7][i] = now->position[i];
		c[8][i] = before->right[i]; c[9][i] = before->up[i]; c[10][i] = before->forward[i]; c[11][i] = before->position[i];
	}
	c[7][3] = now->distance;
	c[12][0] = before->scale_x; c[12][1] = before->scale_y; c[12][2] = before->offset_x; c[12][3] = before->offset_y;
	c[13][0] = 0.5f; c[13][1] = -0.5f; c[13][2] = (1.0f + 0.5f / v->half_width) * 0.5f; c[13][3] = (1.0f + 0.5f / v->half_height) * 0.5f;
	c[15][0] = s->depth_copy && s->depth_copy_valid ? 1.0f : 0.0f;
	IDirect3DDevice9_SetPixelShaderConstantF(d, 0, c[0], 16);
	sample(d, 0, (IDirect3DBaseTexture9 *)v->depth, D3DTEXF_POINT, FALSE);
	sample(d, 1, (IDirect3DBaseTexture9 *)(s->depth_copy && s->depth_copy_valid ? s->depth_copy : v->depth), D3DTEXF_POINT, FALSE);
	quad(d, v->width, v->height);
}
static void taa(IDirect3DDevice9 *d, const aa_view *v, view_slot *s)
{
	float c[16][4], t;
	const camera *now = &g_camera, *before;
	int write;
	memset(c, 0, sizeof(c));
	/* the history: begun again with this frame when there is none for this view, its size or place changed, or the view
	   was not drawn in the frame before (TemporalReprojection.cs: "bootstrap", a copy of the frame) */
	if (!s->history_valid || s->x != v->x || s->y != v->y || s->last_frame + 1 != g_frame || !s->previous.valid)
	{
		IDirect3DDevice9_StretchRect(d, s->color_s, NULL, s->history_s[s->read], NULL, D3DTEXF_NONE);
		s->history_valid = 1;
		s->previous = *now;
		g_resets++;
	}
	before = &s->previous;
	velocity(d, v, s, s->velocity_s, now, before);
	if (g_debug == 3 && g_scenes % 15 == 0) velocity_check(d, v, s, now, before);
	/* the resolve (TemporalReprojection.shader) into the other history picture */
	write = !s->read;
	pass(d, s->history_s[write], 0, 0, v->width, v->height, ALL);
	IDirect3DDevice9_SetPixelShader(d, g_taa_resolve_ps);
	memset(c, 0, sizeof(c));
	depth_mapping(v, c[0]);
	c[1][0] = 1.0f / v->width; c[1][1] = 1.0f / v->height; c[1][2] = (float)v->width; c[1][3] = (float)v->height;
	c[2][0] = 1.0f / v->depth_width; c[2][1] = 1.0f / v->depth_height;
	c[3][0] = g_jitter[0] / v->width; c[3][1] = g_jitter[1] / v->height; c[3][2] = g_jitter_prev[0] / v->width; c[3][3] = g_jitter_prev[1] / v->height;
	t = GetTickCount() / 1000.0f;
	c[4][0] = 0.88f; c[4][1] = 0.97f; c[4][2] = sinf(t / 8.0f);       /* _FeedbackMin, _FeedbackMax, _SinTime.x */
	g_dump_sin = c[4][2];
	IDirect3DDevice9_SetPixelShaderConstantF(d, 0, c[0], 5);
	sample(d, 0, (IDirect3DBaseTexture9 *)s->color, D3DTEXF_LINEAR, FALSE);
	sample(d, 1, (IDirect3DBaseTexture9 *)s->history[s->read], D3DTEXF_LINEAR, FALSE);
	sample(d, 2, (IDirect3DBaseTexture9 *)s->velocity, D3DTEXF_POINT, FALSE);
	sample(d, 3, (IDirect3DBaseTexture9 *)v->depth, D3DTEXF_POINT, FALSE);
	quad(d, v->width, v->height);
	IDirect3DDevice9_SetTexture(d, 1, NULL); IDirect3DDevice9_SetTexture(d, 2, NULL); IDirect3DDevice9_SetTexture(d, 3, NULL);
	/* the result into the game's picture (red, green and blue: the game's alpha stays) */
	if (g_debug != 1 && g_debug != 2) copy(d, s->history[write], v->color_surface, v->x, v->y, v->width, v->height, RGB_ONLY);
	else
	{
		/* H2N_AADEBUG: the motion, or this frame against the history moved by the motion (before clipping), in its place */
		pass(d, v->color_surface, v->x, v->y, v->width, v->height, RGB_ONLY);
		IDirect3DDevice9_SetPixelShader(d, g_taa_debug_ps);
		memset(c, 0, sizeof(c));
		c[0][0] = g_debug == 1 ? 1.0f : 0.0f; c[0][1] = g_debug == 2 ? 1.0f : 0.0f; c[0][2] = 50.0f; c[0][3] = 4.0f;
		c[1][0] = g_jitter[0] / v->width; c[1][1] = g_jitter[1] / v->height;
		IDirect3DDevice9_SetPixelShaderConstantF(d, 0, c[0], 2);
		sample(d, 0, (IDirect3DBaseTexture9 *)s->color, D3DTEXF_LINEAR, FALSE);
		sample(d, 1, (IDirect3DBaseTexture9 *)s->history[s->read], D3DTEXF_LINEAR, FALSE);
		sample(d, 2, (IDirect3DBaseTexture9 *)s->velocity, D3DTEXF_POINT, FALSE);
		quad(d, v->width, v->height);
	}
	s->read = write;
	s->previous = *now;
}

/* ================================================================ DLSS (H2N_AA=dlss, through H2Classic_DLSS.exe: gfx_dlss.c) */
/* the graphics card has done what was drawn so far (the helper reads the textures on another device: Direct3D 9Ex
   shared textures have no other synchronisation) */
static int gpu_wait(IDirect3DDevice9 *d)
{
	DWORD start = GetTickCount();
	HRESULT hr;
	if (!g_gpu_done && FAILED(IDirect3DDevice9_CreateQuery(d, D3DQUERYTYPE_EVENT, &g_gpu_done))) { g_gpu_done = NULL; return 0; }
	IDirect3DQuery9_Issue(g_gpu_done, D3DISSUE_END);
	while ((hr = IDirect3DQuery9_GetData(g_gpu_done, NULL, 0, D3DGETDATA_FLUSH)) == S_FALSE)
		if (GetTickCount() - start > 500) return 0;
	return SUCCEEDED(hr);
}
/* one of the helper's textures, opened in this device by its shared handle (same size, a matching format) */
static HRESULT open_shared(IDirect3DDevice9 *d, unsigned w, unsigned h, D3DFORMAT f, HANDLE handle, IDirect3DTexture9 **t, IDirect3DSurface9 **s)
{
	HANDLE shared = handle;
	HRESULT hr = IDirect3DDevice9_CreateTexture(d, w, h, 1, D3DUSAGE_RENDERTARGET, f, D3DPOOL_DEFAULT, t, &shared);
	if (FAILED(hr)) { *t = NULL; return hr; }
	if (s && FAILED(hr = IDirect3DTexture9_GetSurfaceLevel(*t, 0, s))) { *s = NULL; return hr; }
	return S_OK;
}
/* the view through DLSS: its colour, motion and depth into the helper's textures, DLSS (DLAA) in the helper, its result
   back into the view before the HUD. 0: not done (the helper is not ready, or DLSS has been turned off): TAA does it */
static int dlss_view(IDirect3DDevice9 *d, const aa_view *v, view_slot *s)
{
	const camera *now = &g_camera;
	float c[4];
	RECT r;
	int reset;
	if (dlss_state() != DLSS_STATE_READY) return 0;
	if (s->dl_width != v->width || s->dl_height != v->height || !s->dl_output)
	{
		HANDLE handles[4];
		int direct = 0;
		HRESULT hr;
		const char *what;
		dl_free(s);
		if (!dlss_setup((unsigned)g_slot, v->width, v->height, handles, &direct)) return 0;
		/* the helper's B8G8R8A8 and R16G16B16A16_FLOAT as Direct3D 9 knows them (s10f: the output is 16-bit float too:
		   Direct3D 9 has no render target in the 8-bit RGBA order) */
		what = "colour"; hr = open_shared(d, v->width, v->height, D3DFMT_A8R8G8B8, handles[0], &s->dl_color, &s->dl_color_s);
		if (SUCCEEDED(hr)) { what = "motion"; hr = open_shared(d, v->width, v->height, D3DFMT_A16B16G16R16F, handles[1], &s->dl_motion, &s->dl_motion_s); }
		if (SUCCEEDED(hr)) { what = "depth"; hr = open_shared(d, v->width, v->height, D3DFMT_A16B16G16R16F, handles[2], &s->dl_depth, &s->dl_depth_s); }
		if (SUCCEEDED(hr)) { what = "output"; hr = open_shared(d, v->width, v->height, D3DFMT_A16B16G16R16F, handles[3], &s->dl_output, NULL); }
		if (FAILED(hr))
		{
			char why[160];
			_snprintf(why, sizeof(why), "Direct3D 9Ex could not open the helper's %s texture (%ux%u, handle %p, %08lx)", what, v->width, v->height, handles[what[0] == 'c' ? 0 : what[0] == 'm' ? 1 : what[0] == 'd' ? 2 : 3], (unsigned long)hr);
			dl_free(s);
			dlss_failed(why);
			return 0;
		}
		s->dl_width = v->width; s->dl_height = v->height; s->dl_valid = 0;
		h2n_log("       picture: DLSS: view %d, %ux%u: the helper's textures are open in Direct3D 9Ex%s\n", g_slot, v->width, v->height,
			direct ? " (its motion and depth go to DLSS as written here)" : "");
	}
	/* DLSS starts again (its InReset) where TAA would begin its history again */
	reset = !s->dl_valid || s->x != v->x || s->y != v->y || s->last_frame + 1 != g_frame || !s->previous.valid;
	if (reset) s->previous = *now;
	/* the colour as drawn (jittered), the motion (without the jitter: taa_velocity), the depth (taa_depth) */
	r.left = (LONG)v->x; r.top = (LONG)v->y; r.right = (LONG)(v->x + v->width); r.bottom = (LONG)(v->y + v->height);
	if (FAILED(IDirect3DDevice9_StretchRect(d, v->color_surface, &r, s->dl_color_s, NULL, D3DTEXF_NONE))) { dlss_failed("the view could not be copied into the helper's colour texture"); return 0; }
	velocity(d, v, s, s->dl_motion_s, now, &s->previous);
	pass(d, s->dl_depth_s, 0, 0, v->width, v->height, ALL);
	IDirect3DDevice9_SetVertexShader(d, g_quad_vs); IDirect3DDevice9_SetPixelShader(d, g_taa_depth_ps);
	depth_mapping(v, c);
	IDirect3DDevice9_SetPixelShaderConstantF(d, 0, c, 1);
	sample(d, 0, (IDirect3DBaseTexture9 *)v->depth, D3DTEXF_POINT, FALSE);
	quad(d, v->width, v->height);
	done(d);
	if (!gpu_wait(d)) { dlss_failed("the graphics card did not finish the view's inputs within half a second"); return 0; }
	/* the jitter as DLSS takes it: the shift of the picture in render pixels (x right, y down). The 3D is drawn so that
	   a pixel shows the point that belongs g_jitter further on (taa_velocity: the point's place without the jitter is
	   this place + the jitter), so the picture is shifted by -g_jitter; H2N_DLSS_JITTER=flip hands over +g_jitter */
	if (!dlss_evaluate((unsigned)g_slot, v->width, v->height, g_dlss_flip ? g_jitter[0] : -g_jitter[0], g_dlss_flip ? g_jitter[1] : -g_jitter[1], reset, g_frame_ms)) return 0;
	copy(d, s->dl_output, v->color_surface, v->x, v->y, v->width, v->height, RGB_ONLY);
	s->previous = *now;
	s->dl_valid = 1;
	s->history_valid = 0;                 /* if TAA takes over, it begins again */
	g_dlss_views++;
	return 1;
}

int aa_scene(IDirect3DDevice9 *d, const aa_view *v)
{
	view_slot *s;
	RECT r;
	int mode = aa_mode();
	if (mode == AA_OFF) return 0;
	if (!make(d)) return 0;
	if (!v->width || !v->height || v->x + v->width > v->full_width || v->y + v->height > v->full_height) { g_skipped++; return 0; }
	if (temporal(mode) && (!v->depth || !g_camera.valid || v->x + v->width > v->depth_width || v->y + v->height > v->depth_height))
	{
		static int told;
		if (!v->depth && !told) { told = 1; h2n_log("       picture: anti-aliasing (H2N_AA=taa) needs the depth buffer as a texture (INTZ), which this device does not have: the views are left as they are\n"); }
		g_skipped++;
		return 0;
	}
	s = slot_for(d, v->width, v->height);
	if (!s) { g_skipped++; return 0; }
	/* the view as the game drew it, copied out (a surface cannot be read while it is drawn into) */
	r.left = (LONG)v->x; r.top = (LONG)v->y; r.right = (LONG)(v->x + v->width); r.bottom = (LONG)(v->y + v->height);
	if (FAILED(IDirect3DDevice9_StretchRect(d, v->color_surface, &r, s->color_s, NULL, D3DTEXF_NONE))) { g_skipped++; return 0; }
	static int dump_read;
	if (!dump_read) { dump_read = 1; g_dump_at = (unsigned)h2n_env_int("H2N_AADUMP", 0); }
	int dump_now = g_dump_at && !g_dumped && g_frame >= g_dump_at;
	if (dump_now) dump(d, s->color_s, NULL, D3DFMT_A8R8G8B8, v->width, v->height, "input");
	if (mode == AA_FXAA) fxaa(d, v, s);
	else if (mode == AA_SMAA) smaa(d, v, s);
	else if (mode == AA_DLSS && dlss_view(d, v, s)) { }
	else { if (mode == AA_DLSS) g_dlss_taa++; taa(d, v, s); }
	done(d);
	if (dump_now)
	{
		g_dumped = 1;
		if (mode == AA_SMAA) { dump(d, s->edges_s, NULL, D3DFMT_A8R8G8B8, v->width, v->height, "edges"); dump(d, s->blend_s, NULL, D3DFMT_A8R8G8B8, v->width, v->height, "blend"); }
		if (mode == AA_TAA)
		{
			IDirect3DTexture9 *dt = NULL; IDirect3DSurface9 *ds = NULL;
			float c[4];
			dump(d, s->velocity_s, NULL, g_velocity_format, v->width, v->height, "velocity");
			dump(d, s->history_s[!s->read], NULL, D3DFMT_A8R8G8B8, v->width, v->height, "history");
			dump(d, s->history_s[s->read], NULL, D3DFMT_A8R8G8B8, v->width, v->height, "resolved");
			if (target(d, v->width, v->height, D3DFMT_R32F, &dt, &ds))
			{
				/* the depth as the view's texture coordinates see it (taa_depth.ps.asm into a float texture) */
				pass(d, ds, 0, 0, v->width, v->height, ALL);
				IDirect3DDevice9_SetVertexShader(d, g_quad_vs); IDirect3DDevice9_SetPixelShader(d, g_taa_depth_ps);
				depth_mapping(v, c);
				IDirect3DDevice9_SetPixelShaderConstantF(d, 0, c, 1);
				sample(d, 0, (IDirect3DBaseTexture9 *)v->depth, D3DTEXF_POINT, FALSE);
				quad(d, v->width, v->height);
				done(d);
				dump(d, ds, NULL, D3DFMT_R32F, v->width, v->height, "depth");
				IDirect3DSurface9_Release(ds); IDirect3DTexture9_Release(dt);
			}
			h2n_log("       picture: anti-aliasing test: jitter %.6f %.6f (pixels), feedback 0.88 0.97, sin %.6f\n", g_jitter[0], g_jitter[1], g_dump_sin);
		}
		dump(d, v->color_surface, &r, D3DFMT_A8R8G8B8, v->width, v->height, "output");
	}
	s->x = v->x; s->y = v->y; s->last_frame = g_frame; s->depth_copy_valid = 0;
	g_scenes++;
	return 1;
}

void aa_report(void)
{
	if (g_mode > 0)
		h2n_log("picture: anti-aliasing (H2N_AA=%s): %u views done, %u left as they were%s; %u history restarts, %u first-person depth copies\n",
			g_mode == AA_FXAA ? "fxaa" : g_mode == AA_SMAA ? "smaa" : g_mode == AA_DLSS ? "dlss" : "taa", g_scenes, g_skipped, g_failed ? " (it was turned off: see above)" : "", g_resets, g_first_person);
	if (g_mode == AA_DLSS)
	{
		h2n_log("picture: DLSS: %u views through DLSS, %u through TAA (while the helper started, or after DLSS was turned off)\n", g_dlss_views, g_dlss_taa);
		dlss_report();
		dlss_stop();
	}
}

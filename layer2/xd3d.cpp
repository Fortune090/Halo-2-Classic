/* H2Classic: the Xbox Direct3D calls the game makes, compiled with the Xbox SDK's own compiler and headers so every
 * type, constant and calling convention is the SDK's.
 *
 * Stage 3: a device that keeps all of the state and all of the resources (textures, surfaces and palettes have real
 * memory, in the layouts the SDK documents, so the game can lock, fill and read them) and counts what it is asked to
 * draw, but draws nothing. The picture comes in the next stage, behind the same calls. */
#include <xtl.h>
#include <stddef.h>
#include "gfx.h"

extern "C" {
void __cdecl h2n_log(const char *format, ...);
unsigned __cdecl h2n_next_sequence(void);
void *__cdecl h2n_contig_alloc(unsigned size);
void __cdecl h2n_contig_free(void *address);
void __cdecl h2n_vblank_set_callback(void *callback);
unsigned __cdecl h2n_present(unsigned interval, const unsigned *frame_counters, unsigned counter_count);
void *__cdecl h2n_calloc(unsigned size);
void __cdecl h2n_free(void *address);
int __cdecl h2n_env_int(const char *name, int otherwise);
int __cdecl h2n_readable(const void *address, unsigned size);
void __cdecl gx_memory_changed(const unsigned char *memory);
void __cdecl gx_frame_counts(unsigned *draws, unsigned *skipped, unsigned *uploads);
int __cdecl gx_closed(void);
void __cdecl h2n_window_closed(void);
}
static int g_trace_left = -1;
static bool trace_on() { if (g_trace_left < 0) g_trace_left = h2n_env_int("H2N_D3DTRACE", 0); return g_trace_left > 0; }
#define TRACE(args) do { if (trace_on()) { g_trace_left--; h2n_log args; } } while (0)


/* ---- what the SDK's inline functions write to directly (D3D8.h) ---- */
extern "C" {
DWORD D3D__RenderState[D3DRS_MAX];
DWORD D3D__TextureState[D3DTSS_MAXSTAGES][D3DTSS_MAX];
DWORD D3D__DirtyFlags;
static DWORD g_push_buffer[0x40000];          /* 1 MB the game's raw GPU commands go into; not read yet */
static DWORD g_device_block[0x4000];          /* what D3D__pDevice points to: +0 put, +4 threshold */
IDirect3DDevice8 *D3D__pDevice = (IDirect3DDevice8 *)g_device_block;
}

enum { C_DRAW, C_DRAW_INDEXED, C_BEGIN, C_VERTICES, C_CLEAR, C_STATE, C_TEXTURE, C_SHADER, C_CONSTANT, C_TARGET, C_PUSH, C_LOCK, C_DRAWN, C_SKIPPED, C_UPLOAD, C_COUNT };
static unsigned g_counters[C_COUNT];
static const char *const g_counter_names = "draws,indexed draws,immediate begins,vertices,clears,state sets,texture sets,shader sets,constant sets,target sets,raw pushes,locks,drawn in the window,not drawn,textures unpacked";

static void push_reset()
{
	g_device_block[0] = (DWORD)g_push_buffer;
	g_device_block[1] = (DWORD)(g_push_buffer + 0x40000 - 0x4000);
}

/* ---- formats and layouts ---- */
static DWORD format_bits(DWORD f)
{
	switch (f)
	{
	case D3DFMT_A8R8G8B8: case D3DFMT_X8R8G8B8: case D3DFMT_A8B8G8R8: case D3DFMT_B8G8R8A8: case D3DFMT_R8G8B8A8:
	case D3DFMT_LIN_A8R8G8B8: case D3DFMT_LIN_X8R8G8B8: case D3DFMT_LIN_A8B8G8R8: case D3DFMT_LIN_B8G8R8A8: case D3DFMT_LIN_R8G8B8A8:
	case D3DFMT_D24S8: case D3DFMT_F24S8: case D3DFMT_LIN_D24S8: case D3DFMT_LIN_F24S8: case D3DFMT_V16U16: case D3DFMT_LIN_V16U16:
		return 32;
	case D3DFMT_A8: case D3DFMT_P8: case D3DFMT_L8: case D3DFMT_AL8: case D3DFMT_LIN_A8: case D3DFMT_LIN_AL8: case D3DFMT_LIN_L8:
		return 8;
	case D3DFMT_DXT1: return 4;
	case D3DFMT_DXT2: case D3DFMT_DXT4: return 8;
	}
	return 16;
}
static bool format_compressed(DWORD f) { return f == D3DFMT_DXT1 || f == D3DFMT_DXT2 || f == D3DFMT_DXT4; }
static bool format_linear(DWORD f)
{
	switch (f)
	{
	case D3DFMT_LIN_A1R5G5B5: case D3DFMT_LIN_A4R4G4B4: case D3DFMT_LIN_A8: case D3DFMT_LIN_A8B8G8R8: case D3DFMT_LIN_A8R8G8B8:
	case D3DFMT_LIN_B8G8R8A8: case D3DFMT_LIN_G8B8: case D3DFMT_LIN_R4G4B4A4: case D3DFMT_LIN_R5G5B5A1: case D3DFMT_LIN_R5G6B5:
	case D3DFMT_LIN_R6G5B5: case D3DFMT_LIN_R8B8: case D3DFMT_LIN_R8G8B8A8: case D3DFMT_LIN_X1R5G5B5: case D3DFMT_LIN_X8R8G8B8:
	case D3DFMT_LIN_A8L8: case D3DFMT_LIN_AL8: case D3DFMT_LIN_L16: case D3DFMT_LIN_L8: case D3DFMT_LIN_V16U16:
	case D3DFMT_LIN_D24S8: case D3DFMT_LIN_F24S8: case D3DFMT_LIN_D16: case D3DFMT_LIN_F16: case D3DFMT_YUY2: case D3DFMT_UYVY:
		return true;
	}
	return false;
}
static DWORD log2_of(DWORD v) { DWORD n = 0; while ((1u << n) < v) n++; return n; }

struct layout { DWORD format, width, height, depth, levels, pitch; bool cube; };
static void container_layout(const D3DPixelContainer *p, layout *out)
{
	out->format = (p->Format & D3DFORMAT_FORMAT_MASK) >> D3DFORMAT_FORMAT_SHIFT;
	out->cube = (p->Format & D3DFORMAT_CUBEMAP) != 0;
	if (p->Size)
	{
		out->width = (p->Size & D3DSIZE_WIDTH_MASK) + 1;
		out->height = ((p->Size & D3DSIZE_HEIGHT_MASK) >> D3DSIZE_HEIGHT_SHIFT) + 1;
		out->pitch = (((p->Size & D3DSIZE_PITCH_MASK) >> D3DSIZE_PITCH_SHIFT) + 1) * D3DTEXTURE_PITCH_ALIGNMENT;
		out->depth = 1; out->levels = 1;
	}
	else
	{
		out->width = 1u << ((p->Format & D3DFORMAT_USIZE_MASK) >> D3DFORMAT_USIZE_SHIFT);
		out->height = 1u << ((p->Format & D3DFORMAT_VSIZE_MASK) >> D3DFORMAT_VSIZE_SHIFT);
		out->depth = 1u << ((p->Format & D3DFORMAT_PSIZE_MASK) >> D3DFORMAT_PSIZE_SHIFT);
		out->levels = (p->Format & D3DFORMAT_MIPMAP_MASK) >> D3DFORMAT_MIPMAP_SHIFT;
		if (!out->levels) out->levels = 1;
		out->pitch = format_compressed(out->format) ? ((out->width + 3) / 4) * (out->format == D3DFMT_DXT1 ? 8 : 16)
			: out->width * format_bits(out->format) / 8;
	}
}
static DWORD level_bytes(const layout *l, DWORD level, DWORD *width, DWORD *height, DWORD *depth, DWORD *pitch)
{
	DWORD w = l->width >> level, h = l->height >> level, d = l->depth >> level, row;
	if (!w) w = 1; if (!h) h = 1; if (!d) d = 1;
	if (format_compressed(l->format)) { row = ((w + 3) / 4) * (l->format == D3DFMT_DXT1 ? 8 : 16); h = (h + 3) / 4; if (width) *width = w; }
	else { row = level == 0 ? l->pitch : w * format_bits(l->format) / 8; if (width) *width = w; }
	if (height) *height = l->height >> level ? l->height >> level : 1;
	if (depth) *depth = d;
	if (pitch) *pitch = row;
	return row * h * d;
}
static DWORD chain_bytes(const layout *l)
{
	DWORD total = 0;
	for (DWORD i = 0; i < l->levels; i++) total += level_bytes(l, i, NULL, NULL, NULL, NULL);
	return total;
}
static BYTE *resource_memory(const D3DResource *r) { return (BYTE *)(r->Data | 0x80000000); }
static BYTE *level_memory(const D3DPixelContainer *p, const layout *l, DWORD face, DWORD level)
{
	BYTE *memory = resource_memory(p);
	if (face) memory += face * ((chain_bytes(l) + D3DTEXTURE_CUBEFACE_ALIGNMENT - 1) & ~(D3DTEXTURE_CUBEFACE_ALIGNMENT - 1));
	for (DWORD i = 0; i < level; i++) memory += level_bytes(l, i, NULL, NULL, NULL, NULL);
	return memory;
}

/* ---- resources Direct3D creates itself: a small record of ours sits in front of the SDK's structure ---- */
struct owned { DWORD magic, usage, bytes; void *memory; D3DBaseTexture *parent; DWORD pad[3]; D3DSurface resource; };
#define OWNED_MAGIC 0x6832644e
static owned *owned_of(D3DResource *r)
{
	owned *o = (owned *)((BYTE *)r - offsetof(owned, resource));
	if (!(r->Common & D3DCOMMON_D3DCREATED)) return NULL;
	/* The game copies headers made here into memory of its own, flag included: render target 3 is the main colour
	   surface's header over the depth buffer's memory, in a 24-byte VirtualAlloc of its own (0x1cd30 via 0x14bc0, used by
	   the loading screen, 0x289e0). The bytes before such a copy are not a record of ours, and are unmapped when the copy
	   starts a page; a record of ours always has them. */
	if (((DWORD)r & 0xfff) < offsetof(owned, resource) && !h2n_readable(o, offsetof(owned, resource))) return NULL;
	return o->magic == OWNED_MAGIC ? o : NULL;
}
static D3DSurface *new_container(DWORD common_type, DWORD format_field, DWORD size_field, DWORD usage, DWORD bytes)
{
	owned *o = (owned *)h2n_calloc(sizeof(owned));
	o->magic = OWNED_MAGIC; o->usage = usage; o->bytes = bytes;
	o->memory = bytes ? h2n_contig_alloc(bytes) : NULL;
	o->resource.Common = 1 | common_type | D3DCOMMON_D3DCREATED;
	o->resource.Data = (DWORD)o->memory & 0x7fffffff;
	o->resource.Format = format_field;
	o->resource.Size = size_field;
	return &o->resource;
}
static DWORD linear_size_field(DWORD width, DWORD height, DWORD pitch)
{
	return (width - 1) | ((height - 1) << D3DSIZE_HEIGHT_SHIFT) | ((pitch / D3DTEXTURE_PITCH_ALIGNMENT - 1) << D3DSIZE_PITCH_SHIFT);
}
static D3DSurface *new_surface(DWORD width, DWORD height, DWORD format, DWORD usage)
{
	DWORD pitch = (width * format_bits(format) / 8 + D3DTEXTURE_PITCH_ALIGNMENT - 1) & ~(D3DTEXTURE_PITCH_ALIGNMENT - 1);
	return new_container(D3DCOMMON_TYPE_SURFACE, D3DFORMAT_DMACHANNEL_A | (2 << D3DFORMAT_DIMENSION_SHIFT) | (format << D3DFORMAT_FORMAT_SHIFT)
		| (1 << D3DFORMAT_MIPMAP_SHIFT), linear_size_field(width, height, pitch), usage, pitch * height);
}

/* ---- the device ---- */
static struct
{
	bool created;
	DWORD width, height, interval;
	D3DSurface *back[3], *depth, *target, *target_depth;
	DWORD back_count, back_index;
	D3DBaseTexture *textures[D3DTSS_MAXSTAGES];
	D3DPalette *palettes[D3DTSS_MAXSTAGES];
	DWORD vertex_shader, pixel_shader;
	D3DPIXELSHADERDEF pixel_program; bool has_pixel_program;
	float constants[192][4];
	D3DVIEWPORT8 viewport;
	DWORD simple_states[0x800];
	DWORD swaps;
	DWORD program[136][4], program_start; bool program_selected;
	D3DVERTEXATTRIBUTEFORMAT format; bool has_format;
	D3DSTREAM_INPUT streams[16]; DWORD stream_count, base_vertex;
	float current[16][4];
	bool in_begin; DWORD begin_type, begin_count, begin_capacity; float *begin_vertices;
	D3DRECT scissor; bool scissor_on;
	float clip_min, clip_max; bool clip_planes;       /* the depth range the game gave with SetDepthClipPlanes, in the depth buffer's units */
	short method_state[0x800];
} g_d3d;

/* ---- handing a draw to the picture ---- */
static gx_state g_state;
static void describe_texture(D3DBaseTexture *texture, D3DPalette *palette, gx_texture *out)
{
	layout l;
	memset(out, 0, sizeof(*out));
	if (!texture) return;
	container_layout(texture, &l);
	out->memory = resource_memory(texture);
	out->format = l.format; out->width = l.width; out->height = l.height; out->depth = l.depth; out->levels = l.levels > 12 ? 12 : l.levels; out->pitch = l.pitch;
	out->cube = l.cube; out->linear = texture->Size != 0 || format_linear(l.format);
	out->volume = ((texture->Format & D3DFORMAT_DIMENSION_MASK) >> D3DFORMAT_DIMENSION_SHIFT) == 3 && l.depth > 1;
	out->face_bytes = (chain_bytes(&l) + D3DTEXTURE_CUBEFACE_ALIGNMENT - 1) & ~(D3DTEXTURE_CUBEFACE_ALIGNMENT - 1);
	DWORD offset = 0;
	for (DWORD i = 0; i < out->levels; i++) { out->level_offset[i] = offset; out->level_bytes[i] = level_bytes(&l, i, NULL, NULL, NULL, NULL); offset += out->level_bytes[i]; }
	out->palette = l.format == D3DFMT_P8 && palette ? (const unsigned *)resource_memory(palette) : NULL;
}
static void describe_surface(D3DSurface *surface, gx_surface *out)
{
	layout l;
	memset(out, 0, sizeof(*out));
	if (!surface) return;
	container_layout(surface, &l);
	out->memory = resource_memory(surface); out->width = l.width; out->height = l.height; out->format = l.format;
}
static float depth_max()
{
	/* the game's vertex programs end in screen coordinates and scale their depth to the depth buffer's own range themselves
	 * (their constants 10 and 11). Nothing is written into the game's constants for it: the SDK's Direct3D keeps a viewport
	 * scale and offset in constants 58 and 59 only in its 96-constant mode, and this game uses those two for bone matrices. */
	DWORD depth_format = D3DFMT_LIN_D24S8;
	if (g_d3d.target_depth) { layout l; container_layout(g_d3d.target_depth, &l); depth_format = l.format; }
	return depth_format == D3DFMT_D16 || depth_format == D3DFMT_LIN_D16 ? 65535.0f : 16777215.0f;
}
static const gx_state *current_state()
{
	gx_state *s = &g_state;
	const DWORD *r = D3D__RenderState;
	s->program = (const unsigned (*)[4])g_d3d.program; s->program_start = g_d3d.program_start; s->program_selected = g_d3d.program_selected; s->fvf = g_d3d.vertex_shader;
	s->constants = (const float (*)[4])g_d3d.constants;
	s->depth_max = depth_max();
	s->depth_clip = r[D3DRS_DEPTHCLIPCONTROL];
	if (g_d3d.clip_planes) { s->clip_min = g_d3d.clip_min; s->clip_max = g_d3d.clip_max; }
	else { s->clip_min = 0.0f; s->clip_max = s->depth_max; }
	memcpy(s->current, g_d3d.current, sizeof(s->current));
	for (int i = 0; i < 8; i++)
	{
		s->alpha_inputs[i] = r[D3DRS_PSALPHAINPUTS0 + i]; s->rgb_inputs[i] = r[D3DRS_PSRGBINPUTS0 + i];
		s->alpha_outputs[i] = r[D3DRS_PSALPHAOUTPUTS0 + i]; s->rgb_outputs[i] = r[D3DRS_PSRGBOUTPUTS0 + i];
		s->constant0[i] = r[D3DRS_PSCONSTANT0_0 + i]; s->constant1[i] = r[D3DRS_PSCONSTANT1_0 + i];
	}
	s->final_abcd = r[D3DRS_PSFINALCOMBINERINPUTSABCD]; s->final_efg = r[D3DRS_PSFINALCOMBINERINPUTSEFG];
	s->final_constant0 = r[D3DRS_PSFINALCOMBINERCONSTANT0]; s->final_constant1 = r[D3DRS_PSFINALCOMBINERCONSTANT1];
	s->combiner_count = r[D3DRS_PSCOMBINERCOUNT]; s->texture_modes = r[D3DRS_PSTEXTUREMODES]; s->dot_mapping = r[D3DRS_PSDOTMAPPING];
	s->input_texture = r[D3DRS_PSINPUTTEXTURE]; s->compare_mode = r[D3DRS_PSCOMPAREMODE];
	s->combiners = g_d3d.has_pixel_program || g_d3d.pixel_shader;
	for (int i = 0; i < 4; i++)
	{
		const DWORD *t = D3D__TextureState[i];
		gx_sampler *m = &s->sampler[i];
		describe_texture(g_d3d.textures[i], g_d3d.palettes[i], &s->texture[i]);
		m->address_u = t[D3DTSS_ADDRESSU]; m->address_v = t[D3DTSS_ADDRESSV]; m->address_w = t[D3DTSS_ADDRESSW];
		m->mag = t[D3DTSS_MAGFILTER]; m->min = t[D3DTSS_MINFILTER]; m->mip = t[D3DTSS_MIPFILTER];
		m->border_color = t[D3DTSS_BORDERCOLOR]; m->color_key_op = t[D3DTSS_COLORKEYOP]; m->color_sign = t[D3DTSS_COLORSIGN]; m->alpha_kill = t[D3DTSS_ALPHAKILL];
		m->max_mip = t[D3DTSS_MAXMIPLEVEL]; m->max_anisotropy = t[D3DTSS_MAXANISOTROPY];
		memcpy(&m->lod_bias, &t[D3DTSS_MIPMAPLODBIAS], 4);
		memcpy(&m->bump[0], &t[D3DTSS_BUMPENVMAT00], 4); memcpy(&m->bump[1], &t[D3DTSS_BUMPENVMAT01], 4);
		memcpy(&m->bump[2], &t[D3DTSS_BUMPENVMAT10], 4); memcpy(&m->bump[3], &t[D3DTSS_BUMPENVMAT11], 4);
		memcpy(&m->luminance_scale, &t[D3DTSS_BUMPENVLSCALE], 4); memcpy(&m->luminance_offset, &t[D3DTSS_BUMPENVLOFFSET], 4);
	}
	s->alpha_test = r[D3DRS_ALPHATESTENABLE]; s->alpha_func = r[D3DRS_ALPHAFUNC]; s->alpha_ref = r[D3DRS_ALPHAREF];
	s->blend = r[D3DRS_ALPHABLENDENABLE]; s->blend_source = r[D3DRS_SRCBLEND]; s->blend_destination = r[D3DRS_DESTBLEND]; s->blend_op = r[D3DRS_BLENDOP];
	s->blend_color = r[D3DRS_BLENDCOLOR]; s->color_mask = r[D3DRS_COLORWRITEENABLE];
	s->z_enable = r[D3DRS_ZENABLE]; s->z_write = r[D3DRS_ZWRITEENABLE]; s->z_func = r[D3DRS_ZFUNC]; s->z_bias = r[D3DRS_ZBIAS];
	s->shadow_func = r[D3DRS_SHADOWFUNC];                 /* (s10k) a light's shadow map: the compare of a stage that reads a depth buffer */
	s->offset_on = r[r[D3DRS_FILLMODE] == D3DFILL_POINT ? D3DRS_POINTOFFSETENABLE : r[D3DRS_FILLMODE] == D3DFILL_WIREFRAME ? D3DRS_WIREFRAMEOFFSETENABLE : D3DRS_SOLIDOFFSETENABLE];
	memcpy(&s->offset_slope, &r[D3DRS_POLYGONOFFSETZSLOPESCALE], 4); memcpy(&s->offset_units, &r[D3DRS_POLYGONOFFSETZOFFSET], 4);
	s->stencil = r[D3DRS_STENCILENABLE]; s->stencil_func = r[D3DRS_STENCILFUNC]; s->stencil_ref = r[D3DRS_STENCILREF]; s->stencil_mask = r[D3DRS_STENCILMASK];
	s->stencil_write_mask = r[D3DRS_STENCILWRITEMASK]; s->stencil_fail = r[D3DRS_STENCILFAIL]; s->stencil_zfail = r[D3DRS_STENCILZFAIL]; s->stencil_pass = r[D3DRS_STENCILPASS];
	s->cull = r[D3DRS_CULLMODE]; s->front_face = r[D3DRS_FRONTFACE]; s->fill = r[D3DRS_FILLMODE]; s->shade = r[D3DRS_SHADEMODE];
	s->fog = r[D3DRS_FOGENABLE]; s->fog_color = r[D3DRS_FOGCOLOR]; s->fog_table_mode = r[D3DRS_FOGTABLEMODE]; s->dither = r[D3DRS_DITHERENABLE];
	memcpy(&s->fog_start, &r[D3DRS_FOGSTART], 4); memcpy(&s->fog_end, &r[D3DRS_FOGEND], 4); memcpy(&s->fog_density, &r[D3DRS_FOGDENSITY], 4);
	s->viewport_x = g_d3d.viewport.X; s->viewport_y = g_d3d.viewport.Y; s->viewport_width = g_d3d.viewport.Width; s->viewport_height = g_d3d.viewport.Height;
	s->viewport_min_z = g_d3d.viewport.MinZ; s->viewport_max_z = g_d3d.viewport.MaxZ;
	s->scissor_on = g_d3d.scissor_on; s->scissor[0] = g_d3d.scissor.x1; s->scissor[1] = g_d3d.scissor.y1; s->scissor[2] = g_d3d.scissor.x2; s->scissor[3] = g_d3d.scissor.y2;
	describe_surface(g_d3d.target, &s->target); describe_surface(g_d3d.target_depth, &s->depth);
	return s;
}
static void stream_attributes()
{
	gx_state *s = &g_state;
	for (int i = 0; i < 16; i++)
	{
		const D3DVERTEXSHADERINPUT *in = &g_d3d.format.Input[i];
		s->attribute[i].data = NULL; s->attribute[i].stride = 0; s->attribute[i].type = 0;
		if (!g_d3d.has_format || in->Format == D3DVSDT_NONE || in->StreamIndex >= g_d3d.stream_count || !g_d3d.streams[in->StreamIndex].VertexBuffer) continue;
		const D3DSTREAM_INPUT *stream = &g_d3d.streams[in->StreamIndex];
		s->attribute[i].data = resource_memory(stream->VertexBuffer) + stream->Offset + in->Offset + g_d3d.base_vertex * stream->Stride;
		s->attribute[i].stride = stream->Stride; s->attribute[i].type = in->Format;
	}
}
static void set_current(INT reg, float a, float b, float c, float d)
{
	if (reg == D3DVSDE_VERTEX) reg = 0;
	if (reg < 0 || reg > 15) return;
	float *v = g_d3d.current[reg];
	v[0] = a; v[1] = b; v[2] = c; v[3] = d;
	if (reg != 0 || !g_d3d.in_begin) return;
	if (g_d3d.begin_count == g_d3d.begin_capacity)
	{
		DWORD capacity = g_d3d.begin_capacity ? g_d3d.begin_capacity * 2 : 1024;
		float *bigger = (float *)h2n_calloc(capacity * 64 * sizeof(float));
		if (g_d3d.begin_vertices) { memcpy(bigger, g_d3d.begin_vertices, g_d3d.begin_count * 64 * sizeof(float)); h2n_free(g_d3d.begin_vertices); }
		g_d3d.begin_vertices = bigger; g_d3d.begin_capacity = capacity;
	}
	memcpy(g_d3d.begin_vertices + g_d3d.begin_count * 64, g_d3d.current, 64 * sizeof(float));
	g_d3d.begin_count++;
	g_counters[C_VERTICES]++;
}
static void set_defaults()
{
	DWORD *r = D3D__RenderState;
	r[D3DRS_ZENABLE] = TRUE; r[D3DRS_ZWRITEENABLE] = TRUE; r[D3DRS_ZFUNC] = D3DCMP_LESSEQUAL; r[D3DRS_ALPHAFUNC] = D3DCMP_ALWAYS;
	r[D3DRS_SRCBLEND] = D3DBLEND_ONE; r[D3DRS_DESTBLEND] = D3DBLEND_ZERO; r[D3DRS_BLENDOP] = D3DBLENDOP_ADD; r[D3DRS_COLORWRITEENABLE] = D3DCOLORWRITEENABLE_ALL;
	r[D3DRS_CULLMODE] = D3DCULL_CCW; r[D3DRS_FRONTFACE] = D3DFRONT_CW; r[D3DRS_FILLMODE] = D3DFILL_SOLID; r[D3DRS_BACKFILLMODE] = D3DFILL_SOLID; r[D3DRS_SHADEMODE] = D3DSHADE_GOURAUD;
	r[D3DRS_STENCILFUNC] = D3DCMP_ALWAYS; r[D3DRS_STENCILFAIL] = D3DSTENCILOP_KEEP; r[D3DRS_STENCILZFAIL] = D3DSTENCILOP_KEEP; r[D3DRS_STENCILPASS] = D3DSTENCILOP_KEEP;
	r[D3DRS_STENCILMASK] = 0xffffffff; r[D3DRS_STENCILWRITEMASK] = 0xffffffff; r[D3DRS_DITHERENABLE] = TRUE;
	for (int i = 0; i < D3DTSS_MAXSTAGES; i++)
	{
		DWORD *t = D3D__TextureState[i];
		t[D3DTSS_ADDRESSU] = D3DTADDRESS_WRAP; t[D3DTSS_ADDRESSV] = D3DTADDRESS_WRAP; t[D3DTSS_ADDRESSW] = D3DTADDRESS_WRAP;
		t[D3DTSS_MAGFILTER] = D3DTEXF_POINT; t[D3DTSS_MINFILTER] = D3DTEXF_POINT; t[D3DTSS_MIPFILTER] = D3DTEXF_NONE; t[D3DTSS_MAXANISOTROPY] = 1;
		t[D3DTSS_TEXCOORDINDEX] = i;
	}
	for (int i = 0; i < 16; i++) { g_d3d.current[i][0] = 0.0f; g_d3d.current[i][1] = 0.0f; g_d3d.current[i][2] = 0.0f; g_d3d.current[i][3] = 1.0f; }
	for (int i = 0; i < 0x800; i++) g_d3d.method_state[i] = -1;
	for (int i = 0; i < D3DRS_SIMPLE_MAX; i++) g_d3d.method_state[(D3DSIMPLERENDERSTATEENCODE[i] >> 2) & 0x7ff] = (short)i;
}

/* ---- GPU commands the game writes itself: the SDK's inline functions and a few routines of the game's own put
 * them straight into the push buffer. They are read here, in order with the calls around them. ---- */
static DWORD *g_push_parsed = g_push_buffer;
static bool g_flushing;
static DWORD *g_raw; static DWORD g_raw_count, g_raw_capacity;           /* vertex data given inline */
static WORD *g_elements; static DWORD g_element_count, g_element_capacity;   /* indices given inline */
static DWORD g_constant_load, g_program_load, g_program_word;
static void begin_primitive(DWORD type);
static void end_primitive();
static void flush_push();
static inline void sync() { if (!g_flushing && (DWORD *)g_device_block[0] != g_push_parsed) flush_push(); }
static DWORD attribute_bytes(DWORD format)
{
	DWORD count = format >> 4, kind = format & 15;
	switch (kind)
	{
	case 2: return (count == 7 ? 3 : count) * 4;
	case 1: case 5: return (count * 2 + 3) & ~3u;
	case 0: case 4: case 6: return 4;
	}
	return 0;
}
static void push_method(DWORD method, DWORD value)
{
	static DWORD seen[40]; static DWORD seen_count;
	float f; memcpy(&f, &value, 4);
	if (method == 0x17fc) { if (value) begin_primitive(value); else end_primitive(); return; }
	if (method == 0x1818)
	{
		if (g_raw_count == g_raw_capacity)
		{
			DWORD capacity = g_raw_capacity ? g_raw_capacity * 2 : 16384;
			DWORD *bigger = (DWORD *)h2n_calloc(capacity * 4);
			if (g_raw) { memcpy(bigger, g_raw, g_raw_count * 4); h2n_free(g_raw); }
			g_raw = bigger; g_raw_capacity = capacity;
		}
		g_raw[g_raw_count++] = value;
		return;
	}
	if (method == 0x1800 || method == 0x1808)
	{
		if (g_element_count + 2 > g_element_capacity)
		{
			DWORD capacity = g_element_capacity ? g_element_capacity * 2 : 16384;
			WORD *bigger = (WORD *)h2n_calloc(capacity * 2);
			if (g_elements) { memcpy(bigger, g_elements, g_element_count * 2); h2n_free(g_elements); }
			g_elements = bigger; g_element_capacity = capacity;
		}
		g_elements[g_element_count++] = (WORD)value;
		if (method == 0x1800) g_elements[g_element_count++] = (WORD)(value >> 16);
		return;
	}
	if (method == 0x1810)
	{
		const gx_state *s = current_state(); stream_attributes();
		gx_draw(s, g_d3d.begin_type, (value >> 24) + 1, NULL, value & 0xffffff);
		return;
	}
	if (method == 0x1ea4) { g_constant_load = value; return; }
	if (method >= 0xb80 && method < 0xc00)
	{
		DWORD word = (method - 0xb80) / 4, slot = g_constant_load + word / 4;
		if (slot < 192) g_d3d.constants[slot][word & 3] = f;
		if ((word & 3) == 3 && method == 0xbfc) g_constant_load += 8;
		return;
	}
	if (method == 0x1e9c) { g_program_load = value; g_program_word = 0; return; }
	if (method >= 0xb00 && method < 0xb80)
	{
		if (g_program_load < 136) g_d3d.program[g_program_load][g_program_word] = value;
		if (++g_program_word == 4) { g_program_word = 0; g_program_load++; }
		return;
	}
	if (method == 0x1ea0) { g_d3d.program_start = value < 136 ? value : 0; return; }
	if (method == 0x1e94) { g_d3d.program_selected = (value & 3) == 2; return; }
	if (method >= 0x1a00 && method < 0x1b00)
	{
		DWORD reg = (method - 0x1a00) / 16, part = ((method - 0x1a00) / 4) & 3;
		g_d3d.current[reg][part] = f;
		if (part == 3) set_current((INT)reg, g_d3d.current[reg][0], g_d3d.current[reg][1], g_d3d.current[reg][2], f);
		return;
	}
	if (method >= 0x1880 && method < 0x1900)
	{
		DWORD reg = (method - 0x1880) / 8, part = ((method - 0x1880) / 4) & 1;
		g_d3d.current[reg][part] = f;
		if (part == 1) set_current((INT)reg, g_d3d.current[reg][0], f, 0.0f, 1.0f);
		return;
	}
	if (method >= 0x1940 && method < 0x1980)
	{
		set_current((INT)((method - 0x1940) / 4), (value & 255) / 255.0f, ((value >> 8) & 255) / 255.0f, ((value >> 16) & 255) / 255.0f, (value >> 24) / 255.0f);
		return;
	}
	if (method >= 0x1900 && method < 0x1940)
	{
		set_current((INT)((method - 0x1900) / 4), (float)(SHORT)value, (float)(SHORT)(value >> 16), 0.0f, 1.0f);
		return;
	}
	if (method >= 0x1518 && method < 0x1528)
	{
		DWORD part = (method - 0x1518) / 4;
		g_d3d.current[0][part] = f;
		if (part == 3) set_current(0, g_d3d.current[0][0], g_d3d.current[0][1], g_d3d.current[0][2], f);
		return;
	}
	if (method == 0x17cc) { if (value) gx_visibility_begin(); return; }      /* the visibility test: start counting the pixels that pass the depth test */
	if (method == 0x17c8 || method == 0x100 || method == 0x1d6c || method == 0x1d70 || method == 0x1d7c) return;   /* clearing that count, no-operation, back-end markers */
	{
		short state = g_d3d.method_state[(method >> 2) & 0x7ff];
		if (state >= 0) { D3D__RenderState[state] = value; return; }
	}
	for (DWORD i = 0; i < seen_count; i++) if (seen[i] == method) return;
	if (seen_count < 40)
	{
		seen[seen_count++] = method;
		h2n_log("       the game wrote a GPU command this build does not act on yet: method %#lx (value %#lx)\n", method, value);
	}
}
static void flush_push()
{
	DWORD *end = (DWORD *)g_device_block[0], *p = g_push_parsed;
	if (g_flushing) return;
	if (end < g_push_buffer || end > g_push_buffer + 0x40000 || p > end) { g_push_parsed = end; return; }
	g_flushing = true;
	gx_u64 t0 = h2n_gfx_tsc(), g0 = h2n_gfx_gx_total();          /* (s9r: the time spent reading them, for the log) */
	while (p < end)
	{
		DWORD command = *p++;
		DWORD method = command & 0x1ffc, count = (command >> 18) & 0x7ff;
		if (command & 0x20000003) { static bool told; if (!told) { told = true; h2n_log("       the game wrote a GPU jump (%#lx); the commands after it are not read\n", command); } break; }
		if (p + count > end) break;
		for (DWORD i = 0; i < count; i++) push_method(command & 0x40000000 ? method : method + 4 * i, p[i]);
		p += count;
	}
	{
		gx_u64 spent = h2n_gfx_tsc() - t0, inner = h2n_gfx_gx_total() - g0;
		h2n_gfx_push_time(spent > inner ? spent - inner : 0);
	}
	g_flushing = false;
	g_push_parsed = end;
}
static void push_restart()       /* everything written so far has been read: the game may start at the beginning again */
{
	flush_push();
	push_reset();
	g_push_parsed = g_push_buffer;
}
static void begin_primitive(DWORD type)
{
	g_counters[C_BEGIN]++;
	g_d3d.in_begin = true; g_d3d.begin_type = type; g_d3d.begin_count = 0;
	g_raw_count = 0; g_element_count = 0;
}
static void end_primitive()
{
	if (!g_d3d.in_begin) return;
	g_d3d.in_begin = false;
	if (g_raw_count)
	{
		/* vertices given inline: each holds the attributes of the current vertex format, in register order */
		DWORD size = 0, offset = 0;
		for (int i = 0; i < 16; i++) if (g_d3d.has_format && g_d3d.format.Input[i].Format != D3DVSDT_NONE) size += attribute_bytes(g_d3d.format.Input[i].Format);
		if (!size) { g_raw_count = 0; return; }
		const gx_state *s = current_state();
		for (int i = 0; i < 16; i++)
		{
			DWORD format = g_d3d.format.Input[i].Format;
			g_state.attribute[i].data = NULL; g_state.attribute[i].stride = 0; g_state.attribute[i].type = 0;
			if (format == D3DVSDT_NONE || !attribute_bytes(format)) continue;
			g_state.attribute[i].data = (const unsigned char *)g_raw + offset; g_state.attribute[i].stride = size; g_state.attribute[i].type = format;
			offset += attribute_bytes(format);
		}
		g_counters[C_VERTICES] += g_raw_count * 4 / size;
		gx_draw(s, g_d3d.begin_type, g_raw_count * 4 / size, NULL, 0);
		g_raw_count = 0;
	}
	if (g_element_count)
	{
		const gx_state *s = current_state(); stream_attributes();
		g_counters[C_VERTICES] += g_element_count;
		gx_draw(s, g_d3d.begin_type, g_element_count, g_elements, 0);
		g_element_count = 0;
	}
	if (g_d3d.begin_count)
	{
		const gx_state *s = current_state();
		for (int i = 0; i < 16; i++) { g_state.attribute[i].data = (const unsigned char *)(g_d3d.begin_vertices + i * 4); g_state.attribute[i].stride = 64 * sizeof(float); g_state.attribute[i].type = D3DVSDT_FLOAT4; }
		gx_draw(s, g_d3d.begin_type, g_d3d.begin_count, NULL, 0);
		g_d3d.begin_count = 0;
	}
}

static void create_buffers(D3DPRESENT_PARAMETERS *pp)
{
	DWORD depth_format = pp->AutoDepthStencilFormat == D3DFMT_F24S8 || pp->AutoDepthStencilFormat == D3DFMT_LIN_F24S8 ? D3DFMT_LIN_F24S8 : D3DFMT_LIN_D24S8;
	g_d3d.width = pp->BackBufferWidth ? pp->BackBufferWidth : 640;
	g_d3d.height = pp->BackBufferHeight ? pp->BackBufferHeight : 480;
	g_d3d.back_count = pp->BackBufferCount ? pp->BackBufferCount + 1 : 2;
	if (g_d3d.back_count > 3) g_d3d.back_count = 3;
	for (DWORD i = 0; i < g_d3d.back_count; i++)
		g_d3d.back[i] = new_surface(g_d3d.width, g_d3d.height, D3DFMT_LIN_A8R8G8B8, D3DUSAGE_RENDERTARGET);
	g_d3d.depth = pp->EnableAutoDepthStencil ? new_surface(g_d3d.width, g_d3d.height, depth_format, D3DUSAGE_DEPTHSTENCIL) : NULL;
	set_defaults();
	g_d3d.target = g_d3d.back[0]; g_d3d.target_depth = g_d3d.depth;
	g_d3d.viewport.X = 0; g_d3d.viewport.Y = 0; g_d3d.viewport.Width = g_d3d.width; g_d3d.viewport.Height = g_d3d.height;
	g_d3d.viewport.MinZ = 0.0f; g_d3d.viewport.MaxZ = 1.0f;
	g_d3d.interval = pp->FullScreen_PresentationInterval;
	h2n_log("[%4u] Direct3D device: %lux%lu, back buffer format %#lx, depth %#lx, %lu buffers, presentation interval %#lx, refresh %lu Hz, flags %#lx\n",
		h2n_next_sequence(), g_d3d.width, g_d3d.height, (DWORD)pp->BackBufferFormat, (DWORD)pp->AutoDepthStencilFormat, g_d3d.back_count,
		pp->FullScreen_PresentationInterval, pp->FullScreen_RefreshRateInHz, pp->Flags);
}

extern "C" {

HRESULT WINAPI Direct3D_CreateDevice(UINT Adapter, D3DDEVTYPE DeviceType, void *pUnused, DWORD BehaviorFlags,
	D3DPRESENT_PARAMETERS *pPresentationParameters, D3DDevice **ppReturnedDeviceInterface)
{
	push_reset();
	create_buffers(pPresentationParameters);
	g_d3d.created = true;
	gx_open(g_d3d.width, g_d3d.height);
	*ppReturnedDeviceInterface = D3D__pDevice;
	return S_OK;
}
void WINAPI Direct3D_SetPushBufferSize(DWORD PushBufferSize, DWORD KickOffSize) { }
HRESULT WINAPI D3DDevice_Reset(D3DPRESENT_PARAMETERS *pp)
{
	h2n_log("[%4u] D3DDevice_Reset\n", h2n_next_sequence());
	g_d3d.interval = pp->FullScreen_PresentationInterval;
	return S_OK;
}
ULONG WINAPI D3DDevice_Release() { return 0; }
HRESULT WINAPI D3DDevice_PersistDisplay() { return S_OK; }
BOOL WINAPI D3DDevice_IsBusy() { return FALSE; }
void WINAPI D3DDevice_KickPushBuffer() { }
void WINAPI D3DDevice_FlushVertexCache() { }
void WINAPI D3DDevice_SetFlickerFilter(DWORD Filter) { }
void WINAPI D3DDevice_SetSoftDisplayFilter(BOOL Enable) { }
void WINAPI D3DDevice_SetGammaRamp(DWORD Flags, CONST D3DGAMMARAMP *pRamp) { }
void WINAPI D3DDevice_SetBackBufferScale(float x, float y) { g_counters[C_STATE]++; }
/* The console's Direct3D keeps the two numbers as they are given and sends them to the graphics chip as the range of
 * depth values that are kept (its SET_CLIP_MIN and SET_CLIP_MAX, written by CommonSetViewport, retail 0x3f6190). The game
 * calls this from 0x13450 every frame with near/far * 16777215 and 16777215: it draws with the w-buffer (D3DRS_ZENABLE 2),
 * whose depth is the w its vertex programs put out, which the game scales so that the far plane is the largest depth. */
void WINAPI D3DDevice_SetDepthClipPlanes(float Near, float Far, DWORD Flags)
{
	sync();
	TRACE(("  d3d SetDepthClipPlanes %g %g flags %lu\n", Near, Far, Flags));
	if (Flags == D3DSDCP_SET_VERTEXPROGRAM_PLANES) { g_d3d.clip_min = Near; g_d3d.clip_max = Far; g_d3d.clip_planes = true; }
	else if (Flags == D3DSDCP_USE_DEFAULT_VERTEXPROGRAM_PLANES) g_d3d.clip_planes = false;
	g_counters[C_STATE]++;
}
void WINAPI D3DDevice_SetStipple(CONST DWORD *pPattern) { g_counters[C_STATE]++; }
void WINAPI D3DDevice_SetScissors(DWORD Count, BOOL Exclusive, CONST D3DRECT *pRects) { sync(); g_d3d.scissor_on = Count == 1 && pRects && !Exclusive; if (g_d3d.scissor_on) g_d3d.scissor = pRects[0]; g_counters[C_STATE]++; }
void WINAPI D3DDevice_SetVerticalBlankCallback(D3DVBLANKCALLBACK pCallback) { h2n_vblank_set_callback((void *)pCallback); }
void WINAPI D3DDevice_InsertCallback(D3DCALLBACKTYPE Type, D3DCALLBACK pCallback, DWORD Context) { if (pCallback) pCallback(Context); }

/* raw GPU commands the game writes itself */
DWORD *WINAPI D3DDevice_BeginPush(DWORD Count) { TRACE(("  d3d BeginPush %lu\n", Count)); g_counters[C_PUSH]++; push_restart(); return g_push_buffer; }
void WINAPI D3DDevice_EndPush(DWORD *pPush) {
	if (trace_on()) { DWORD n = (DWORD)(pPush - g_push_buffer); h2n_log("  d3d EndPush %lu words:", n); for (DWORD i = 0; i < n && i < 24; i++) h2n_log(" %08lx", g_push_buffer[i]); h2n_log("\n"); g_trace_left--; }
	if (pPush >= g_push_buffer && pPush <= g_push_buffer + 0x40000) g_device_block[0] = (DWORD)pPush;
	flush_push(); }
DWORD *WINAPI D3DDevice_MakeSpace() { TRACE(("  d3d MakeSpace (put %#lx)\n", g_device_block[0] - (DWORD)g_push_buffer)); push_restart(); return g_push_buffer; }

/* presenting */
extern "C" void __cdecl h2n_perf(int point);   /* timing.c (s9q) */
DWORD WINAPI D3DDevice_Swap(DWORD Flags)
{
	h2n_perf(7);                                   /* (s9q: the swap starts) */
	TRACE(("  d3d Swap (put %#lx)\n", g_device_block[0] - (DWORD)g_push_buffer));
	push_restart();
	g_d3d.swaps++;
	{
		gx_surface back;
		describe_surface(g_d3d.back[0], &back);
		gx_frame_counts(&g_counters[C_DRAWN], &g_counters[C_SKIPPED], &g_counters[C_UPLOAD]);
		g_counters[C_DRAWN] -= g_counters[C_SKIPPED];
		gx_present(&back);
		if (gx_closed()) h2n_window_closed();
		/* the console flips its buffers: the surface the game holds as "the back buffer" now stands for the other
		 * block of memory, and the one just shown becomes the front buffer. The game counts swaps to know which
		 * block is which (it keeps texture headers of its own over both). */
		if (g_d3d.back_count >= 2 && g_d3d.back[0] && g_d3d.back[1])
		{
			DWORD shown = g_d3d.back[0]->Data;
			for (DWORD i = 0; i + 1 < g_d3d.back_count; i++) g_d3d.back[i]->Data = g_d3d.back[i + 1]->Data;
			g_d3d.back[g_d3d.back_count - 1]->Data = shown;
		}
	}
	h2n_present(g_d3d.interval, g_counters, C_COUNT);
	memset(g_counters, 0, sizeof(g_counters));
	push_reset();
	return g_d3d.swaps;
}
void WINAPI D3DDevice_Clear(DWORD Count, CONST D3DRECT *pRects, DWORD Flags, D3DCOLOR Color, float Z, DWORD Stencil) { sync(); TRACE(("  d3d Clear count %lu flags %#lx color %08lx z %g\n", Count, Flags, Color, Z)); g_counters[C_CLEAR]++;
	if (Count && pRects) for (DWORD i = 0; i < Count; i++) gx_clear(current_state(), Flags, Color, Z, Stencil, (const int *)&pRects[i]);
	else gx_clear(current_state(), Flags, Color, Z, Stencil, NULL); }

/* drawing: counted, not drawn yet */
void WINAPI D3DDevice_DrawVertices(D3DPRIMITIVETYPE PrimitiveType, UINT StartVertex, UINT VertexCount) { sync(); TRACE(("  d3d DrawVertices type %d start %u count %u\n", PrimitiveType, StartVertex, VertexCount)); g_counters[C_DRAW]++; g_counters[C_VERTICES] += VertexCount;
	const gx_state *s = current_state(); stream_attributes(); gx_draw(s, PrimitiveType, VertexCount, NULL, StartVertex); }
void WINAPI D3DDevice_DrawIndexedVertices(D3DPRIMITIVETYPE PrimitiveType, UINT VertexCount, CONST WORD *pIndexData) { sync(); TRACE(("  d3d DrawIndexedVertices type %d count %u indices %p\n", PrimitiveType, VertexCount, pIndexData)); g_counters[C_DRAW_INDEXED]++; g_counters[C_VERTICES] += VertexCount;
	if (!pIndexData) return;
	const gx_state *s = current_state(); stream_attributes(); gx_draw(s, PrimitiveType, VertexCount, pIndexData, 0); }
void WINAPI D3DDevice_Begin(D3DPRIMITIVETYPE PrimitiveType) { TRACE(("  d3d Begin type %d (vs %#lx, ps %#lx, program %d, tex %p %p %p %p)\n", PrimitiveType, g_d3d.vertex_shader, g_d3d.pixel_shader, g_d3d.has_pixel_program, g_d3d.textures[0], g_d3d.textures[1], g_d3d.textures[2], g_d3d.textures[3])); sync(); begin_primitive((DWORD)PrimitiveType); }
void WINAPI D3DDevice_End() { TRACE(("  d3d End\n")); sync(); end_primitive(); }
void WINAPI D3DDevice_SetVertexData2f(INT Register, float a, float b) { sync(); TRACE(("  d3d   data2f %d: %g %g\n", Register, a, b)); set_current(Register, a, b, 0.0f, 1.0f); }
void WINAPI D3DDevice_SetVertexData2s(INT Register, SHORT a, SHORT b) { sync(); TRACE(("  d3d   data2s %d: %d %d\n", Register, a, b)); set_current(Register, (float)a, (float)b, 0.0f, 1.0f); }
void WINAPI D3DDevice_SetVertexData4f(INT Register, float a, float b, float c, float d) { sync(); TRACE(("  d3d   data4f %d: %g %g %g %g\n", Register, a, b, c, d)); set_current(Register, a, b, c, d); }
void WINAPI D3DDevice_SetVertexData4ub(INT Register, BYTE a, BYTE b, BYTE c, BYTE d) { sync(); TRACE(("  d3d   data4ub %d: %u %u %u %u\n", Register, a, b, c, d)); set_current(Register, a / 255.0f, b / 255.0f, c / 255.0f, d / 255.0f); }
void WINAPI D3DDevice_SetVertexDataColor(INT Register, D3DCOLOR Color) { sync(); TRACE(("  d3d   datacolor %d: %08lx\n", Register, Color));
	set_current(Register, ((Color >> 16) & 255) / 255.0f, ((Color >> 8) & 255) / 255.0f, (Color & 255) / 255.0f, (Color >> 24) / 255.0f); }
void WINAPI D3DDevice_SetIndices(D3DIndexBuffer *pIndexData, UINT BaseVertexIndex) { sync(); TRACE(("  d3d SetIndices %p base %u\n", pIndexData, BaseVertexIndex)); g_d3d.base_vertex = BaseVertexIndex; g_counters[C_STATE]++; }
/* The visibility test, as retail's Direct3D has it (0x3f85d0, 0x3f8630): the end of a test files the count of pixels that
 * passed the depth test under a number, stamped with the number of swaps so far; asking for it gives the count, or
 * D3DERR_TESTINCOMPLETE while the graphics chip has not reported yet. The game starts a test with GPU commands of its own
 * (push_method, 0x17cc). Lens flares use it: a flare is as bright as the share of its test square that was visible. */
static DWORD g_visibility_stamp[D3DVISIBILITY_TEST_MAX];
HRESULT WINAPI D3DDevice_EndVisibilityTest(DWORD Index)
{
	sync();
	TRACE(("  d3d EndVisibilityTest %lu\n", Index));
	if (Index < D3DVISIBILITY_TEST_MAX) g_visibility_stamp[Index] = g_d3d.swaps;
	gx_visibility_end(Index);
	return S_OK;
}
HRESULT WINAPI D3DDevice_GetVisibilityTestResult(DWORD Index, UINT *pResult, ULONGLONG *pTimeStamp)
{
	unsigned pixels = 0;
	int state;
	sync();
	state = gx_visibility_result(Index, &pixels);
	if (state == 0) return D3DERR_TESTINCOMPLETE;
	if (pResult) *pResult = state > 0 ? pixels : 0;
	if (pTimeStamp) *pTimeStamp = Index < D3DVISIBILITY_TEST_MAX ? g_visibility_stamp[Index] : 0;
	return S_OK;
}

/* shaders and their constants */
void WINAPI D3DDevice_SetVertexShader(DWORD Handle) { sync(); TRACE(("  d3d SetVertexShader %#lx\n", Handle)); g_d3d.vertex_shader = Handle; g_d3d.program_selected = false; g_counters[C_SHADER]++; }
void WINAPI D3DDevice_SelectVertexShaderDirect(D3DVERTEXATTRIBUTEFORMAT *pVAF, DWORD Address) { sync(); TRACE(("  d3d SelectVertexShaderDirect vaf %p address %lu\n", pVAF, Address));
	g_d3d.program_selected = true; g_d3d.program_start = Address < 136 ? Address : 0;
	if (pVAF) { g_d3d.format = *pVAF; g_d3d.has_format = true; }
	g_counters[C_SHADER]++; }
void WINAPI D3DDevice_SetVertexShaderInputDirect(D3DVERTEXATTRIBUTEFORMAT *pVAF, UINT StreamCount, CONST D3DSTREAM_INPUT *pStreamInputs) { sync();
	if (trace_on()) { h2n_log("  d3d SetVertexShaderInputDirect vaf %p streams %u:", pVAF, StreamCount);
		for (UINT i = 0; i < StreamCount; i++) h2n_log(" [vb %p data %#lx stride %lu offset %lu]", pStreamInputs[i].VertexBuffer, pStreamInputs[i].VertexBuffer ? pStreamInputs[i].VertexBuffer->Data : 0, pStreamInputs[i].Stride, pStreamInputs[i].Offset);
		if (pVAF) for (int i = 0; i < 16; i++) if (pVAF->Input[i].Format != D3DVSDT_NONE) h2n_log(" a%d(s%lu+%lu t%#lx)", i, pVAF->Input[i].StreamIndex, pVAF->Input[i].Offset, pVAF->Input[i].Format);
		h2n_log("\n"); g_trace_left--; }
	if (pVAF) { g_d3d.format = *pVAF; g_d3d.has_format = true; }
	g_d3d.stream_count = StreamCount > 16 ? 16 : StreamCount;
	for (UINT i = 0; i < g_d3d.stream_count; i++) g_d3d.streams[i] = pStreamInputs[i];
	g_counters[C_SHADER]++; }
void WINAPI D3DDevice_LoadVertexShaderProgram(CONST DWORD *pFunction, DWORD Address) { sync(); TRACE(("  d3d LoadVertexShaderProgram %p (header %08lx) at %lu\n", pFunction, pFunction ? pFunction[0] : 0, Address));
	if (pFunction)
	{
		DWORD count = pFunction[0] >> 16;
		if (Address < 136) { if (Address + count > 136) count = 136 - Address; memcpy(g_d3d.program[Address], pFunction + 1, count * 16); }
	}
	g_counters[C_SHADER]++; }
void WINAPI D3DDevice_SetPixelShader(DWORD Handle) { sync(); TRACE(("  d3d SetPixelShader %#lx\n", Handle)); g_d3d.pixel_shader = Handle; g_d3d.has_pixel_program = false; g_counters[C_SHADER]++; }
void WINAPI D3DDevice_SetPixelShaderProgram(CONST D3DPIXELSHADERDEF *pPSDef)
{ sync();
	TRACE(("  d3d SetPixelShaderProgram %p\n", pPSDef));
	g_d3d.has_pixel_program = pPSDef != NULL;
	if (pPSDef)
	{
		/* the definition is the first 57 render states in order, except that the texture modes have a render state of their own */
		g_d3d.pixel_program = *pPSDef;
		const DWORD *words = (const DWORD *)pPSDef;
		for (int i = 0; i <= D3DRS_PSINPUTTEXTURE; i++) if (i != 54) D3D__RenderState[i] = words[i];
		D3D__RenderState[D3DRS_PSTEXTUREMODES] = pPSDef->PSTextureModes;
	}
	g_counters[C_SHADER]++;
}
static void set_constants(INT Register, CONST void *data, DWORD count)
{
	sync();
	/* the SDK's inline callers have already added 96 to the register and pass the count in floats, not constants */
	INT first = Register;
	TRACE(("  d3d constants %d x%lu: %g %g %g %g\n", Register, count, ((const float *)data)[0], ((const float *)data)[1], ((const float *)data)[2], ((const float *)data)[3]));
	g_counters[C_CONSTANT]++;
	if (first < 0 || first * 4 + (INT)count > 192 * 4) return;
	memcpy(g_d3d.constants[first], data, count * 4);
}
void D3DFASTCALL D3DDevice_SetVertexShaderConstant1(INT Register, CONST void *pConstantData) { set_constants(Register, pConstantData, 4); }
void D3DFASTCALL D3DDevice_SetVertexShaderConstant1Fast(INT Register, CONST void *pConstantData) { set_constants(Register, pConstantData, 4); }
void D3DFASTCALL D3DDevice_SetVertexShaderConstant4(INT Register, CONST void *pConstantData) { set_constants(Register, pConstantData, 16); }
void D3DFASTCALL D3DDevice_SetVertexShaderConstantNotInline(INT Register, CONST void *pConstantData, DWORD ConstantCount) { set_constants(Register, pConstantData, ConstantCount); }
void D3DFASTCALL D3DDevice_SetVertexShaderConstantNotInlineFast(INT Register, CONST void *pConstantData, DWORD ConstantCount) { set_constants(Register, pConstantData, ConstantCount); }

/* render states */
void D3DFASTCALL D3DDevice_SetRenderState_Simple(DWORD Method, DWORD Value) { sync(); TRACE(("  d3d simple state %#lx = %#lx\n", Method, Value)); g_d3d.simple_states[(Method >> 2) & 0x7ff] = Value;
	{ short state = g_d3d.method_state[(Method >> 2) & 0x7ff]; if (state >= 0) D3D__RenderState[state] = Value; } g_counters[C_STATE]++; }
void WINAPI D3DDevice_SetRenderStateNotInline(D3DRENDERSTATETYPE State, DWORD Value) { sync(); if ((DWORD)State < D3DRS_MAX) D3D__RenderState[State] = Value; g_counters[C_STATE]++; }
#define RENDER_STATE(name, state) void WINAPI D3DDevice_SetRenderState_##name(DWORD Value) { sync(); D3D__RenderState[state] = Value; g_counters[C_STATE]++; }
RENDER_STATE(BackFillMode, D3DRS_BACKFILLMODE) RENDER_STATE(CullMode, D3DRS_CULLMODE) RENDER_STATE(DoNotCullUncompressed, D3DRS_DONOTCULLUNCOMPRESSED)
RENDER_STATE(Dxt1NoiseEnable, D3DRS_DXT1NOISEENABLE) RENDER_STATE(EdgeAntiAlias, D3DRS_EDGEANTIALIAS) RENDER_STATE(FillMode, D3DRS_FILLMODE)
RENDER_STATE(FogColor, D3DRS_FOGCOLOR) RENDER_STATE(FrontFace, D3DRS_FRONTFACE) RENDER_STATE(LineWidth, D3DRS_LINEWIDTH) RENDER_STATE(LogicOp, D3DRS_LOGICOP)
RENDER_STATE(MultiSampleAntiAlias, D3DRS_MULTISAMPLEANTIALIAS) RENDER_STATE(MultiSampleMask, D3DRS_MULTISAMPLEMASK) RENDER_STATE(MultiSampleMode, D3DRS_MULTISAMPLEMODE)
RENDER_STATE(MultiSampleRenderTargetMode, D3DRS_MULTISAMPLERENDERTARGETMODE) RENDER_STATE(NormalizeNormals, D3DRS_NORMALIZENORMALS)
RENDER_STATE(OcclusionCullEnable, D3DRS_OCCLUSIONCULLENABLE) RENDER_STATE(PSTextureModes, D3DRS_PSTEXTUREMODES) RENDER_STATE(RopZCmpAlwaysRead, D3DRS_ROPZCMPALWAYSREAD)
RENDER_STATE(RopZRead, D3DRS_ROPZREAD) RENDER_STATE(SampleAlpha, D3DRS_SAMPLEALPHA) RENDER_STATE(ShadowFunc, D3DRS_SHADOWFUNC)
RENDER_STATE(StencilCullEnable, D3DRS_STENCILCULLENABLE) RENDER_STATE(StencilEnable, D3DRS_STENCILENABLE) RENDER_STATE(StencilFail, D3DRS_STENCILFAIL)
RENDER_STATE(TextureFactor, D3DRS_TEXTUREFACTOR) RENDER_STATE(TwoSidedLighting, D3DRS_TWOSIDEDLIGHTING) RENDER_STATE(VertexBlend, D3DRS_VERTEXBLEND)
RENDER_STATE(YuvEnable, D3DRS_YUVENABLE) RENDER_STATE(ZEnable, D3DRS_ZENABLE)
/* the depth bias is a polygon offset on the console (retail 0x3f59b0): minus the bias in units of the depth buffer, a slope
   factor a quarter of that, and the offset switched on for points, lines and filled triangles alike while the bias is not zero */
void WINAPI D3DDevice_SetRenderState_ZBias(DWORD Value)
{
	float units = -(float)Value, slope = units * 0.25f;
	DWORD on = Value != 0;
	sync();
	memcpy(&D3D__RenderState[D3DRS_POLYGONOFFSETZSLOPESCALE], &slope, 4);
	memcpy(&D3D__RenderState[D3DRS_POLYGONOFFSETZOFFSET], &units, 4);
	D3D__RenderState[D3DRS_POINTOFFSETENABLE] = on; D3D__RenderState[D3DRS_WIREFRAMEOFFSETENABLE] = on; D3D__RenderState[D3DRS_SOLIDOFFSETENABLE] = on;
	D3D__RenderState[D3DRS_ZBIAS] = Value;
	g_counters[C_STATE]++;
}

/* textures and their stage states */
void WINAPI D3DDevice_SetTexture(DWORD Stage, D3DBaseTexture *pTexture)
{ sync();
	static unsigned logged;
	if (pTexture && !(pTexture->Common & D3DCOMMON_D3DCREATED) && logged++ < 12)
	{
		layout l; container_layout(pTexture, &l);
		h2n_log("       texture from the game's own data bound: %lux%lu, %lu levels, format %#lx, data %#lx (memory %p)\n", l.width, l.height, l.levels, l.format,
			pTexture->Data, resource_memory(pTexture));
	}
	TRACE(("  d3d SetTexture %lu %p\n", Stage, pTexture));
	if (Stage < D3DTSS_MAXSTAGES) g_d3d.textures[Stage] = pTexture;
	g_counters[C_TEXTURE]++;
}
void WINAPI D3DDevice_SetPalette(DWORD Stage, D3DPalette *pPalette) { sync(); if (Stage < D3DTSS_MAXSTAGES) g_d3d.palettes[Stage] = pPalette; g_counters[C_TEXTURE]++; }
void WINAPI D3DDevice_SetTextureState_BorderColor(DWORD Stage, DWORD Value) { D3D__TextureState[Stage][D3DTSS_BORDERCOLOR] = Value; g_counters[C_STATE]++; }
void WINAPI D3DDevice_SetTextureState_ColorKeyColor(DWORD Stage, DWORD Value) { D3D__TextureState[Stage][D3DTSS_COLORKEYCOLOR] = Value; g_counters[C_STATE]++; }
void WINAPI D3DDevice_SetTextureState_TexCoordIndex(DWORD Stage, DWORD Value) { D3D__TextureState[Stage][D3DTSS_TEXCOORDINDEX] = Value; g_counters[C_STATE]++; }
void WINAPI D3DDevice_SetTextureState_BumpEnv(DWORD Stage, D3DTEXTURESTAGESTATETYPE Type, DWORD Value) { D3D__TextureState[Stage][Type] = Value; g_counters[C_STATE]++; }

/* viewport and targets */
void WINAPI D3DDevice_SetViewport(CONST D3DVIEWPORT8 *pViewport) { sync(); if (pViewport) TRACE(("  d3d SetViewport %lu %lu %lu %lu %g %g\n", pViewport->X, pViewport->Y, pViewport->Width, pViewport->Height, pViewport->MinZ, pViewport->MaxZ)); if (pViewport) g_d3d.viewport = *pViewport; g_counters[C_STATE]++; }
void WINAPI D3DDevice_GetViewport(D3DVIEWPORT8 *pViewport) { *pViewport = g_d3d.viewport; }
static D3DSurface *add_reference(D3DSurface *s) { if (s) s->Common++; return s; }
void WINAPI D3DDevice_SetRenderTarget(D3DSurface *pRenderTarget, D3DSurface *pNewZStencil)
{ sync();
	TRACE(("  d3d SetRenderTarget %p %p (the game's back buffers %08lx %08lx, depth %08lx, [509370] %08lx; ours %p %p %p)\n", pRenderTarget, pNewZStencil,
		*(DWORD *)0x50935c, *(DWORD *)0x509360, *(DWORD *)0x509364, *(DWORD *)0x509370, g_d3d.back[0], g_d3d.back[1], g_d3d.depth));
	if (pRenderTarget) g_d3d.target = pRenderTarget;
	g_d3d.target_depth = pNewZStencil;
	if (g_d3d.target)
	{
		layout l; container_layout(g_d3d.target, &l);
		g_d3d.viewport.X = 0; g_d3d.viewport.Y = 0; g_d3d.viewport.Width = l.width; g_d3d.viewport.Height = l.height;
	}
	g_counters[C_TARGET]++;
}
void WINAPI D3DDevice_SetRenderTargetFast(D3DSurface *pRenderTarget, D3DSurface *pNewZStencil, DWORD Flags) { D3DDevice_SetRenderTarget(pRenderTarget, pNewZStencil); }
D3DSurface *WINAPI D3DDevice_GetRenderTarget2() { return add_reference(g_d3d.target); }
D3DSurface *WINAPI D3DDevice_GetDepthStencilSurface2() { return add_reference(g_d3d.target_depth); }
D3DSurface *WINAPI D3DDevice_GetBackBuffer2(INT BackBuffer)
{
	TRACE(("  d3d GetBackBuffer2 %d\n", BackBuffer));
	/* -1 is the front buffer, 0 the one being drawn */
	/* the surface the game draws into stays the same object from frame to frame, as on the console */
	DWORD index = BackBuffer < 0 ? 1 : (DWORD)BackBuffer >= g_d3d.back_count ? 0 : (DWORD)BackBuffer == 1 ? (g_d3d.back_count > 2 ? 2 : 1) : (DWORD)BackBuffer;
	return add_reference(g_d3d.back[index]);
}

/* creating, locking and releasing resources */
D3DTexture *WINAPI D3DDevice_CreateTexture2(DWORD Width, DWORD Height, DWORD Depth, DWORD Levels, DWORD Usage, D3DFORMAT Format, D3DRESOURCETYPE D3DType)
{
	static unsigned logged;
	DWORD format = (DWORD)Format, field, size_field = 0, bytes;
	bool cube = D3DType == D3DRTYPE_CUBETEXTURE, volume = D3DType == D3DRTYPE_VOLUMETEXTURE;
	bool power_of_two = !(Width & (Width - 1)) && !(Height & (Height - 1));
	layout l;
	D3DSurface *t;
	if (!Depth || !volume) Depth = 1;
	if (cube) Height = Width;
	if (format_linear(format) || !power_of_two)
	{
		DWORD pitch = (Width * format_bits(format) / 8 + D3DTEXTURE_PITCH_ALIGNMENT - 1) & ~(D3DTEXTURE_PITCH_ALIGNMENT - 1);
		field = D3DFORMAT_DMACHANNEL_A | (2 << D3DFORMAT_DIMENSION_SHIFT) | (format << D3DFORMAT_FORMAT_SHIFT) | (1 << D3DFORMAT_MIPMAP_SHIFT);
		size_field = linear_size_field(Width, Height, pitch);
	}
	else
	{
		DWORD most = Width > Height ? Width : Height;
		if (volume && Depth > most) most = Depth;
		if (!Levels || Levels > log2_of(most) + 1) Levels = log2_of(most) + 1;
		field = D3DFORMAT_DMACHANNEL_A | ((volume ? 3 : 2) << D3DFORMAT_DIMENSION_SHIFT) | (format << D3DFORMAT_FORMAT_SHIFT) | (Levels << D3DFORMAT_MIPMAP_SHIFT)
			| (log2_of(Width) << D3DFORMAT_USIZE_SHIFT) | (log2_of(Height) << D3DFORMAT_VSIZE_SHIFT) | (log2_of(Depth) << D3DFORMAT_PSIZE_SHIFT)
			| (cube ? D3DFORMAT_CUBEMAP : 0);
	}
	{
		D3DPixelContainer probe; probe.Format = field; probe.Size = size_field;
		container_layout(&probe, &l);
		bytes = chain_bytes(&l);
		if (cube) bytes = 6 * ((bytes + D3DTEXTURE_CUBEFACE_ALIGNMENT - 1) & ~(D3DTEXTURE_CUBEFACE_ALIGNMENT - 1));
	}
	t = new_container(D3DCOMMON_TYPE_TEXTURE, field, size_field, Usage, bytes);
	if (logged++ < 60)
		h2n_log("       texture created: %lux%lux%lu, %lu levels, format %#lx, usage %#lx, type %lu, %lu bytes at %p\n", Width, Height, Depth, l.levels, format, Usage,
			(DWORD)D3DType, bytes, resource_memory(t));
	return (D3DTexture *)t;
}
D3DPalette *WINAPI D3DDevice_CreatePalette2(D3DPALETTESIZE Size)
{
	DWORD entries = Size == D3DPALETTE_256 ? 256 : Size == D3DPALETTE_128 ? 128 : Size == D3DPALETTE_64 ? 64 : 32;
	D3DSurface *p = new_container(D3DCOMMON_TYPE_PALETTE | ((DWORD)Size << D3DPALETTE_COMMON_PALETTESIZE_SHIFT), 0, 0, 0, entries * 4);
	return (D3DPalette *)p;
}
D3DCOLOR *WINAPI D3DPalette_Lock2(D3DPalette *pThis, DWORD Flags) { g_counters[C_LOCK]++; return (D3DCOLOR *)resource_memory(pThis); }

static void lock_level(D3DPixelContainer *p, DWORD face, DWORD level, D3DLOCKED_RECT *locked, CONST RECT *rect)
{
	layout l;
	DWORD pitch;
	BYTE *memory;
	container_layout(p, &l);
	level_bytes(&l, level, NULL, NULL, NULL, &pitch);
	memory = level_memory(p, &l, face, level);
	if (rect)
	{
		if (format_compressed(l.format)) memory += (rect->top / 4) * pitch + (rect->left / 4) * (l.format == D3DFMT_DXT1 ? 8 : 16);
		else memory += rect->top * pitch + rect->left * (format_bits(l.format) / 8);
	}
	locked->Pitch = (INT)pitch;
	locked->pBits = memory;
	gx_memory_changed(resource_memory(p));
	g_counters[C_LOCK]++;
}
void WINAPI D3DSurface_LockRect(D3DSurface *pThis, D3DLOCKED_RECT *pLockedRect, CONST RECT *pRect, DWORD Flags) { lock_level(pThis, 0, 0, pLockedRect, pRect); }
void WINAPI D3DTexture_LockRect(D3DTexture *pThis, UINT Level, D3DLOCKED_RECT *pLockedRect, CONST RECT *pRect, DWORD Flags) { lock_level(pThis, 0, Level, pLockedRect, pRect); }
void WINAPI D3DCubeTexture_LockRect(D3DCubeTexture *pThis, D3DCUBEMAP_FACES FaceType, UINT Level, D3DLOCKED_RECT *pLockedRect, CONST RECT *pRect, DWORD Flags)
{
	lock_level(pThis, (DWORD)FaceType, Level, pLockedRect, pRect);
}
void WINAPI D3DVolumeTexture_LockBox(D3DVolumeTexture *pThis, UINT Level, D3DLOCKED_BOX *pLockedVolume, CONST D3DBOX *pBox, DWORD Flags)
{
	layout l;
	DWORD pitch, height;
	BYTE *memory;
	container_layout(pThis, &l);
	level_bytes(&l, Level, NULL, &height, NULL, &pitch);
	memory = level_memory(pThis, &l, 0, Level);
	if (pBox) memory += pBox->Front * pitch * height + pBox->Top * pitch + pBox->Left * (format_bits(l.format) / 8);
	pLockedVolume->RowPitch = (INT)pitch;
	pLockedVolume->SlicePitch = (INT)(pitch * height);
	pLockedVolume->pBits = memory;
	gx_memory_changed(resource_memory(pThis));
	g_counters[C_LOCK]++;
}
void WINAPI D3DSurface_GetDesc(D3DSurface *pThis, D3DSURFACE_DESC *pDesc)
{
	layout l;
	owned *o = owned_of(pThis);
	container_layout(pThis, &l);
	pDesc->Format = (D3DFORMAT)l.format;
	pDesc->Type = D3DRTYPE_SURFACE;
	pDesc->Usage = o ? o->usage : 0;
	pDesc->Size = level_bytes(&l, 0, NULL, NULL, NULL, NULL);
	pDesc->MultiSampleType = D3DMULTISAMPLE_NONE;
	pDesc->Width = l.width;
	pDesc->Height = l.height;
}
D3DSurface *WINAPI D3DTexture_GetSurfaceLevel2(D3DTexture *pThis, UINT Level)
{
	layout l;
	DWORD width, height, pitch, field, size_field = 0;
	owned *o = (owned *)h2n_calloc(sizeof(owned)), *parent = owned_of(pThis);
	container_layout(pThis, &l);
	level_bytes(&l, Level, &width, &height, NULL, &pitch);
	if (pThis->Size) { field = pThis->Format; size_field = pThis->Size; }
	else
		field = (pThis->Format & ~(D3DFORMAT_MIPMAP_MASK | D3DFORMAT_USIZE_MASK | D3DFORMAT_VSIZE_MASK | D3DFORMAT_PSIZE_MASK | D3DFORMAT_CUBEMAP))
			| (1 << D3DFORMAT_MIPMAP_SHIFT) | (log2_of(width) << D3DFORMAT_USIZE_SHIFT) | (log2_of(height) << D3DFORMAT_VSIZE_SHIFT);
	o->magic = OWNED_MAGIC; o->usage = parent ? parent->usage : 0; o->parent = pThis;
	o->resource.Common = 1 | D3DCOMMON_TYPE_SURFACE | D3DCOMMON_D3DCREATED;
	o->resource.Data = (DWORD)level_memory(pThis, &l, 0, Level) & 0x7fffffff;
	o->resource.Format = field; o->resource.Size = size_field; o->resource.Parent = pThis;
	pThis->Common++;
	return &o->resource;
}
ULONG WINAPI D3DResource_Release(D3DResource *pThis)
{
	DWORD count;
	owned *o;
	if (!pThis) return 0;
	count = pThis->Common & D3DCOMMON_REFCOUNT_MASK;
	if (count) count--;
	pThis->Common = (pThis->Common & ~D3DCOMMON_REFCOUNT_MASK) | count;
	if (!count && (o = owned_of(pThis)) != NULL)
	{
		for (int i = 0; i < D3DTSS_MAXSTAGES; i++)
		{
			if ((D3DResource *)g_d3d.textures[i] == pThis) g_d3d.textures[i] = NULL;
			if ((D3DResource *)g_d3d.palettes[i] == pThis) g_d3d.palettes[i] = NULL;
		}
		if (o->parent) D3DResource_Release(o->parent);
		if (o->memory) { gx_memory_gone((const unsigned char *)o->memory); h2n_contig_free(o->memory); }
		o->magic = 0;
		h2n_free(o);
	}
	return count;
}
BOOL WINAPI D3DResource_IsBusy(D3DResource *pThis) { return FALSE; }
void WINAPI D3DResource_BlockUntilNotBusy(D3DResource *pThis) { }
void WINAPI D3DResource_Register(D3DResource *pThis, void *pBase)
{
	static unsigned logged;
	if (logged++ < 12)
	{
		D3DPixelContainer *p = (D3DPixelContainer *)pThis;
		h2n_log("       resource registered: common %#lx, data %#lx + base %p, format %#lx, size %#lx\n", pThis->Common, pThis->Data, pBase,
			(pThis->Common & D3DCOMMON_TYPE_MASK) == D3DCOMMON_TYPE_TEXTURE ? p->Format : 0, (pThis->Common & D3DCOMMON_TYPE_MASK) == D3DCOMMON_TYPE_TEXTURE ? p->Size : 0);
	}
	pThis->Data += (DWORD)pBase & 0x7fffffff;
}

/* ---- H2Classic: Spooky Ghost's Widescreen Fix for the HUD (the mod's o_ws_fix, v2.0.23; 6h - 6h.13, v2.0.4 - v2.0.5) ----
 * In widescreen the game draws its HUD for a 4:3 picture into the 640x480 frame, and the frame is shown 4/3 as wide, so
 * every HUD element comes out 4/3 too wide. As the mod does, each element is drawn 3/4 as wide about its own anchor - the
 * point the game places it from: the safe frame's corner for the corner pieces, the centre for crosshairs - so it keeps its
 * place and gets its shape back, and the pieces of one element (a meter and its frame) stay together. The same elements and
 * anchors as the mod: the three widget drawers that 0x13a532 calls (shader grid 0x22b8e2, bitmap 0x22aff1, text 0x22aa16)
 * and the centre element 0x200462 (from 0x13954b), each about the anchor it asks 0x1396c7 for; the motion tracker's quad on
 * the screen (0x2548f0) about the safe frame's left (0x4b9dda), as the mod's htrkv_hook. Only in a game (the game options'
 * state 1 or 2) while the game itself draws in widescreen (its own test, 0x16a90), as the mod's hsq_widescreen.
 * The mod moves each kind of thing an element draws (its textured quads, a shader widget's 3x3 grid, the text's rectangle,
 * the letters only past 16:9); here everything the element draws into the back buffer is narrowed through the renderer's
 * own screen mapping (gx_squeeze), so the letters have their 4:3 width too and no kind of drawing is left out. The texture
 * coordinates are the game's. H2N_WIDEFIX=0 leaves the HUD as the game draws it. ---- */
extern "C" float __cdecl h2n_ws_factor(void);     /* xapi.c (s9y): 4/3, or 16/9 for 21:9 */
extern "C++" unsigned char __fastcall function_016a90(void);     /* the game's (?function_016a90@@YIEXZ) */
static int g_widefix = -1;
static int widefix_on() { if (g_widefix < 0) g_widefix = h2n_env_int("H2N_WIDEFIX", 1); return g_widefix; }
static int hud_widescreen()
{
	DWORD options = *(DWORD *)0x4E6948;              /* the game options (s_game_options_view): +8 1 campaign, 2 multiplayer */
	if (!options) return 0;
	DWORD state = *(DWORD *)(options + 8);
	return (state == 1 || state == 2) && (function_016a90() & 0xff);
}
/* What is narrowed now: a HUD element about its anchor; the tracker's quad about the safe frame's left; or - the menus, the
 * mod's v2.0.15 - v2.0.22 and v2.0.37 - what the game's windows and the panels drawn like them (the score panel, the help
 * box) draw, about the middle of the 2D rectangle, unless the source has paused it for a draw it leaves whole or draws
 * itself (h2n_ui_pause). */
static int g_hud_in, g_hud_on, g_trk_on, g_ui_depth, g_ui_pause, g_ui_scope, g_ui_panel, g_ui_text, g_txt_on;
static float g_hud_ax, g_txt_cx, g_txt_k = 1.0f;
static int menu_sq(float *cx, float *x0, float *x1)
{
	/* the mod's menu_sq: the widescreen SETTING (g_485ac2), not the game's widescreen test (0x16a90 says no on the main menu
	   and in its lobbies by design, though the picture there is just as wide); the 2D rectangle (shorts top, left, bottom,
	   right at 0x485648, by which 0x35b90 maps pixels to the screen) at least 64 wide */
	if (!widefix_on() || !*(BYTE *)0x485AC2) return 0;
	int l = *(short *)0x48564A, r = *(short *)0x48564E;
	if (r - l < 64) return 0;
	*x0 = (float)l; *x1 = (float)r; *cx = 0.5f * (float)(l + r);
	return 1;
}
static void squeeze_now()
{
	float cx, x0, x1;
	gx_surface back;
	sync();                                          /* what the game wrote before this is drawn as it was */
	if (g_hud_in && g_hud_on) cx = g_hud_ax;
	else if (g_trk_on) cx = (float)*(short *)0x4B9DDA;
	else if (g_txt_on) cx = g_txt_cx;
	else if (!(g_ui_depth > 0 && g_ui_scope > 0 && !g_ui_pause && menu_sq(&cx, &x0, &x1))) { gx_squeeze(NULL, 0.0f, 1.0f); return; }
	describe_surface(g_d3d.back[0], &back);
	/* 1 / the widescreen factor the projection (0x2fb70) uses: 4/3, 16/9 at 21:9 (s9y), 8/3 at 32:9; a text of text_sq's
	   to its 16:9 width */
	gx_squeeze(back.memory, cx, !(g_hud_in && g_hud_on) && !g_trk_on && g_txt_on ? g_txt_k : 1.0f / h2n_ws_factor());
}
extern "C" void __cdecl h2n_hud_element(int on) { g_hud_in = on; g_hud_on = 0; squeeze_now(); }     /* around a HUD element's drawer */
extern "C" void __cdecl h2n_hud_anchor(const float *anchor)                             /* after the drawer asked for its anchor */
{
	if (!g_hud_in || !anchor || !widefix_on() || !hud_widescreen()) return;
	float x = anchor[0];
	if (!(x > -4096.0f && x < 4096.0f)) return;
	g_hud_ax = x; g_hud_on = 1;
	squeeze_now();
}
extern "C" void __cdecl h2n_hud_tracker(int on) { g_trk_on = on && widefix_on() && hud_widescreen(); squeeze_now(); }   /* around the tracker's quad */
/* (s10f) Text the HUD fix never reached, narrowed as Spooky Ghost does it (the mod's mod_text_sq: v2.0.5, v2.0.17 - the
 * pick-up prompt and the HUD's other messages, v2.0.38 - the chapter titles):
 * - the HUD's messages (function_24cf66, compiled from src/unknown_24c7c1.cpp): the pick-up and vehicle prompt, the
 *   messages under it (Checkpoint... done) and the status lines at the bottom middle (WELCOME!, GAME OVER) - each drawn
 *   by the string-in-a-box routine function_1fa30 (the text, its rectangle {top, left, bottom, right}, viewport-
 *   relative); its calls there (retail's 0x24d2f3, 0x24d4e3, 0x24d632, 0x24d6bb, the mod's prompt_box_hook) go through
 *   that file's h2n_text_box. (s10f patched those calls in the XBE's copy of 0x24cf66, which never runs - the game
 *   calls the compiled function_24cf66 - so nothing changed for these texts in build28; s10g hooks the source.)
 * - the chapter titles: the cinematic titles' one call to the text drawer (0x13ca42 -> 0x1fa50, the mod's
 *   title_text_hook), in function_13c780 (src/unknown_13cf00.cpp).
 * None of these is a HUD element's drawer or in the game's windows: at 21:9 and 32:9 they came out 16/9 and 8/3 as wide
 * as on a 4:3 picture (the user's screenshots), where every other text is 4/3 (the 16:9 look). As the mod: wider than
 * 16:9 only, the letters are pulled toward the point the game places the text from - its rectangle's left edge, right
 * edge or middle, by the justification the game set for it (the text state 0x4e73a0 +0x0c: 0 left, 1 right, 2 centred) -
 * by (4/3) / the widescreen factor (3/4 at 21:9, 1/2 at 32:9): as wide as at 16:9, and where it was. At 16:9 and 4:3
 * nothing is done. Widescreen as the mod's aspect_query: the game's own test (0x16a90) or, with the fix on, the setting
 * (0x485ac2). The routine draws nothing but the text's letters (0x1fa30 -> 0x1fa50 -> 0x1f070 / 0x13fbd0 / 0x320560), so
 * narrowing all it draws is narrowing its letters, as the mod's letter hook does. */
static void text_sq(const short *rect)
{
	g_txt_on = 0;
	if (!rect || !widefix_on() || !((function_016a90() & 0xff) || *(BYTE *)0x485AC2)) return;
	float m = h2n_ws_factor(), rel = m > 1.3335f ? m * 0.75f : 1.0f;   /* the mod's ws_rel: how much wider than 16:9 */
	if (!(rel > 1.0005f)) return;
	long justification = *(volatile long *)(0x4E73A0 + 0x0C);
	float left = (float)rect[1], right = (float)rect[3], view = (float)*(short *)0x4B9DD2;   /* the render window's left */
	g_txt_k = 1.0f / rel;
	g_txt_cx = view + (justification == 0 ? left : justification == 1 ? right : 0.5f * (left + right));
	g_txt_on = g_txt_cx > -4096.0f && g_txt_cx < 4096.0f;
}
extern "C" void __cdecl h2n_hud_text_box(int on, const short *rect)   /* around function_24cf66's text boxes (h2n_text_box) */
{
	if (on) text_sq(rect); else g_txt_on = 0;
	squeeze_now();
}
extern "C" void __cdecl h2n_title_text(const short *rect)              /* a chapter title's text: its rectangle, then NULL after */
{
	if (rect) text_sq(rect); else g_txt_on = 0;
	squeeze_now();
}
extern "C" void __cdecl h2n_ui_window(int on) { if (on) g_ui_depth++; else if (g_ui_depth > 0) g_ui_depth--; squeeze_now(); }
extern "C" void __cdecl h2n_ui_pause(int on) { if (on) g_ui_pause++; else if (g_ui_pause > 0) g_ui_pause--; squeeze_now(); }
/* s9l: only what the mod narrows in the menus is narrowed - the menus' text (its letters), their pictures, the textured
 * quads drawn while they are (emblems, the help box's pieces), the filled rectangles of the panels - each in a scope the
 * source opens around that one draw. s9k narrowed everything drawn while a window was, and the game's own copies of the
 * screen behind a dialog (the back buffer copied into a texture and drawn back, 0x82c1b000) were narrowed too: drawn back
 * 3/4 as wide each frame, the menu behind the dialog shrank and the background squeezed (your video and F11 pictures). */
static void ui_scope(int on) { if (on) g_ui_scope++; else if (g_ui_scope > 0) g_ui_scope--; squeeze_now(); }
extern "C" void __cdecl h2n_ui_scope(int on) { ui_scope(on); }
extern "C" void __cdecl h2n_ui_panel(int on)      /* the score panel, the help box: drawn like a window, every letter narrowed */
{
	if (on) { g_ui_depth++; g_ui_panel++; } else { if (g_ui_depth > 0) g_ui_depth--; if (g_ui_panel > 0) g_ui_panel--; }
	squeeze_now();
}
extern "C" void __cdecl h2n_ui_text(int on) { if (on) g_ui_text++; else if (g_ui_text > 0) g_ui_text--; }   /* the windows' text call (0x22d090) */
static int g_glyph_scope;
extern "C" void __cdecl h2n_log_callers(const char *what);
extern "C" void __cdecl h2n_ui_glyph(int on)      /* around each letter's quad (0x1f3a0's call of 0x363a0): the mod's letter hook */
{
	if (on && g_ui_depth > 0 && !g_ui_text && !g_ui_panel && !g_hud_in && h2n_env_int("H2N_UIDIAG", 0))
	{
		static unsigned told;
		float cx, x0, x1;
		if (told < 12 && menu_sq(&cx, &x0, &x1)) { told++; h2n_log_callers("a letter drawn in the menus outside their text call"); }
	}
	if (on) { g_glyph_scope = g_ui_depth > 0 && (g_ui_text > 0 || g_ui_panel > 0) && !g_hud_in; if (g_glyph_scope) ui_scope(1); }
	else if (g_glyph_scope) { g_glyph_scope = 0; ui_scope(0); }
}
extern "C" int __cdecl h2n_ui_quad(const float *corners)   /* a textured quad (0x22a664) while the menus are drawn: 1 = narrowed, close it */
{
	float cx, x0, x1, lo;
	if (g_hud_in || g_ui_depth <= 0 || !corners || !menu_sq(&cx, &x0, &x1)) return 0;
	lo = corners[0];
	for (int i = 1; i < 4; i++) if (corners[i * 2] < lo) lo = corners[i * 2];
	if (!(lo > -8192.0f) || lo <= x0 + 4.0f) return 0;    /* from the left edge: the game's own (the mod's hsq_quad) */
	ui_scope(1);
	return 1;
}
extern "C" int __cdecl h2n_ui_fill(short left)   /* a filled rectangle (0x36880) of a panel: 1 = narrowed, close it */
{
	float cx, x0, x1;
	if (g_ui_depth <= 0 || !menu_sq(&cx, &x0, &x1) || (float)left <= x0 + 4.0f) return 0;   /* from the left edge (the HUD's fade): the game's */
	ui_scope(1);
	return 1;
}
extern "C" int __cdecl h2n_ui_squeeze(float *cx, float *x0, float *x1) { return g_ui_depth > 0 && menu_sq(cx, x0, x1); }   /* the menus apply now */
/* The menus' pictures (the mod's menu_pic_before / mp_edge_bar / menu_pic_next / menu_pic_after, v2.0.17 - v2.0.22): the
 * pictures the windows draw through 0x4b220 (from 0x52040, retail 0x52ce2) are 4 vertices of 20 bytes {x, y, u, v, colour}
 * in the 640x480 2D rectangle. Most are simply narrowed with everything else; these are not:
 *  - one that starts at the screen's left edge (within 4): a thin line across the whole screen (20 tall or less), a plain
 *    fill of one colour across it (the pause menu's darkening) or one that is not a plain rectangle keeps the game's width;
 *    one that stands still (the same place, size and height as the last time it was seen) or covers the whole 4:3 width is
 *    drawn as a bar: what the 4:3 screen shows of it (x0..x1, its u cut to match) narrowed, then its column at the left edge
 *    drawn out to the screen's left edge, and if it reached the right edge its column there out to the right; one that is
 *    moving, or only crosses the edge part-way off the screen, is narrowed like any other (it slides in at its 4:3 shape);
 *  - a piece of a moving line (a scrolling bitmap, 0x235f31 from the bitmap widget 0x2b0565) that reaches an edge of the
 *    screen: narrowed, and drawn again one repeat apart toward each edge the line reached in 4:3, out to that edge (at
 *    most 8 repeats each way).
 * The source calls h2n_menu_pic_begin before its draw, draws again while h2n_menu_pic_next says so, and h2n_menu_pic_end puts
 * the game's vertices back. Here the narrowing itself is the renderer's (the vertices it is given are the game's, or cut,
 * or moved by whole repeats); the extensions are given in screen coordinates and drawn with the narrowing paused. */
#define UI_EDGE 4.0f
static float g_mps[16][4]; static int g_mps_n, g_mps_at;   /* left-edge pictures seen: top, bottom, width, left */
static int mp_near(float a, float b) { return a - b < 0.01f && b - a < 0.01f; }
static int mp_still(float lo, float hi, float ylo, float yhi)
{
	int i;
	for (i = 0; i < g_mps_n; i++)
		if (mp_near(g_mps[i][0], ylo) && mp_near(g_mps[i][1], yhi) && mp_near(g_mps[i][2], hi - lo))
		{
			int still = mp_near(g_mps[i][3], lo);
			g_mps[i][3] = lo;
			return still;
		}
	i = g_mps_n < 16 ? g_mps_n++ : (g_mps_at++ & 15);
	g_mps[i][0] = ylo; g_mps[i][1] = yhi; g_mps[i][2] = hi - lo; g_mps[i][3] = lo;
	return 0;
}
static int g_wrap_on; static float g_wrap_x0, g_wrap_x1;    /* a scrolling bitmap's pieces are being drawn: its whole rectangle */
static struct { float *v; float x[4], u[4]; int n, nmax, ext, edge, paused, scoped, side[4]; float step, ex[2][2], eu[2], mx[4], mu[4]; } g_mp;
static void mp_put(int n) { for (int i = 0; i < 4; i++) g_mp.v[i * 5] = (g_mp.edge ? g_mp.mx[i] : g_mp.x[i]) + (float)n * g_mp.step; }
static void mp_put_ext(int e)
{
	for (int i = 0; i < 4; i++) { g_mp.v[i * 5] = g_mp.ex[e][g_mp.side[i]]; g_mp.v[i * 5 + 2] = g_mp.eu[e]; }
	if (!g_mp.paused) { g_mp.paused = 1; h2n_ui_pause(1); }
}
static int mp_edge_bar(float lo, float hi, float cx, float x0, float x1, float k)
{
	float y[4], u[4], w[4], ylo, yhi, ul = 0.0f, ur = 0.0f, dudx, a, b, ua, ub, sa, sb;
	int i, nl = 0, nr = 0, flat = 1;
	for (i = 0; i < 4; i++)
	{
		y[i] = g_mp.v[i * 5 + 1]; u[i] = g_mp.v[i * 5 + 2]; w[i] = g_mp.v[i * 5 + 3];
		if (!(y[i] > -8192.0f && y[i] < 8192.0f) || !(u[i] > -65536.0f && u[i] < 65536.0f) || !(w[i] > -65536.0f && w[i] < 65536.0f)) return 0;
		if (i && (u[i] - u[0] > 1e-6f || u[0] - u[i] > 1e-6f || w[i] - w[0] > 1e-6f || w[0] - w[i] > 1e-6f)) flat = 0;
	}
	ylo = yhi = y[0];
	for (i = 1; i < 4; i++) { if (y[i] < ylo) ylo = y[i]; if (y[i] > yhi) yhi = y[i]; }
	if (yhi - ylo <= 20.0f && hi >= x1 - 0.5f) return 0;  /* a line across the screen (not a moving one) */
	if (flat && hi >= x1 - 0.5f) return 0;                 /* one colour across the whole screen: the darkening */
	if (!(hi - lo > 1.0f)) return 0;
	for (i = 0; i < 4; i++)
	{
		if (g_mp.x[i] - lo < 0.01f) { if (nl && (u[i] - ul > 1e-4f || ul - u[i] > 1e-4f)) return 0; g_mp.side[i] = 0; ul = u[i]; nl++; }
		else if (hi - g_mp.x[i] < 0.01f) { if (nr && (u[i] - ur > 1e-4f || ur - u[i] > 1e-4f)) return 0; g_mp.side[i] = 1; ur = u[i]; nr++; }
		else return 0;
	}
	if (nl != 2 || nr != 2) return 0;
	if (!(lo >= x0 - UI_EDGE || hi >= x1 - 0.5f)) return 2;    /* only crossing the edge, part-way off the screen */
	if (!mp_still(lo, hi, ylo, yhi)) return 2;                 /* moving (or seen for the first time) */
	dudx = (ur - ul) / (hi - lo);
	a = lo < x0 ? x0 : lo; b = hi > x1 ? x1 : hi;              /* the part the 4:3 screen shows */
	if (!(b - a > 1.0f)) return 2;
	/* s9r (your screenshots: a dark line between the bottom bar and its extension, then with s9q's overlap a light one):
	 * the extension is the mod's - its colour is the column one pixel in from the edge (as the mod has it), because the
	 * very edge column samples the texture at its edge, where it blends with what lies past it. The narrowed part still
	 * drew that edge column (on the console it is at the screen's edge, here it lands where the bar meets its extension):
	 * at a high render size it showed as a dark line. Now the narrowed part stops where the extension begins - at that
	 * column, one pixel in, at each end that has an extension - and the extension starts exactly there, with that
	 * column's colour, as before. Nothing is drawn twice (the bar is see-through: an overlap shows lighter). */
	if (hi >= x1 - 0.5f && b - cx > 1.0f) b -= 1.0f;            /* the right end has an extension (below): stop a pixel in */
	a += 1.0f;                                                /* the left end always has one */
	if (!(b - a > 1.0f)) return 2;
	ua = ul + (a - lo) * dudx; ub = ul + (b - lo) * dudx;
	sa = cx + (a - cx) * k; sb = cx + (b - cx) * k;
	for (i = 0; i < 4; i++) { g_mp.mx[i] = g_mp.side[i] ? b : a; g_mp.mu[i] = g_mp.side[i] ? ub : ua; }   /* narrowed by the renderer */
	g_mp.ex[0][0] = x0 - 1.0f; g_mp.ex[0][1] = sa; g_mp.eu[0] = ua;   /* the column a pixel in from the edge, out to it */
	g_mp.ext = 1;
	if (hi >= x1 - 0.5f && sb < x1) { g_mp.ex[1][0] = sb; g_mp.ex[1][1] = x1 + 1.0f; g_mp.eu[1] = ub; g_mp.ext |= 2; }
	return 1;
}
extern "C" void __cdecl h2n_menu_pic_begin(float *v)
{
	float cx, x0, x1, k = 1.0f / h2n_ws_factor(), lo, hi;
	int i, n0 = 0, n1 = 0;
	memset(&g_mp, 0, sizeof(g_mp));
	if (!v || !h2n_ui_squeeze(&cx, &x0, &x1) || g_ui_pause) return;
	for (i = 0; i < 4; i++) { g_mp.x[i] = v[i * 5]; g_mp.u[i] = v[i * 5 + 2]; if (!(g_mp.x[i] > -8192.0f && g_mp.x[i] < 8192.0f)) return; }
	lo = hi = g_mp.x[0];
	for (i = 1; i < 4; i++) { if (g_mp.x[i] < lo) lo = g_mp.x[i]; if (g_mp.x[i] > hi) hi = g_mp.x[i]; }
	g_mp.v = v;
	if (g_wrap_on && (g_wrap_x0 <= x0 + UI_EDGE || g_wrap_x1 >= x1 - 0.5f))
	{
		float step = (g_wrap_x1 - g_wrap_x0) * k, a = cx + (lo - cx) * k, b = cx + (hi - cx) * k, f;
		if (step >= 8.0f)
		{
			if (g_wrap_x0 <= x0 + UI_EDGE) { f = (x0 - b) / step; if (f < -8.0f) f = -8.0f; if (f > 8.0f) f = 8.0f; n0 = (int)f; if ((float)n0 < f) n0++; }
			if (g_wrap_x1 >= x1 - 0.5f) { f = (x1 - a) / step; if (f < -8.0f) f = -8.0f; if (f > 8.0f) f = 8.0f; n1 = (int)f; if ((float)n1 > f) n1--; }
			if (n0 > 0) n0 = 0;
			if (n1 < 0) n1 = 0;
			g_mp.step = g_wrap_x1 - g_wrap_x0;                   /* one repeat, before the renderer narrows it */
		}
	}
	else if (lo <= x0 + UI_EDGE)
	{
		int r = mp_edge_bar(lo, hi, cx, x0, x1, k);
		if (!r) { g_mp.v = NULL; return; }                 /* the game's whole width: no scope */
		g_mp.edge = r == 1;
		if (!g_mp.edge) g_mp.ext = 0;
	}
	mp_put(n0);
	if (g_mp.edge) for (i = 0; i < 4; i++) v[i * 5 + 2] = g_mp.mu[i];
	g_mp.scoped = 1; ui_scope(1);                      /* narrowed by the renderer */
	g_mp.n = n0 + 1; g_mp.nmax = n1;
}
extern "C" int __cdecl h2n_menu_pic_next(float *v)   /* after each draw: 1 = the vertices hold one more to draw */
{
	if (!g_mp.v || g_mp.v != v) return 0;
	if (g_mp.n <= g_mp.nmax) { mp_put(g_mp.n); g_mp.n++; return 1; }
	if (g_mp.ext & 1) { g_mp.ext &= ~1; mp_put_ext(0); return 1; }
	if (g_mp.ext & 2) { g_mp.ext &= ~2; mp_put_ext(1); return 1; }
	return 0;
}
extern "C" void __cdecl h2n_menu_pic_end(float *v)
{
	if (g_mp.v && g_mp.v == v) for (int i = 0; i < 4; i++) { v[i * 5] = g_mp.x[i]; v[i * 5 + 2] = g_mp.u[i]; }
	if (g_mp.paused) h2n_ui_pause(0);
	if (g_mp.scoped) ui_scope(0);
	memset(&g_mp, 0, sizeof(g_mp));
}
extern "C" void __cdecl h2n_menu_wrap(const float *rect, const float *split)   /* around a scrolling bitmap's draw; NULL after */
{
	g_wrap_on = 0;
	if (!rect || !split || !(split[0] > 0.0f && split[0] < 1.0f)) return;   /* one that scrolls only up or down: as any picture */
	if (!(rect[0] > -8192.0f && rect[0] < 8192.0f && rect[1] > rect[0] + 1.0f && rect[1] < 8192.0f)) return;
	g_wrap_x0 = rect[0]; g_wrap_x1 = rect[1]; g_wrap_on = 1;
}
extern "C" void __cdecl h2n_menu_model(short *left, short *right)   /* the model widget's 3D view: narrowed about the middle */
{
	float cx, x0, x1;
	if (!h2n_ui_squeeze(&cx, &x0, &x1)) return;
	int l = *left, r = *right;
	if (r - l < 2 || l < -4096 || r > 4096) return;
	float a = cx + ((float)l - cx) / h2n_ws_factor(), b = cx + ((float)r - cx) / h2n_ws_factor();
	*left = (short)(a + (a < 0.0f ? -0.5f : 0.5f)); *right = (short)(b + (b < 0.0f ? -0.5f : 0.5f));
}
extern "C" int __cdecl h2n_menu_wide(void)       /* the main menu's 3D drawn widescreen (the mod's mm_proj / mm_water) */
{
	DWORD options = *(DWORD *)0x4E6948;
	return widefix_on() && *(BYTE *)0x485AC2 && options && *(DWORD *)(options + 8) == 3;
}

}   /* extern "C" */

/* ---- Direct3D internals the game's own code reaches into (declared by the decompiled source, not by the SDK) ---- */
namespace D3D
{
	class CDevice { public: void LazySetStateVB(DWORD value); static DWORD m_PushSegmentSize; };
	DWORD CDevice::m_PushSegmentSize = 0x8000;
	void CDevice::LazySetStateVB(DWORD value) { }
	DWORD volatile *WINAPI MakeRequestedSpace(DWORD fallback_size, DWORD requested_size) { TRACE(("  d3d MakeRequestedSpace %lu %lu (put %#lx)\n", fallback_size, requested_size, g_device_block[0] - (DWORD)g_push_buffer)); g_counters[C_PUSH]++; push_restart(); return g_push_buffer; }
	DWORD WINAPI SetFence(DWORD flags) { return 0; }
	namespace PixelJar
	{
		void __stdcall FindSurfaceWithinTexture(D3DPixelContainer *container, D3DCUBEMAP_FACES face, unsigned int level,
			BYTE **bits, DWORD *pitch, DWORD *width, DWORD *height, DWORD *slice)
		{
			static DWORD nothing[64];
			layout l;
			DWORD w, h, d, row;
			if (!container) { *bits = (BYTE *)nothing; *pitch = 0; *width = 0; *height = 0; *slice = 0; return; }
			container_layout(container, &l);
			level_bytes(&l, level, &w, &h, &d, &row);
			*bits = level_memory(container, &l, (DWORD)face, level);
			*pitch = row; *width = w; *height = h; *slice = row * h;
		}
	}
}
extern "C" const char *const h2n_d3d_counter_names = g_counter_names;

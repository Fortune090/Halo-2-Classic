/* H2Classic: what the Xbox Direct3D layer (xd3d.cpp, built with the Xbox SDK's compiler) hands to the picture
 * back end (gfx9.c, built with the Windows headers). Everything here is in the console's own terms: its texture
 * formats, its vertex program, its register combiners, its state values. The back end turns them into Direct3D 9. */
#ifndef H2N_GFX_H
#define H2N_GFX_H
#ifdef __cplusplus
extern "C" {
#endif

typedef struct gx_texture
{
	const unsigned char *memory;     /* level 0 of face 0; null = nothing bound */
	unsigned format;                 /* the console's format code */
	unsigned width, height, depth, levels, pitch;
	unsigned cube, linear, volume;
	unsigned face_bytes;             /* cube maps: bytes from one face to the next */
	unsigned level_offset[12];       /* each level, from memory */
	unsigned level_bytes[12];
	const unsigned *palette;         /* 256 colours, or null */
} gx_texture;

typedef struct gx_sampler
{
	unsigned address_u, address_v, address_w, mag, min, mip, border_color, color_key_op, color_sign, alpha_kill, max_mip, max_anisotropy;
	float lod_bias, bump[4], luminance_scale, luminance_offset;
} gx_sampler;

typedef struct gx_attribute { const unsigned char *data; unsigned stride, type; } gx_attribute;   /* data null: the current value */

typedef struct gx_surface { const unsigned char *memory; unsigned width, height, format; } gx_surface;

typedef struct gx_state
{
	/* vertices */
	const unsigned (*program)[4];    /* the 136 instruction slots of the vertex program memory */
	unsigned program_start;
	int program_selected;            /* 0: the fixed-function pipeline, with fvf */
	unsigned fvf;
	const float (*constants)[4];     /* 192 */
	gx_attribute attribute[16];
	float current[16][4];

	/* pixels: the register combiner settings, as the console's registers hold them */
	unsigned alpha_inputs[8], rgb_inputs[8], alpha_outputs[8], rgb_outputs[8], constant0[8], constant1[8];
	unsigned final_abcd, final_efg, final_constant0, final_constant1, combiner_count, texture_modes, dot_mapping, input_texture, compare_mode;
	int combiners;                   /* 0: the fixed-function texture stages */
	gx_texture texture[4];
	gx_sampler sampler[4];

	/* the rest of the state, in the console's values */
	unsigned alpha_test, alpha_func, alpha_ref, blend, blend_source, blend_destination, blend_op, blend_color, color_mask;
	unsigned z_enable, z_write, z_func, z_bias, stencil, stencil_func, stencil_ref, stencil_mask, stencil_write_mask, stencil_fail, stencil_zfail, stencil_pass;
	unsigned cull, front_face, fill, shade, fog, fog_color, fog_table_mode, dither, point_sprite;
	float fog_start, fog_end, fog_density;
	unsigned offset_on;              /* the polygon offset of the fill mode in use is switched on */
	float offset_slope, offset_units;/* depth += slope * (the steepest change of depth from one pixel to the next) + units, in the depth buffer's units */
	unsigned viewport_x, viewport_y, viewport_width, viewport_height;
	float viewport_min_z, viewport_max_z;
	float depth_max;      /* the largest value of the depth buffer in use: what the game's vertex programs scale their depth to */
	unsigned depth_clip;  /* the console's depth clip control: with bit 4 (0x10) depth is clamped to the buffer's range and nothing is cut off at the near and far planes */
	float clip_min, clip_max;      /* the range of depth values the console keeps (in the depth buffer's units): what is outside is cut off, or clamped */
	int scissor[4], scissor_on;
	gx_surface target, depth;
	unsigned shadow_func;            /* (s10k) D3DRS_SHADOWFUNC: how a texture stage that reads a depth buffer compares (0x200 never .. 0x207 always) */
} gx_state;

int  __cdecl gx_open(unsigned width, unsigned height);
void __cdecl gx_present(const gx_surface *back);
void __cdecl gx_clear(const gx_state *state, unsigned flags, unsigned color, float z, unsigned stencil, const int *rectangle);
void __cdecl gx_draw(const gx_state *state, unsigned primitive, unsigned vertex_count, const unsigned short *indices, unsigned first_vertex);
void __cdecl gx_memory_gone(const unsigned char *memory);
/* H2Classic, the Widescreen Fix: from now on what is drawn into the surface at target is narrowed by k about the x pivot, in
   the console's screen coordinates (target NULL: nothing is) */
void __cdecl gx_squeeze(const unsigned char *target, float pivot, float k);
int  __cdecl gx_running(void);
/* the console's visibility test: count the pixels that pass the depth test between begin and end, and ask for the count later */
void __cdecl gx_visibility_begin(void);
void __cdecl gx_visibility_end(unsigned index);
int  __cdecl gx_visibility_result(unsigned index, unsigned *pixels);      /* 1: the count is there; 0: not yet; -1: no test of that number */
/* s9r: where the draw's time goes - the processor's time stamp counter; all of gx_*'s own time so far; the time spent
   reading the game's GPU commands (xd3d.cpp's flush, less the gx_* calls made from it) */
#ifdef _MSC_VER
typedef unsigned __int64 gx_u64;
#else
typedef unsigned long long gx_u64;
#endif
gx_u64 __cdecl h2n_gfx_tsc(void);
gx_u64 __cdecl h2n_gfx_gx_total(void);
void __cdecl h2n_gfx_push_time(gx_u64 reading);

#ifdef __cplusplus
}
#endif
#endif

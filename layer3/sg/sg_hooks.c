/* H2Classic: the Spooky Ghost Menu's hooks.
 *
 * On the console the mod's hooks are `call` instructions in the XBE pointed at routines of its own (hooks.S), which see
 * the registers the XBE's link-time code uses. Here:
 *  - calls made by the COMPILED game code are pointed at the C functions below in the objects (scripts/sg_hooks.py with
 *    native/sg_hooks.json: the call's relocation is aimed at sg_w_...). Each takes exactly what the hooked function is
 *    declared to take (the compiler passes the arguments by that declaration), does what the mod's routine does around
 *    the call, and calls the function itself;
 *  - calls made by the XBE's OWN code where the build runs it (routes; in 1.5, what the update changed; and whatever
 *    those reach) are patched in the XBE's code as on the console, with the mod's own routines (sg_retail.S), each call
 *    checked to be that call first (sg_sites.h);
 *  - before each tick, before each frame's drawing and around the first-person model's set-up, the layer's own hooks of
 *    those places (interp.c) call sg_tick_before, sg_draw_before, sg_fp_before, sg_fp_after (spooky.c).
 * With the mod off (H2Classic Settings; H2N_SPOOKY) every wrapper goes straight to the game's function and nothing of
 * the XBE's code is patched.
 */
#include <windows.h>
#include <stdlib.h>
#include <string.h>
typedef unsigned char u8; typedef unsigned short u16; typedef unsigned int u32; typedef signed short s16; typedef signed int s32;
#define SG_FAST __attribute__((fastcall))
#define SG_STD __attribute__((stdcall))
#define SG_GAME(name) __asm__(name)
extern int h2n_v15;
void h2n_log(const char *format, ...);
int h2n_env_int(const char *name, int otherwise);
int h2n_retail_code_loaded(void);
int sg_on;

/* spooky.c */
void spooky_main_frame(void); void spooky_frame(void); void spooky_after_windows(void); void spooky_view(u32 view);
u32 spooky_input(void *handle, void *state);
s32 ia_answer(u32 weapon_index); void bc_before(u32 weapon_index); void bc_after(void);
void ohk_before(u32 data, u32 object_index); void ohk_after(void); s32 rdmg_skip(u32 object_index);
void natural_save_check(void); extern s32 g_cp_skip; void death_revert_before(void); void death_revert_after(void);
extern s32 o_acro; s32 acro_player_biped(u32 index); void acro_before(u32 index); void acro_after(u32 index);
void ct_won(void);
u32 tp_camera(u32 cam, s32 user); s32 hud_skip(void); s32 fpm_answer(u32 object_index); s32 persp_answer(s32 user);
s32 cam_mode_answer(s32 game, u32 unit, u32 out); void cam_target_fix(u32 rec);
void fw_fp_filter(u32 actor, u32 squad, u32 entries, u32 count); void tvt_note(u32 index, u32 point, u32 result);

/* ---- the game's functions (the decompilation's declarations) ---- */
void SG_FAST game_12b450(void) SG_GAME("?function_12b450@@YIXXZ");                 /* the main loop's pass */
u32 SG_STD game_XInputGetState(void *handle, void *state) SG_GAME("_XInputGetState@8");   /* (the layer's: xpad.cpp) */
void SG_FAST game_13c780(void) SG_GAME("?function_13c780@@YIXXZ");                 /* the HUD's text pass */
void SG_FAST game_13c20(void) SG_GAME("?function_13c20@@YIXXZ");                   /* the close of the pass that draws the game's windows */
void SG_STD game_2c790(const u8 *source) SG_GAME("?function_2c790@@YGXPBE@Z");     /* the full-screen interface pass (its view record) */
u8 SG_FAST game_106030(s32 weapon_index) SG_GAME("?function_106030@@YI_NJ@Z");
void SG_STD game_102fb0(s32 weapon_index, s32 barrel_index) SG_GAME("?function_102fb0@@YGXJF@Z");
void SG_STD game_d7b80(void *data, s32 object_index, s32 node_index, s32 unknown0c, s32 region_entry_index, const float *unknown14) SG_GAME("?function_d7b80@@YGXPAUs_type_1e6529@@JFFFPBTvector3f@@@Z");
void SG_FAST game_daaa0(void *info, s32 object_index, const void *owner, s32 region_index, float damage, void *accumulator) SG_GAME("?function_daaa0@@YIXPAUs_damage_info@@JPBUs_damage_owner@@JMPAUs_damage_region_accumulator@@@Z");
void SG_FAST game_12b790(void) SG_GAME("?function_12b790@@YIXXZ");
void SG_FAST game_138e40(void) SG_GAME("?game_revert_countdown_update@@YIXXZ");
u8 SG_FAST game_e4680(s32 biped) SG_GAME("?function_e4680@@YI_NJ@Z");
void SG_FAST game_e3700(s32 biped) SG_GAME("?function_e3700@@YIXJ@Z");
void SG_FAST game_1388e0(void) SG_GAME("?game_finish@@YIXXZ");

/* ---- the wrappers (the mod's routine named beside each) ---- */
void SG_FAST sg_w_main(void) SG_GAME("@sg_w_main@0");                              /* main_hook: the mod's frame, then the game's */
void SG_FAST sg_w_main(void) { if (sg_on) spooky_main_frame(); game_12b450(); }
u32 SG_STD sg_w_input(void *handle, void *state) SG_GAME("_sg_w_input@8");         /* input_hook: the mod reads the pad (and passes on what the game may see) */
u32 SG_STD sg_w_input(void *handle, void *state) { return sg_on ? spooky_input(handle, state) : game_XInputGetState(handle, state); }
void SG_FAST sg_w_hud(void) SG_GAME("@sg_w_hud@0");                                /* hud_hook: the game's text first, then the mod's overlay and menu */
void SG_FAST sg_w_hud(void) { game_13c780(); if (sg_on) spooky_frame(); }
void SG_FAST sg_w_late(void) SG_GAME("@sg_w_late@0");                              /* late_hook: the mod's menu over the game's windows, then on into the call */
void SG_FAST sg_w_late(void) { if (sg_on) spooky_after_windows(); game_13c20(); }
void SG_STD sg_w_hud_view(const u8 *source) SG_GAME("_sg_w_hud_view@4");           /* hud_view_hook: the view record noted, then the game's */
void SG_STD sg_w_hud_view(const u8 *source) { if (sg_on) spooky_view((u32)(ULONG_PTR)source); game_2c790(source); }
u8 SG_FAST sg_w_inf_ammo(s32 weapon_index) SG_GAME("@sg_w_inf_ammo@4");            /* ia_hook: the mod's answer, or (2) the game's */
u8 SG_FAST sg_w_inf_ammo(s32 weapon_index)
{
	if (sg_on) { s32 a = ia_answer((u32)weapon_index); if (a != 2) return (u8)a; }
	return game_106030(weapon_index);
}
void SG_STD sg_w_fire(s32 weapon_index, s32 barrel_index) SG_GAME("_sg_w_fire@8"); /* bc_fire_hook */
void SG_STD sg_w_fire(s32 weapon_index, s32 barrel_index)
{
	if (!sg_on) { game_102fb0(weapon_index, barrel_index); return; }
	bc_before((u32)weapon_index); game_102fb0(weapon_index, barrel_index); bc_after();
}
void SG_STD sg_w_damage(void *data, s32 object_index, s32 node_index, s32 unknown0c, s32 region_entry_index, const float *unknown14) SG_GAME("_sg_w_damage@24");   /* ohk_hook */
void SG_STD sg_w_damage(void *data, s32 object_index, s32 node_index, s32 unknown0c, s32 region_entry_index, const float *unknown14)
{
	if (!sg_on) { game_d7b80(data, object_index, node_index, unknown0c, region_entry_index, unknown14); return; }
	ohk_before((u32)(ULONG_PTR)data, (u32)object_index);
	game_d7b80(data, object_index, node_index, unknown0c, region_entry_index, unknown14);
	ohk_after();
}
void SG_FAST sg_w_region_damage(void *info, s32 object_index, const void *owner, s32 region_index, float damage, void *accumulator) SG_GAME("@sg_w_region_damage@24");   /* rdmg_hook */
void SG_FAST sg_w_region_damage(void *info, s32 object_index, const void *owner, s32 region_index, float damage, void *accumulator)
{
	if (sg_on && rdmg_skip((u32)object_index)) return;                             /* a vehicle with Invincible Vehicles on: its parts don't break */
	game_daaa0(info, object_index, owner, region_index, damage, accumulator);
}
void SG_FAST sg_w_save(void) SG_GAME("@sg_w_save@0");                              /* hsave_hook */
void SG_FAST sg_w_save(void) { if (sg_on) { natural_save_check(); if (g_cp_skip) return; } game_12b790(); }
void SG_FAST sg_w_revert(void) SG_GAME("@sg_w_revert@0");                          /* hrevert_hook */
void SG_FAST sg_w_revert(void)
{
	if (!sg_on) { game_138e40(); return; }
	death_revert_before(); game_138e40(); death_revert_after();
}
u8 SG_FAST sg_w_jump(s32 biped) SG_GAME("@sg_w_jump@4");                           /* acro_jump_hook */
u8 SG_FAST sg_w_jump(s32 biped)
{
	u8 r;
	if (!sg_on || !o_acro) return game_e4680(biped);
	acro_before((u32)biped); r = game_e4680(biped); acro_after((u32)biped);
	return r;
}
void SG_FAST sg_w_fall(s32 biped) SG_GAME("@sg_w_fall@4");                         /* acro_fall_hook */
void SG_FAST sg_w_fall(s32 biped) { if (sg_on && o_acro && acro_player_biped((u32)biped)) return; game_e3700(biped); }
void SG_FAST sg_w_finish(void) SG_GAME("@sg_w_finish@0");                          /* won_hook (the script's game_won) */
void SG_FAST sg_w_finish(void) { if (sg_on) ct_won(); game_1388e0(); }

/* ---- cameras, trigger volumes, a spawned squad's firing positions ---- */
void SG_FAST game_223080(s32 arg_0, s32 arg_1, s32 arg_2, void *arg_3, s32 arg_4, s32 arg_5, const void *arg_6) SG_GAME("?function_223080@@YIXJJJPAUs_223080@@JJPBUs_223081@@@Z");
void SG_FAST game_1917eb(void) SG_GAME("?function_1917eb@@YIXXZ");
u8 SG_FAST game_16640f(s32 object_index, s32 marker_name, void *position, void *forward, void *up) SG_GAME("?first_person_weapon_get_marker@@YI_NJJPATpoint3f@@PATvector3f@@1@Z");
s32 SG_FAST game_155760(s32 index) SG_GAME("?function_155760@@YIJJ@Z");
s16 SG_FAST game_155810(s32 unit_index, s16 *mode) SG_GAME("?function_155810@@YIFJPAF@Z");
void SG_FAST game_1871e0(s32 player_index, void *camera) SG_GAME("?player_control_get_camera@@YIXJPAUs_player_control_camera@@@Z");
u8 SG_STD game_261ec0(s32 actor_index, s32 squad_index, void *entries, s16 *count, s32 maximum_count, void *search) SG_GAME("?function_261ec0@@YG_NJJPAUs_261d20_entry@@PAFJPAUs_prop_search@@@Z");
u8 SG_FAST game_11c470(s32 trigger_volume_index, const float *point) SG_GAME("?function_11c470@@YI_NJPBTpoint3f@@@Z");

/* tp_view_hook: a player's view built from the mod's camera record when it has moved the view (third person, the free
   camera, Field of View). The console build hands tp_camera the player (the function's arg_1) and the camera record
   (its arg_6: the record the view is built from) and passes on what it returns in that record's place. */
void SG_FAST sg_w_view_build(s32 arg_0, s32 arg_1, s32 arg_2, void *arg_3, s32 arg_4, s32 arg_5, const void *arg_6) SG_GAME("@sg_w_view_build@28");
void SG_FAST sg_w_view_build(s32 arg_0, s32 arg_1, s32 arg_2, void *arg_3, s32 arg_4, s32 arg_5, const void *arg_6)
{
	if (sg_on) arg_6 = (const void *)(ULONG_PTR)tp_camera((u32)(ULONG_PTR)arg_6, arg_1);
	game_223080(arg_0, arg_1, arg_2, arg_3, arg_4, arg_5, arg_6);
}
void SG_FAST sg_w_hud_draw(void) SG_GAME("@sg_w_hud_draw@0");                      /* hudd_hook: no HUD in the free camera's view, or with Hide HUD */
void SG_FAST sg_w_hud_draw(void) { if (sg_on && hud_skip()) return; game_1917eb(); }
u8 SG_FAST sg_w_fp_marker(s32 object_index, s32 marker_name, void *position, void *forward, void *up) SG_GAME("@sg_w_fp_marker@20");   /* fpm_hook */
u8 SG_FAST sg_w_fp_marker(s32 object_index, s32 marker_name, void *position, void *forward, void *up)
{
	if (sg_on) { s32 a = fpm_answer((u32)object_index); if (a != 2) return (u8)a; }      /* a moved view: no first-person marker (the mod's answer), or (2) the game's */
	return game_16640f(object_index, marker_name, position, forward, up);
}
s32 SG_FAST sg_w_perspective(s32 index) SG_GAME("@sg_w_perspective@4");             /* persp_hook: 3 (following) for a moved view, or (-1) the game's */
s32 SG_FAST sg_w_perspective(s32 index)
{
	if (sg_on) { s32 a = persp_answer(index); if (a != -1) return a; }
	return game_155760(index);
}
s16 SG_FAST sg_w_cam_mode(s32 unit_index, s16 *mode) SG_GAME("@sg_w_cam_mode@8");   /* hcam_mode_hook: the game's answer, then the mod's for a possessed vehicle */
s16 SG_FAST sg_w_cam_mode(s32 unit_index, s16 *mode)
{
	s16 r = game_155810(unit_index, mode);
	if (sg_on) r = (s16)cam_mode_answer((s32)(u16)r, (u32)unit_index, (u32)(ULONG_PTR)mode);
	return r;
}
void SG_FAST sg_w_cam_target(s32 player_index, void *camera) SG_GAME("@sg_w_cam_target@8");   /* htarget_hook: a possessed vehicle's driver seat camera */
void SG_FAST sg_w_cam_target(s32 player_index, void *camera) { game_1871e0(player_index, camera); if (sg_on) cam_target_fix((u32)(ULONG_PTR)camera); }
u8 SG_STD sg_w_fp_gather(s32 actor_index, s32 squad_index, void *entries, s16 *count, s32 maximum_count, void *search) SG_GAME("_sg_w_fp_gather@24");   /* fp_gather_hook */
u8 SG_STD sg_w_fp_gather(s32 actor_index, s32 squad_index, void *entries, s16 *count, s32 maximum_count, void *search)
{
	u8 r = game_261ec0(actor_index, squad_index, entries, count, maximum_count, search);
	if (sg_on) fw_fp_filter((u32)actor_index, (u32)squad_index, (u32)(ULONG_PTR)entries, (u32)(ULONG_PTR)count);
	return r;
}
u8 SG_FAST sg_w_tvt(s32 trigger_volume_index, const float *point) SG_GAME("@sg_w_tvt@8");   /* tvt_hook: a script's trigger volume test, noted after the game's answer */
u8 SG_FAST sg_w_tvt(s32 trigger_volume_index, const float *point)
{
	u8 r = game_11c470(trigger_volume_index, point);
	if (sg_on) tvt_note((u32)trigger_volume_index, (u32)(ULONG_PTR)point, (u32)r);
	return r;
}

/* ---- Wireframe (Other Tools): off / on / xray ----
 * on: after each of the game's draws of triangles or quads, the same draw once more as lines - wireframe fill, the
 *   wireframe depth offset the game uses for decals, and the logic operation SET, which makes every pixel written white
 *   (the console build's drawidx_hook / drawv_hook around Direct3D's DrawIndexedVertices and DrawVertices: all eight and
 *   all three of the game's calls). The layer's Direct3D draws that state as the console does (gfx9.c).
 * xray: the frame's own "fill mode: solid" becomes wireframe while the world is drawn (the console build's fill_hook at
 *   the call in function_132d0). In this build that function sets it through the game's state setter (function_0222d0),
 *   so the wrapper is on that function's calls of the setter and acts on the fill mode alone. */
extern volatile u8 spooky_wire_on, spooky_fill_wire;
void wire_line_begin(void); void wire_line_end(void); void wire_fill(void);
void SG_STD d3d_draw_indexed(u32 primitive_type, u32 vertex_count, const u16 *index_data) SG_GAME("_D3DDevice_DrawIndexedVertices@12");
void SG_STD d3d_draw_vertices(u32 primitive_type, u32 start_vertex, u32 vertex_count) SG_GAME("_D3DDevice_DrawVertices@12");
void SG_FAST game_0222d0(u32 state, u32 value) SG_GAME("?function_0222d0@@YIXW4_D3DRENDERSTATETYPE@@K@Z");
void SG_STD sg_w_draw_indexed(u32 primitive_type, u32 vertex_count, const u16 *index_data) SG_GAME("_sg_w_draw_indexed@12");
void SG_STD sg_w_draw_indexed(u32 primitive_type, u32 vertex_count, const u16 *index_data)
{
	d3d_draw_indexed(primitive_type, vertex_count, index_data);
	if (!sg_on || !spooky_wire_on || primitive_type < 5) return;                  /* (5: triangle list - points and lines are not outlined) */
	wire_line_begin(); d3d_draw_indexed(primitive_type, vertex_count, index_data); wire_line_end();
}
void SG_STD sg_w_draw_vertices(u32 primitive_type, u32 start_vertex, u32 vertex_count) SG_GAME("_sg_w_draw_vertices@12");
void SG_STD sg_w_draw_vertices(u32 primitive_type, u32 start_vertex, u32 vertex_count)
{
	d3d_draw_vertices(primitive_type, start_vertex, vertex_count);
	if (!sg_on || !spooky_wire_on || primitive_type < 5) return;
	wire_line_begin(); d3d_draw_vertices(primitive_type, start_vertex, vertex_count); wire_line_end();
}
void SG_FAST sg_w_frame_state(u32 state, u32 value) SG_GAME("@sg_w_frame_state@8");
void SG_FAST sg_w_frame_state(u32 state, u32 value)
{
	if (sg_on && state == 0x8b) { wire_fill(); if (spooky_fill_wire) value = 0x1B01; }     /* D3DRS_FILLMODE: wireframe */
	game_0222d0(state, value);
}

/* ---- the XBE's own code: the mod's call sites, as on the console ---- */
struct sg_site { DWORD site[2], target[2]; void (*routine)(void); DWORD *orig; const char *name; };
void sg_r_ia_hook(void); void sg_r_bc_fire_hook(void); void sg_r_ohk_hook(void); void sg_r_rdmg_hook(void);
void sg_r_hsave_hook(void); void sg_r_hrevert_hook(void); void sg_r_acro_jump_hook(void); void sg_r_acro_fall_hook(void);
void sg_r_fpm_hook(void); void sg_r_persp_hook(void); void sg_r_hcam_mode_hook(void); void sg_r_htarget_hook(void); void sg_r_tvt_hook(void); void sg_r_fp_gather_hook(void);
extern DWORD sg_orig_fp_marker, sg_orig_perspective, sg_orig_cam_mode, sg_orig_cam_target, sg_orig_tvt, sg_orig_fp_gather;
extern DWORD sg_orig_inf_ammo_fn, sg_orig_fire, sg_orig_damage, sg_orig_region_dmg, sg_orig_main_save, sg_orig_death_revert, sg_orig_jump_logic, sg_orig_fall_check;
#include "sg_sites.h"

/* ---- this build's functions and the XBE's ----
 * The game's tables (vtables, callbacks) hold this build's functions here, not the XBE's addresses. Where the mod looks at
 * a function's CODE (a Havok shape's getType is read, never called) or only needs to know that a table entry is a game
 * function, it asks for the XBE's code of it: the XBE is in place at its own addresses in both game versions. */
struct known_function { DWORD retail; BYTE *ours; };
extern const struct known_function h2n_all_functions[], h2n15_all_functions[];
static struct known_function *g_by_ours; static unsigned g_by_ours_n;
static int by_ours(const void *a, const void *b) { DWORD x = (DWORD)(ULONG_PTR)((const struct known_function *)a)->ours, y = (DWORD)(ULONG_PTR)((const struct known_function *)b)->ours; return x < y ? -1 : x > y; }
u32 sg_xbe_code(u32 function)
{
	unsigned lo = 0, hi = g_by_ours_n;
	if (!h2n_retail_code_loaded()) return 0;
	if (function >= 0x11000u && function < (h2n_v15 ? 0x0041b000u : 0x00411000u)) return function;
	while (lo < hi)
	{
		unsigned mid = (lo + hi) / 2; DWORD at = (DWORD)(ULONG_PTR)g_by_ours[mid].ours;
		if (at == function) return g_by_ours[mid].retail;
		if (at < function) lo = mid + 1; else hi = mid;
	}
	return 0;
}
static void xbe_code_table(void)
{
	const struct known_function *k = h2n_v15 ? h2n15_all_functions : h2n_all_functions, *e = k;
	while (e->retail) e++;
	g_by_ours_n = (unsigned)(e - k);
	g_by_ours = (struct known_function *)malloc(g_by_ours_n * sizeof(*g_by_ours));
	if (!g_by_ours) { g_by_ours_n = 0; return; }
	memcpy(g_by_ours, k, g_by_ours_n * sizeof(*g_by_ours));
	qsort(g_by_ours, g_by_ours_n, sizeof(*g_by_ours), by_ours);
}

/* a fault inside one of the mod's guarded calls (it skips that frame's work, as on the console): the first few go to the log */
extern DWORD sg_guard_faults, sg_guard_where, sg_guard_touched;
void sg_guard_note(void)
{
	static unsigned said;
	if (said < 6) { said++; h2n_log("Spooky Ghost Menu: a guarded read faulted at %08lx touching %08lx (caught; %lu so far)\n", sg_guard_where, sg_guard_touched, sg_guard_faults); }
}
void sg_init(void)                             /* main.c: once, after the XBE's code is in place */
{
	const struct sg_site *s;
	unsigned done = 0, other = 0, v = h2n_v15 ? 1 : 0;
	sg_on = h2n_env_int("H2N_SPOOKY", 0) ? 1 : 0;
	if (!sg_on) { h2n_log("Spooky Ghost Menu: off (H2Classic Settings)\n"); return; }
	xbe_code_table();
	if (h2n_retail_code_loaded())
		for (s = g_sg_sites; s->routine; s++)
		{
			BYTE *at = (BYTE *)(ULONG_PTR)s->site[v];
			*s->orig = s->target[v];
			/* the mod's own check: the call (or the tail jump) of that function, and nothing else */
			if ((at[0] == 0xe8 || at[0] == 0xe9) && s->site[v] + 5 + *(DWORD *)(at + 1) == s->target[v])
			{
				*(DWORD *)(at + 1) = (DWORD)(ULONG_PTR)s->routine - (s->site[v] + 5);
				done++;
			}
			else { other++; h2n_log("Spooky Ghost Menu: the XBE's code at %08lx is not the call %s expects - left alone\n", s->site[v], s->name); }
		}
	FlushInstructionCache(GetCurrentProcess(), NULL, 0);
	h2n_log("Spooky Ghost Menu v2.0.40 (H2Classic port): on, game version %s; %u call sites in the XBE's own code hooked, %u not as expected\n",
		h2n_v15 ? "1.5" : "1.0", done, other);
}

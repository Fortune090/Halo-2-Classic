/* H2Classic: the Spooky Ghost Menu's calls into the game.
 *
 * In the console build these are small assembly routines (hooks.S) that put the arguments where the XBE's link-time code
 * wants them (eax, esi, ebx ...) and call the function by address. Here the game's functions are this build's own and
 * take their arguments as the decompilation declares them, so each routine is the same call written in C: the function
 * by its own name, the mod's arguments in the declared places. The names and the C prototypes are the mod's (spooky.c
 * declares them); which argument is which is from each function's source in the decompilation, named beside it. */
typedef unsigned char u8; typedef unsigned short u16; typedef unsigned int u32; typedef signed short s16; typedef signed int s32; typedef signed char s8;
#define SG_FAST __attribute__((fastcall))
#define SG_STD __attribute__((stdcall))
#define SG_GAME(name) __asm__(name)

/* ---- drawing: the game's own state setters, then the layer's Direct3D ---- */
void SG_FAST game_render_state(u32 state, u32 value) SG_GAME("?function_0222d0@@YIXW4_D3DRENDERSTATETYPE@@K@Z");   /* 0x222d0 (state, value) */
u32 SG_FAST game_vs_select(s32 index) SG_GAME("?function_1cc30@@YIKJ@Z");                                          /* 0x1cc30 */
void SG_FAST game_ps_set(const void *definition) SG_GAME("?function_15180@@YIXPBU_D3DPixelShaderDef@@@Z");         /* 0x15180 */
/* the register already with Direct3D's 96 added and the count in floats: what the XBE's 0x3f7410 takes and the mod passes */
void SG_FAST d3d_vs_constants(s32 reg, const void *data, u32 floats) SG_GAME("@D3DDevice_SetVertexShaderConstantNotInline@12");
void gfx_render_state(u32 state, u32 value) { game_render_state(state, value); }
void gfx_vs_select(u32 index) { game_vs_select((s32)index); }
void gfx_ps_set(const void *program) { game_ps_set(program); }
void gfx_vs_constants(u32 first, const void *data, u32 dwords) { d3d_vs_constants((s32)first, data, dwords); }
u32 gfx_call0(u32 fn) { return ((u32 (*)(void))(unsigned long)fn)(); }
void gfx_call1(u32 fn, u32 a) { ((void SG_STD (*)(u32))(unsigned long)fn)(a); }
void gfx_call3(u32 fn, u32 a, u32 b, u32 c) { ((void SG_STD (*)(u32, u32, u32))(unsigned long)fn)(a, b, c); }
u32 game_call_eax(u32 fn, u32 a) { return (u32)(u8)((s8 SG_FAST (*)(u32))(unsigned long)fn)(a); }

/* ---- objects ---- */
u8 SG_FAST game_e4050(s32 object_index) SG_GAME("?function_e4050@@YI_NJ@Z");
void SG_FAST game_ba1d0(s32 object_index, float *linear_velocity, float *angular_velocity) SG_GAME("?function_ba1d0@@YIXJPATvector3f@@0@Z");
void SG_STD game_b77d0(s32 object_index, const float *linear_velocity, const float *angular_velocity) SG_GAME("?function_b77d0@@YGXJPBTvector3f@@0@Z");
void SG_STD game_b9b90(s32 object_index, s32 disable) SG_GAME("?function_b9b90@@YGXJ_N@Z");
void *SG_FAST game_b9ef0(s32 object_index, void *result) SG_GAME("?function_b9ef0@@YIPATpoint3f@@JPAT1@@Z");
void SG_FAST game_dc0a0(s32 object_index) SG_GAME("?function_dc0a0@@YIXJ@Z");
s32 spooky_airborne(u32 object) { return game_e4050((s32)object) != 0; }
void spooky_get_vel(u32 object, float *v) { game_ba1d0((s32)object, v, 0); }                 /* no angular */
void spooky_set_vel(u32 object, const float *v) { game_b77d0((s32)object, v, 0); }           /* no angular */
void spooky_wake(u32 object) { game_b9b90((s32)object, 0); }
void cam_marker_call(u32 datum, u32 place) { game_b9ef0((s32)datum, (void *)(unsigned long)place); }
void SG_STD sg_kill(u32 object_index) { game_dc0a0((s32)object_index); }
/* game_set_unit(fn, player, unit): function_14cad0(player_index, unit_index) - fn is that function (sg_port.h) */
void game_set_unit(void *fn, u32 player, u32 unit) { ((void SG_FAST (*)(s32, s32))fn)((s32)player, (s32)unit); }

/* ---- trigger volumes ---- */
u8 SG_FAST game_11c380(s32 trigger_volume_index, float *matrix) SG_GAME("?function_11c380@@YI_NJPAUtransform4x3f@@@Z");
s32 tv_matrix_call(s32 index, float *m) { return game_11c380(index, m) != 0; }

/* ---- Object Spawning (H2EK's Drop Objects): the game's own creation and AI calls ---- */
void SG_FAST game_b7930(void *data, s32 tag_index, s32 object_index, const void *owner) SG_GAME("?function_b7930@@YIXPAXJJPBUs_effect_owner@@@Z");
s32 SG_STD game_b7b40(void *creation) SG_GAME("?function_b7b40@@YGJPAX@Z");
void SG_FAST game_1e04a0(s32 primary_tag, s32 character_index, s32 unit_index, s32 secondary_tag, s32 grenade_type) SG_GAME("?function_1e04a0@@YIXJJJJF@Z");
s32 SG_STD game_1e0160(s32 squad_index, s32 entry_index, s32 unit_index, s32 flag) SG_GAME("?function_1e0160@@YGJJJJ_N@Z");
u8 SG_FAST game_1e11b0(s32 actor_index, s32 active) SG_GAME("?function_1e11b0@@YI_NJ_N@Z");
void SG_STD game_202e90(s32 arg_0, s32 arg_1, s32 arg_2) SG_GAME("?function_202e90@@YGXJFJ@Z");
void SG_FAST game_204450(s32 definition_index, s32 squad_index) SG_GAME("?function_204450@@YIXJJ@Z");
u8 SG_FAST game_e6900(s32 unit_index, void *request) SG_GAME("?function_e6900@@YI_NJPAUs_unit_request@@@Z");
void os_place_call(void *rec, u32 tag) { game_b7930(rec, (s32)tag, -1, 0); }                 /* (the record, the tag, no object, no owner) */
u32 os_new_call(void *rec) { return (u32)game_b7b40(rec); }
void os_equip_call(u32 weapon, u32 character, u32 unit, u32 weapon2) { game_1e04a0((s32)weapon, (s32)character, (s32)unit, (s32)weapon2, -1); }   /* no grenade type */
u32 os_actor_call(u32 squad, u32 character, u32 unit, u32 flag) { return (u32)game_1e0160((s32)squad, (s32)character, (s32)unit, (s32)(flag & 0xff)); }
void os_actor_on_call(u32 actor) { game_1e11b0((s32)actor, 1); }
void os_order_call(u32 squad, u32 order) { game_202e90((s32)squad, (s32)(s16)order, 0); }    /* the console build: the order in bx; the squad and 0 on the stack */
void os_follow_call(u32 order, u32 squad) { game_204450((s32)order, (s32)squad); }
void os_request_call(u32 unit, void *request) { game_e6900((s32)unit, request); }

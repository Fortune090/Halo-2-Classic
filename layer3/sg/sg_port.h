/* H2Classic: the Spooky Ghost Menu (v2.0.40) inside the native program - what stands where the console build had
 * addr_v10.h / addr_v15.h and its calls by address.
 *
 * On the console the mod is a section appended to the XBE; it reaches the game by address. Here:
 *  - the game's DATA is at the XBE's own addresses in both game versions, so every data address is the mod's own, picked
 *    by version when it is used (sg_addr.h, generated from the mod's two headers: SGA(v1.0, v1.5));
 *  - the game's FUNCTIONS are this build's compiled functions (or lead to the XBE's code where the build runs that), which
 *    take their arguments as the decompilation declares them, not as the XBE's link-time code does. Each one the mod
 *    calls is declared below under its own name, and the mod's calls are written with the declared arguments;
 *  - the XBE's Direct3D is never started: drawing goes through the layer's Direct3D (xd3d.cpp), as the game's own does;
 *  - files go through the layer's file calls (xapi.c): T:, U:, Z: are folders under .\h2classic\, D: is the game folder
 *    and is never written.
 */
#ifndef SG_PORT_H
#define SG_PORT_H
#define SG_H2CLASSIC 1
/* what the port has so far (sgport/edits/edit4.py): 0 = its menu rows and shortcuts are out of reach, its code stays */
#define SG_CAMERAS 1       /* third person, free camera, Field of View, Hide HUD, Centered Crosshair */
#define SG_TRIGGERS 1      /* Trigger Volume Overlay */
#define SG_WIREFRAME 1     /* Wireframe (on: white lines over the picture; xray) */
extern int h2n_v15;                                  /* main.c: 0 = game version 1.0, 1 = 1.5 */
#define SGA(v10, v15) (h2n_v15 ? (v15) : (v10))
#include "sg_addr.h"
#define ADDR_VERSION_TEXT (h2n_v15 ? "v1.5" : "v1.0")
#define H2_VERSION_TEXT ADDR_VERSION_TEXT

#define SG_FAST __attribute__((fastcall))
#define SG_STD __attribute__((stdcall))
#define SG_GAME(name) __asm__(name)

/* ---- the layer (H2Classic's own code) ---- */
void *memset(void *d, int c, unsigned int n);
void *memcpy(void *d, const void *s, unsigned int n);
void h2n_log(const char *format, ...);
extern short h2n_refresh;                          /* timing.c: the refresh rate the game is given */
extern unsigned char h2n_pace_clamp;               /* timing.c: 0 while frames are not held to 30 a second */
int h2n_unlock_wanted(void);
extern int sg_on;                                    /* sg_hooks.c: the mod is on (H2Classic Settings; off: every hook passes straight through) */
u32 SG_STD sg_XInputGetState(void *handle, xinput_state *st) SG_GAME("_XInputGetState@8");
void *SG_STD sg_CreateFileA(const char *name, u32 access, u32 share, void *security, u32 creation, u32 flags, void *template_file) SG_GAME("_CreateFileA@28");
s32 SG_STD sg_WriteFile(void *file, const void *data, u32 size, u32 *written, void *overlapped) SG_GAME("_WriteFile@20");
s32 SG_STD sg_ReadFile(void *file, void *data, u32 size, u32 *read, void *overlapped) SG_GAME("_ReadFile@20");
u32 SG_STD sg_SetFilePointer(void *file, s32 distance, s32 *high, u32 method) SG_GAME("_SetFilePointer@16");
s32 SG_STD sg_CloseHandle(void *handle) SG_GAME("_CloseHandle@4");
u32 SG_STD sg_GetFileSize(void *file, u32 *high) SG_GAME("_GetFileSize@8");
void *SG_STD sg_VirtualAlloc(void *address, u32 size, u32 type, u32 protect) SG_GAME("_VirtualAlloc@16");
s32 SG_STD sg_VirtualFree(void *address, u32 size, u32 type) SG_GAME("_VirtualFree@12");
#define XInputGetState_real sg_XInputGetState
#define CreateFileA_x sg_CreateFileA
#define WriteFile_x sg_WriteFile
#define ReadFile_x sg_ReadFile
#define SetFilePointer_x sg_SetFilePointer
#define CloseHandle_x sg_CloseHandle
#define GetFileSize_x sg_GetFileSize
#define VirtualAlloc_x sg_VirtualAlloc
#define VirtualFree_x sg_VirtualFree

/* ---- the game's functions, as this build declares them (the decompilation's source; native/funcmap.json) ---- */
void SG_FAST game_text_style_clear(void) SG_GAME("?function_13e8a0@@YIXXZ");                       /* 0x13e8a0 */
void *SG_FAST game_font_get(s32 font) SG_GAME("?font_get@@YIPAUs_font_header@@J@Z");               /* 0x1224c0 */
/* 0x1fa50: function_1fa50(bounds, string, clip, position, line_gap, scale, color, shadow) */
void SG_FAST game_draw_text(const rect16 *bounds, const u16 *string, const void *clip, const void *position, s32 line_gap,
	float scale, s32 color, const void *shadow) SG_GAME("?function_1fa50@@YIXPBUshort_rectangle2d@@PBGPBX2JMJ2@Z");
#define text_style_clear game_text_style_clear
#define font_get game_font_get
#define DRAW_STRING_GLOBALS ((u8 *)ADDR_DRAW_STRING_GLOBALS)
#define FONT_HANDLES ((u32 *)ADDR_FONT_HANDLES)
/* the mod's own text. Wider than 16:9 its letters keep their 16:9 width about the line's placement point: the console
 * build does that at the game's glyph quad (mod_text_sq, v2.0.5); the layer has the same routine for the HUD's messages
 * (xd3d.cpp h2n_hud_text_box: the rectangle and the text state's justification), used here around each line */
void h2n_hud_text_box(int on, const short *rect);
#define DRAW_TEXT(text, bounds, scale) (h2n_hud_text_box(1, (const short *)(bounds)), game_draw_text(bounds, text, 0, 0, 0, scale, 0, 0), h2n_hud_text_box(0, 0))
#define G_485602 (*(volatile u16 *)ADDR_FLAG_485602)   /* render target in use; the string drawer only draws when it is 0 */
#define G_51080C (*(volatile u8 *)ADDR_FLAG_51080C)    /* the string drawer also skips while this is set */
/* a function pointer found in the game's data (a vtable's entry): the address of the XBE's code for it - itself when it
   is in the XBE's code, the XBE's function when it is this build's compiled function; 0 when it is neither (sg_hooks.c) */
u32 sg_xbe_code(u32 function);
float h2n_ws_factor(void);                           /* xapi.c: the widescreen factor the game's projection uses */

/* ---- game functions the mod's C code names by address: this build's functions, called as they are declared ----
 * (gfx_call0 / gfx_call1 / gfx_call3 and game_call_eax - sg_game.c - call what they are given as: no arguments;
 * __stdcall with one; __stdcall with three; __fastcall with one. Each target below is declared that way.) */
u8 SG_FAST game_window_in_use(void) SG_GAME("?window_manager_any_window_in_use@@YI_NXZ");          /* 0x149227 */
u8 SG_FAST game_reconnect_dialog(void) SG_GAME("?function_18f973@@YI_NXZ");                         /* 0x18f973 */
void SG_FAST game_havok_rebuild(s32 object_index) SG_GAME("?function_1c3850@@YIXJ@Z");              /* 0x1c3850 */
extern char game_scripted_camera[] SG_GAME("?function_16c840@@YGXJJPAUs_observer_command@@@Z");     /* 0x16c840 (compared with, never called) */
extern char game_first_person_camera[] SG_GAME("?function_23c110@@YGXPAX0PAUs_observer_command@@@Z");   /* 0x23c110 (the same) */
void SG_STD game_player_effects(s32 local_index, void *matrix) SG_GAME("?function_1546f0@@YGXJPAUtransform4x3f@@@Z");   /* 0x1546f0 */
u8 SG_FAST game_widescreen(void) SG_GAME("?function_016a90@@YIEXZ");                                /* 0x16a90 */
void SG_STD game_vs_cache(void *memory) SG_GAME("?function_1c710@@YGXPAX@Z");                       /* 0x1c710 */
void SG_FAST game_commit(void) SG_GAME("?function_1cf50@@YIXXZ");                                   /* 0x1cf50 */
void SG_STD sg_d3d_begin(u32 primitive_type) SG_GAME("_D3DDevice_Begin@4");                         /* the layer's Direct3D (xd3d.cpp) */
void SG_STD sg_d3d_end(void) SG_GAME("_D3DDevice_End@0");
void SG_STD sg_d3d_vertex2s(s32 reg, s32 a, s32 b) SG_GAME("_D3DDevice_SetVertexData2s@12");
s8 SG_FAST game_deactivation_veto(void *deactivator) SG_GAME("?function_1d1910@@YIDPAVc_1d19d0@@@Z");   /* 0x1d1910 */
void SG_FAST game_squads_refresh(void) SG_GAME("?function_201df0@@YIXXZ");                          /* 0x201df0 */
void SG_STD game_firing_positions_clear(s32 actor_index, u32 reference) SG_GAME("?function_2628f0@@YGXJUs_reference@@@Z");   /* 0x2628f0 */
void SG_FAST game_set_unit_fn(s32 player_index, s32 unit_index) SG_GAME("?function_14cad0@@YIXJJ@Z");   /* 0x14cad0 */
void SG_STD sg_kill(u32 object_index);                                                              /* sg_game.c: 0xdc0a0 */
#define ADDR_UI_WINDOW_IN_USE ((u32)(unsigned long)game_window_in_use)
#define ADDR_RECONNECT_DIALOG ((u32)(unsigned long)game_reconnect_dialog)
#define ADDR_HAVOK_REBUILD ((u32)(unsigned long)game_havok_rebuild)
#define ADDR_SCRIPTED_CAM_FN ((u32)(unsigned long)game_scripted_camera)
#define ADDR_FP_CAM_FN ((u32)(unsigned long)game_first_person_camera)
#define ADDR_PLAYER_FX ((u32)(unsigned long)game_player_effects)
#define ADDR_WIDESCREEN_FN ((u32)(unsigned long)game_widescreen)
#define ADDR_VS_CACHE ((u32)(unsigned long)game_vs_cache)
#define ADDR_COMMIT ((u32)(unsigned long)game_commit)
#define ADDR_BEGIN ((u32)(unsigned long)sg_d3d_begin)
#define ADDR_END ((u32)(unsigned long)sg_d3d_end)
#define ADDR_VERTEX2S ((u32)(unsigned long)sg_d3d_vertex2s)
#define ADDR_DEACT_VETO ((u32)(unsigned long)game_deactivation_veto)
#define ADDR_SQUADS_REFRESH ((u32)(unsigned long)game_squads_refresh)
#define ADDR_FP_CLEAR ((u32)(unsigned long)game_firing_positions_clear)
#define ADDR_SET_UNIT ((u32)(unsigned long)game_set_unit_fn)
#define ADDR_KILL ((u32)(unsigned long)sg_kill)
#define ADDR_SETFILEPOINTER 1      /* (tested for being there) */
#define ADDR_SQUAD_ORDER 1
#define ADDR_UNIT_REQUEST 1
#define ADDR_AFTER_WINDOWS 1
#ifdef SG_PRUNE_PASS   /* (only while pipeline.sh finds the unused code: never in a build) */
#define ADDR_D3D_DEVICE 1
#define ADDR_PUSH_SEG 1
#define ADDR_XLAUNCH 1
#define ADDR_D3D_ADDREF 1
#define ADDR_D3D_RELEASE 1
#define ADDR_D3D_CLEAR 1
#define ADDR_CLEAR_COLOUR 1
#define ADDR_SET_VIEWPORT 1
#define ADDR_TEX_DIRTY 1
#define ADDR_TEX_STATE 1
#define ADDR_TSS 1
#define ADDR_RS_ALPHAFUNC 1
#define ADDR_RS_ALPHATEST 1
#define ADDR_RS_ALPHAREF 1
#define ADDR_PRESENT 1
#define ADDR_KEEPALIVE_DRAW 1
#define ADDR_INF_AMMO_FN 1
#define ADDR_TRAIT_FN 1
#define ADDR_OOB_TEST 1
#define ADDR_HUD_DRAW 1
#define ADDR_D3D_DIRTY 1
#define ADDR_SQUAD_FOLLOW 1
#define ADDR_ASPECT_INSN 1
#endif
#endif

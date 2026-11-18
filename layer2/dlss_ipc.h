/* H2Classic: what the game (32-bit, Direct3D 9Ex) and its DLSS helper (H2Classic_DLSS.exe, 64-bit, Direct3D 11 + NVIDIA
 * NGX) share. DLSS exists only for 64-bit programs, and this exe has to stay 32-bit, so DLSS runs in a second program
 * on the same graphics card: the helper makes the textures, the game opens them by their shared handles and fills them
 * (the view before its HUD, its motion, its depth), the helper runs DLSS into the output texture, and the game draws
 * that into the view before the game's HUD goes on top.
 *
 * One block of shared memory, "Local\h2classic_dlss_<game pid>", two auto-reset events, "..._req_<pid>" (the game has
 * written a request) and "..._rep_<pid>" (the helper has written the reply). Only fixed-size fields: the same layout in
 * both programs. The helper also writes its log lines into a ring here, which the game copies into h2classic_log.txt. */
#ifndef H2N_DLSS_IPC_H
#define H2N_DLSS_IPC_H

#include <stdint.h>

#define DLSS_IPC_MAGIC   0x53534c44u      /* "DLSS" */
#define DLSS_IPC_VERSION 1u
#define DLSS_LOG_RING    16384u

enum { DLSS_STATE_STARTING = 0, DLSS_STATE_READY = 1, DLSS_STATE_FAILED = 2 };
enum { DLSS_CMD_NONE = 0, DLSS_CMD_SETUP = 1, DLSS_CMD_EVALUATE = 2, DLSS_CMD_RELEASE = 3, DLSS_CMD_QUIT = 4 };
enum { DLSS_OK = 0, DLSS_ERR_STATE = -1, DLSS_ERR_TEXTURES = -2, DLSS_ERR_CREATE = -3, DLSS_ERR_EVALUATE = -4, DLSS_ERR_WAIT = -5, DLSS_ERR_SLOT = -6 };

typedef struct dlss_ipc
{
	uint32_t magic, version;
	volatile int32_t state;           /* DLSS_STATE_*, set by the helper */
	int32_t ngx_result;               /* the last NGX result the helper had */
	char message[256];                /* why, when the state is FAILED */
	uint32_t game_luid_low;           /* the game's graphics card (IDirect3D9Ex::GetAdapterLUID) */
	int32_t game_luid_high;
	/* a request: written by the game, then the request event set */
	uint32_t seq, cmd, slot, width, height, reset, flags;   /* flags (DLSS_CMD_SETUP, s10g): bits 0-7 the DLAA render preset, 0 = NVIDIA's default */
	float jitter_x, jitter_y;         /* this frame's jitter in render pixels, as DLSS takes it */
	float mv_scale_x, mv_scale_y;     /* the motion's units to DLSS's pixels (the motion is this frame minus the last, in the view's texture coordinates) */
	float frame_ms;
	/* the reply: written by the helper, then the reply event set */
	uint32_t reply_seq;
	int32_t status, status_ngx;
	uint64_t color, motion, depth, output;   /* shared handles of a slot's textures (DLSS_CMD_SETUP) */
	uint32_t uses_direct;             /* 1: the helper hands the motion and depth to DLSS as the game wrote them (no conversion) */
	uint32_t pad;
	/* the helper's log, for the game's log */
	volatile uint32_t log_written;
	uint32_t log_pad;
	char log[DLSS_LOG_RING];
} dlss_ipc;

#endif

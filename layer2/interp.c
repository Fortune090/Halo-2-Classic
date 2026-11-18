/* H2Classic: Spooky Ghost's in-between frames (the mod's Performance Mode interpolation: 6h.25, 6h.26, 6i.2, 6i.9, 6i.48,
 * 6i.50, 6i.129, 6i.189, 6i.204; and 6i.35's particle fix), only with H2N_UNLOCK.
 *
 * With H2N_UNLOCK the frames come as fast as they are ready, but the game still runs its ticks 30 times a second: at 60
 * frames, every other frame has no tick, and without this it shows the same moment twice - the world moves at 30. As the
 * mod does, each frame is drawn between the last two ticks, by how far the next tick has come (the game time's leftover,
 * +0x10): before each tick (the tick loop's call of one tick, 0x137f70 -> 0x137fe0) the node matrices of everything that
 * can move are kept; before the frame's director (0x12b450 -> 0x1554b0: it builds the cameras) each of those objects' nodes
 * is set between the kept ones and the tick's own, and so are the local players' units' own place, facing, up, looking
 * vector and crouch, and the body of the vehicle they ride (the cameras are built from those); the units' own fields go
 * back before the draw (0x223240) and the nodes after it - the game's next tick sees its own state, its 30 ticks untouched.
 * One tick behind, as any interpolation. Not on the main menu (the game options' +8 3) and not in cutscenes (the frame rate
 * is held to 30 there: H2N_UNLOCK's own rule, h2n_pace_clamp). The mod's rules, kept:
 *  - what is tracked: a biped, vehicle, machine or creature (type 0, 1, 7, 12), anything carried or mounted (a parent), and
 *    anything with a velocity or spin (+0x88..+0x9c); the mod also keeps it within 80 units of the view for the console's
 *    memory, which a PC does not need - everything that moves is tracked;
 *  - the first-person arms and weapon: each local player's two hands' blocks of nodes (the first-person users at
 *    *0x4e9bc8, 0x20cc each: +0xc two of 0x1010 - flags +0 bit 0 active, weapon +4, node count +0x304, nodes +0x308),
 *    not blended across a weapon change (6i.204), their axes' lengths kept (6i.129);
 *  - a node turned more than ~75 degrees between the two ticks keeps the tick's matrix (6i.129: a straight blend that far
 *    shrinks it); an object whose root moved 4 units or more in a tick is shown at once (a jump);
 *  - particle systems (0x174990 -> 0x174a30): on a frame with no tick since the last, a system that spawned at that tick
 *    counts as having spawned again (byte +0xd: bit 1 -> bit 0), so one still building up to its first particle is not
 *    deleted between ticks (6i.35: effects that never showed at 60, the lifts on Midship).
 * Not yet (the mod's later steps): the view's kick (v2.0.24), render-only nodes (v2.0.25), cutscene cameras (v2.0.31),
 * what an object carries - its lights, effects and bounding sphere (v2.0.34). H2N_INTERP=0 turns this off. */
#include <windows.h>
#include <string.h>
#include "h2n.h"

int h2n_unlock_wanted(void);
int h2n_env_int(const char *name, int otherwise);
double h2n_seconds(void);
void *h2n_calloc(unsigned size);
extern unsigned char h2n_pace_clamp;

#define GAME_TIME   0x510C54u   /* the game time globals: +0 active, +4 seconds a tick, +8 the tick, +0x10 how far the next one has come */
#define OBJECTS     0x4E0300u   /* the objects' data array: +0x20 count, +0x24 entry size (12: salt +0, object +8), +0x44 entries */
#define LOCAL_USERS 0x4E8C20u   /* +0xc + 4 u: the player of local user u */
#define PLAYERS     0x4E8C24u   /* the players' data array: unit +0x2c */
#define FP_USERS    0x4E9BC8u   /* the first-person users (see above) */
#define OPTIONS     0x4E6948u   /* the game options: +8 3 on the main menu */
#define HAVOK_COMPONENTS 0x51E9B8u   /* the Havok components' data array (the mod's ADDR_HAVOK_COMPONENTS_PTR): see ip_cam_motion */
#define DIRECTOR_PROCS 0x4E8C50u     /* the director's camera procedure for user 0 (0x140 a user) */
#define CAM_SCRIPT     0x510C6Cu     /* -> the scripted camera's state: +2 its mode (1 point, 2 pan, 3 animation, 4 first person) */
#define CINEMATIC      0x510C50u     /* -> the cinematic globals: +5 a cutscene is playing (up on the main menu's views too) */
#define TAG_INSTANCES  0x4E3B44u     /* -> the tag instances, 16 bytes each: group +0, data +8 */
extern char scripted_camera[] __asm__("?function_16c840@@YGXJJPAUs_observer_command@@@Z");   /* (our 0x16c840: the scripted camera) */
/* the game's own quaternion-to-matrix (0x1421f0) and transform product (0x142a60), as its draw builds render-only nodes */
extern void __attribute__((stdcall)) game_orientation_matrix(void *out, const void *orientation) __asm__("?function_1421f0@@YGXPAUtransform4x3f@@PBUrigid_transform_scaled@@@Z");
extern int __attribute__((fastcall)) game_transform_product(const void *a, const void *b, void *result) __asm__("?function_142a60@@YIHPBUtransform4x3f@@0PAU1@@Z");

static int interp_wanted(void) { static int on = -1; if (on < 0) on = h2n_env_int("H2N_INTERP", 1) ? 1 : 0; return on; }
/* a pointer the game's tables can hold. On the console everything is in its memory (0x80000000 - 0x88000000, the mod's
 * ptr_ok); natively the game also allocates from the PC's heap - Havok's data among it (the Havok components' array
 * sat at 0x09870000 in a test), so the s9n check refused the vehicle camera's body and it was never blended (the jitter
 * in a vehicle). Elsewhere: a region of the process that is committed and readable, looked up once (VirtualQuery) and
 * kept until the next level. */
static struct { DWORD base, end; } g_regions[32];
static int g_region_n, g_region_next;
static int pointer(DWORD p)
{
	MEMORY_BASIC_INFORMATION m;
	int i;
	if (p & 3) return 0;
	if (p >= 0x80000000u && p < 0x88000000u) return 1;
	if (p < 0x10000u) return 0;
	for (i = 0; i < g_region_n; i++) if (p - g_regions[i].base < g_regions[i].end - g_regions[i].base) return 1;
	if (!VirtualQuery((void *)(ULONG_PTR)p, &m, sizeof(m)) || m.State != MEM_COMMIT || (m.Protect & (PAGE_NOACCESS | PAGE_GUARD)) || !(m.Protect & 0xff)) return 0;
	i = g_region_n < 32 ? g_region_n++ : (g_region_next++ & 31);
	g_regions[i].base = (DWORD)(ULONG_PTR)m.BaseAddress; g_regions[i].end = g_regions[i].base + (DWORD)m.RegionSize;
	return 1;
}
#define U32(a) (*(volatile DWORD *)(DWORD)(a))
#define U16(a) (*(volatile WORD *)(DWORD)(a))
#define U8(a) (*(volatile BYTE *)(DWORD)(a))
#define F32(a) (*(volatile float *)(DWORD)(a))

static int ip_on(void)          /* s9o: the main menu and cutscenes too (the mod's own rule is in a game only - see timing.c) */
{
	if (!h2n_unlock_wanted() || !interp_wanted() || h2n_pace_clamp) return 0;
	return pointer(U32(OPTIONS));
}
static BYTE *pool_get(DWORD pool_global, DWORD datum)          /* a data array's entry by its datum, if it is that one */
{
	DWORD pool = U32(pool_global), data, size, index = datum & 0xffff;
	if (datum == 0xffffffffu || !pointer(pool) || index >= U32(pool + 0x20)) return NULL;
	data = U32(pool + 0x44); size = U32(pool + 0x24);
	if (!pointer(data) || !size || size > 0x4000) return NULL;
	if (U16(data + index * size) != (datum >> 16)) return NULL;
	return (BYTE *)(data + index * size);
}
static DWORD object_of(DWORD datum)
{
	BYTE *h = pool_get(OBJECTS, datum);
	DWORD o;
	if (!h) return 0;
	o = U32((DWORD)h + 8);
	return pointer(o) ? o : 0;
}

/* ---- the view's kick and the view's effects between two ticks (the mod's v2.0.24: "a melee at a high frame rate should be
 * a fluid camera shake") ----
 * THE KICK (the melee swing, a weapon's recoil - what the first-person animation does to the view) is the animation's
 * camera node: each tick the game copies that node of the right hand's weapon (its block's node, +0x2094) to the user's
 * first-person state +0x2098 (13 floats) and sets flag 4 at +0 (0x167e0a). Each frame the camera effects (0x16ebf0)
 * multiply the view by it, and the first-person models are drawn by its inverse - the arms stay put while the world
 * swings. The arms' nodes were blended between ticks but this copy was the tick's: the view stepped once a tick. Now it
 * is noted at the snapshot and blended with the right hand's block (same rules: not across a weapon change, a jump, a
 * node turned too far; its axes' lengths kept), put back after the draw.
 * THE EFFECTS (every camera impulse and shake, a landed melee's among them) reach the view through 0x1546f0, called by
 * 0x16ebf0 each frame. How strong each is comes from tick counters (it held for a tick, then jumped) and the shake's
 * direction is drawn at random on every call (a new place every frame instead of 30 times a second). On a frame drawn
 * between two ticks the game's own function is now called once a tick, on an identity, at the first frame after the tick,
 * and the view gets the blend of the last two ticks' results. A frame not drawn between two ticks gets the game's own call. */
static DWORD g_kick_prev[4][13], g_kick_back[4][13], g_kick_cur[4];
static int g_kick_node[4], g_kick_ok[4];
static void kick_take(int u, DWORD fp)
{
	DWORD us = fp + (DWORD)u * 0x20CCu, k;
	g_kick_ok[u] = 0;
	g_kick_node[u] = (int)U32(us + 0x2094);
	if ((U32(us) & 4) && g_kick_node[u] >= 0 && g_kick_node[u] < 64)
	{
		for (k = 0; k < 13; k++) g_kick_prev[u][k] = U32(us + 0x2098 + 4 * k);
		g_kick_ok[u] = 1;
	}
}
static void kick_restore(void)
{
	int u, k;
	for (u = 0; u < 4; u++)
	{
		if (!g_kick_cur[u]) continue;
		for (k = 0; k < 13; k++) U32(g_kick_cur[u] + 4 * k) = g_kick_back[u][k];
		g_kick_cur[u] = 0;
	}
}

/* ---- what is kept before each tick ---- */
#define ENT_MAX 8192
#define NODE_MAX 131072
typedef struct { DWORD datum, first, count, cur, weapon; } ip_ent;      /* cur: the nodes' address while blended (0: not) */
static ip_ent g_ent[ENT_MAX];
static DWORD *g_prev, *g_back;                   /* 13 words a node: before the tick; the real ones while blended */
static int g_ent_n, g_nodes, g_valid, g_blended;
/* (s10j) A LOOPING MACHINE'S WRAP (your HCM AnimationFixes.cpp, fix 2, the Ivory Tower lifts): a device's position
 * (+0x140: what its animation shows, 0 to 1, going round when its definition says it loops - 0x106790, function_11eed0
 * with wrap) runs past 1 and starts again near 0; its pose jumps back a whole travel in one tick, and the blend of the two
 * ticks ran it backward through all of it for that tick. Now a device whose position moved by more than half a cycle in
 * the tick is drawn as the tick left it (and what rides on it, up its parents). */
static float g_phase[ENT_MAX];                   /* by entry: a device's position at the tick before (NaN: not a device) */
static unsigned g_wraps;
static DWORD g_tick;
static float g_alpha;
static unsigned g_left_out, g_jumps, g_turns, g_weapon_changes, g_frames_blended;

/* the local players' units' own fields (6i.48): origin, forward, up, (a unit) looking vector, (a biped) crouch */
static const WORD g_own_off[13] = { 0x64, 0x68, 0x6C, 0x70, 0x74, 0x78, 0x7C, 0x80, 0x84, 0x168, 0x16C, 0x170, 0x2C4 };
typedef struct { DWORD datum, obj; int n; float v[13]; DWORD bk[13], wr[13]; int type; } ip_own;
static DWORD g_own_changed, g_own_changed_off; static int g_own_changed_type = -1;   /* (H2N_INTERP=2: a blended field the game wrote before the draw) */
static ip_own g_own[8];
static int g_own_n, g_own_set;
/* the vehicle camera's body (6i.50): the rigid body's motion (+0x70: its place) of what a local player rides */
typedef struct { DWORD datum, motion; float v[3]; DWORD bk[3]; int set; } ip_hk;
static ip_hk g_hk[4];
static int g_hk_n;
static int g_trace_blend, g_trace_own, g_trace_hk;     /* (H2N_INTERP=3: what this frame's blend did) */
unsigned __cdecl h2n_frames(void);
void __cdecl h2n_perf(int point);
void __cdecl h2n_perf_tick(void);
static LONGLONG g_ip_cost;                       /* (s9q) performance-counter time spent in the in-between frames since the last report */
static LONGLONG ip_clock(void) { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart; }
double h2n_ip_cost_ms(int reset) { LARGE_INTEGER f; double ms; QueryPerformanceFrequency(&f); ms = (double)g_ip_cost * 1000.0 / (double)f.QuadPart; if (reset) g_ip_cost = 0; return ms; }

static int g_frame_blended;                      /* this frame is drawn between two ticks (the mod's g_ip_frame) */

/* ---- render-only nodes (the mod's v2.0.25: the cutscene jaws and fingers, and others' hands at high frame rates) ----
 * A model marks some nodes RENDER-ONLY (its 'hlmt' node set at +0xA4: fingers, a jaw). The tick does not animate them:
 * it builds every node from the default pose and the animation for all nodes BUT those, and sets bit 30 of the object's
 * +4 - "render-only nodes not yet animated". The draw does it (0x137800 -> 0xbd610): the first time the object is drawn
 * with bit 30 set, it clears the bit, samples those nodes into the object's orientations (+0x112, 0x20 each) and builds
 * their matrices from their parents as they are in memory then. Put back after each in-between frame, they were drawn
 * in the default pose on every later frame of the tick - fingers and jaws flicking between the animation and the
 * default pose once a tick. Now, on a frame where the game has already animated them this tick (bit 30 clear), each
 * render-only node of a blended object is built again exactly as the draw builds it - with the game's own two functions:
 * its parent's matrix (blended) times its sampled orientation, parents first. Put back after the draw like every node. */
static unsigned g_ro_objects, g_ro_nodes;
static DWORD tag_data(DWORD index, DWORD group)
{
	DWORD tab = U32(TAG_INSTANCES), e, d;
	if (!pointer(tab) || index == 0xffffffffu) return 0;
	e = tab + (index & 0xffff) * 16;
	if (group && U32(e) != group) return 0;
	d = U32(e + 8);
	return pointer(d) ? d : 0;
}
static struct { DWORD obj, cur, count; } g_ro_check[64];   /* (H2N_INTERP=3: objects the game's draw animates this frame, to compare) */
static int g_ro_check_n;
static unsigned g_ro_checked, g_ro_differ;
static float g_ro_worst;
static void render_only_build(DWORD obj, DWORD cur, DWORD count, float *check);
static void render_only(DWORD obj, DWORD cur, DWORD count)
{
	if (U32(obj + 4) & 0x40000000u)                                           /* not yet animated this tick: the game's draw does it */
	{
		if (g_ro_check_n < 64) { g_ro_check[g_ro_check_n].obj = obj; g_ro_check[g_ro_check_n].cur = cur; g_ro_check[g_ro_check_n].count = count; g_ro_check_n++; }
		return;
	}
	render_only_build(obj, cur, count, NULL);
}
/* (H2N_INTERP=3) after the draw: what the game built for those objects against what render_only_build makes of the same */
static void render_only_check(void)
{
	int i;
	for (i = 0; i < g_ro_check_n; i++)
		if (!(U32(g_ro_check[i].obj + 4) & 0x40000000u)) render_only_build(g_ro_check[i].obj, g_ro_check[i].cur, g_ro_check[i].count, &g_ro_worst);
	g_ro_check_n = 0;
}
static void render_only_build(DWORD obj, DWORD cur, DWORD count, float *check)
{
	DWORD def, model, rmodel, rnodes, ori, mask[8], any = 0, k, lim;
	unsigned built = 0;
	if ((short)U16(obj + 0x112) == -1) return;                                /* no orientations kept */
	def = tag_data(U32(obj), 0);
	model = def ? tag_data(U32(def + 0x38), 0x686C6D74u) : 0;                 /* 'hlmt' */
	if (!model) return;
	for (k = 0; k < 8; k++) { mask[k] = U32(model + 0xA4 + 4 * k); any |= mask[k]; }
	if (!any) return;                                                         /* no render-only nodes */
	rmodel = tag_data(U32(model + 4), 0x6D6F6465u);                           /* 'mode' */
	if (!rmodel || !pointer(rnodes = U32(rmodel + 0x4C))) return;
	lim = (DWORD)(int)(short)U16(obj + 0x110) / 0x20u;                        /* the game's bound: the orientations' count */
	if (lim > count) lim = count;
	if (lim > U32(rmodel + 0x48)) lim = U32(rmodel + 0x48);
	if (lim > 256) lim = 256;
	ori = obj + (DWORD)(int)(short)U16(obj + 0x112);
	for (k = 1; k < lim; k++)
	{
		float local[13], out[13];
		int p;
		if (!(mask[k >> 5] & (1u << (k & 31)))) continue;
		p = (short)U16(rnodes + k * 0x60u + 4);
		if (p < 0 || (DWORD)p >= k) continue;                                 /* (a parent always comes first) */
		game_orientation_matrix(local, (const void *)(ori + k * 0x20u));
		game_transform_product((const void *)(cur + (DWORD)p * 0x34u), local, out);
		if (check)
		{
			int q, differ = 0;
			static int told;
			for (q = 0; q < 13; q++) { float d = out[q] - F32(cur + k * 0x34u + 4u * (DWORD)q); if (d < 0) d = -d; if (d > *check) *check = d; if (d > 1e-4f) differ = 1; }
			g_ro_checked++; g_ro_differ += (unsigned)differ;
			if (differ && told < 24)
			{
				told++;
				h2n_log("ipr differ: object %08lx type %u tag %08lx node %lu of %lu (parent %d%s) game %.4f %.4f %.4f %.4f | %.4f %.4f %.4f  ours %.4f %.4f %.4f %.4f | %.4f %.4f %.4f\n",
					obj, U8(obj + 0xAA), U32(obj), k, lim, p, (mask[p >> 5] & (1u << (p & 31))) ? ", render-only" : "",
					F32(cur + k * 0x34u), F32(cur + k * 0x34u + 4), F32(cur + k * 0x34u + 8), F32(cur + k * 0x34u + 12),
					F32(cur + k * 0x34u + 0x28), F32(cur + k * 0x34u + 0x2C), F32(cur + k * 0x34u + 0x30), out[0], out[1], out[2], out[3], out[10], out[11], out[12]);
			}
			continue;
		}
		memcpy((void *)(cur + k * 0x34u), out, sizeof(out));
		built++;
	}
	if (built) { g_ro_objects++; g_ro_nodes += built; }
}

/* ---- the scripted camera between its ticks (the mod's v2.0.31): cutscenes, and the main menu's views ----
 * A cutscene's view is made by the scripted camera (the director's camera procedure for the user is 0x16c840). In its
 * animation mode (3) it samples the camera's animation at the game's time in whole ticks, in its pan mode (2) the pan's
 * profile at the tick, and the observer takes it as it is: the camera moved 30 times a second while the objects were
 * drawn between two ticks. So, for those two modes, the camera as the first frame after each tick has it is kept, and a
 * frame drawn between two ticks gets the camera between the last two, by the fraction the objects are drawn at - the
 * game's camera now, as this tick's turned to the blend (what moved it within the tick kept on top). The game's own
 * record is not written: the view is built from a copy. Not across a cut: a new camera command (its mode, object, or
 * its animation's or pan's start), a move of 4 units or more or a turn of 35 degrees or more in one tick, or a field of
 * view changed by half again, is shown at once. The point (1) and first-person (4) modes are left as they are. */
#define CC_CUT_D2 16.0f
#define CC_CUT_DOT 0.819f
typedef struct { float v[10]; DWORD tick, id; } ip_cc;   /* position, forward, up, field of view; the tick; the command */
static ip_cc g_cc[4][2];                                   /* [0] the tick before's camera, [1] this tick's */
static int g_cc_n[4];
static unsigned g_cc_blends, g_cc_cuts;
static DWORD g_cc_cut_tick[4];
static __attribute__((aligned(16))) BYTE g_view[4][0x358];   /* the copy a blended view is built from */
static int cine_cam(int user)                    /* this user's view is the scripted camera's, in its pan (2) or animation (3) mode */
{
	DWORD st, proc;
	int mode;
	if (user < 0 || user > 3) return 0;
	proc = U32(DIRECTOR_PROCS + (DWORD)user * 0x140u);
	if (proc != (DWORD)(ULONG_PTR)scripted_camera && proc != 0x16C840u) return 0;
	st = U32(CAM_SCRIPT);
	if (!pointer(st)) return 0;
	mode = (short)U16(st + 2);
	return mode == 2 || mode == 3 ? mode : 0;
}
static DWORD cine_id(void)                       /* the scripted camera's command: its mode, its object, the mode's own */
{
	static const BYTE off[7] = { 0x3C, 0x40, 0x44, 0x48, 0x4C, 0x58, 0x5C };
	DWORD st = U32(CAM_SCRIPT), h = 0x811C9DC5u, k;
	if (!pointer(st)) return 0;
	h = (h ^ (DWORD)U16(st + 2)) * 0x01000193u;
	for (k = 0; k < 7; k++) h = (h ^ U32(st + off[k])) * 0x01000193u;
	return h;
}
static void cine_sample(int user, DWORD cam)     /* each frame at the view build: the first after a tick keeps the camera */
{
	DWORD time = U32(GAME_TIME), now, k;
	ip_cc *c;
	if (user < 0 || user > 3) return;
	if (!cine_cam(user) || !pointer(time)) { g_cc_n[user] = 0; return; }
	now = U32(time + 8);
	if (g_cc_n[user] && g_cc[user][1].tick == now) return;                    /* this tick's is held */
	if (g_cc_n[user] && g_cc[user][1].tick + 1u == now) { g_cc[user][0] = g_cc[user][1]; g_cc_n[user] = 2; }
	else g_cc_n[user] = 1;                                                    /* the first, or a tick went by with no frame drawn */
	c = &g_cc[user][1];
	for (k = 0; k < 3; k++) { c->v[k] = F32(cam + 4 * k); c->v[3 + k] = F32(cam + 0x20 + 4 * k); c->v[6 + k] = F32(cam + 0x2C + 4 * k); }
	c->v[9] = F32(cam + 0x4C);
	c->tick = now; c->id = cine_id();
	for (k = 0; k < 10; k++) if (!(c->v[k] == c->v[k] && c->v[k] < 1e9f && c->v[k] > -1e9f)) { g_cc_n[user] = 0; return; }
}
static float cc_dot(const float *a, const float *b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
static int cc_axes(const float *f, const float *u, float *m)   /* forward, left, up (m, m + 3, m + 6): unit and square to each other */
{
	float l;
	int k;
	for (k = 0; k < 3; k++) { m[k] = f[k]; m[6 + k] = u[k]; }
	l = cc_dot(m, m); if (!(l > 1e-8f)) return 0;
	l = 1.0f / __builtin_sqrtf(l); for (k = 0; k < 3; k++) m[k] *= l;
	l = cc_dot(m + 6, m); for (k = 0; k < 3; k++) m[6 + k] -= l * m[k];
	l = cc_dot(m + 6, m + 6); if (!(l > 1e-8f)) return 0;
	l = 1.0f / __builtin_sqrtf(l); for (k = 0; k < 3; k++) m[6 + k] *= l;
	m[3] = m[7] * m[2] - m[8] * m[1]; m[4] = m[8] * m[0] - m[6] * m[2]; m[5] = m[6] * m[1] - m[7] * m[0];   /* up x forward */
	return 1;
}
static void cc_nlerp(float *r, const float *p, const float *c, float a)   /* the mod's ip_nlerp: a unit direction between two */
{
	float l = 0.0f;
	int k;
	if (p[0] == c[0] && p[1] == c[1] && p[2] == c[2]) { r[0] = c[0]; r[1] = c[1]; r[2] = c[2]; return; }
	for (k = 0; k < 3; k++) { r[k] = p[k] + (c[k] - p[k]) * a; l += r[k] * r[k]; }
	if (!(l > 1e-6f)) { r[0] = c[0]; r[1] = c[1]; r[2] = c[2]; return; }
	l = 1.0f / __builtin_sqrtf(l);
	for (k = 0; k < 3; k++) r[k] *= l;
}
static DWORD cine_view(int user, DWORD cam)      /* the scripted camera between its last two ticks (else the game's, as it is) */
{
	const ip_cc *p, *c;
	float a = g_alpha, d2 = 0.0f, bf[3], bu[3], B[9], C[9], rf[3], ru[3], o[10];
	DWORD time = U32(GAME_TIME), b, k;
	if (user < 0 || user > 3 || !g_frame_blended || g_cc_n[user] < 2 || !pointer(time)) return cam;
	p = &g_cc[user][0]; c = &g_cc[user][1];
	if (c->tick != U32(time + 8)) return cam;                                 /* (not this tick's) */
	for (k = 0; k < 3; k++) d2 += (c->v[k] - p->v[k]) * (c->v[k] - p->v[k]);
	if (p->id != c->id || !(d2 < CC_CUT_D2) || !(cc_dot(p->v + 3, c->v + 3) > CC_CUT_DOT) || !(cc_dot(p->v + 6, c->v + 6) > CC_CUT_DOT)
		|| !(p->v[9] < c->v[9] * 1.5f && c->v[9] < p->v[9] * 1.5f))
	{
		if (g_cc_cut_tick[user] != c->tick) { g_cc_cut_tick[user] = c->tick; g_cc_cuts++; }
		return cam;                                                           /* a cut: shown at once */
	}
	cc_nlerp(bf, p->v + 3, c->v + 3, a); cc_nlerp(bu, p->v + 6, c->v + 6, a);   /* the camera between the two */
	if (!cc_axes(bf, bu, B) || !cc_axes(c->v + 3, c->v + 6, C)) return cam;
	for (k = 0; k < 3; k++) { rf[k] = F32(cam + 0x20 + 4 * k); ru[k] = F32(cam + 0x2C + 4 * k); }
	for (k = 0; k < 3; k++)                                                   /* the game's camera now, as this tick's turned */
	{                                                                         /* to the blend: what moved it within the tick kept */
		o[k] = F32(cam + 4 * k) + (p->v[k] - c->v[k]) * (1.0f - a);
		o[3 + k] = B[k] * cc_dot(rf, C) + B[3 + k] * cc_dot(rf, C + 3) + B[6 + k] * cc_dot(rf, C + 6);
		o[6 + k] = B[k] * cc_dot(ru, C) + B[3 + k] * cc_dot(ru, C + 3) + B[6 + k] * cc_dot(ru, C + 6);
	}
	o[9] = F32(cam + 0x4C) + (p->v[9] - c->v[9]) * (1.0f - a);
	for (k = 0; k < 10; k++) if (!(o[k] == o[k] && o[k] < 1e9f && o[k] > -1e9f)) return cam;
	b = (DWORD)(ULONG_PTR)g_view[user];
	memcpy((void *)b, (const void *)cam, 0x358);
	for (k = 0; k < 3; k++) { F32(b + 4 * k) = o[k]; F32(b + 0x20 + 4 * k) = o[3 + k]; F32(b + 0x2C + 4 * k) = o[6 + k]; }
	F32(b + 0x4C) = o[9];
	g_cc_blends++;
	return b;
}
/* ---- the first-person legs while turning (the mod's 6h.26 / 6i.15 / 6i.30: "the legs jitter when looking down and
 * turning") ----
 * The first-person body (the legs you see looking down) is the unit's own nodes, copied as they are by the first-person
 * setup (0x3d2c0 -> 0x165f16). Those are blended between the last two ticks, but the view turns every frame (the turn
 * is a frame's, real time, 0x185be0) while the body takes the view's heading once a tick: seen from the view, the legs
 * swung by up to a tick's turn and back each frame. For the first-person build only, on foot, the unit's (blended)
 * nodes are now turned about the camera's upright line by how far the view has turned past the view the ticks were made
 * with - the view's heading on the first frame drawn after each of the last two ticks (6i.30: the game turns the body in
 * a tick by the view of the frame drawn right after it), blended as far as the nodes are - and put back right after the
 * setup. The legs keep the game's own lag behind the view, steadily, at any frame rate. */
#define VIEW_USER 0x4B9ED8u                   /* the player whose view is being drawn (g_4b9ed8) */
static DWORD local_unit(int u, DWORD *parent);
static float g_camp[4][3], g_camf[4][3], g_camu[4][3], g_tcam[4][2][2];
static int g_camok[4], g_tcam_n[4], g_tcam_want[4];
static DWORD g_fpbk[256 * 13], g_fp_at, g_fp_n;
static DWORD g_fp_turned;                     /* frames the legs were turned (H2N_INTERP=2) */
static DWORD g_fp_turned_ext(void) { return g_fp_turned; }
static float g_fp_last;                       /* and the last turn, in degrees */
static int cam_heading(int u, float *h)      /* the camera's heading, level (looking up or down: from its up vector) */
{
	float fz = g_camf[u][2], c = __builtin_sqrtf(g_camf[u][0] * g_camf[u][0] + g_camf[u][1] * g_camf[u][1]), l;
	h[0] = g_camf[u][0] * c - g_camu[u][0] * fz; h[1] = g_camf[u][1] * c - g_camu[u][1] * fz;
	l = __builtin_sqrtf(h[0] * h[0] + h[1] * h[1]);
	if (!(l > 1e-3f)) return 0;
	h[0] /= l; h[1] /= l;
	return 1;
}
static float at_atan2(float y, float x);
static DWORD g_fp_turned_ext(void);
static int cine_now_ext(void);
static float g_lastframe_h[4][2];             /* (H2N_INTERP=3: the heading of the last frame drawn, for the per-tick "legs tick" lines) */
static void tcam_tick(void)                  /* before each tick (the snapshot): the view the last tick was made with */
{
	int u;
	if (h2n_env_int("H2N_INTERP", 1) >= 3)
	{
		static int lines;
		DWORD parent, unit = local_unit(0, &parent), obj = unit != 0xffffffffu ? object_of(unit) : 0;
		if (obj && parent == 0xffffffffu && lines < 3000 && !cine_now_ext())
		{
			lines++;
			h2n_log("       legs tick %u: body forward %.2f, aim %.2f; first frame after the last tick %.2f (%s), last frame %.2f; turned %u\n",
				U32(U32(GAME_TIME) + 8), at_atan2(F32(obj + 0x74), F32(obj + 0x70)) * 57.29578f, at_atan2(F32(obj + 0x16C), F32(obj + 0x168)) * 57.29578f,
				at_atan2(g_tcam[0][1][1], g_tcam[0][1][0]) * 57.29578f, g_tcam_want[0] ? "none since" : "noted",
				at_atan2(g_lastframe_h[0][1], g_lastframe_h[0][0]) * 57.29578f, g_fp_turned_ext());
		}
	}
	for (u = 0; u < 4; u++)
	{
		float h[2];
		if (g_tcam_want[u])                      /* no frame drawn since the last tick: the last frame's view */
		{
			if (g_camok[u] && cam_heading(u, h)) { g_tcam[u][1][0] = h[0]; g_tcam[u][1][1] = h[1]; if (g_tcam_n[u] < 2) g_tcam_n[u]++; }
			else g_tcam_n[u] = 0;
		}
		g_tcam[u][0][0] = g_tcam[u][1][0]; g_tcam[u][0][1] = g_tcam[u][1][1];
		g_tcam_want[u] = 1;
	}
}
static void cam_note(int u, DWORD cam)       /* at the view build: this frame's first-person camera */
{
	int k;
	float h[2];
	for (k = 0; k < 3; k++) { g_camp[u][k] = F32(cam + 4 * k); g_camf[u][k] = F32(cam + 0x20 + 4 * k); g_camu[u][k] = F32(cam + 0x2C + 4 * k); }
	g_camok[u] = 1;
	if (cam_heading(u, h)) { g_lastframe_h[u][0] = h[0]; g_lastframe_h[u][1] = h[1]; }
	if (g_tcam_want[u])                          /* the first frame after a tick: its view */
	{
		if (cam_heading(u, h)) { g_tcam[u][1][0] = h[0]; g_tcam[u][1][1] = h[1]; if (g_tcam_n[u] < 2) g_tcam_n[u]++; }
		g_tcam_want[u] = 0;
	}
}
static float at_atan2(float y, float x);
static float at_deg(float sn, float cs) { return at_atan2(sn, cs) * 57.29578f; }   /* the angle of (cs, sn), in degrees (for the log only) */
/* s9r - the render-only nodes as the game's own first-person setup finds them (your video, 17 Nov: the legs still shaking).
 * The first-person setup (0x3d2c0, from the view's draw 0x2ba10) copies the unit's node matrices as they are into the
 * first-person body. It runs before the unit's own draw (0x137800 -> 0xbd610), so in the game it always finds the
 * render-only nodes (the model's 'hlmt' +0xA4 set) as the tick left them: in the model's default pose, bit 30 of the
 * object's +4 still set - at 30 frames a second every frame is the first after a tick. With the in-between frames, the
 * first frame after a tick was the same, but on the later ones the game's draw had animated them (bit 30 clear) and the
 * render-only stage builds them again from that animation for the draw (v2.0.25): the first-person body showed those
 * parts in the default pose on one frame of each tick and animated on the others - once a tick, at any frame rate above
 * 30. For the first-person setup only, they are now as the game's own setup finds them: the two ticks' poses blended as
 * far as the rest of the body is (what the in-between frame holds before that stage), and put back right after it. */
static int g_render_only_on;
static void fp_render_only_ticked(DWORD obj, DWORD m, DWORD n, const DWORD *pv, const DWORD *bk, float a)
{
	DWORD def = tag_data(U32(obj), 0), model, mask[8], any = 0, k, q;
	model = def ? tag_data(U32(def + 0x38), 0x686C6D74u) : 0;                 /* 'hlmt' */
	if (!model) return;
	for (k = 0; k < 8; k++) { mask[k] = U32(model + 0xA4 + 4 * k); any |= mask[k]; }
	if (!any) return;
	for (k = 1; k < n && k < 256; k++)
	{
		const DWORD *p0 = pv + k * 13u, *p1 = bk + k * 13u;
		DWORD nd = m + k * 0x34u, diff = 0;
		float A[13], B[13], t = 0.0f, sa = 0.0f, sb = 0.0f;
		if (!(mask[k >> 5] & (1u << (k & 31)))) continue;
		for (q = 0; q < 13; q++) diff |= p0[q] ^ p1[q];
		memcpy(A, p0, sizeof(A)); memcpy(B, p1, sizeof(B));
		if (diff) for (q = 1; q <= 9; q++) { t += A[q] * B[q]; sa += A[q] * A[q]; sb += B[q] * B[q]; }
		if (!diff || t < 0.25f * (sa + sb)) { for (q = 0; q < 13; q++) U32(nd + 4 * q) = p1[q]; continue; }   /* as blend_nodes: the tick's */
		for (q = 0; q < 13; q++) F32(nd + 4 * q) = A[q] + (B[q] - A[q]) * a;
	}
}
void __cdecl h2n_fp_before(void)             /* before the first-person setup (0x3d2c0): the legs turned to the view */
{
	int u = (int)U32(VIEW_USER), ei;
	DWORD parent, unit, obj, n, noff, m, k;
	float hx, hy, bx, by, l, cs, sn, px, py, h[2];
	g_fp_n = 0;
	if (u < 0 || u > 3 || !g_frame_blended || !ip_on()) return;
	unit = local_unit(u, &parent);
	if (unit == 0xffffffffu || parent != 0xffffffffu || !(obj = object_of(unit))) return;   /* on foot */
	n = U16(obj + 0x114); noff = U16(obj + 0x116);
	if (!n || n % 0x34 || !noff || noff > 0x4000 || n / 0x34 > 256) return;
	n /= 0x34;
	m = obj + noff;
	for (ei = 0; ei < g_ent_n; ei++) if (g_ent[ei].datum == unit) break;
	if (ei < g_ent_n && g_ent[ei].cur == m && g_ent[ei].count == n && g_render_only_on > 0)   /* blended, and its render-only nodes built again */
	{
		for (k = 0; k < n * 13u; k++) g_fpbk[k] = U32(m + 4 * k);
		g_fp_at = m; g_fp_n = n;                                                 /* (put back after the setup with the rest) */
		fp_render_only_ticked(obj, m, n, g_prev + g_ent[ei].first * 13u, g_back + g_ent[ei].first * 13u, g_alpha);
	}
	if (!g_camok[u] || g_tcam_n[u] < 2) return;
	if (!cam_heading(u, h)) return;
	hx = h[0]; hy = h[1];                                                     /* the view now */
	bx = g_tcam[u][0][0] + (g_tcam[u][1][0] - g_tcam[u][0][0]) * g_alpha;    /* the view the nodes were made with */
	by = g_tcam[u][0][1] + (g_tcam[u][1][1] - g_tcam[u][0][1]) * g_alpha;
	l = __builtin_sqrtf(bx * bx + by * by); if (l < 1e-3f) return; bx /= l; by /= l;
	cs = bx * hx + by * hy; sn = bx * hy - by * hx;                          /* the turn between them */
	{
		static int forced = -1;                     /* (H2N_LEGSTEST=45 or 90, testing only: the body turned by that much, to see what it moves) */
		if (forced < 0) forced = h2n_env_int("H2N_LEGSTEST", 0);
		if (forced == 45) { cs = 0.70710678f; sn = 0.70710678f; } else if (forced == 90) { cs = 0.0f; sn = 1.0f; }
	}
	if (cs > 0.99999f && sn < 1e-5f && sn > -1e-5f) return;
	px = g_camp[u][0]; py = g_camp[u][1];
	g_fp_turned++; g_fp_last = sn;
	if (h2n_env_int("H2N_INTERP", 1) >= 3 && !cine_now_ext())
	{
		static int flines;
		int ei, found = -1, blended = 0;
		for (ei = 0; ei < g_ent_n; ei++) if (g_ent[ei].datum == unit) { found = ei; blended = g_ent[ei].cur == obj + noff; break; }
		if (flines++ < 6000)
			h2n_log("       legs frame: tick %u at %.3f; view %.2f, the ticks' %.2f and %.2f, made with %.2f; turned %.2f; the body %s\n", U32(U32(GAME_TIME) + 8), g_alpha,
				at_atan2(hy, hx) * 57.29578f, at_atan2(g_tcam[u][0][1], g_tcam[u][0][0]) * 57.29578f, at_atan2(g_tcam[u][1][1], g_tcam[u][1][0]) * 57.29578f,
				at_atan2(by, bx) * 57.29578f, at_deg(sn, cs), found < 0 ? "not in the snapshot" : blended ? "blended" : "in the snapshot, not blended this frame");
	}
	if (cs < 0.94f && h2n_env_int("H2N_INTERP", 1) >= 2)                       /* (a turn over 20 degrees: what it was made of) */
	{
		static double at;
		double now = h2n_seconds();
		if (now - at > 1.0)
		{
			at = now;
			h2n_log("       legs: a turn of %.1f degrees at %.2f of a tick: the view's heading now (%.3f %.3f), the ticks' (%.3f %.3f) and (%.3f %.3f) (%d noted); "
				"the camera's forward (%.3f %.3f %.3f) up (%.3f %.3f %.3f); view user %d\n", at_deg(sn, cs), g_alpha, hx, hy,
				g_tcam[u][0][0], g_tcam[u][0][1], g_tcam[u][1][0], g_tcam[u][1][1], g_tcam_n[u],
				g_camf[u][0], g_camf[u][1], g_camf[u][2], g_camu[u][0], g_camu[u][1], g_camu[u][2], u);
		}
	}
	if (!g_fp_n) { for (k = 0; k < n * 13u; k++) g_fpbk[k] = U32(m + 4 * k); g_fp_at = m; g_fp_n = n; }
	for (k = 0; k < n; k++)
	{
		DWORD nd = m + k * 0x34, v;
		float x = F32(nd + 0x28) - px, y = F32(nd + 0x2C) - py;
		F32(nd + 0x28) = px + x * cs - y * sn; F32(nd + 0x2C) = py + x * sn + y * cs;
		for (v = 0x04; v <= 0x1C; v += 0x0C)                                  /* forward, left, up: turned about the upright */
		{
			float vx = F32(nd + v), vy = F32(nd + v + 4);
			F32(nd + v) = vx * cs - vy * sn; F32(nd + v + 4) = vx * sn + vy * cs;
		}
	}
	if (h2n_env_int("H2N_INTERP", 1) >= 3 && !cine_now_ext())             /* (testing: each node's heading less the view's, after the turn) */
	{
		static int nlines;
		char line[420];
		int len = 0;
		float vh = at_atan2(hy, hx) * 57.29578f;
		len += snprintf(line, sizeof(line), " [%08lx, %lu nodes, bit30 %lu]", unit, n, (U32(obj + 4) >> 30) & 1u);
		for (k = 0; k < n && k < 64 && len < 380; k++)
		{
			DWORD nd = m + k * 0x34;
			float d = at_atan2(F32(nd + 0x08), F32(nd + 0x04)) * 57.29578f - vh;
			while (d > 180.0f) d -= 360.0f; while (d < -180.0f) d += 360.0f;
			len += snprintf(line + len, sizeof(line) - (size_t)len, " %.0f", d);
		}
		if (nlines++ < 4000) h2n_log("       legs nodes at %.3f:%s\n", g_alpha, line);
	}
}
void __cdecl h2n_fp_after(void)              /* after the first-person setup: the nodes back as they were */
{
	DWORD k;
	if (!g_fp_n) return;
	for (k = 0; k < g_fp_n * 13u; k++) U32(g_fp_at + 4 * k) = g_fpbk[k];
	g_fp_n = 0;
}
/* the frame's view build (0x2234e0 -> 0x223080, the camera record its last argument): the record it is built from */
const void *__cdecl h2n_ip_view(long user, const void *record)
{
	DWORD cam = (DWORD)(ULONG_PTR)record;
	if (user >= 0 && user <= 3) g_camok[user] = 0;
	if (!record || user < 0 || user > 3 || !h2n_unlock_wanted() || !interp_wanted()) return record;
	if (g_frame_blended) cam_note((int)user, cam);                            /* the first-person camera, for the legs' heading */
	cine_sample((int)user, cam);
	if (g_cc_n[user]) return (const void *)(ULONG_PTR)cine_view((int)user, cam);
	return record;
}
/* the view's effects (0x16ebf0's call of 0x1546f0 - __stdcall (user, the view's adjustment)): see the kick's note */
static float g_fx_e[4][2][13];
static DWORD g_fx_tick[4];
static int g_fx_have[4];
static unsigned g_fx_calls;
static void fx_blend(const float *p, const float *c, float a, float *o)   /* as the nodes' blend, for one transform */
{
	float t = 0.0f, sa = 0.0f, sb = 0.0f;
	int k;
	for (k = 1; k <= 9; k++) { t += p[k] * c[k]; sa += p[k] * p[k]; sb += c[k] * c[k]; }
	if (t < 0.25f * (sa + sb)) { for (k = 0; k < 13; k++) o[k] = c[k]; return; }   /* turned too far: this tick's */
	for (k = 0; k < 13; k++) o[k] = p[k] + (c[k] - p[k]) * a;
	for (k = 1; k <= 7; k += 3)
	{
		float la = __builtin_sqrtf(p[k] * p[k] + p[k + 1] * p[k + 1] + p[k + 2] * p[k + 2]), lb = __builtin_sqrtf(c[k] * c[k] + c[k + 1] * c[k + 1] + c[k + 2] * c[k + 2]);
		float l = __builtin_sqrtf(o[k] * o[k] + o[k + 1] * o[k + 1] + o[k + 2] * o[k + 2]), sc;
		if (!(l > 1e-6f)) continue;
		sc = (la + (lb - la) * a) / l;
		o[k] *= sc; o[k + 1] *= sc; o[k + 2] *= sc;
	}
}
typedef void (__attribute__((stdcall)) *view_effects_fn)(long user, void *matrix);
void __cdecl h2n_view_effects(long user, void *matrix, void *game)
{
	view_effects_fn fn = (view_effects_fn)game;
	DWORD time = U32(GAME_TIME), tick;
	float e[13], v[13], r[13], id[13];
	int k;
	if (user < 0 || user > 3) { fn(user, matrix); return; }
	/* (the mod's v2.0.31) a cutscene's camera is kept here, before its effects: the observer's record (0x358 a user, the
	 * camera at +0xb8 - the record the view is built from) holds the frame's camera as the scripted camera made it */
	if (h2n_unlock_wanted() && interp_wanted()) cine_sample((int)user, 0x4E9C8Cu + (DWORD)user * 0x358u);
	if (!g_frame_blended || !ip_on() || !pointer(time)) { g_fx_have[user] = 0; fn(user, matrix); return; }   /* not between ticks: the game's own */
	tick = U32(time + 8);
	if (!g_fx_have[user] || tick != g_fx_tick[user])                         /* the first frame after a tick: this tick's effects, once */
	{
		int prev = g_fx_have[user] && tick == g_fx_tick[user] + 1;
		if (prev) memcpy(g_fx_e[user][0], g_fx_e[user][1], sizeof(g_fx_e[user][0]));
		for (k = 0; k < 13; k++) id[k] = k == 0 || k == 1 || k == 5 || k == 9 ? 1.0f : 0.0f;   /* an identity */
		fn(user, id);
		g_fx_calls++;
		memcpy(g_fx_e[user][1], id, sizeof(id));
		g_fx_tick[user] = tick; g_fx_have[user] = prev ? 2 : 1;
	}
	if (g_fx_have[user] == 2) fx_blend(g_fx_e[user][0], g_fx_e[user][1], g_alpha, e);
	else memcpy(e, g_fx_e[user][1], sizeof(e));
	memcpy(v, matrix, sizeof(v));
	game_transform_product(v, e, r);                                          /* the adjustment times the effects (the game's 0x142a60) */
	memcpy(matrix, r, sizeof(r));
}
/* (the mod's v2.0.31, its other half) in a cutscene - the cinematic flag, or the scripted camera making the view - an
 * object that cannot move on its own (no velocity, nothing carrying it) is moved by its animation: such an object is
 * taken too when its parts changed since the last snapshot looked at it (a hash of its node matrices, per object slot) */
#define MH_SLOTS 8192
static DWORD g_mh[MH_SLOTS];
static unsigned g_moved_n;
static int moved(DWORD slot, DWORD salt, DWORD src, DWORD words)
{
	DWORD h = 0x811C9DC5u ^ salt, k, was;
	for (k = 0; k < words; k++) h = (h ^ U32(src + 4 * k)) * 0x01000193u;
	h |= 1u;
	if (slot >= MH_SLOTS) return 1;
	was = g_mh[slot]; g_mh[slot] = h;
	return was != h;
}
static int cine_now(void)
{
	DWORD cg = U32(CINEMATIC);
	return (pointer(cg) && U8(cg + 5)) || cine_cam(0);
}

static int cine_now_ext(void) { return cine_now(); }
static DWORD local_unit(int u, DWORD *parent)
{
	DWORD lp = U32(LOCAL_USERS), player, unit, obj;
	BYTE *p;
	*parent = 0xffffffffu;
	if (!pointer(lp)) return 0xffffffffu;
	player = U32(lp + 0xC + 4u * (DWORD)u);
	if (!(p = pool_get(PLAYERS, player))) return 0xffffffffu;
	unit = U32((DWORD)p + 0x2C);
	if (unit == 0xffffffffu || !(obj = object_of(unit))) return 0xffffffffu;
	*parent = U32(obj + 0x14);
	return unit;
}
static DWORD ip_cam_motion(DWORD datum)          /* the root's rigid body's motion (the mod's ip_cam_motion), or 0 */
{
	DWORD obj = object_of(datum), p, comp, bodies, ent, mo;
	int k = 0, idx;
	BYTE *c;
	while (obj && (p = U32(obj + 0x14)) != 0xffffffffu && k++ < 8) obj = object_of(p);
	if (!obj || !(c = pool_get(HAVOK_COMPONENTS, U32(obj + 0xB4)))) return 0;
	comp = (DWORD)c;
	idx = (int)(signed char)U8(comp + 0x18);
	if (idx < 0 || idx >= (int)U32(comp + 0x74) || !pointer(bodies = U32(comp + 0x70))) return 0;
	ent = U32(bodies + (DWORD)idx * 0x60u + 0x40u);
	if (!pointer(ent) || !pointer(mo = U32(ent + 0x3Cu))) return 0;
	return mo;
}
static void own_take(DWORD datum)
{
	DWORD obj = object_of(datum);
	BYTE type;
	int k;
	ip_own *o;
	if (!obj || g_own_n >= 8) return;
	for (k = 0; k < g_own_n; k++) if (g_own[k].datum == datum) return;    /* (two of you in one vehicle) */
	type = U8(obj + 0xAA);
	o = &g_own[g_own_n++];
	o->datum = datum; o->obj = 0;
	o->n = type == 0 ? 13 : type == 1 ? 12 : 9;                            /* a unit: its looking vector too; a biped: its crouch */
	for (k = 0; k < o->n; k++) o->v[k] = F32(obj + g_own_off[k]);
}
static void hk_take(DWORD datum)
{
	DWORD mo = ip_cam_motion(datum);
	int k;
	if (!mo || g_hk_n >= 4) return;
	for (k = 0; k < g_hk_n; k++) if (g_hk[k].motion == mo) return;
	g_hk[g_hk_n].datum = datum; g_hk[g_hk_n].motion = mo; g_hk[g_hk_n].set = 0;
	for (k = 0; k < 3; k++) g_hk[g_hk_n].v[k] = F32(mo + 0x70u + 4u * (DWORD)k);
	g_hk_n++;
}
static int keep(DWORD datum, DWORD src, DWORD n, DWORD weapon)
{
	DWORD k, at;
	if (g_ent_n >= ENT_MAX || g_nodes + (int)n > NODE_MAX) { g_left_out++; return 0; }
	at = (DWORD)g_nodes * 13u;
	for (k = 0; k < n * 13u; k++) g_prev[at + k] = U32(src + 4 * k);
	g_ent[g_ent_n].datum = datum; g_ent[g_ent_n].first = (DWORD)g_nodes; g_ent[g_ent_n].count = n; g_ent[g_ent_n].cur = 0; g_ent[g_ent_n].weapon = weapon;
	g_phase[g_ent_n] = __builtin_nanf("");                                     /* (a device's: set by the snapshot) */
	g_nodes += (int)n; g_ent_n++;
	return 1;
}
static void snapshot(void)
{
	DWORD time = U32(GAME_TIME), pool = U32(OBJECTS), data, count, i, fp, ud[4], up[4];
	int u, pass, cine;
	g_valid = 0;
	if (!ip_on() || !pointer(time) || !pointer(pool)) return;
	if (!g_prev)
	{
		g_prev = (DWORD *)h2n_calloc(NODE_MAX * 13u * 4u);     /* (the layer's own allocation: VirtualAlloc here is the game's) */
		g_back = (DWORD *)h2n_calloc(NODE_MAX * 13u * 4u);
		if (!g_prev || !g_back) return;
		h2n_log("       in-between frames: on (H2N_UNLOCK) - each frame drawn between the last two ticks\n");
	}
	if (U32(time + 8) < g_tick) g_region_n = 0;                               /* a new level: the heap's regions looked up again */
	tcam_tick();                                                              /* the view each tick was made with (6i.15, 6i.30) */
	data = U32(pool + 0x44); count = U32(pool + 0x20);
	if (!pointer(data) || U32(pool + 0x24) != 12 || count > 8192) return;
	g_ent_n = 0; g_nodes = 0; g_own_n = 0; g_hk_n = 0;
	for (u = 0; u < 4; u++)
	{
		ud[u] = local_unit(u, &up[u]);
		if (ud[u] == 0xffffffffu) continue;
		own_take(ud[u]);
		if (up[u] != 0xffffffffu) { own_take(up[u]); hk_take(ud[u]); }   /* seated: what it rides, and the vehicle's body */
	}
	fp = U32(FP_USERS);                                                       /* the first-person arms and weapon (6i.9) */
	if (pointer(fp))
		for (u = 0; u < 4; u++)
		{
			DWORD wi;
			g_kick_ok[u] = 0;
			if (ud[u] == 0xffffffffu) continue;
			kick_take(u, fp);                                                     /* the view's kick, as this tick begins (v2.0.24) */
			for (wi = 0; wi < 2; wi++)
			{
				DWORD w = fp + (DWORD)u * 0x20CCu + 0xCu + wi * 0x1010u, n = U32(w + 0x304);
				if (!(U32(w) & 1) || U32(w + 4) == 0xffffffffu || !n || n > 64) continue;   /* active, armed */
				keep(0xffff0000u | ((DWORD)u << 8) | wi, w + 0x308, n, U32(w + 4));
			}
		}
	cine = cine_now();
	for (pass = 0; pass < 2; pass++)                                          /* everything that can move; then (a cutscene, */
	for (i = 0; i < count && i < 0x10000u; i++)                               /* the main menu) what its animation moved */
	{
		DWORD h = data + i * 12, salt = U16(h), obj, n, off, k, live = 0;
		BYTE type;
		if (!salt) continue;
		obj = U32(h + 8);
		if (!pointer(obj)) continue;
		n = U16(obj + 0x114); off = U16(obj + 0x116);
		if (!n || n % 0x34 || !off || off > 0x4000) continue;
		type = U8(obj + 0xAA);
		live = type == 0 || type == 1 || type == 7 || type == 12 || U32(obj + 0x14) != 0xffffffffu;
		for (k = 0; !live && k < 6; k++) if (U32(obj + 0x88 + 4 * k) & 0x7fffffffu) live = 1;
		if (pass == 0)
		{
			if (live && keep((salt << 16) | i, obj + off, n / 0x34u, 0) && type >= 7 && type <= 9)
				g_phase[g_ent_n - 1] = F32(obj + 0x140);                            /* (s10j) a device: its position now */
			continue;
		}
		if (live || !cine) continue;
		if (moved(i, salt, obj + off, (n / 0x34u) * 13u) && keep((salt << 16) | i, obj + off, n / 0x34u, 0)) g_moved_n++;
	}
	g_tick = U32(time + 8);
	g_valid = 1;
}

/* ---- each frame: blended before the director, put back around the draw ---- */
static void blend_nodes(DWORD cur, const DWORD *pv, const DWORD *bk, DWORD count, float a, int fp)
{
	DWORD n, k;
	for (n = 0; n < count; n++)
	{
		const DWORD *p0 = pv + n * 13u, *p1 = bk + n * 13u;
		DWORD nd = cur + n * 0x34u, diff = 0;
		float A[13], B[13], t = 0.0f, sa = 0.0f, sb = 0.0f;
		for (k = 0; k < 13; k++) diff |= p0[k] ^ p1[k];
		if (!diff) continue;                                                    /* where it was a tick ago */
		memcpy(A, p0, sizeof(A)); memcpy(B, p1, sizeof(B));
		for (k = 1; k <= 9; k++) { t += A[k] * B[k]; sa += A[k] * A[k]; sb += B[k] * B[k]; }
		if (t < 0.25f * (sa + sb)) { g_turns++; continue; }                     /* turned too far: the tick's, as it is */
		for (k = 0; k < 13; k++) F32(nd + 4 * k) = A[k] + (B[k] - A[k]) * a;
		if (fp)
			for (k = 1; k <= 7; k += 3)                                             /* forward, left, up: their lengths kept */
			{
				float la = (float)__builtin_sqrtf(A[k] * A[k] + A[k + 1] * A[k + 1] + A[k + 2] * A[k + 2]);
				float lb = (float)__builtin_sqrtf(B[k] * B[k] + B[k + 1] * B[k + 1] + B[k + 2] * B[k + 2]);
				float v0 = F32(nd + 4 * k), v1 = F32(nd + 4 * k + 4), v2 = F32(nd + 4 * k + 8), l = __builtin_sqrtf(v0 * v0 + v1 * v1 + v2 * v2), s;
				if (!(l > 1e-6f)) continue;
				s = (la + (lb - la) * a) / l;
				F32(nd + 4 * k) = v0 * s; F32(nd + 4 * k + 4) = v1 * s; F32(nd + 4 * k + 8) = v2 * s;
			}
	}
}
static void kick_blend(int u, float a)          /* user u's view kick, a of the way from the tick before's (v2.0.24) */
{
	DWORD fpu = U32(FP_USERS), us, k, same = 1;
	if (u < 0 || u > 3 || !g_kick_ok[u] || !pointer(fpu)) return;
	us = fpu + (DWORD)u * 0x20CCu;
	if (!(U32(us) & 4) || (int)U32(us + 0x2094) != g_kick_node[u]) return;   /* none now, or another node's */
	for (k = 0; k < 13; k++) { g_kick_back[u][k] = U32(us + 0x2098 + 4 * k); if (g_kick_back[u][k] != g_kick_prev[u][k]) same = 0; }
	if (same) return;
	g_kick_cur[u] = us + 0x2098;                                             /* (before writing: put back after the draw) */
	blend_nodes(us + 0x2098, g_kick_prev[u], g_kick_back[u], 1, a, 1);
}
static void nlerp(float *r, const float *p, const float *c, float a)    /* a direction between two (normalised; else c) */
{
	float x = p[0] + (c[0] - p[0]) * a, y = p[1] + (c[1] - p[1]) * a, z = p[2] + (c[2] - p[2]) * a, l = __builtin_sqrtf(x * x + y * y + z * z);
	float lc = __builtin_sqrtf(c[0] * c[0] + c[1] * c[1] + c[2] * c[2]);
	if (!(l > 1e-6f)) { r[0] = c[0]; r[1] = c[1]; r[2] = c[2]; return; }
	l = lc / l; r[0] = x * l; r[1] = y * l; r[2] = z * l;
}
static void restore_nodes(void)
{
	int i;
	DWORD k;
	kick_restore();
	if (!g_blended) return;
	for (i = 0; i < g_ent_n; i++)
	{
		ip_ent *e = &g_ent[i];
		const DWORD *bk = g_back + e->first * 13u;
		if (!e->cur) continue;
		for (k = 0; k < e->count * 13u; k++) U32(e->cur + 4 * k) = bk[k];
		e->cur = 0;
	}
	g_blended = 0;
}
static void own_restore(void)
{
	int i, k;
	for (i = 0; i < g_own_n; i++)
	{
		ip_own *o = &g_own[i];
		if (!o->obj) continue;
		for (k = 0; k < o->n; k++)
		{
			if (U32(o->obj + g_own_off[k]) != o->wr[k]) { g_own_changed++; g_own_changed_off = g_own_off[k]; g_own_changed_type = o->type; }   /* the game wrote it meanwhile */
			U32(o->obj + g_own_off[k]) = o->bk[k];
		}
		o->obj = 0;
	}
	for (i = 0; i < g_hk_n; i++)
	{
		ip_hk *h = &g_hk[i];
		if (!h->set) continue;
		for (k = 0; k < 3; k++) U32(h->motion + 0x70u + 4u * (DWORD)k) = h->bk[k];
		h->set = 0;
	}
	g_own_set = 0;
}
static int g_render_only_on = -1;                 /* (s9q, for comparing: H2N_RENDERONLY=0 leaves the render-only parts to the game's draw; declared above for the legs) */
static DWORD g_wrapped[16], g_wrapped_n;
static int wrapped_ride(DWORD obj)               /* (s10j) the object is a device that went round this tick, or rides on one */
{
	DWORD k, j, par;
	for (k = 0; obj && k < 6; k++)
	{
		for (j = 0; j < g_wrapped_n; j++) if (g_wrapped[j] == obj) return 1;
		par = U32(obj + 0x14);
		obj = par == 0xffffffffu ? 0 : object_of(par);
	}
	return 0;
}
static void blend(void)
{
	DWORD time = U32(GAME_TIME), k;
	int i;
	float a;
	if (g_render_only_on < 0) g_render_only_on = h2n_env_int("H2N_RENDERONLY", 1) ? 1 : 0;
	if (!g_valid || !ip_on() || !pointer(time) || U32(time + 8) != g_tick + 1) return;   /* the snapshot is the tick before */
	a = F32(time + 0x10);
	a = a > 0.0f ? (a < 1.0f ? a : 1.0f) : 0.0f;
	g_alpha = a;
	g_frames_blended++;
	g_frame_blended = 1;
	{                                                                             /* (s10j) devices that went round this tick */
		DWORD wrapped[16], nw = 0;
		for (i = 0; i < g_ent_n; i++)
		{
			DWORD o;
			float d;
			if (g_phase[i] != g_phase[i] || !(o = object_of(g_ent[i].datum))) continue;
			d = F32(o + 0x140) - g_phase[i];
			if ((d > 0.5f || d < -0.5f) && nw < 16)
			{
				static DWORD logged_tick, logged;
				wrapped[nw++] = o;
				if (logged < 12 && logged_tick != g_tick) { logged++; logged_tick = g_tick; h2n_log("       in-between frames: device %08lx (type %u) went round in tick %lu (its position %.3f -> %.3f): drawn as the tick left it\n", g_ent[i].datum, (unsigned)U8(o + 0xAA), g_tick + 1, g_phase[i], F32(o + 0x140)); }
			}
		}
		g_wrapped_n = 0;
		for (k = 0; k < nw; k++) g_wrapped[g_wrapped_n++] = wrapped[k];
	}
	for (i = 0; i < g_ent_n; i++)
	{
		ip_ent *e = &g_ent[i];
		DWORD obj, cur, same = 1, *pv = g_prev + e->first * 13u, *bk = g_back + e->first * 13u;
		float d0, d1, d2;
		int fp = (e->datum >> 16) == 0xffffu;
		e->cur = 0;
		if (fp)
		{
			DWORD fpu = U32(FP_USERS), w;
			if (!pointer(fpu)) continue;
			w = fpu + ((e->datum >> 8) & 3u) * 0x20CCu + 0xCu + (e->datum & 1u) * 0x1010u;
			if (!(U32(w) & 1) || U32(w + 0x304) != e->count) continue;
			if (U32(w + 4) != e->weapon) { g_weapon_changes++; continue; }       /* another weapon in that hand now (6i.204) */
			cur = w + 0x308;
		}
		else
		{
			obj = object_of(e->datum);
			if (!obj || U16(obj + 0x114) != e->count * 0x34u) continue;           /* gone, or another object */
			if (g_wrapped_n && wrapped_ride(obj)) { g_wraps++; continue; }          /* (s10j) a looping device's wrap: as the tick left it */
			cur = obj + U16(obj + 0x116);
		}
		{ float p[3]; memcpy(p, pv + 10, sizeof(p)); d0 = F32(cur + 0x28) - p[0]; d1 = F32(cur + 0x2C) - p[1]; d2 = F32(cur + 0x30) - p[2]; }
		if (!(d0 * d0 + d1 * d1 + d2 * d2 < 16.0f)) { g_jumps++; continue; }    /* a jump of 4 units or more: at once */
		for (k = 0; k < e->count * 13u; k++) { bk[k] = U32(cur + 4 * k); if (bk[k] != pv[k]) same = 0; }
		if (same) continue;                                                       /* still */
		e->cur = cur; g_blended++;
		blend_nodes(cur, pv, bk, e->count, a, fp);
		if (fp && !(e->datum & 1u)) kick_blend((int)((e->datum >> 8) & 3u), a);   /* the right hand's block: the view's kick with it */
		if (!fp && g_render_only_on) render_only(obj, cur, e->count);           /* its render-only nodes, as the draw builds them */
	}
	for (i = 0; i < g_own_n; i++)                                                 /* the units' own fields (6i.48) */
	{
		ip_own *o = &g_own[i];
		DWORD obj = object_of(o->datum);
		float c[13], r[13], dd = 0.0f;
		int same = 1;
		o->obj = 0;
		if (!obj) continue;
		for (k = 0; k < (DWORD)o->n; k++)
		{
			c[k] = F32(obj + g_own_off[k]);
			if (!(c[k] == c[k] && o->v[k] == o->v[k] && c[k] < 1e9f && c[k] > -1e9f)) break;
			if (c[k] != o->v[k]) same = 0;
		}
		if (k < (DWORD)o->n || same) continue;
		for (k = 0; k < 3; k++) dd += (c[k] - o->v[k]) * (c[k] - o->v[k]);
		if (!(dd < 16.0f)) continue;
		for (k = 0; k < 3; k++) r[k] = o->v[k] == c[k] ? c[k] : o->v[k] + (c[k] - o->v[k]) * a;
		nlerp(r + 3, o->v + 3, c + 3, a); nlerp(r + 6, o->v + 6, c + 6, a);
		if (o->n > 9) nlerp(r + 9, o->v + 9, c + 9, a);
		if (o->n > 12) r[12] = o->v[12] == c[12] ? c[12] : o->v[12] + (c[12] - o->v[12]) * a;
		for (k = 0; k < (DWORD)o->n; k++) o->bk[k] = U32(obj + g_own_off[k]);
		o->obj = obj; g_own_set++; o->type = U8(obj + 0xAA);
		for (k = 0; k < (DWORD)o->n; k++) { F32(obj + g_own_off[k]) = r[k]; o->wr[k] = U32(obj + g_own_off[k]); }
	}
	for (i = 0; i < g_hk_n; i++)                                                  /* the vehicle camera's body (6i.50) */
	{
		ip_hk *h = &g_hk[i];
		float c[3], dd = 0.0f;
		h->set = 0;
		if (ip_cam_motion(h->datum) != h->motion) continue;
		for (k = 0; k < 3; k++) { c[k] = F32(h->motion + 0x70u + 4u * k); if (!(c[k] == c[k] && h->v[k] == h->v[k] && c[k] < 1e9f && c[k] > -1e9f)) break; dd += (c[k] - h->v[k]) * (c[k] - h->v[k]); }
		if (k < 3 || !(dd < 16.0f) || dd == 0.0f) continue;
		for (k = 0; k < 3; k++) h->bk[k] = U32(h->motion + 0x70u + 4u * k);
		h->set = 1; g_own_set++;
		for (k = 0; k < 3; k++) F32(h->motion + 0x70u + 4u * k) = h->v[k] + (c[k] - h->v[k]) * a;
	}
	g_trace_blend = 1; g_trace_own = g_own_set;
	for (i = 0, g_trace_hk = 0; i < g_hk_n; i++) if (g_hk[i].set) g_trace_hk++;
}

/* H2N_INTERP=3 (a test): one log line a frame while local player 0 rides something - the frame's tick and blend, what it
 * rides as drawn (its root node) and as the tick has it (its origin), and the camera the frame was drawn from - to see
 * whether the camera and the vehicle it follows are drawn from the same moment */
/* (H2N_INTERP=3, a test) the frame-by-frame cutscene lines are written only for 4 seconds after F9 is pressed (or the key
 * file's "tracewindow"), each press starting their line counts again - so they cover the moment asked for at any frame rate */
static double g_tw_until;
static unsigned g_tw_serial;
void __cdecl h2n_trace_window_start(void)
{
	g_tw_until = h2n_seconds() + 4.0; g_tw_serial++;
	h2n_log("       trace: the next 4 seconds of a cutscene are described frame by frame (F9, H2N_INTERP=3)\n");
}
static int trace_window(unsigned *serial, int *lines)
{
	if (*serial != g_tw_serial) { *serial = g_tw_serial; *lines = 0; }
	return h2n_seconds() < g_tw_until;
}
static void trace_vehicles(void)        /* every 10 s: each vehicle's Havok component and the camera body the mod blends */
{
	static double last;
	DWORD pool = U32(OBJECTS), data, count, i;
	int shown = 0;
	if (h2n_seconds() - last < 10.0 || !pointer(pool)) return;
	last = h2n_seconds();
	h2n_log("ipr render-only nodes the game's draw built, checked against the in-between frames' own: %u checked, %u differ, largest difference %g\n", g_ro_checked, g_ro_differ, g_ro_worst);
	data = U32(pool + 0x44); count = U32(pool + 0x20);
	if (!pointer(data) || count > 8192) return;
	for (i = 0; i < count && shown < 12; i++)
	{
		DWORD h = data + i * 12, salt = U16(h), obj, comp_datum, hpool, entry_salt = 0xffffffffu;
		BYTE *c;
		if (!salt || !pointer(obj = U32(h + 8)) || U8(obj + 0xAA) != 1) continue;
		comp_datum = U32(obj + 0xB4);
		hpool = U32(HAVOK_COMPONENTS);
		if (pointer(hpool) && comp_datum != 0xffffffffu && (comp_datum & 0xffff) < U32(hpool + 0x20) && pointer(U32(hpool + 0x44)))
			entry_salt = U16(U32(hpool + 0x44) + (comp_datum & 0xffff) * U32(hpool + 0x24));
		c = pool_get(HAVOK_COMPONENTS, comp_datum);
		h2n_log("ipv vehicle %08lx parent %08lx havok component %08lx (pool %08lx size %lx, entry salt %04lx) found %d camera body motion %08lx\n",
			(salt << 16) | i, U32(obj + 0x14), comp_datum, hpool, pointer(hpool) ? U32(hpool + 0x24) : 0, entry_salt, c != NULL, ip_cam_motion((salt << 16) | i));
		shown++;
	}
}
extern _Bool __attribute__((fastcall)) game_periodic_layer_active(long object) __asm__("?unit_animation_periodic_layer_active@@YI_NJ@Z");
extern _Bool __attribute__((fastcall)) game_function_10edd0(long object) __asm__("?function_10edd0@@YI_NJ@Z");
static void trace_render_only(void)     /* (H2N_INTERP=3) once per biped or creature: its render-only nodes and the draw's overlay paths */
{
	static DWORD seen[64];
	static int seen_n;
	int i, k;
	for (i = 0; i < g_ent_n && seen_n < 64; i++)
	{
		const ip_ent *e = &g_ent[i];
		DWORD obj, def, model, rmodel, rnodes = 0, mask[8], n, count = 0;
		char list[400];
		int at = 0;
		if ((e->datum >> 16) == 0xffffu || !(obj = object_of(e->datum)) || U8(obj + 0xAA) > 1) continue;
		for (k = 0; k < seen_n; k++) if (seen[k] == e->datum) break;
		if (k < seen_n) continue;
		seen[seen_n++] = e->datum;
		def = tag_data(U32(obj), 0);
		model = def ? tag_data(U32(def + 0x38), 0x686C6D74u) : 0;
		rmodel = model ? tag_data(U32(model + 4), 0x6D6F6465u) : 0;
		if (rmodel && pointer(U32(rmodel + 0x4C))) rnodes = U32(rmodel + 0x4C);
		for (k = 0; k < 8; k++) mask[k] = model ? U32(model + 0xA4 + 4 * k) : 0;
		list[0] = 0;
		for (n = 0; n < e->count && n < 256; n++)
			if (mask[n >> 5] & (1u << (n & 31)))
			{
				count++;
				if (at < 360) at += snprintf(list + at, sizeof(list) - at, " %lu<%d", n, rnodes ? (int)(short)U16(rnodes + n * 0x60u + 4) : -9);
			}
		h2n_log("ipo object %08lx type %u tag %08lx: %lu nodes, %lu render-only:%s; periodic layer %d, 0x10edd0 %d, bit 30 %d\n",
			e->datum, U8(obj + 0xAA), U32(obj), e->count, count, list, (int)game_periodic_layer_active((long)e->datum),
			(int)game_function_10edd0((long)e->datum), (U32(obj + 4) >> 30) & 1);
	}
}
static void trace_cutscene(void)        /* (H2N_INTERP=3) in a cutscene, each frame: one 68-node character's root and last node (a Prophet's headpiece) */
{
	static int lines;
	static unsigned serial;
	static DWORD who;
	static LARGE_INTEGER last;
	LARGE_INTEGER now, f;
	DWORD time = U32(GAME_TIME), obj = 0, nodes;
	int i;
	if (!trace_window(&serial, &lines) || lines >= 3000 || !pointer(time) || !cine_now()) return;
	if (who) obj = object_of(who);
	if (!obj)
		for (i = 0, who = 0; i < g_ent_n; i++)
		{
			DWORD o = (g_ent[i].datum >> 16) == 0xffffu ? 0 : object_of(g_ent[i].datum);
			if (o && U8(o + 0xAA) == 0 && g_ent[i].count == 68) { who = g_ent[i].datum; obj = o; break; }
		}
	if (!obj) return;
	nodes = obj + U16(obj + 0x116);
	QueryPerformanceCounter(&now); QueryPerformanceFrequency(&f);
	lines++;
	h2n_log("ipc %u t=%u a=%.3f b=%d dt=%.2f root %.4f %.4f %.4f n40 %.4f %.4f %.4f n67 %.4f %.4f %.4f cam %.4f %.4f %.4f\n", h2n_frames(), U32(time + 8),
		g_alpha, g_frame_blended, last.QuadPart ? (double)(now.QuadPart - last.QuadPart) * 1000.0 / (double)f.QuadPart : 0.0,
		F32(nodes + 0x28), F32(nodes + 0x2C), F32(nodes + 0x30), F32(nodes + 40 * 0x34 + 0x28), F32(nodes + 40 * 0x34 + 0x2C), F32(nodes + 40 * 0x34 + 0x30),
		F32(nodes + 67 * 0x34 + 0x28), F32(nodes + 67 * 0x34 + 0x2C), F32(nodes + 67 * 0x34 + 0x30), F32(0x4E9C8C), F32(0x4E9C90), F32(0x4E9C94));
	last = now;
}
static void local_of(const float *pn, const float *cn, float *out)   /* (test) part cn's place in part pn's frame */
{
	float r0 = cn[10] - pn[10], r1 = cn[11] - pn[11], r2 = cn[12] - pn[12];
	out[0] = r0 * pn[1] + r1 * pn[2] + r2 * pn[3];
	out[1] = r0 * pn[4] + r1 * pn[5] + r2 * pn[6];
	out[2] = r0 * pn[7] + r1 * pn[8] + r2 * pn[9];
}
static int ent_of(DWORD datum) { int e; for (e = 0; e < g_ent_n; e++) if (g_ent[e].datum == datum) return e; return -1; }
static void trace_children(void)        /* (H2N_INTERP=3, test) in a cutscene, each frame: what is attached to each 68-node character, as drawn */
{
	static int lines;
	static unsigned serial;
	DWORD pool = U32(OBJECTS), time = U32(GAME_TIME), data, count, i;
	if (!trace_window(&serial, &lines) || lines >= 4000 || !cine_now() || !pointer(pool) || !pointer(time)) return;
	data = U32(pool + 0x44); count = U32(pool + 0x20);
	if (!pointer(data) || count > 8192) return;
	for (i = 0; i < count && lines < 4000; i++)
	{
		DWORD h = data + i * 12, salt = U16(h), obj, par, pobj, pn, cn, node, n, k, datum;
		float drawn_c[13], drawn_p[13], lc_d[3], lc_b[3], lc_t[3], worst = 0.0f;
		const float *pv_c = 0, *bk_c = 0, *pv_p = 0, *bk_p = 0;
		int e, pe, ce_blended, pe_blended;
		if (!salt || !pointer(obj = U32(h + 8))) continue;
		par = U32(obj + 0x14);
		if (par == 0xffffffffu || !(pobj = object_of(par)) || U8(pobj + 0xAA) != 0) continue;
		pn = U16(pobj + 0x114) / 0x34u; cn = U16(obj + 0x114) / 0x34u;
		if (pn != 68 || !cn || cn > 64) continue;
		node = (DWORD)(int)(signed char)U8(obj + 0x18);
		if (node >= pn) continue;
		datum = (salt << 16) | i;
		e = ent_of(datum); pe = ent_of(par);
		ce_blended = e >= 0 && g_ent[e].cur != 0; pe_blended = pe >= 0 && g_ent[pe].cur != 0;
		memcpy(drawn_c, (const void *)(obj + U16(obj + 0x116)), sizeof(drawn_c));
		memcpy(drawn_p, (const void *)(pobj + U16(pobj + 0x116) + node * 0x34u), sizeof(drawn_p));
		local_of(drawn_p, drawn_c, lc_d);
		if (e >= 0) { pv_c = (const float *)(g_prev + g_ent[e].first * 13u); bk_c = (const float *)(g_back + g_ent[e].first * 13u); }
		if (pe >= 0) { pv_p = (const float *)(g_prev + (g_ent[pe].first + node) * 13u); bk_p = (const float *)(g_back + (g_ent[pe].first + node) * 13u); }
		lc_b[0] = lc_b[1] = lc_b[2] = lc_t[0] = lc_t[1] = lc_t[2] = -999.0f;
		if (pv_c && pv_p) local_of(pv_p, pv_c, lc_b);
		if (bk_c && bk_p && ce_blended && pe_blended) local_of(bk_p, bk_c, lc_t);
		if (ce_blended)                     /* every part of the child as drawn, against our blend of it */
			for (n = 0; n < cn; n++)
			{
				const float *A = (const float *)(g_prev + (g_ent[e].first + n) * 13u), *B = (const float *)(g_back + (g_ent[e].first + n) * 13u);
				for (k = 10; k < 13; k++)
				{
					float want = A[k] + (B[k] - A[k]) * g_alpha, d = F32(obj + U16(obj + 0x116) + n * 0x34u + 4u * k) - want;
					if (d < 0) d = -d;
					if (d > worst) worst = d;
				}
			}
		lines++;
		h2n_log("ipk %u t=%u a=%.3f b=%d child %08lx type %u parts %lu kept %d blended %d (drawn vs blend %.5f) parent %08lx part %lu kept %d blended %d: "
			"child root in the parent's part: drawn %.4f %.4f %.4f, tick before %.4f %.4f %.4f, tick %.4f %.4f %.4f; parent part %.4f %.4f %.4f\n",
			h2n_frames(), U32(time + 8), g_alpha, g_frame_blended, datum, U8(obj + 0xAA), cn, e >= 0, ce_blended, worst, par, node, pe >= 0, pe_blended,
			lc_d[0], lc_d[1], lc_d[2], lc_b[0], lc_b[1], lc_b[2], lc_t[0], lc_t[1], lc_t[2], drawn_p[10], drawn_p[11], drawn_p[12]);
	}
}
static void trace_characters(void)      /* (H2N_INTERP=3, test) in a cutscene, each frame: each blended 68-node character's parts as drawn, against our blend */
{
	static int lines;
	static unsigned serial;
	DWORD time = U32(GAME_TIME);
	int i;
	if (!trace_window(&serial, &lines) || lines >= 3000 || !cine_now() || !pointer(time)) return;
	for (i = 0; i < g_ent_n && lines < 3000; i++)
	{
		const ip_ent *e = &g_ent[i];
		DWORD n, k, moved_n = 0, off_n = 0, worst_n = 0;
		float worst = 0.0f, most = 0.0f;
		DWORD most_n = 0;
		if (e->count != 68 || !e->cur) continue;
		for (n = 0; n < 68; n++)
		{
			const float *A = (const float *)(g_prev + (e->first + n) * 13u), *B = (const float *)(g_back + (e->first + n) * 13u);
			float d_n = 0.0f, m_n = 0.0f;
			for (k = 0; k < 13; k++)
			{
				float want = A[k] + (B[k] - A[k]) * g_alpha, d = F32(e->cur + n * 0x34u + 4u * k) - want, m = B[k] - A[k];
				if (d < 0) d = -d;
				if (m < 0) m = -m;
				if (d > d_n) d_n = d;
				if (m > m_n) m_n = m;
			}
			if (m_n > 0.0f) moved_n++;
			if (d_n > 1e-4f) off_n++;
			if (d_n > worst) { worst = d_n; worst_n = n; }
			if (m_n > most) { most = m_n; most_n = n; }
		}
		lines++;
		{
			static DWORD seen_datum[8], seen_tick[8], seen_b[8][68 * 13];
			static int seen_n;
			int s, changed = -1;
			for (s = 0; s < seen_n; s++) if (seen_datum[s] == e->datum) break;
			if (s == seen_n && seen_n < 8) { seen_n++; seen_datum[s] = e->datum; seen_tick[s] = 0xffffffffu; }
			if (s < 8)
			{
				const DWORD *B = g_back + e->first * 13u;
				if (seen_tick[s] == U32(time + 8))   /* another frame of the same tick: did the tick's own parts change since the last? */
					for (n = 0, changed = 0; n < 68; n++) { for (k = 0; k < 13; k++) if (B[n * 13 + k] != seen_b[s][n * 13 + k]) break; if (k < 13) changed++; }
				memcpy(seen_b[s], B, sizeof(seen_b[s])); seen_tick[s] = U32(time + 8);
			}
			h2n_log("ipp %u t=%u a=%.3f %08lx: %lu parts moved this tick (most: part %lu by %.5f), %lu drawn other than blended (worst: part %lu by %.5f); %d of the tick's parts changed since its last frame; drawn part 0 %.4f %.4f %.4f part 67 %.4f %.4f %.4f\n",
				h2n_frames(), U32(time + 8), g_alpha, e->datum, moved_n, most_n, most, off_n, worst_n, worst, changed,
				F32(e->cur + 0x28), F32(e->cur + 0x2C), F32(e->cur + 0x30), F32(e->cur + 67 * 0x34 + 0x28), F32(e->cur + 67 * 0x34 + 0x2C), F32(e->cur + 67 * 0x34 + 0x30));
		}
		{
			static DWORD last_tick[8], last_datum[8];
			int s;
			for (s = 0; s < 8; s++) if (last_datum[s] == e->datum || !last_datum[s]) break;
			if (s < 8 && last_tick[s] != U32(time + 8))   /* once a tick: each part's move this tick ('.' none, 'o' under 0.002, 'O' more) */
			{
				char row[69];
				last_datum[s] = e->datum; last_tick[s] = U32(time + 8);
				for (n = 0; n < 68; n++)
				{
					const float *A = (const float *)(g_prev + (e->first + n) * 13u), *B = (const float *)(g_back + (e->first + n) * 13u);
					float m_n = 0.0f;
					for (k = 0; k < 13; k++) { float m = B[k] - A[k]; if (m < 0) m = -m; if (m > m_n) m_n = m; }
					row[n] = m_n == 0.0f ? '.' : m_n < 0.002f ? 'o' : 'O';
				}
				row[68] = 0;
				h2n_log("ipn t=%u %08lx %s\n", U32(time + 8), e->datum, row);
			}
		}
	}
}
static void trace_particles(void);
static void trace_frame(void)
{
	static int lines;
	trace_render_only();
	trace_cutscene();
	trace_children();
	trace_characters();
	trace_particles();
	DWORD time = U32(GAME_TIME), unit, parent, obj, root;
	trace_vehicles();
	if (lines >= 1500 || !pointer(time)) return;
	unit = local_unit(0, &parent);
	if (unit == 0xffffffffu || parent == 0xffffffffu || !(obj = object_of(parent))) return;
	root = obj + U16(obj + 0x116);
	lines++;
	h2n_log("ipt %u t=%u a=%.3f b=%d own=%d hk=%d/%d root %.4f %.4f %.4f org %.4f %.4f %.4f cam %.4f %.4f %.4f fwd %.4f %.4f %.4f\n",
		h2n_frames(), U32(time + 8), g_trace_blend ? g_alpha : -1.0f, g_trace_blend, g_trace_own, g_trace_hk, g_hk_n,
		F32(root + 0x28), F32(root + 0x2C), F32(root + 0x30), F32(obj + 0x64), F32(obj + 0x68), F32(obj + 0x6C),
		F32(0x4E9C8C), F32(0x4E9C90), F32(0x4E9C94), F32(0x4E9CAC), F32(0x4E9CB0), F32(0x4E9CB4));   /* user 0's camera record (0x358 each): position, forward +0x20 */
}

/* ---- what a moving object carries, between ticks (the mod's v2.0.34, with v2.0.35 - v2.0.39) ----
 * Between two ticks an object's parts are drawn blended, but what it carries keeps the place the tick gave it: its lights
 * (the lights' array, 0x110 each - on it through a marker, +0x4C / +0x54, or a part, +0x58 / +0x5C) are placed once a
 * tick (0xc2d00, 0xc28b0) and the draw reads them as they are; a particle system of an effect on it is drawn in its
 * emitters' frames, set once a tick from the effect's places (0x2477b0). So the glow, its light on the ground and the
 * streaks under a hover vehicle sat where the vehicle will be at the next tick - a shake at 30 a second. And what is
 * drawn at all goes by each object's bounding sphere (+0x30 centre, +0x50 the second), also set once a tick from its root
 * part. Now, on a frame drawn between two ticks, just before the draw: each blended object's spheres are moved as its
 * root part was, and each light, particle location and emitter on a blended object (by its own part, or the part nearest
 * it) is moved and turned as that part was - all put back right after the draw, as the parts are. What sits still in the
 * world (an effect none of whose places is on a part, a system riding a particle, anything in first person) is left as
 * the game has it, and so is everything of a player whose view is first person (their unit and what it holds, up its
 * parents): the first-person arms are built from those. In third person - a vehicle's view - it is carried like anything
 * else. A particle location's +0x20 (the centre of the sphere its particles are culled by) is left as the game has it. */
#define LIGHTS          0x4E030Cu   /* -> the lights (0x110 each) */
#define EFFECTS         0x4EA93Cu   /* -> the effects (0x190 each: +0x48 the object it is on, +0x7C 32 first locations) */
#define EFFECT_PLACES   0x4EA938u   /* -> the effect locations (0x3C each: +2 its part, -1 none; +4 the next) */
#define PARTICLE_SYSTEMS 0x510C74u  /* -> the particle systems (0x54 each: +0x18 its effect, +0x30 its first location, +0x48 the system it rides) */
#define PARTICLE_PLACES 0x51EC8Cu   /* -> the particle locations (0x34 each: +2 first person, +4 first emitter, +0xC next, +0x10 position) */
#define EMITTERS        0x51EC88u   /* -> the particle emitters (0x4C each: +8 next, +0x10 forward, left, up, +0x34 position) */
extern char first_person_camera[] __asm__("?function_23c110@@YGXPAX0PAUs_observer_command@@@Z");   /* (our 0x23c110) */
#define CA_LOG_WORDS 3072
#define CA_ENTRY(w) ((w) + 4u)           /* a block of w words in the log: its owner, the owner's first word, the address, the words, the count */
#define CA_YOURS 0x8000u                 /* in the slot map: the entry is yours */
static DWORD g_ca_log[CA_LOG_WORDS + 16], g_ca_n;
static WORD g_ca_slot[MH_SLOTS];         /* by object slot: its blended entry + 1 (0: not blended this frame) */
static int g_ca_set;
static DWORD g_ca_me[4];                 /* this frame's units of players seen in first person (0: none) */
static unsigned g_ca_spheres, g_ca_lights, g_ca_places, g_ca_emitters, g_ca_full;
static float ca_f(DWORD w) { union { DWORD u; float f; } x; x.u = w; return x.f; }
static const ip_ent *ca_ent(DWORD datum)  /* a blended object's entry, by its datum (the salt checked), or 0; not one of yours */
{
	DWORD sl = datum & 0xffffu, e;
	if (!g_ca_set || datum == 0xffffffffu || (datum >> 16) == 0xffffu || sl >= MH_SLOTS || !(e = g_ca_slot[sl])) return 0;
	if (e & CA_YOURS) return 0;
	if (!e || e > (DWORD)g_ent_n) return 0;
	return g_ent[e - 1].datum == datum && g_ent[e - 1].cur ? &g_ent[e - 1] : 0;
}
static int g_ca_mine_on = -1;            /* (s10j) H2N_CARRY_MINE=0: the effects on your own unit stay where the tick put them in first person (as up to s10i) */
static const ip_ent *ca_ent_unit(DWORD datum)   /* (s10j) as ca_ent, and your own unit itself too (not what it holds) - for its effects' particle places */
{
	DWORD sl = datum & 0xffffu, e, obj;
	int u;
	if (!g_ca_set || datum == 0xffffffffu || (datum >> 16) == 0xffffu || sl >= MH_SLOTS || !(e = g_ca_slot[sl])) return 0;
	if (!(e & CA_YOURS)) return ca_ent(datum);
	if (!g_ca_mine_on || !(obj = object_of(datum))) return 0;
	for (u = 0; u < 4; u++) if (g_ca_me[u] == obj) break;
	if (u == 4) return 0;                                                       /* something your unit holds, or what that holds: left */
	e &= ~CA_YOURS;
	if (!e || e > (DWORD)g_ent_n) return 0;
	return g_ent[e - 1].datum == datum && g_ent[e - 1].cur ? &g_ent[e - 1] : 0;
}
static int ca_first_person(int u)        /* this player's view shows the first-person arms: the director's own test (0x155760) */
{
	DWORD e = DIRECTOR_PROCS + (DWORD)u * 0x140u;
	return (U32(e) == (DWORD)(ULONG_PTR)first_person_camera || U32(e) == 0x23C110u) && F32(e - 8u) == 0.0f;
}
static int ca_mine(DWORD obj)            /* yours: the unit of a player seen in first person, or anything on it (up its parents) */
{
	DWORD k, par;
	int u;
	for (k = 0; obj && k < 8; k++)
	{
		for (u = 0; u < 4; u++) if (g_ca_me[u] == obj) return 1;
		par = U32(obj + 0x14);
		obj = par == 0xffffffffu ? 0 : object_of(par);
	}
	return 0;
}
static DWORD ca_pool(DWORD ptr_at, DWORD size, DWORD *hw)   /* a data array's elements (its size checked) and how many are in use */
{
	DWORD pool = U32(ptr_at), data;
	if (!pointer(pool) || !U8(pool + 0x29) || U32(pool + 0x24) != size || !pointer(data = U32(pool + 0x44))) return 0;   /* (+0x29: valid) */
	*hw = U32(pool + 0x38);
	if (*hw > U32(pool + 0x20) || *hw > 0x400u) return 0;
	return data;
}
static DWORD ca_datum(DWORD data, DWORD hw, DWORD size, DWORD index)   /* an element by its datum (the salt checked), or 0 */
{
	DWORD a;
	if (!data || index == 0xffffffffu || (index & 0xffffu) >= hw) return 0;
	a = data + (index & 0xffffu) * size;
	return U16(a) && U16(a) == (index >> 16) ? a : 0;
}
static void ca_node(const ip_ent *e, DWORD n, float *T, float *B)   /* part n: as the tick left it (T) and as drawn (B) */
{
	const DWORD *bk = g_back + (e->first + n) * 13u;
	DWORD k;
	for (k = 0; k < 13; k++) { T[k] = ca_f(bk[k]); B[k] = F32(e->cur + n * 0x34u + 4u * k); }
}
static DWORD ca_nearest(const ip_ent *e, const float *p)   /* the part whose place (at the tick) is nearest a point */
{
	const DWORD *bk = g_back + e->first * 13u;
	DWORD n, best = 0;
	float bd = 3.0e38f;
	for (n = 0; n < e->count; n++)
	{
		float x = ca_f(bk[n * 13u + 10]) - p[0], y = ca_f(bk[n * 13u + 11]) - p[1], z = ca_f(bk[n * 13u + 12]) - p[2], d = x * x + y * y + z * z;
		if (d < bd) { bd = d; best = n; }
	}
	return best;
}
static int ca_room(DWORD total) { if (total > CA_LOG_WORDS || g_ca_n > CA_LOG_WORDS - total) { g_ca_full++; return 0; } return 1; }
static void ca_save(DWORD owner, DWORD at, DWORD words)  /* (after ca_room) */
{
	DWORD k;
	g_ca_log[g_ca_n++] = owner; g_ca_log[g_ca_n++] = U32(owner); g_ca_log[g_ca_n++] = at;
	for (k = 0; k < words; k++) g_ca_log[g_ca_n++] = U32(at + 4u * k);
	g_ca_log[g_ca_n++] = words;
}
static void ca_point(const float *T, const float *B, DWORD a)   /* a point the tick placed by part T: where part B has it */
{
	float d0 = F32(a) - T[10], d1 = F32(a + 4) - T[11], d2 = F32(a + 8) - T[12], sc = T[0] != 0.0f ? B[0] / T[0] : 1.0f;
	float l0 = d0 * T[1] + d1 * T[2] + d2 * T[3], l1 = d0 * T[4] + d1 * T[5] + d2 * T[6], l2 = d0 * T[7] + d1 * T[8] + d2 * T[9];
	F32(a) = B[10] + sc * (l0 * B[1] + l1 * B[4] + l2 * B[7]);
	F32(a + 4) = B[11] + sc * (l0 * B[2] + l1 * B[5] + l2 * B[8]);
	F32(a + 8) = B[12] + sc * (l0 * B[3] + l1 * B[6] + l2 * B[9]);
}
static void ca_vector(const float *T, const float *B, DWORD a)  /* a direction: turned as the part was, its length kept */
{
	float v0 = F32(a), v1 = F32(a + 4), v2 = F32(a + 8), l = v0 * v0 + v1 * v1 + v2 * v2, m, r0, r1, r2;
	float l0 = v0 * T[1] + v1 * T[2] + v2 * T[3], l1 = v0 * T[4] + v1 * T[5] + v2 * T[6], l2 = v0 * T[7] + v1 * T[8] + v2 * T[9];
	r0 = l0 * B[1] + l1 * B[4] + l2 * B[7]; r1 = l0 * B[2] + l1 * B[5] + l2 * B[8]; r2 = l0 * B[3] + l1 * B[6] + l2 * B[9];
	m = r0 * r0 + r1 * r1 + r2 * r2;
	if (!(m > 1e-12f) || !(l > 1e-12f)) return;
	m = __builtin_sqrtf(l / m);
	F32(a) = r0 * m; F32(a + 4) = r1 * m; F32(a + 8) = r2 * m;
}
static void ca_spheres(void)             /* each blended object's bounding spheres, moved as its root part was */
{
	int i;
	for (i = 0; i < g_ent_n; i++)
	{
		const ip_ent *e = &g_ent[i];
		DWORD obj, k, same = 1, sl = e->datum & 0xffffu;
		float T[13], B[13], c[3];
		if (sl >= MH_SLOTS || g_ca_slot[sl] != (DWORD)i + 1u || !(obj = object_of(e->datum))) continue;   /* carried for: blended, not yours */
		ca_node(e, 0, T, B);
		for (k = 0; k < 13; k++) if (T[k] != B[k]) same = 0;
		if (same || !ca_room(2u * CA_ENTRY(3))) continue;
		ca_save(obj, obj + 0x30, 3); ca_save(obj, obj + 0x50, 3);
		for (k = 0; k < 3; k++) c[k] = F32(obj + 0x30 + 4 * k);
		ca_point(T, B, obj + 0x30);
		for (k = 0; k < 3; k++) F32(obj + 0x50 + 4 * k) = F32(obj + 0x50 + 4 * k) + F32(obj + 0x30 + 4 * k) - c[k];   /* the second: kept at its offset */
		g_ca_spheres++;
	}
}
static void ca_lights(void)              /* lights on blended objects: placed as their part is drawn */
{
	DWORD hw, data = ca_pool(LIGHTS, 0x110, &hw), i;
	for (i = 0; data && i < hw; i++)
	{
		DWORD L = data + i * 0x110u, def, n;
		int marker, node;
		const ip_ent *e;
		float T[13], B[13], p[3];
		if (!U16(L) || !(U16(L + 2) & 2)) continue;                             /* free, or off */
		marker = (short)U16(L + 0x54);
		if (!(e = ca_ent(U32(L + (marker != -1 ? 0x4C : 0x58))))) continue;    /* not on a blended object (or on yours, in first person) */
		def = tag_data(U32(L + 4), 0);
		if (def && (U8(def) & 0x20)) continue;                                  /* a weapon's in first person: the draw places it itself */
		node = marker == -1 ? (short)U16(L + 0x5C) : -1;
		for (n = 0; n < 3; n++) p[n] = F32(L + 0x84 + 4 * n);
		n = node >= 0 && (DWORD)node < e->count ? (DWORD)node : ca_nearest(e, p);
		if (!ca_room(CA_ENTRY(11) + CA_ENTRY(17))) break;
		ca_save(L, L + 0x18, 11); ca_save(L, L + 0x84, 17);
		ca_node(e, n, T, B);
		ca_point(T, B, L + 0x18); ca_point(T, B, L + 0x28); ca_point(T, B, L + 0x38);
		ca_point(T, B, L + 0x84); ca_point(T, B, L + 0x90); ca_point(T, B, L + 0xA0);
		ca_vector(T, B, L + 0xAC); ca_vector(T, B, L + 0xB8);
		g_ca_lights++;
	}
}
static int ca_on_part(DWORD E, DWORD fl, DWORD flhw)   /* an effect with a place on one of its object's parts */
{
	DWORD j, n, li, R;
	for (j = 0; j < 32; j++)
		for (n = 0, li = U32(E + 0x7C + 4 * j); n < 8 && (R = ca_datum(fl, flhw, 0x3C, li)); n++, li = U32(R + 4))
			if ((short)U16(R + 2) >= 0) return 1;
	return 0;
}
static void ca_particles(void)           /* the particle locations and emitters of effects on blended objects */
{
	DWORD pshw, fxhw, flhw, plhw, emhw, i;
	DWORD ps = ca_pool(PARTICLE_SYSTEMS, 0x54, &pshw), fx = ca_pool(EFFECTS, 0x190, &fxhw), fl = ca_pool(EFFECT_PLACES, 0x3C, &flhw);
	DWORD pl = ca_pool(PARTICLE_PLACES, 0x34, &plhw), em = ca_pool(EMITTERS, 0x4C, &emhw);
	if (!ps || !fx || !fl || !pl || !em) return;
	for (i = 0; i < pshw; i++)
	{
		DWORD S = ps + i * 0x54u, E, P, loc, n;
		const ip_ent *e;
		if (!U16(S) || U32(S + 0x48)) continue;                                 /* free, or riding a particle */
		if (!(E = ca_datum(fx, fxhw, 0x190, U32(S + 0x18))) || !(e = ca_ent_unit(U32(E + 0x48))) || !ca_on_part(E, fl, flhw)) continue;   /* (s10j: on your unit too - its shield's recharge rings) */
		for (n = 0, loc = U32(S + 0x30); n < 16 && (P = ca_datum(pl, plhw, 0x34, loc)); n++, loc = U32(P + 0xC))
		{
			DWORD k, m, Em, part;
			float T[13], B[13], p[3];
			if (U8(P + 2)) continue;                                            /* first person */
			for (k = 0; k < 3; k++) p[k] = F32(P + 0x10 + 4 * k);
			part = ca_nearest(e, p);
			ca_node(e, part, T, B);
			if (!ca_room(CA_ENTRY(3))) return;
			ca_save(P, P + 0x10, 3);
			ca_point(T, B, P + 0x10);
			g_ca_places++;
			for (m = 0, k = U32(P + 4); m < 8 && (Em = ca_datum(em, emhw, 0x4C, k)); m++, k = U32(Em + 8))
			{
				if (!ca_room(CA_ENTRY(12))) return;
				ca_save(Em, Em + 0x10, 12);
				ca_vector(T, B, Em + 0x10); ca_vector(T, B, Em + 0x1C); ca_vector(T, B, Em + 0x28); ca_point(T, B, Em + 0x34);
				g_ca_emitters++;
			}
		}
	}
}
static void ca_restore(void)             /* all of it put back as the tick left it (an element reused since: left) */
{
	DWORD n = g_ca_n, i;
	g_ca_n = 0;
	if (n > CA_LOG_WORDS) n = 0;
	while (n >= CA_ENTRY(1))                                                    /* backward: the first value saved for an address stays */
	{
		DWORD w = g_ca_log[n - 1], start, owner, first, a, k;
		if (!w || w > 17u || CA_ENTRY(w) > n) break;                           /* (not an entry: stop) */
		start = n - CA_ENTRY(w);
		owner = g_ca_log[start]; first = g_ca_log[start + 1]; a = g_ca_log[start + 2];
		if (!pointer(owner) || a < owner || a + 4u * w > owner + 0x110u) break;
		if (((U32(owner) ^ first) & 0xffffu) == 0)                             /* the same element still (its salt; an object: its tag) */
			for (k = 0; k < w; k++) U32(a + 4u * k) = g_ca_log[start + 3 + k];
		n = start;
	}
	if (g_ca_set) for (i = 0; i < (DWORD)g_ent_n; i++) { DWORD sl = g_ent[i].datum & 0xffffu; if (sl < MH_SLOTS) g_ca_slot[sl] = 0; }
	g_ca_set = 0;
}
#ifdef H2N_SHIELD_TEST
static void shield_test_trace(int after);
#endif
static void ca_apply(void)               /* before the draw of a frame drawn between two ticks */
{
	DWORD i, parent;
	int u;
	ca_restore();                                                               /* (none should be left) */
	if (!g_frame_blended || !g_blended) return;
	if (g_ca_mine_on < 0) g_ca_mine_on = h2n_env_int("H2N_CARRY_MINE", 1) ? 1 : 0;
	for (u = 0; u < 4; u++) { DWORD unit = local_unit(u, &parent); g_ca_me[u] = unit != 0xffffffffu && ca_first_person(u) ? object_of(unit) : 0; }
	for (i = 0; i < (DWORD)g_ent_n && i + 1u < CA_YOURS; i++)
	{
		const ip_ent *e = &g_ent[i];
		DWORD sl = e->datum & 0xffffu, obj;
		if (e->cur && (e->datum >> 16) != 0xffffu && sl < MH_SLOTS && (obj = object_of(e->datum)))
			g_ca_slot[sl] = (WORD)((i + 1u) | (ca_mine(obj) ? CA_YOURS : 0u));
	}
	g_ca_set = 1;
#ifdef H2N_SHIELD_TEST
	shield_test_trace(0);
#endif
	ca_spheres(); ca_lights(); ca_particles();
#ifdef H2N_SHIELD_TEST
	shield_test_trace(1);
#endif
}
#ifdef H2N_SHIELD_TEST
/* (test builds here only: -DH2N_SHIELD_TEST) every 5 s player 1's shield is taken to a quarter, so the game starts its
 * recharge effect; on each frame drawn between ticks the particle places of effects on what is yours are written to the log */
static unsigned g_st_ticks, g_st_lines, g_st_frame;
static void shield_test_tick(void)
{
	DWORD parent, unit = local_unit(0, &parent), obj;
	if (++g_st_ticks % 150u != 0 || unit == 0xffffffffu || !(obj = object_of(unit))) return;
	if (F32(obj + 0xF0) < 1.0f || (U16(obj + 0x10A) & 0x2Eu)) return;
	F32(obj + 0xF0) = 0.25f; U16(obj + 0x104) = 0;
	h2n_log("       shield test: tick %u, unit %08x: shield 0.25\n", g_st_ticks, (unsigned)unit);
}
static void shield_test_trace(int after)
{
	DWORD pshw, fxhw, flhw, plhw, emhw, i;
	DWORD ps = ca_pool(PARTICLE_SYSTEMS, 0x54, &pshw), fx = ca_pool(EFFECTS, 0x190, &fxhw), fl = ca_pool(EFFECT_PLACES, 0x3C, &flhw);
	DWORD pl = ca_pool(PARTICLE_PLACES, 0x34, &plhw), em = ca_pool(EMITTERS, 0x4C, &emhw);
	if (!after) g_st_frame++;
	if (!ps || !fx || !fl || !pl || !em || g_st_lines > 3000) return;
	for (i = 0; i < pshw; i++)
	{
		DWORD S = ps + i * 0x54u, E, P, loc, n, obj, sl, ent, j, li, R;
		const ip_ent *e;
		if (!U16(S) || !(E = ca_datum(fx, fxhw, 0x190, U32(S + 0x18)))) continue;
		if (!(obj = object_of(U32(E + 0x48))) || !ca_mine(obj)) continue;
		sl = U32(E + 0x48) & 0xffffu;
		ent = sl < MH_SLOTS ? g_ca_slot[sl] & ~CA_YOURS : 0;
		e = ent && ent <= (DWORD)g_ent_n ? &g_ent[ent - 1] : 0;
		h2n_log("       shield test (%s): frame %u alpha %.2f system %u (riding %08x) effect %08x tag %08x on %s%s, effect places:", after ? "as drawn" : "as the tick left it", g_st_frame, g_alpha, (unsigned)i,
			(unsigned)U32(S + 0x48), (unsigned)U32(S + 0x18), (unsigned)U32(E + 4), g_ca_me[0] == obj ? "your unit" : "something of yours", e ? "" : " (not blended)");
		for (j = 0; j < 32; j++)
			for (n = 0, li = U32(E + 0x7C + 4 * j); n < 8 && (R = ca_datum(fl, flhw, 0x3C, li)); n++, li = U32(R + 4))
				h2n_log(" [%u] node %04x", (unsigned)j, (unsigned)U16(R + 2));
		h2n_log("\n");
		g_st_lines++;
		for (n = 0, loc = U32(S + 0x30); n < 16 && (P = ca_datum(pl, plhw, 0x34, loc)); n++, loc = U32(P + 0xC))
		{
			float p[3], T[13], B[13];
			DWORD part = 0, k, Em;
			for (k = 0; k < 3; k++) p[k] = F32(P + 0x10 + 4 * k);
			if (e) { part = ca_nearest(e, p); ca_node(e, part, T, B); }
			h2n_log("         place %u: first person %u at %.3f %.3f %.3f; nearest part %u at the tick %.3f %.3f %.3f, drawn %.3f %.3f %.3f",
				(unsigned)n, (unsigned)U8(P + 2), p[0], p[1], p[2], (unsigned)part, e ? T[10] : 0.0f, e ? T[11] : 0.0f, e ? T[12] : 0.0f, e ? B[10] : 0.0f, e ? B[11] : 0.0f, e ? B[12] : 0.0f);
			if ((Em = ca_datum(em, emhw, 0x4C, U32(P + 4)))) h2n_log("; emitter at %.3f %.3f %.3f", F32(Em + 0x34), F32(Em + 0x38), F32(Em + 0x3C));
			h2n_log("\n");
			g_st_lines++;
		}
	}
}
#endif
#define PARTICLES 0x51EC84u     /* -> the particles (0x40 each: +4 the next, +8 age, +0x1C position, +0x28 velocity) */
static void trace_particles(void)        /* (H2N_INTERP=3, test) in a cutscene, each frame: the particles of effects on 68-node characters, as drawn */
{
	static int lines;
	DWORD pshw, fxhw, plhw, emhw, i, pool = U32(PARTICLES), pdata = 0, phw = 0, time = U32(GAME_TIME);
	DWORD ps = ca_pool(PARTICLE_SYSTEMS, 0x54, &pshw), fx = ca_pool(EFFECTS, 0x190, &fxhw);
	DWORD pl = ca_pool(PARTICLE_PLACES, 0x34, &plhw), em = ca_pool(EMITTERS, 0x4C, &emhw);
	{
		static double last;
		if (cine_now() && h2n_seconds() - last > 5.0)
		{
			DWORD by[16] = { 0 }, none = 0, gone = 0, p68 = 0, total = 0, j, ps_pool = U32(PARTICLE_SYSTEMS), fx_pool = U32(EFFECTS);
			last = h2n_seconds();
			for (j = 0; ps && j < pshw; j++)
			{
				DWORD S = ps + j * 0x54u, E, o;
				if (!U16(S)) continue;
				total++;
				if (!(E = ca_datum(fx, fxhw, 0x190, U32(S + 0x18)))) { gone++; continue; }
				if (U32(E + 0x48) == 0xffffffffu) { none++; continue; }
				if (!(o = object_of(U32(E + 0x48)))) { gone++; continue; }
				by[U8(o + 0xAA) & 15]++;
				if (U8(o + 0xAA) == 0 && U16(o + 0x114) / 0x34u == 68) p68++;
			}
			h2n_log("ipq summary: pools: systems %s (size %lx, %lu in use of %lu), effects %s (size %lx, %lu of %lu), places %s, emitters %s, particles %s (size %lx); "
				"%lu systems: effect gone %lu, on nothing %lu; on bipeds %lu (68-part %lu), vehicles %lu, weapons %lu, equipment %lu, scenery %lu, machines %lu, crates %lu, creatures %lu, other %lu\n",
				ps ? "ok" : "NOT", pointer(ps_pool) ? U32(ps_pool + 0x24) : 0, pointer(ps_pool) ? U32(ps_pool + 0x38) : 0, pointer(ps_pool) ? U32(ps_pool + 0x20) : 0,
				fx ? "ok" : "NOT", pointer(fx_pool) ? U32(fx_pool + 0x24) : 0, pointer(fx_pool) ? U32(fx_pool + 0x38) : 0, pointer(fx_pool) ? U32(fx_pool + 0x20) : 0,
				pl ? "ok" : "NOT", em ? "ok" : "NOT", pointer(pool) ? "there" : "NOT", pointer(pool) ? U32(pool + 0x24) : 0,
				total, gone, none, by[0], p68, by[1], by[2], by[3], by[6], by[7], by[11], by[12], by[4] + by[5] + by[8] + by[9] + by[10] + by[13] + by[14] + by[15]);
		}
	}
	{
		static unsigned serial;
		if (!trace_window(&serial, &lines)) return;
	}
	if (lines >= 3000 || !cine_now() || !ps || !fx || !pl || !em || !pointer(time)) return;
	if (pointer(pool) && U32(pool + 0x24) == 0x40 && pointer(U32(pool + 0x44))) { pdata = U32(pool + 0x44); phw = U32(pool + 0x38); if (phw > U32(pool + 0x20)) phw = 0; }
	for (i = 0; i < pshw && lines < 3000; i++)
	{
		DWORD S = ps + i * 0x54u, E, obj, P, loc, n;
		if (!U16(S)) continue;
		if (!(E = ca_datum(fx, fxhw, 0x190, U32(S + 0x18))) || !(obj = object_of(U32(E + 0x48)))) continue;
		if (U8(obj + 0xAA) != 0 || U16(obj + 0x114) / 0x34u != 68) continue;
		for (n = 0, loc = U32(S + 0x30); n < 4 && (P = ca_datum(pl, plhw, 0x34, loc)); n++, loc = U32(P + 0xC))
		{
			DWORD Em = ca_datum(em, emhw, 0x4C, U32(P + 4)), cnt = 0, pi, A = 0;
			float ep[3] = { 0, 0, 0 };
			if (Em) { ep[0] = F32(Em + 0x34); ep[1] = F32(Em + 0x38); ep[2] = F32(Em + 0x3C); }
			for (pi = Em ? U32(Em + 4) : 0xffffffffu; pi != 0xffffffffu && pdata && (pi & 0xffffu) < phw && cnt < 256; cnt++)
			{
				DWORD a = pdata + (pi & 0xffffu) * 0x40u;
				if (!A) A = a;
				pi = U32(a + 4);
			}
			lines++;
			h2n_log("ipq %u t=%u a=%.3f system %lu (flags %04x, its effect %08lx on %08lx, riding %08lx) place %lu: at %.4f %.4f %.4f; its emitter at %.4f %.4f %.4f; %lu particles, the first at %.4f %.4f %.4f moving %.3f %.3f %.3f (age %.3f)\n",
				h2n_frames(), U32(time + 8), g_alpha, i, U16(S + 0xC), U32(S + 0x18), U32(E + 0x48), U32(S + 0x48), n,
				F32(P + 0x10), F32(P + 0x14), F32(P + 0x18), ep[0], ep[1], ep[2], cnt,
				A ? F32(A + 0x1C) : 0.0f, A ? F32(A + 0x20) : 0.0f, A ? F32(A + 0x24) : 0.0f, A ? F32(A + 0x28) : 0.0f, A ? F32(A + 0x2C) : 0.0f, A ? F32(A + 0x30) : 0.0f, A ? F32(A + 8) : 0.0f);
		}
	}
}

/* ---- Spooky Ghost's Increased LODs (v2.0, v2.0.10, v2.0.12) and Draw Distance (v2.0.11) - with H2N_UNLOCK, as the
 * mod's Performance Mode Settings set to "Increased LODs: on" and "Draw Distance: 300 %" (H2N_LODS=0 turns the first off,
 * H2N_DRAWDIST=5..30 sets the second in tenths: 10 is the game's own). The source calls these at the mod's sites:
 *  - h2n_lods(): instanced geometry never faded or culled with distance (0x133520's fade byte: 255, the mod's
 *    inst_fade_hook at 0x13366d); every object at its highest level of detail (0x4bcc0: 0x4c450's answer made 5, which the
 *    game's own cap then lowers to the most it allows - obj_lod_hook 0x4be4d); every shader at its most detailed level (its
 *    LOD bias given as 5, the game's own "never lower", to 0x1bf50 from 0x40e30, 0x41040 and 0x41490 - shader_lod_hook).
 *  - h2n_draw_distance(view): after the game builds a player's view (0x223080's two calls of 0x2f800), its distance scale
 *    (+0x2c) divided by the setting: every distance the game draws by - whether an object is drawn and fades, its level
 *    of detail, a particle system's fade - happens 3 times as far away (dd_view_hook at 0x223185 / 0x223220).
 * s10l: the instances' fade and the objects' level of detail run as retail's code (0x133520 is routed to retail, and
 * 0x4bcc0 is reached only from retail's 0x4b5d0), so the source's lines there were never reached and both stayed the
 * game's own; h2n_lods_retail() below puts the mod's two hooks into retail's code at the mod's two places. */
int __cdecl h2n_lods(void)
{
	static int on = -1;
	if (on < 0)
	{
		on = h2n_unlock_wanted() && h2n_env_int("H2N_LODS", 1) ? 1 : 0;
		if (on) h2n_log("       Increased LODs (H2N_UNLOCK): objects at their most detailed model, shaders at their most detailed level, instances never faded by distance\n");
	}
	return on;
}
/* s10l: the two places of Increased LODs that run as retail's code (the instances' fade, 0x13366d; the objects' level of
 * detail, 0x4be4d - helpers.S says why the source's versions are never reached there) go through the mod's two hooks.
 * Each call is checked to be the one the mod patches (a call to 0x322b0c / 0x4c450) before it is pointed elsewhere. */
extern char h2n_inst_fade_hook[], h2n_obj_lod_hook[];
int h2n_retail_code_loaded(void);
void __cdecl h2n_lods_retail(void)
{
	static const struct { DWORD at, callee; const char *what; } site[2] = {
		{ 0x13366d, 0x322b0c, "the instanced geometry's fade (0x133520)" }, { 0x4be4d, 0x4c450, "an object's level of detail (0x4bcc0)" } };
	void *hook[2] = { h2n_inst_fade_hook, h2n_obj_lod_hook };
	unsigned i, done = 0;
	if (!h2n_lods() || !h2n_retail_code_loaded()) return;
	for (i = 0; i < 2; i++)
	{
		BYTE *p = (BYTE *)(ULONG_PTR)site[i].at;
		if (p[0] != 0xE8 || site[i].at + 5 + *(DWORD *)(p + 1) != site[i].callee)
		{
			h2n_log("       Increased LODs: retail's call at %08lx is not the one expected (%s); left as it is\n", site[i].at, site[i].what);
			continue;
		}
		*(DWORD *)(p + 1) = (DWORD)(ULONG_PTR)hook[i] - (site[i].at + 5);
		done++;
	}
	h2n_log("       Increased LODs: %u of 2 places in retail's code go through the layer (the instanced geometry's fade, 0x13366d; the objects' level of detail, 0x4be4d)\n", done);
}
#ifdef H2N_LODS_TEST
extern unsigned h2n_t_lods[4];
static void lods_test_report(void)
{
	static unsigned calls;
	if (++calls % 240) return;
	h2n_log("LODS TEST (240 views): instances %u, %u of them faded or left out by the game's own fade; objects %u, %u of them given less detail by the game's own level\n",
		h2n_t_lods[0], h2n_t_lods[1], h2n_t_lods[2], h2n_t_lods[3]);
	h2n_t_lods[0] = h2n_t_lods[1] = h2n_t_lods[2] = h2n_t_lods[3] = 0;
}
#endif
void __cdecl h2n_draw_distance(void *view)
{
	static int tenths = -1;
	float sc;
#ifdef H2N_LODS_TEST
	lods_test_report();
#endif
	if (tenths < 0)
	{
		tenths = h2n_unlock_wanted() ? h2n_env_int("H2N_DRAWDIST", 30) : 10;
		if (tenths < 5 || tenths > 30) tenths = 10;
		if (tenths != 10) h2n_log("       Draw Distance (H2N_UNLOCK): %d%% - the views' distance scale divided by %.1f\n", tenths * 10, tenths / 10.0);
	}
	if (tenths == 10 || !view) return;
	sc = *(float *)((BYTE *)view + 0x2c);
	if (!(sc > 0.001f && sc < 100.0f)) return;
	*(float *)((BYTE *)view + 0x2c) = sc * 10.0f / (float)tenths;
}
/* ---- the zoom that zooms straight back out at a high frame rate (the mod's v2.0, with Performance Mode) ----
 * The player control (0x186ab0) runs once a frame. On the zoom press it notes the level it left and starts a count
 * (record +0x8c, flag +0x89); each frame the button stays down the count goes up by one; on release, a count above 15
 * means "that was a hold" and it zooms back out. Fifteen frames are half a second at 30 frames a second and a sixteenth
 * at 240 - shorter than an ordinary click. With H2N_UNLOCK the count is written each frame from the clock instead,
 * before the player control runs: thirtieths of a second since the press, so a hold is half a second at every rate. */
#define PLAYER_CONTROL 0x4ED284u     /* -> the player control: each local player's record at +0x14 + 0x94 x index */
static double g_zh_t0[4];
void __cdecl h2n_zoom_hold(void)
{
	DWORD pc, i;
	double now;
	h2n_perf(1);                                  /* (s9q: the game's update starts) */
	if (!h2n_unlock_wanted() || h2n_pace_clamp) { g_zh_t0[0] = g_zh_t0[1] = g_zh_t0[2] = g_zh_t0[3] = 0.0; return; }
	pc = U32(PLAYER_CONTROL);
	if (!pointer(pc) || !pointer(pc + 0x14 + 4 * 0x94)) return;
	now = h2n_seconds();
	for (i = 0; i < 4; i++)
	{
		DWORD rec = pc + 0x14 + i * 0x94, n;
		if (!U8(rec + 0x89)) { g_zh_t0[i] = 0.0; continue; }                 /* no zoom press being held */
		if (g_zh_t0[i] == 0.0) g_zh_t0[i] = now > 0.0 ? now : 1e-9;          /* the press: seen this frame */
		n = (DWORD)((now - g_zh_t0[i]) * 30.0);
		if (n > 0x7Fu) n = 0x7Fu;
		if (U16(rec + 0x8C) != n) U16(rec + 0x8C) = (WORD)n;
	}
}

/* ---- the game's calls (the source calls these at the mod's hook sites) ---- */
static unsigned char g_ptc_hold;                 /* a frame's update with no tick since the last frame's draw (6i.35) */
/* ---- H2N_AIMLOG=1: where your shots go (the report of 17 Nov: "Banshee and other vehicles' aim appears to be very off.
 * Turrets also seem to be aiming slightly down and to the right") ----
 * Read only: nothing is changed. Before each tick, every projectile (object type 5) that appeared since the last tick
 * within 8 units of a local player's unit or of what it rides is noted, with that moment's view (the camera record
 * 0x4E9C8C + 0x358 a user: position +0, forward +0x20), your unit's aim (+0x168, as the barrel code's 0xc8290 reads it)
 * and the aim of what you ride. A tick later its heading (where it went in that tick) is logged against each of them:
 * how many degrees right and up of each it flew. The game's barrel code (0x104150) fires along the aim of the unit that
 * holds the weapon (or what that unit rides) unless the unit rides something itself, then along the barrel's marker. */
#define AIM_TRACK 64
#define AIM_SEEN 512
typedef struct { DWORD datum, tick; float p[3], cam[3], view[3], aim[3], raim[3]; int u, rides, rtype; } aim_shot;
static aim_shot g_at[AIM_TRACK];
static int g_at_n, g_aimlog = -1;
static DWORD g_at_seen[AIM_SEEN], g_at_seen_next, g_at_lines, g_at_skipped;
static double g_at_second;
static float at_atan(float x)                   /* arctangent, |error| < 2e-5 */
{
	int inv = x > 1.0f || x < -1.0f;
	float y = inv ? 1.0f / x : x, y2 = y * y, r;
	r = y * (0.99997726f + y2 * (-0.33262347f + y2 * (0.19354346f + y2 * (-0.11643287f + y2 * (0.05265332f + y2 * -0.01172120f)))));
	return inv ? (x > 0.0f ? 1.5707963f : -1.5707963f) - r : r;
}
static float at_atan2(float y, float x)
{
	if (x > 0.0f) return at_atan(y / x);
	if (x < 0.0f) return at_atan(y / x) + (y >= 0.0f ? 3.1415927f : -3.1415927f);
	return y > 0.0f ? 1.5707963f : y < 0.0f ? -1.5707963f : 0.0f;
}
static int at_unit(float *v)                    /* to unit length; 0 if it has none */
{
	float l = __builtin_sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
	if (!(l > 1e-6f) || !(l < 1e9f)) return 0;
	v[0] /= l; v[1] /= l; v[2] /= l;
	return 1;
}
static void at_heading(const float *d, float *yaw, float *pitch)   /* degrees: yaw to the left of +x, pitch up */
{
	*yaw = at_atan2(d[1], d[0]) * 57.29578f;
	*pitch = at_atan2(d[2], __builtin_sqrtf(d[0] * d[0] + d[1] * d[1])) * 57.29578f;
}
static void at_off(const float *d, const float *ref, float *right, float *up)   /* d against ref: degrees right and up of it */
{
	float f[3] = { ref[0], ref[1], ref[2] }, l[3], u[3], a, b, c;
	if (!at_unit(f)) { *right = *up = 0.0f; return; }
	l[0] = -f[1]; l[1] = f[0]; l[2] = 0.0f;                                   /* the world's up x forward: to the left */
	if (!at_unit(l)) { l[0] = 0.0f; l[1] = 1.0f; l[2] = 0.0f; }
	u[0] = f[1] * l[2] - f[2] * l[1]; u[1] = f[2] * l[0] - f[0] * l[2]; u[2] = f[0] * l[1] - f[1] * l[0];
	a = d[0] * f[0] + d[1] * f[1] + d[2] * f[2]; b = d[0] * l[0] + d[1] * l[1] + d[2] * l[2]; c = d[0] * u[0] + d[1] * u[1] + d[2] * u[2];
	*right = at_atan2(-b, a) * 57.29578f;
	*up = at_atan2(c, __builtin_sqrtf(a * a + b * b)) * 57.29578f;
}
static void at_vec(DWORD a, float *v) { v[0] = F32(a); v[1] = F32(a + 4); v[2] = F32(a + 8); }
static void aim_log(void)
{
	DWORD time = U32(GAME_TIME), pool = U32(OBJECTS), data, count, size, i, tick;
	DWORD unit[4], ride[4], uobj[4], robj[4];
	int u, k, j;
	if (g_aimlog < 0) { g_aimlog = h2n_env_int("H2N_AIMLOG", 0) ? 1 : 0; if (g_aimlog) h2n_log("       aim: H2N_AIMLOG=1 - each shot fired near a local player is compared with the view and the aims (\"aim:\" lines)\n"); }
	if (!g_aimlog || !pointer(time) || !pointer(pool)) return;
	tick = U32(time + 8);
	for (u = 0; u < 4; u++)
	{
		DWORD parent;
		unit[u] = local_unit(u, &parent); ride[u] = parent;
		uobj[u] = unit[u] != 0xffffffffu ? object_of(unit[u]) : 0;
		robj[u] = parent != 0xffffffffu ? object_of(parent) : 0;
	}
	for (k = 0; k < g_at_n; k++)                                              /* noted last tick: where they went since */
	{
		aim_shot *a = &g_at[k];
		DWORD obj = object_of(a->datum);
		float p[3], d[3], v[3], yaw, pitch, r1, u1, r2, u2, r3, u3, r4, u4, speed;
		double now;
		if (!obj || U8(obj + 0xAA) != 5 || tick != a->tick + 1) continue;
		at_vec(obj + 0x64, p); at_vec(obj + 0x88, v);
		d[0] = p[0] - a->p[0]; d[1] = p[1] - a->p[1]; d[2] = p[2] - a->p[2];
		speed = __builtin_sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]) * 30.0f;
		if (!at_unit(d)) continue;
		now = h2n_seconds();
		if (now - g_at_second >= 1.0) { if (g_at_skipped) h2n_log("       aim: (%u more shots not listed)\n", g_at_skipped); g_at_second = now; g_at_lines = 0; g_at_skipped = 0; }
		if (++g_at_lines > 8) { g_at_skipped++; continue; }
		at_heading(d, &yaw, &pitch);
		at_off(d, a->view, &r1, &u1); at_off(d, a->aim, &r2, &u2); at_off(d, a->raim, &r3, &u3);
		at_unit(v); at_off(v, d, &r4, &u4);
		h2n_log("       aim: tick %u, user %d %s: a shot from %.2f units of the view's point flew yaw %.1f pitch %.1f at %.1f units/s; "
			"of the view %.2f right %.2f up, of your aim %.2f right %.2f up, of %s %.2f right %.2f up (its velocity field: %.2f right %.2f up of where it went)\n",
			a->tick, a->u, a->rides ? (a->rtype == 1 ? "riding a vehicle" : "riding something") : "on foot",
			__builtin_sqrtf((a->p[0] - a->cam[0]) * (a->p[0] - a->cam[0]) + (a->p[1] - a->cam[1]) * (a->p[1] - a->cam[1]) + (a->p[2] - a->cam[2]) * (a->p[2] - a->cam[2])),
			yaw, pitch, speed, r1, u1, r2, u2, a->rides ? "what you ride's aim" : "(nothing ridden)", r3, u3, r4, u4);
	}
	g_at_n = 0;
	data = U32(pool + 0x44); count = U32(pool + 0x20); size = U32(pool + 0x24);
	if (!pointer(data) || size < 12 || size > 0x4000 || count > 0x4000) return;
	for (i = 0; i < count; i++)                                               /* the projectiles new since the last tick */
	{
		DWORD e = data + i * size, salt = U16(e), obj = U32(e + 8), datum;
		float p[3];
		if (!salt || !pointer(obj) || U8(obj + 0xAA) != 5) continue;
		datum = (salt << 16) | i;
		for (j = 0; j < AIM_SEEN; j++) if (g_at_seen[j] == datum) break;
		if (j < AIM_SEEN) continue;
		g_at_seen[g_at_seen_next++ % AIM_SEEN] = datum;
		at_vec(obj + 0x64, p);
		for (u = 0; u < 4; u++)
		{
			float q[3], d2 = 1e30f, e2;
			if (!uobj[u]) continue;
			at_vec(uobj[u] + 0x64, q);
			d2 = (p[0] - q[0]) * (p[0] - q[0]) + (p[1] - q[1]) * (p[1] - q[1]) + (p[2] - q[2]) * (p[2] - q[2]);
			if (robj[u]) { at_vec(robj[u] + 0x64, q); e2 = (p[0] - q[0]) * (p[0] - q[0]) + (p[1] - q[1]) * (p[1] - q[1]) + (p[2] - q[2]) * (p[2] - q[2]); if (e2 < d2) d2 = e2; }
			if (d2 < 64.0f) break;
		}
		if (u == 4 || g_at_n >= AIM_TRACK) continue;
		{
			aim_shot *a = &g_at[g_at_n++];
			DWORD cam = 0x4E9C8Cu + (DWORD)u * 0x358u;
			a->datum = datum; a->tick = tick; a->u = u;
			a->p[0] = p[0]; a->p[1] = p[1]; a->p[2] = p[2];
			at_vec(cam, a->cam); at_vec(cam + 0x20, a->view); at_vec(uobj[u] + 0x168, a->aim);
			a->rides = robj[u] != 0; a->rtype = robj[u] ? U8(robj[u] + 0xAA) : -1;
			if (robj[u]) at_vec(robj[u] + 0x168, a->raim); else { a->raim[0] = a->aim[0]; a->raim[1] = a->aim[1]; a->raim[2] = a->aim[2]; }
		}
	}
}
/* (s10j) THE ROCKET LAUNCHER'S ANIMATION FIX (your HCM AnimationFixes.cpp, fix 3: "the grenade throw pop"): the same
 * animation data is in the Xbox's shared.map (its raw data, at 0x8aa4ad0 in the copy here) - every one of the 51 places
 * holds the bytes your fix replaces. The animation cache streams a graph's data into a block of its memory when the game
 * asks for it (animation_cache_get_resource, 0x1237e0, which now tells this function each time it hands data out): data
 * not seen before in its block is searched once for the 40-byte header, and where the header is, the 51 places that
 * still hold the old bytes get yours (in memory only; the map file is never written). A block reloaded with the same data
 * is noticed by its first place holding the old bytes again. Only with Unlock Performance (H2N_UNLOCK), as the other
 * fixes of the in-between frames: the pop is seen above 30 frames a second. H2N_ROCKETFIX=0 leaves it out. */
static const BYTE g_rk_signature[40] = { 0x03, 0x15, 0x04, 0x00, 0x28, 0x96, 0xcf, 0x38, 0x33, 0x36, 0x10, 0x3f, 0x00, 0x1d, 0x00, 0x00, 0x40, 0x25, 0x00, 0x00, 0x60, 0x01, 0x00, 0x00, 0x10, 0x02, 0x00, 0x00, 0xb0, 0x00, 0x00, 0x00, 0x73, 0xbd, 0x5e, 0xfd, 0xac, 0x03, 0xc3, 0x92 };
static const struct { WORD rel; BYTE fix[8], orig[8]; } g_rk_patch[51] = {
	{ 0x180, { 0x15, 0xa3, 0xa9, 0xf5, 0x4e, 0xba, 0x3b, 0xcb }, { 0x25, 0x17, 0xe2, 0xe5, 0xa5, 0x12, 0x46, 0x86 } },
	{ 0x440, { 0x00, 0x00, 0xf9, 0x5d, 0x00, 0x00, 0x18, 0xa9 }, { 0x00, 0x00, 0xf8, 0x5d, 0x00, 0x00, 0x19, 0xa9 } },
	{ 0x700, { 0xef, 0x73, 0x09, 0xdd, 0xfb, 0xef, 0xbf, 0xd9 }, { 0x39, 0xe4, 0x32, 0xb3, 0x76, 0xff, 0x74, 0x9d } },
	{ 0x188, { 0xe3, 0xa3, 0xef, 0xf4, 0x71, 0xba, 0xce, 0xc9 }, { 0x95, 0x18, 0xaa, 0xe5, 0x7d, 0x13, 0xbd, 0x86 } },
	{ 0x448, { 0x00, 0x00, 0x67, 0x5e, 0x00, 0x00, 0x91, 0xa9 }, { 0x00, 0x00, 0x67, 0x5e, 0x00, 0x00, 0x91, 0xa9 } },
	{ 0x708, { 0x42, 0x73, 0x0c, 0xdc, 0xda, 0xf0, 0x4b, 0xd8 }, { 0x6a, 0xe2, 0x74, 0xb2, 0x36, 0x00, 0x91, 0x9e } },
	{ 0x190, { 0x9a, 0xa6, 0xbb, 0xf2, 0x2c, 0xbb, 0x07, 0xc5 }, { 0x4a, 0x1d, 0x37, 0xe5, 0x77, 0x16, 0x68, 0x88 } },
	{ 0x450, { 0x00, 0x00, 0x91, 0x5f, 0x00, 0x00, 0xdb, 0xaa }, { 0x00, 0x00, 0x91, 0x5f, 0x00, 0x00, 0xdb, 0xaa } },
	{ 0x710, { 0xbc, 0x70, 0xa1, 0xd8, 0x25, 0xf3, 0xbf, 0xd3 }, { 0x75, 0xdc, 0xe3, 0xb0, 0x3f, 0x02, 0xe6, 0xa1 } },
	{ 0x198, { 0x89, 0xad, 0x59, 0xee, 0x6b, 0xbe, 0x80, 0xb9 }, { 0x46, 0x28, 0x5d, 0xe5, 0x45, 0x1e, 0x65, 0x8d } },
	{ 0x458, { 0x00, 0x00, 0x43, 0x61, 0x00, 0x00, 0xcc, 0xac }, { 0x00, 0x00, 0x43, 0x61, 0x00, 0x00, 0xcc, 0xac } },
	{ 0x718, { 0x45, 0x69, 0x0e, 0xd0, 0xae, 0xf6, 0x00, 0xca }, { 0x6e, 0xce, 0xa7, 0xb0, 0x63, 0x05, 0xd3, 0xa8 } },
	{ 0x1a0, { 0x09, 0xc7, 0x3f, 0xe5, 0x8c, 0xcf, 0xa1, 0x9b }, { 0xeb, 0x44, 0xbe, 0xea, 0x24, 0x35, 0x96, 0xa4 } },
	{ 0x460, { 0x00, 0x00, 0x49, 0x63, 0x00, 0x00, 0x39, 0xaf }, { 0x00, 0x00, 0x49, 0x63, 0x00, 0x00, 0x39, 0xaf } },
	{ 0x720, { 0x7c, 0x4a, 0x03, 0xb9, 0xf7, 0xfc, 0xef, 0xb3 }, { 0x47, 0xa9, 0xfe, 0xbc, 0xa0, 0x09, 0x97, 0xbe } },
	{ 0x1a8, { 0xdd, 0xfa, 0xa8, 0xde, 0xa8, 0xf6, 0xe2, 0x84 }, { 0x7b, 0x5f, 0x98, 0xfa, 0xa5, 0x4d, 0x3e, 0xdd } },
	{ 0x468, { 0x00, 0x00, 0x72, 0x65, 0x00, 0x00, 0xf3, 0xb1 }, { 0x00, 0x00, 0x71, 0x65, 0x00, 0x00, 0xf3, 0xb1 } },
	{ 0x728, { 0x4e, 0x09, 0x83, 0xa4, 0xa6, 0x05, 0x26, 0xa7 }, { 0x33, 0x85, 0x56, 0xe3, 0xa8, 0x0a, 0xd4, 0xec } },
	{ 0x1b0, { 0xb9, 0x10, 0xce, 0xdc, 0x2a, 0x08, 0x5a, 0x86 }, { 0xa4, 0x5d, 0x14, 0x03, 0xc0, 0x56, 0x24, 0xf7 } },
	{ 0x470, { 0x00, 0x00, 0x26, 0x6b, 0x00, 0x00, 0xfc, 0xb9 }, { 0x00, 0x00, 0x26, 0x6b, 0x00, 0x00, 0xfc, 0xb9 } },
	{ 0x730, { 0xa2, 0xed, 0xe8, 0x9e, 0x22, 0x0a, 0x48, 0xaf }, { 0xe6, 0x80, 0x95, 0xf5, 0x95, 0x0a, 0x9a, 0x02 } },
	{ 0x1b8, { 0x47, 0x17, 0xbc, 0xdb, 0xae, 0x0f, 0x80, 0x88 }, { 0x00, 0x54, 0x98, 0x07, 0x45, 0x60, 0x8c, 0xff } },
	{ 0x478, { 0x00, 0x00, 0x24, 0x72, 0x00, 0x00, 0x15, 0xc6 }, { 0x00, 0x00, 0x24, 0x72, 0x00, 0x00, 0x15, 0xc6 } },
	{ 0x738, { 0xc9, 0xe4, 0xa8, 0x98, 0x29, 0x0d, 0xcc, 0xba }, { 0xef, 0x80, 0x15, 0xfc, 0x29, 0x0a, 0xe2, 0x0a } },
	{ 0x1c0, { 0xd9, 0x19, 0x3e, 0xdc, 0x23, 0x13, 0x62, 0x89 }, { 0xde, 0x4c, 0x92, 0x0a, 0xc3, 0x65, 0xa3, 0x02 } },
	{ 0x480, { 0x00, 0x00, 0x7e, 0x75, 0x00, 0x00, 0x39, 0xcd }, { 0x00, 0x00, 0x7e, 0x75, 0x00, 0x00, 0x39, 0xcd } },
	{ 0x740, { 0x67, 0xe1, 0x36, 0x95, 0x45, 0x0f, 0x47, 0xc2 }, { 0x2e, 0x81, 0xa4, 0xfe, 0xc4, 0x08, 0xd6, 0x0e } },
	{ 0x1c8, { 0x59, 0x18, 0xe6, 0xe0, 0x76, 0x15, 0x28, 0x88 }, { 0xd3, 0x3d, 0x27, 0x0c, 0x63, 0x6f, 0x4a, 0x02 } },
	{ 0x488, { 0x00, 0x00, 0x5c, 0x79, 0x00, 0x00, 0x52, 0xd7 }, { 0x00, 0x00, 0x5b, 0x79, 0x00, 0x00, 0x52, 0xd7 } },
	{ 0x748, { 0xc4, 0xe1, 0x6c, 0x8e, 0x82, 0x0f, 0xbe, 0xcf }, { 0x10, 0x81, 0x96, 0xfe, 0xdb, 0x01, 0x3d, 0x10 } },
	{ 0x1d0, { 0xeb, 0x18, 0xdf, 0xdf, 0x58, 0x15, 0x86, 0x88 }, { 0x46, 0x40, 0x2a, 0x0c, 0xfc, 0x6d, 0xa2, 0x02 } },
	{ 0x490, { 0x00, 0x00, 0xec, 0x78, 0x00, 0x00, 0x0a, 0xd6 }, { 0x00, 0x00, 0xeb, 0x78, 0x00, 0x00, 0x0a, 0xd6 } },
	{ 0x750, { 0x98, 0xe1, 0xa3, 0x8f, 0xd4, 0x0f, 0x30, 0xcd }, { 0x1b, 0x81, 0xa8, 0xfe, 0xc6, 0x03, 0x3f, 0x10 } },
	{ 0x1d8, { 0xe8, 0x19, 0x01, 0xde, 0xbf, 0x14, 0x26, 0x89 }, { 0x48, 0x45, 0x12, 0x0c, 0xe6, 0x6a, 0x05, 0x03 } },
	{ 0x498, { 0x00, 0x00, 0xdc, 0x77, 0x00, 0x00, 0x18, 0xd3 }, { 0x00, 0x00, 0xdc, 0x77, 0x00, 0x00, 0x18, 0xd3 } },
	{ 0x758, { 0x65, 0xe1, 0x23, 0x92, 0x47, 0x10, 0x37, 0xc8 }, { 0x3b, 0x81, 0xa9, 0xfe, 0x1e, 0x07, 0x1f, 0x10 } },
	{ 0x1e0, { 0x99, 0x1a, 0x99, 0xdc, 0xd7, 0x13, 0x8f, 0x89 }, { 0xd1, 0x49, 0xdd, 0x0b, 0xd8, 0x67, 0x0a, 0x03 } },
	{ 0x4a0, { 0x00, 0x00, 0xaa, 0x76, 0x00, 0x00, 0x06, 0xd0 }, { 0x00, 0x00, 0xa9, 0x76, 0x00, 0x00, 0x06, 0xd0 } },
	{ 0x760, { 0x94, 0xe1, 0x9c, 0x94, 0x92, 0x10, 0x95, 0xc3 }, { 0x62, 0x81, 0x51, 0xfe, 0xe9, 0x09, 0xc8, 0x0f } },
	{ 0x1e8, { 0x88, 0x1a, 0x01, 0xdc, 0xe8, 0x12, 0x91, 0x89 }, { 0xc3, 0x4b, 0x99, 0x0b, 0x7b, 0x66, 0x6b, 0x02 } },
	{ 0x4a8, { 0x00, 0x00, 0x17, 0x76, 0x00, 0x00, 0xa1, 0xce }, { 0x00, 0x00, 0x17, 0x76, 0x00, 0x00, 0xa1, 0xce } },
	{ 0x768, { 0x62, 0xe2, 0xbb, 0x95, 0xa5, 0x10, 0x45, 0xc1 }, { 0x71, 0x81, 0x8a, 0xfd, 0x50, 0x0b, 0x38, 0x0f } },
	{ 0x1f0, { 0x40, 0x1a, 0x31, 0xdb, 0x53, 0x11, 0x84, 0x89 }, { 0x50, 0x4f, 0xf5, 0x0a, 0xd9, 0x63, 0x64, 0x01 } },
	{ 0x4b0, { 0x00, 0x00, 0xd9, 0x74, 0x00, 0x00, 0xc1, 0xcb }, { 0x00, 0x00, 0xd8, 0x74, 0x00, 0x00, 0xc1, 0xcb } },
	{ 0x770, { 0x8e, 0xe3, 0xa7, 0x97, 0x89, 0x10, 0x93, 0xbd }, { 0x83, 0x81, 0x67, 0xfc, 0xf9, 0x0c, 0x38, 0x0e } },
	{ 0x1f8, { 0x72, 0x19, 0x77, 0xda, 0x5e, 0x0e, 0x2c, 0x89 }, { 0x4b, 0x56, 0x8f, 0x09, 0x0b, 0x5e, 0x89, 0xff } },
	{ 0x4b8, { 0x00, 0x00, 0x98, 0x71, 0x00, 0x00, 0x03, 0xc5 }, { 0x00, 0x00, 0x97, 0x71, 0x00, 0x00, 0x03, 0xc5 } },
	{ 0x778, { 0x8d, 0xe5, 0xcc, 0x9b, 0xea, 0x0f, 0x96, 0xb6 }, { 0x92, 0x81, 0x7b, 0xfa, 0xd8, 0x0e, 0x1e, 0x0c } },
	{ 0x200, { 0xf4, 0x16, 0xc2, 0xda, 0x6a, 0x0a, 0x2a, 0x88 }, { 0x74, 0x5c, 0x94, 0x07, 0x18, 0x58, 0xf1, 0xfb } },
	{ 0x4c0, { 0x00, 0x00, 0x86, 0x6d, 0x00, 0x00, 0xc2, 0xbd }, { 0x00, 0x00, 0x85, 0x6d, 0x00, 0x00, 0xc3, 0xbd } },
	{ 0x780, { 0x48, 0xe9, 0xef, 0x9f, 0xb5, 0x0e, 0xdd, 0xaf }, { 0x85, 0x81, 0x64, 0xf7, 0x6d, 0x0f, 0x88, 0x08 } },
};
static DWORD g_rk_seen[512][2];                  /* by a resource's address: it, and the data it was last seen at */
static DWORD g_rk_header[4];
static int g_rk_on = -1;
static unsigned g_rk_applied;
static void rk_patch(DWORD h, DWORD size_left)
{
	unsigned k, n = 0;
	for (k = 0; k < 51; k++)
	{
		DWORD at = h + g_rk_patch[k].rel;
		if (g_rk_patch[k].rel + 8u > size_left) continue;
		if (!memcmp((void *)(ULONG_PTR)at, g_rk_patch[k].orig, 8)) { memcpy((void *)(ULONG_PTR)at, g_rk_patch[k].fix, 8); n++; }
	}
	if (n && g_rk_applied++ < 8) h2n_log("       the rocket launcher's animation fix: %u of 51 places written (the animation data at %08lx)\n", n, h);
}
/* s10m: retail's animation_cache_get_resource (0x1237e0) through helpers.S h2n_anim_get_hook as well, with the fix on:
 * retail's own object creation and drawing code reaches it without passing the source's function. Its first five bytes
 * (push ebx; push esi; mov esi, eax; push edi) and the instruction after them (mov ebx, esi) are checked first. */
extern char h2n_anim_get_hook[];
void __cdecl h2n_animation_retail(void)
{
	static const BYTE head[7] = { 0x53, 0x56, 0x8b, 0xf0, 0x57, 0x8b, 0xde };
	BYTE *p = (BYTE *)(ULONG_PTR)0x1237e0;
	if (g_rk_on < 0) g_rk_on = h2n_unlock_wanted() && h2n_env_int("H2N_ROCKETFIX", 1) ? 1 : 0;
	if (!g_rk_on || !h2n_retail_code_loaded()) return;
	if (memcmp(p, head, sizeof(head)))
	{
		h2n_log("       the rocket launcher's animation fix: retail's 0x1237e0 is not as expected; its own calls are left as they are\n");
		return;
	}
	p[0] = 0xe9;
	*(DWORD *)(p + 1) = (DWORD)(ULONG_PTR)h2n_anim_get_hook - (0x1237e0 + 5);
	h2n_log("       the rocket launcher's animation fix: retail's animation_cache_get_resource (0x1237e0) goes through the layer too\n");
}
void __cdecl h2n_animation_resource(long resource, long size, void *data)
{
	DWORD r = (DWORD)resource, d = (DWORD)(ULONG_PTR)data, slot = (r >> 4) & 511u, k, i;
	if (g_rk_on < 0) g_rk_on = h2n_unlock_wanted() && h2n_env_int("H2N_ROCKETFIX", 1) ? 1 : 0;   /* with Unlock Performance only (it is seen above 30 fps) */
	if (!g_rk_on || !d || size <= 40 || size > 0x400000) return;
	for (k = 0; k < 4; k++)                                                       /* known: still patched? (a reload puts the old bytes back) */
		if (g_rk_header[k] && g_rk_header[k] >= d && g_rk_header[k] + 0x788u <= d + (DWORD)size)
		{
			if (memcmp((void *)(ULONG_PTR)g_rk_header[k], g_rk_signature, 40)) { g_rk_header[k] = 0; continue; }   /* the block holds other data now */
			if (!memcmp((void *)(ULONG_PTR)(g_rk_header[k] + 0x180), g_rk_patch[0].orig, 8)) rk_patch(g_rk_header[k], d + (DWORD)size - g_rk_header[k]);
			return;
		}
	if (g_rk_seen[slot][0] == r && g_rk_seen[slot][1] == d) return;              /* seen in this block already */
	g_rk_seen[slot][0] = r; g_rk_seen[slot][1] = d;
	for (i = 0; i + 40u <= (DWORD)size; i++)
	{
		const BYTE *p = (const BYTE *)memchr((const void *)(ULONG_PTR)(d + i), 0x03, (DWORD)size - 40u - i + 1u);
		if (!p) break;
		i = (DWORD)(ULONG_PTR)p - d;
		if (memcmp(p, g_rk_signature, 40)) continue;
		for (k = 0; k < 4 && g_rk_header[k] && g_rk_header[k] != (DWORD)(ULONG_PTR)p; k++) ;
		if (k < 4) g_rk_header[k] = (DWORD)(ULONG_PTR)p; else g_rk_header[0] = (DWORD)(ULONG_PTR)p;
		rk_patch((DWORD)(ULONG_PTR)p, (DWORD)size - i);
	}
}
void __cdecl h2n_ip_tick_before(void)
{
	aim_log();                                    /* (H2N_AIMLOG=1 only: where the shots go) */
	LONGLONG t0 = ip_clock();
	h2n_perf_tick();
	g_ptc_hold = 0;
	if (g_ca_set || g_ca_n) ca_restore();         /* (none should be set here) */
#ifdef H2N_SHIELD_TEST
	shield_test_tick();
#endif
	if (g_own_set) own_restore();                 /* (none should be set here) */
	if (g_blended) restore_nodes();
	snapshot();
	g_ip_cost += ip_clock() - t0;
}
void __cdecl h2n_ip_director_before(void)
{
	LONGLONG t0;
	h2n_perf(3);                                  /* (s9q: the cameras start) */
	t0 = ip_clock();
	if (g_ca_set || g_ca_n) ca_restore();
	if (g_own_set) own_restore();
	if (g_blended) restore_nodes();
	g_frame_blended = 0;
	blend();
	g_ip_cost += ip_clock() - t0;
}
void __cdecl h2n_ip_draw_before(void)
{
	static double logged;
	LONGLONG t0;
	h2n_perf(4);                                  /* (s9q: the draw starts) */
	t0 = ip_clock();
	g_ptc_hold = (unsigned char)ip_on();
	if (g_own_set) own_restore();                 /* the units' own fields: back before the draw (the nodes after it) */
	{
		static int carry = -1;                    /* (s9q, for comparing: H2N_CARRY=0 leaves what the objects carry where the tick put it) */
		if (carry < 0) carry = h2n_env_int("H2N_CARRY", 1) ? 1 : 0;
		if (carry && g_frame_blended && g_blended) ca_apply();  /* what the blended objects carry, moved with them for the draw */
	}
	g_ip_cost += ip_clock() - t0;
	if (g_frames_blended && h2n_env_int("H2N_INTERP", 1) == 2)
	{
		double now = h2n_seconds();
		if (now - logged > 1.0)
		{
			logged = now;
			DWORD st = U32(CAM_SCRIPT), cg = U32(CINEMATIC), options = U32(OPTIONS);
			h2n_log("       in-between frames: %u objects and blocks, %d nodes kept; this frame at %.2f of the way to the next tick, %d blended; "
				"%u jumps, %u turns too far, %u weapon changes, %u left out so far; vehicle cameras %d; render-only %u objects %u nodes; "
				"%s%s, scripted camera mode %d (user 0's view %s), its frames blended %u, cuts %u; %u moved by animation; %u drawn as the tick left them at a looping device's wrap; carried: %u spheres, %u lights, %u particle places, %u emitters (%u full); first-person legs turned on %u frames (the last by %.2f degrees); %u blended unit fields written by the game before the draw (the last +0x%x of an object of type %d)\n",
				g_ent_n, g_nodes, g_alpha, g_blended, g_jumps, g_turns, g_weapon_changes, g_left_out, g_hk_n, g_ro_objects, g_ro_nodes,
				pointer(options) && U32(options + 8) == 3 ? "main menu" : "in a game", pointer(cg) && U8(cg + 5) ? ", cinematic" : "",
				pointer(st) ? (int)(short)U16(st + 2) : -1, cine_cam(0) ? "its" : "not its", g_cc_blends, g_cc_cuts, g_moved_n, g_wraps,
				g_ca_spheres, g_ca_lights, g_ca_places, g_ca_emitters, g_ca_full, g_fp_turned, g_fp_last * 57.29578f, g_own_changed, g_own_changed_off, g_own_changed_type);
		}
	}
}
void __cdecl h2n_ip_draw_after(void)
{
	static int trace = -1;
	LONGLONG t0 = ip_clock();
	if (trace < 0) trace = h2n_env_int("H2N_INTERP", 1) == 3;
	if (trace) { trace_frame(); render_only_check(); }
	g_ro_check_n = 0;
	g_trace_blend = 0;
	g_frame_blended = 0;
	if (g_ca_set || g_ca_n) ca_restore();          /* what they carry, first */
	if (g_own_set) own_restore();
	if (g_blended) restore_nodes();
	g_ip_cost += ip_clock() - t0;
}
static unsigned g_ptc_kept;
void __cdecl h2n_ip_particle(void *system)       /* before a particle system's update (0x174990 -> 0x174a30), 6i.35 */
{
	BYTE *s = (BYTE *)system;
	if (!g_ptc_hold || !s) return;
	if (s[0xd] & 2) { s[0xd] |= 1; g_ptc_kept++; }   /* spawned at the tick (bit 1): counts as having spawned again (bit 0) */
}

/* H2Classic: anti-aliasing of the game's 3D views (gfx_aa.c). gfx9.c calls aa_scene() when a player's view has been
 * drawn and before the game draws that view's HUD (the scene / HUD split: unknown_023540.cpp, function_2ba10, around
 * its call of function_13680), so the HUD and the menus are left as the game draws them. */
#ifndef H2N_GFX_AA_H
#define H2N_GFX_AA_H

enum { AA_OFF = 0, AA_FXAA = 1, AA_SMAA = 2, AA_TAA = 3, AA_DLSS = 4 };

typedef struct aa_view
{
	IDirect3DTexture9 *color;          /* the surface the view was drawn into (the game's picture), as a texture */
	IDirect3DSurface9 *color_surface;
	IDirect3DTexture9 *depth;          /* its depth buffer as a texture (INTZ), or NULL where the device has none */
	unsigned full_width, full_height;  /* the real size of the picture */
	unsigned depth_width, depth_height;/* and of the depth buffer (at least the picture's) */
	unsigned x, y, width, height;      /* the view's rectangle in them, in real pixels */
	float half_width, half_height;     /* half the view's size in the console's pixels (its projection's terms) */
} aa_view;

int aa_mode(void);                                          /* H2N_AA: off, fxaa, smaa, taa, dlss */
void aa_frame(void);                                        /* each frame shown: the next jitter (taa) */
int aa_jitter(float *x, float *y);                          /* this frame's sub-pixel offset of the 3D, in real pixels; 0 = none */
void aa_scene_begin(void);                                  /* a view's 3D begins: its camera is read from the game */
void aa_scene_constants(const float (*c)[4]);               /* the vertex constants of the view's first 3D draw */
void aa_first_person(IDirect3DDevice9 *device, const aa_view *view);   /* the depth just before the first-person draws (taa) */
int aa_scene(IDirect3DDevice9 *device, const aa_view *view);           /* the view's 3D is done: anti-alias its rectangle */
void aa_release(void);                                      /* the device's surfaces are going away */
void aa_report(void);

#endif

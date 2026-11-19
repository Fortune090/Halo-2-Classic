/* H2Classic: the game's side of DLSS (gfx_dlss.c); the helper is dlss/h2classic_dlss.c, the shared layout dlss_ipc.h */
#ifndef H2N_GFX_DLSS_H
#define H2N_GFX_DLSS_H

int dlss_start(IDirect3D9 *d3d);       /* the Direct3D 9Ex object the game's device came from; starts DLSS Helper.exe */
int dlss_state(void);                  /* DLSS_STATE_READY when views can go to the helper (and the helper's log copied in) */
int dlss_setup(unsigned slot, unsigned w, unsigned h, HANDLE handles[4], int *direct);   /* colour, motion, depth, output */
int dlss_evaluate(unsigned slot, unsigned w, unsigned h, float jitter_x, float jitter_y, int reset, float frame_ms);
void dlss_release(unsigned slot);
void dlss_failed(const char *why);     /* DLSS off for the rest of the run (TAA goes on) */
void dlss_stop(void);
void dlss_report(void);

#endif

/* H2Classic: the console's audio "global processor" (GP), a DSP56300-family core, run by interpretation.
 *
 * The game downloads its own effects image (the XBE's DSPImage section: 15 effects - reverbs, crosstalk, chorus,
 * distortion ...). dsound.lib decrypts each effect's code on the CPU and the GP's loader program (DefaultExec, also
 * from the XBE) copies code to P:0x171 and the effects' states to X:0x80. Every 32-sample frame the GP calls P:0x171,
 * which runs the effects in order; they read and write the 32 mixbins at X:0xC00-0xFFF (also visible at X:0x1400) and
 * reach their delay lines in "scratch" (system memory) through the GP's DMA engine. This file is that core. */
#ifndef H2N_APU_DSP_H
#define H2N_APU_DSP_H
#include <stdint.h>

#define DSP_XSIZE 0x1800
#define DSP_YSIZE 0x0800
#define DSP_PSIZE 0x1000

typedef struct dsp_op
{
	uint16_t kind;
	uint8_t size;
	uint8_t alu;
	uint8_t a, b, c, d;
	uint32_t imm;
} dsp_op;

typedef struct dsp_core
{
	uint32_t x[DSP_XSIZE], y[DSP_YSIZE], p[DSP_PSIZE];
	dsp_op op[DSP_PSIZE];
	uint8_t decoded[DSP_PSIZE];
	int64_t acc[2];                 /* A, B: 56-bit, sign-extended */
	uint32_t xr[2], yr[2];          /* X0 X1, Y0 Y1 */
	uint32_t r[8], n[8], m[8];
	uint32_t sr, omr, sp, sc, sz, la, lc, ep, vba;
	uint32_t ssh[16], ssl[16];
	uint32_t pc;
	/* peripherals */
	uint32_t dma_next, dma_start, dma_control, dma_config, irq_c4, irq_c5;
	uint8_t *scratch;
	uint32_t scratch_size;
	uint64_t count;                 /* instructions executed */
	uint32_t unknown_count, last_unknown;
	uint32_t rep_count;
	int halted, in_rep;
} dsp_core;

void dsp_reset(dsp_core *d);
/* run from 'pc' as a subroutine until it returns; returns 0 if it returned, -1 if it ran over 'budget' or stopped */
int dsp_call(dsp_core *d, uint32_t pc, uint32_t budget);
void dsp_invalidate(dsp_core *d);
/* text for the instruction at p:pc (for logs) */
const char *dsp_describe(dsp_core *d, uint32_t pc);
int dsp_step(dsp_core *d);

#endif

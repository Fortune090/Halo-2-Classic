/* H2Classic: a DSP56300 interpreter for the console's audio global processor (see apu_dsp.h).
 * Written from the DSP56300 Family Manual's instruction set; it covers what the GP's loader (DefaultExec) and the
 * game's effects image use (checked by decoding every word of both). The DMA engine and the few peripheral registers
 * the GP programs use are modelled after what those programs do with them. */
#include <string.h>
#include <stdio.h>
#include "apu_dsp.h"

#define SR_C (1u << 0)
#define SR_V (1u << 1)
#define SR_Z (1u << 2)
#define SR_N (1u << 3)
#define SR_U (1u << 4)
#define SR_E (1u << 5)
#define SR_L (1u << 6)
#define SR_S (1u << 7)
#define SR_S0 (1u << 10)
#define SR_S1 (1u << 11)
#define SR_LF (1u << 14)
#define SR_RM (1u << 19)

static inline int32_t sx24(uint32_t v) { return ((int32_t)(v << 8)) >> 8; }
static inline int64_t sx56(int64_t v) { return (int64_t)((uint64_t)v << 8) >> 8; }
#define M56 0x00ffffffffffffffULL

/* ------------------------------------------------------------------ memory */
static uint32_t periph_read(dsp_core *d, uint32_t a);
static void periph_write(dsp_core *d, uint32_t a, uint32_t v);

static inline uint32_t *xptr(dsp_core *d, uint32_t a)
{
	if (a < 0x1400) return &d->x[a];
	if (a < 0x1800) return &d->x[a - 0x800];          /* 0x1400-0x17ff is the mixbuffer at 0x0c00-0x0fff */
	return 0;
}
static inline uint32_t mem_read(dsp_core *d, int space, uint32_t a)
{
	a &= 0xffffff;
	if (space == 0)
	{
		uint32_t *p = xptr(d, a);
		if (p) return *p;
		if (a >= 0xffff80) return periph_read(d, a);
		return 0;
	}
	if (space == 1) return a < DSP_YSIZE ? d->y[a] : 0;
	return a < DSP_PSIZE ? d->p[a] : 0;
}
static inline void mem_write(dsp_core *d, int space, uint32_t a, uint32_t v)
{
	a &= 0xffffff; v &= 0xffffff;
	if (space == 0)
	{
		uint32_t *p = xptr(d, a);
		if (p) *p = v;
		else if (a >= 0xffff80) periph_write(d, a, v);
		return;
	}
	if (space == 1) { if (a < DSP_YSIZE) d->y[a] = v; return; }
	if (a < DSP_PSIZE) { d->p[a] = v; d->decoded[a] = 0; }
}

/* ------------------------------------------------------------------ DMA */
static void dma_run(dsp_core *d)
{
	int guard = 0;
	while (!(d->dma_next & 0x4000) && guard++ < 4096)
	{
		uint32_t node = d->dma_next & 0x3fff;
		uint32_t next = mem_read(d, 0, node), control = mem_read(d, 0, node + 1), count = mem_read(d, 0, node + 2);
		uint32_t dsp = mem_read(d, 0, node + 3), offset = mem_read(d, 0, node + 4);
		uint32_t base = mem_read(d, 0, node + 5), size = mem_read(d, 0, node + 6) + 1;
		int write = (control >> 1) & 1, writeback = (control >> 4) & 1, buffer = (control >> 5) & 0xf;
		int format = (control >> 10) & 7, item = format == 1 ? 2 : 4;
		int space; uint32_t addr; uint32_t i;
		d->dma_next = next;
		if (dsp < 0x1800) { space = 0; addr = dsp; }
		else if (dsp < 0x2000) { space = 1; addr = dsp - 0x1800; }
		else if (dsp >= 0x2800 && dsp < 0x3800) { space = 2; addr = dsp - 0x2800; }
		else continue;
		for (i = 0; i < count; i++)
		{
			uint32_t byte;
			if (buffer == 0xe) byte = base + (size ? (offset + i * item) % size : 0);
			else byte = offset + i * item;
			if (!d->scratch || byte + item > d->scratch_size) continue;
			if (write)
			{
				uint32_t v = mem_read(d, space, addr + i);
				if (item == 2) { int16_t s = (int16_t)(sx24(v) >> 8); memcpy(d->scratch + byte, &s, 2); }
				else memcpy(d->scratch + byte, &v, 4);
			}
			else
			{
				uint32_t v;
				if (item == 2) { int16_t s; memcpy(&s, d->scratch + byte, 2); v = (uint32_t)(s << 8); }
				else memcpy(&v, d->scratch + byte, 4);
				mem_write(d, space, addr + i, v);
			}
		}
		if (writeback)
		{
			uint32_t o = offset + count * item;
			if (buffer == 0xe && size) o %= size;
			mem_write(d, 0, node + 4, o);
		}
	}
	d->irq_c5 |= 0x80;                       /* end of list */
}

static uint32_t periph_read(dsp_core *d, uint32_t a)
{
	switch (a)
	{
	case 0xffffd4: return d->dma_next;
	case 0xffffd5: return d->dma_start;
	case 0xffffd6: return d->dma_control;
	case 0xffffd7: return d->dma_config;
	case 0xffffc4: return d->irq_c4;
	case 0xffffc5: return d->irq_c5;
	case 0xffffb3: return (uint32_t)(d->count & 0xffffff);
	}
	return 0;
}
static void periph_write(dsp_core *d, uint32_t a, uint32_t v)
{
	switch (a)
	{
	case 0xffffd4: d->dma_next = v; break;
	case 0xffffd5: d->dma_start = v; break;
	case 0xffffd7: d->dma_config = v; break;
	case 0xffffc4: d->irq_c4 = v; break;
	case 0xffffc5: d->irq_c5 &= ~v; break;
	case 0xffffd6:
		switch (v & 7)
		{
		case 1: d->dma_control = (d->dma_control | 0x10) & ~0x20; if (!(d->dma_control & 8)) dma_run(d); break;
		case 2: d->dma_control = (d->dma_control & ~0x10) | 0x20; break;
		case 3: d->dma_control |= 8; break;
		case 4: d->dma_control &= ~8; if (d->dma_control & 0x10) dma_run(d); break;
		case 5: d->dma_control &= ~0x10; break;
		}
		break;
	}
}

/* ------------------------------------------------------------------ registers */
static inline int scaling(dsp_core *d) { return (d->sr >> 10) & 3; }

static uint32_t acc_read24(dsp_core *d, int w)
{
	int64_t v = d->acc[w], t;
	int s = scaling(d);
	t = s == 0 ? v >> 24 : s == 1 ? v >> 25 : v >> 23;
	if (t > 0x7fffff) { d->sr |= SR_L; return 0x7fffff; }
	if (t < -0x800000) { d->sr |= SR_L; return 0x800000; }
	return (uint32_t)t & 0xffffff;
}
static inline void acc_write24(dsp_core *d, int w, uint32_t v) { d->acc[w] = (int64_t)sx24(v) << 24; }

static uint32_t reg_read(dsp_core *d, int code)
{
	switch (code)
	{
	case 0x04: return d->xr[0]; case 0x05: return d->xr[1]; case 0x06: return d->yr[0]; case 0x07: return d->yr[1];
	case 0x08: return (uint32_t)d->acc[0] & 0xffffff; case 0x09: return (uint32_t)d->acc[1] & 0xffffff;
	case 0x0a: return (uint32_t)(int32_t)(int8_t)(d->acc[0] >> 48) & 0xffffff;
	case 0x0b: return (uint32_t)(int32_t)(int8_t)(d->acc[1] >> 48) & 0xffffff;
	case 0x0c: return (uint32_t)(d->acc[0] >> 24) & 0xffffff; case 0x0d: return (uint32_t)(d->acc[1] >> 24) & 0xffffff;
	case 0x0e: return acc_read24(d, 0); case 0x0f: return acc_read24(d, 1);
	}
	if (code >= 0x10 && code < 0x18) return d->r[code & 7];
	if (code >= 0x18 && code < 0x20) return d->n[code & 7];
	if (code >= 0x20 && code < 0x28) return d->m[code & 7];
	switch (code)
	{
	case 0x2a: return d->ep; case 0x30: return d->vba; case 0x31: return d->sc; case 0x38: return d->sz;
	case 0x39: return d->sr; case 0x3a: return d->omr; case 0x3b: return d->sp;
	case 0x3c: { uint32_t v = d->ssh[d->sp & 15]; if (d->sp) d->sp--; return v; }
	case 0x3d: return d->ssl[d->sp & 15];
	case 0x3e: return d->la; case 0x3f: return d->lc;
	}
	return 0;
}
static void reg_write(dsp_core *d, int code, uint32_t v)
{
	v &= 0xffffff;
	switch (code)
	{
	case 0x04: d->xr[0] = v; return; case 0x05: d->xr[1] = v; return; case 0x06: d->yr[0] = v; return; case 0x07: d->yr[1] = v; return;
	case 0x08: d->acc[0] = (d->acc[0] & ~0xffffffLL) | v; return;
	case 0x09: d->acc[1] = (d->acc[1] & ~0xffffffLL) | v; return;
	case 0x0a: d->acc[0] = sx56((d->acc[0] & 0xffffffffffffLL) | ((int64_t)(v & 0xff) << 48)); return;
	case 0x0b: d->acc[1] = sx56((d->acc[1] & 0xffffffffffffLL) | ((int64_t)(v & 0xff) << 48)); return;
	case 0x0c: d->acc[0] = sx56((d->acc[0] & ~(0xffffffLL << 24)) | ((int64_t)v << 24)); return;
	case 0x0d: d->acc[1] = sx56((d->acc[1] & ~(0xffffffLL << 24)) | ((int64_t)v << 24)); return;
	case 0x0e: acc_write24(d, 0, v); return; case 0x0f: acc_write24(d, 1, v); return;
	}
	if (code >= 0x10 && code < 0x18) { d->r[code & 7] = v; return; }
	if (code >= 0x18 && code < 0x20) { d->n[code & 7] = v; return; }
	if (code >= 0x20 && code < 0x28) { d->m[code & 7] = v; return; }
	switch (code)
	{
	case 0x2a: d->ep = v; return; case 0x30: d->vba = v; return; case 0x31: d->sc = v; return; case 0x38: d->sz = v; return;
	case 0x39: d->sr = v; return; case 0x3a: d->omr = v; return; case 0x3b: d->sp = v & 0x1f; return;
	case 0x3c: d->sp = (d->sp + 1) & 15; d->ssh[d->sp] = v; return;
	case 0x3d: d->ssl[d->sp & 15] = v; return;
	case 0x3e: d->la = v; return; case 0x3f: d->lc = v; return;
	}
}

/* ------------------------------------------------------------------ address generation */
static uint32_t mod_add(dsp_core *d, int rn, int32_t delta)
{
	uint32_t r = d->r[rn], m = d->m[rn];
	if (m == 0xffffff) return (r + (uint32_t)delta) & 0xffffff;
	if (m == 0)
	{
		/* reverse-carry add */
		uint32_t a = r, b = (uint32_t)delta & 0xffffff, res = 0, carry = 0; int i;
		for (i = 23; i >= 0; i--)
		{
			uint32_t s = ((a >> i) & 1) + ((b >> i) & 1) + carry;
			res |= (s & 1) << i; carry = s >> 1;
		}
		return res;
	}
	if (m <= 0x7fff)
	{
		uint32_t size = m + 1, block = 1, base;
		int32_t rel;
		while (block < size) block <<= 1;
		if (delta >= 0 ? (uint32_t)delta >= block && ((uint32_t)delta % block) == 0 : ((uint32_t)-delta) >= block && ((uint32_t)-delta % block) == 0)
			return (r + (uint32_t)delta) & 0xffffff;
		base = r & ~(block - 1);
		rel = (int32_t)(r - base) + delta;
		rel %= (int32_t)size;
		if (rel < 0) rel += size;
		return (base + (uint32_t)rel) & 0xffffff;
	}
	return (r + (uint32_t)delta) & 0xffffff;
}
/* effective address for MMMRRR; updates Rn; ext is the extension word */
static uint32_t ea(dsp_core *d, int mmm, int rrr, uint32_t ext)
{
	uint32_t a;
	switch (mmm)
	{
	case 0: a = d->r[rrr]; d->r[rrr] = mod_add(d, rrr, -(int32_t)sx24(d->n[rrr])); return a;
	case 1: a = d->r[rrr]; d->r[rrr] = mod_add(d, rrr, sx24(d->n[rrr])); return a;
	case 2: a = d->r[rrr]; d->r[rrr] = mod_add(d, rrr, -1); return a;
	case 3: a = d->r[rrr]; d->r[rrr] = mod_add(d, rrr, 1); return a;
	case 4: return d->r[rrr];
	case 5: return mod_add(d, rrr, sx24(d->n[rrr]));
	case 7: d->r[rrr] = mod_add(d, rrr, -1); return d->r[rrr];
	}
	return ext;            /* absolute (110 000) */
}
static inline int ea_is_imm(int mmm, int rrr) { return mmm == 6 && rrr == 4; }
static inline int ea_has_ext(int mmm, int rrr) { return mmm == 6 && (rrr == 0 || rrr == 4); }

/* ------------------------------------------------------------------ condition codes */
static void flags_nzue(dsp_core *d, int64_t v)
{
	int s = scaling(d);
	int64_t top;
	uint32_t sr = d->sr & ~(SR_N | SR_Z | SR_U | SR_E);
	if (v < 0) sr |= SR_N;
	if (v == 0) sr |= SR_Z;
	top = s == 0 ? v >> 47 : s == 1 ? v >> 48 : v >> 46;
	if (top != 0 && top != -1) sr |= SR_E;
	{
		int hb = s == 0 ? 47 : s == 1 ? 48 : 46;
		if (((v >> hb) & 1) == ((v >> (hb - 1)) & 1)) sr |= SR_U;
	}
	d->sr = sr;
}
static int cond(dsp_core *d, int cc)
{
	uint32_t sr = d->sr;
	int c = sr & 1, v = (sr >> 1) & 1, z = (sr >> 2) & 1, n = (sr >> 3) & 1, u = (sr >> 4) & 1, e = (sr >> 5) & 1, l = (sr >> 6) & 1;
	switch (cc)
	{
	case 0: return !c; case 1: return !(n ^ v); case 2: return !z; case 3: return !n;
	case 4: return !(z | (!u & !e)); case 5: return !e; case 6: return !l; case 7: return !(z | (n ^ v));
	case 8: return c; case 9: return n ^ v; case 10: return z; case 11: return n;
	case 12: return z | (!u & !e); case 13: return e; case 14: return l; case 15: return z | (n ^ v);
	}
	return 0;
}

/* ------------------------------------------------------------------ data ALU */
static int64_t rnd(dsp_core *d, int64_t v)
{
	int s = scaling(d), bit = s == 0 ? 23 : s == 1 ? 24 : 22;
	int64_t half = (int64_t)1 << bit, mask = (half << 1) - 1;
	v += half;
	if (!(d->sr & SR_RM) && (v & mask) == 0) v &= ~(half << 1);
	v &= ~mask;
	return sx56(v);
}
static int64_t add56(dsp_core *d, int64_t a, int64_t b, int carry_in, int sub)
{
	uint64_t ua = (uint64_t)a & M56, ub = (uint64_t)b & M56, r;
	uint32_t sr = d->sr & ~(SR_C | SR_V);
	int64_t res;
	if (!sub)
	{
		r = ua + ub + (uint64_t)carry_in;
		if (r >> 56) sr |= SR_C;
	}
	else
	{
		r = ua - ub - (uint64_t)carry_in;
		if (ua < ub + (uint64_t)carry_in) sr |= SR_C;
	}
	res = sx56((int64_t)r);
	{
		int sa = a < 0, sb = (sub ? -b : b) < 0, sres = res < 0;
		if (!sub ? (sa == (b < 0) && sres != sa) : (sa != (b < 0) && sres != sa)) sr |= SR_V | SR_L;
	}
	(void)0;
	d->sr = sr;
	flags_nzue(d, res);
	return res;
}
static inline int64_t src_reg48(dsp_core *d, uint32_t hi, uint32_t lo) { return sx56(((int64_t)sx24(hi) << 24) | lo); }
static inline int64_t src24(uint32_t v) { return (int64_t)sx24(v) << 24; }

static void alu(dsp_core *d, int op)
{
	int D = (op >> 3) & 1, S = D ^ 1;
	int64_t *dst = &d->acc[D];
	if (op & 0x80)
	{
		static const uint8_t q1[8] = { 0, 2, 1, 3, 0, 2, 1, 3 };   /* 0 x0 1 x1 2 y0 3 y1 */
		static const uint8_t q2[8] = { 0, 2, 0, 2, 3, 0, 2, 1 };
		uint32_t regs[4] = { d->xr[0], d->xr[1], d->yr[0], d->yr[1] };
		int qq = (op >> 4) & 7;
		int64_t p = ((int64_t)sx24(regs[q1[qq]]) * (int64_t)sx24(regs[q2[qq]])) << 1;
		if (op & 4) p = -p;
		if ((op & 3) >= 2) { int64_t r = sx56(*dst + p); uint32_t keep = d->sr & SR_C; int64_t a = *dst;
			d->sr &= ~SR_V;
			if (((a < 0) == (p < 0)) && ((r < 0) != (a < 0))) d->sr |= SR_V | SR_L;
			*dst = r; (void)keep; }
		else { *dst = sx56(p); d->sr &= ~SR_V; }
		if (op & 1) *dst = rnd(d, *dst);
		flags_nzue(d, *dst);
		return;
	}
	if (op >= 0x40)
	{
		int j = (op >> 4) & 7, k = op & 7;
		uint32_t s = j == 4 ? d->xr[0] : j == 5 ? d->yr[0] : j == 6 ? d->xr[1] : d->yr[1];
		int64_t sv = src24(s);
		switch (k)
		{
		case 0: *dst = add56(d, *dst, sv, 0, 0); return;
		case 1: *dst = sv; return;                       /* tfr */
		case 2: case 3: case 6:
		{
			uint32_t a1 = (uint32_t)(*dst >> 24) & 0xffffff, r = k == 2 ? a1 | s : k == 3 ? a1 ^ s : a1 & s;
			*dst = (*dst & ~(0xffffffLL << 24)) | ((int64_t)r << 24); *dst = sx56(*dst);
			d->sr &= ~(SR_N | SR_Z | SR_V);
			if (r & 0x800000) d->sr |= SR_N;
			if (!r) d->sr |= SR_Z;
			return;
		}
		case 4: *dst = add56(d, *dst, sv, 0, 1); return;
		case 5: add56(d, *dst, sv, 0, 1); return;        /* cmp */
		case 7: { int64_t a = *dst < 0 ? -*dst : *dst, b = sv < 0 ? -sv : sv; add56(d, a, b, 0, 1); return; }
		}
		return;
	}
	{
		int64_t other = d->acc[S];
		switch (op & ~8)
		{
		case 0x00: return;                                   /* move */
		case 0x01: *dst = other; return;                     /* tfr */
		case 0x02: { int64_t h = *dst >> 1; *dst = add56(d, h, other, 0, 0); return; }   /* addr */
		case 0x03: d->sr &= ~SR_V; flags_nzue(d, *dst); return;                          /* tst */
		case 0x05: add56(d, *dst, other, 0, 1); return;      /* cmp */
		case 0x06: { int64_t h = *dst >> 1; *dst = add56(d, h, other, 0, 1); return; }   /* subr */
		case 0x07: { int64_t a = *dst < 0 ? -*dst : *dst, b = other < 0 ? -other : other; add56(d, a, b, 0, 1); return; }
		case 0x10: *dst = add56(d, *dst, other, 0, 0); return;
		case 0x11: *dst = rnd(d, *dst); flags_nzue(d, *dst); return;
		case 0x12: { int64_t t = sx56(*dst << 1); *dst = add56(d, t, other, 0, 0); return; }   /* addl */
		case 0x13: *dst = 0; d->sr = (d->sr & ~(SR_N | SR_V | SR_E)) | SR_Z | SR_U; return;
		case 0x14: *dst = add56(d, *dst, other, 0, 1); return;
		case 0x15:   /* max a,b (op 0x1d) / maxm a,b (op 0x15) - both target B */
		{
			int64_t a = d->acc[0], b = d->acc[1];
			int take = (op == 0x1d) ? (b - a <= 0) : ((b < 0 ? -b : b) - (a < 0 ? -a : a) <= 0);
			if (take) { d->acc[1] = a; d->sr &= ~SR_C; } else d->sr |= SR_C;
			return;
		}
		case 0x16: { int64_t t = sx56(*dst << 1); *dst = add56(d, t, other, 0, 1); return; }   /* subl */
		case 0x17: { uint32_t a1 = ~(uint32_t)(*dst >> 24) & 0xffffff;
			*dst = sx56((*dst & ~(0xffffffLL << 24)) | ((int64_t)a1 << 24));
			d->sr &= ~(SR_N | SR_Z | SR_V); if (a1 & 0x800000) d->sr |= SR_N; if (!a1) d->sr |= SR_Z; return; }
		case 0x20: *dst = add56(d, *dst, src_reg48(d, d->xr[1], d->xr[0]), 0, 0); return;
		case 0x21: *dst = add56(d, *dst, src_reg48(d, d->xr[1], d->xr[0]), d->sr & 1, 0); return;
		case 0x22: { int c = (int)(*dst & 1); *dst = *dst >> 1; d->sr = (d->sr & ~(SR_C | SR_V)) | (c ? SR_C : 0); flags_nzue(d, *dst); return; }
		case 0x23: { uint32_t a1 = (uint32_t)(*dst >> 24) & 0xffffff; int c = a1 & 1; a1 >>= 1;
			*dst = sx56((*dst & ~(0xffffffLL << 24)) | ((int64_t)a1 << 24));
			d->sr = (d->sr & ~(SR_C | SR_V | SR_N | SR_Z)) | (c ? SR_C : 0) | (a1 ? 0 : SR_Z); return; }
		case 0x24: *dst = add56(d, *dst, src_reg48(d, d->xr[1], d->xr[0]), 0, 1); return;
		case 0x25: *dst = add56(d, *dst, src_reg48(d, d->xr[1], d->xr[0]), d->sr & 1, 1); return;
		case 0x26: { int64_t v = *dst < 0 ? -*dst : *dst; *dst = sx56(v); d->sr &= ~SR_V; flags_nzue(d, *dst); return; }
		case 0x27: { uint32_t a1 = (uint32_t)(*dst >> 24) & 0xffffff; int c = a1 & 1; a1 = (a1 >> 1) | ((d->sr & 1) << 23);
			*dst = sx56((*dst & ~(0xffffffLL << 24)) | ((int64_t)a1 << 24));
			d->sr = (d->sr & ~(SR_C | SR_V | SR_N | SR_Z)) | (c ? SR_C : 0) | (a1 & 0x800000 ? SR_N : 0) | (a1 ? 0 : SR_Z); return; }
		case 0x30: *dst = add56(d, *dst, src_reg48(d, d->yr[1], d->yr[0]), 0, 0); return;
		case 0x31: *dst = add56(d, *dst, src_reg48(d, d->yr[1], d->yr[0]), d->sr & 1, 0); return;
		case 0x32: { int c = (int)((*dst >> 55) & 1); int64_t r = sx56(*dst << 1);
			d->sr = (d->sr & ~(SR_C | SR_V)) | (c ? SR_C : 0) | (((r < 0) != (*dst < 0)) ? SR_V | SR_L : 0); *dst = r; flags_nzue(d, r); return; }
		case 0x33: { uint32_t a1 = (uint32_t)(*dst >> 24) & 0xffffff; int c = (a1 >> 23) & 1; a1 = (a1 << 1) & 0xffffff;
			*dst = sx56((*dst & ~(0xffffffLL << 24)) | ((int64_t)a1 << 24));
			d->sr = (d->sr & ~(SR_C | SR_V | SR_N | SR_Z)) | (c ? SR_C : 0) | (a1 & 0x800000 ? SR_N : 0) | (a1 ? 0 : SR_Z); return; }
		case 0x34: *dst = add56(d, *dst, src_reg48(d, d->yr[1], d->yr[0]), 0, 1); return;
		case 0x35: *dst = add56(d, *dst, src_reg48(d, d->yr[1], d->yr[0]), d->sr & 1, 1); return;
		case 0x36: { int64_t r = sx56(-*dst); d->sr &= ~SR_V; if (*dst == r && r != 0) d->sr |= SR_V | SR_L; *dst = r; flags_nzue(d, r); return; }
		case 0x37: { uint32_t a1 = (uint32_t)(*dst >> 24) & 0xffffff; int c = (a1 >> 23) & 1; a1 = ((a1 << 1) | (d->sr & 1)) & 0xffffff;
			*dst = sx56((*dst & ~(0xffffffLL << 24)) | ((int64_t)a1 << 24));
			d->sr = (d->sr & ~(SR_C | SR_V | SR_N | SR_Z)) | (c ? SR_C : 0) | (a1 & 0x800000 ? SR_N : 0) | (a1 ? 0 : SR_Z); return; }
		}
	}
	d->unknown_count++; d->last_unknown = 0x100 | op;
}

/* ------------------------------------------------------------------ decoding */
enum
{
	K_UNKNOWN, K_PAR, K_NOP, K_RTS, K_RTI, K_ENDDO, K_ANDI, K_ORI, K_ALUIMM, K_SHIFTI, K_SHIFTR, K_LSHIFTI, K_LSHIFTR,
	K_MPYS, K_MPYI, K_TCC, K_LUAREL, K_LUA, K_MOVEC_EA, K_MOVEC_REG, K_MOVEC_IMM, K_MOVEM, K_MOVEP_EA, K_MOVEP_REG,
	K_MOVEP_QEA, K_MOVEP_QREG, K_BRANCH, K_JUMP, K_BITBR, K_BITJ, K_BITOP, K_DO, K_REP, K_MOVE_DISP, K_CMPU, K_NORM,
	K_DEBUG, K_ILLEGAL
};
/* PAR move kinds */
enum { MV_NONE, MV_IMMS, MV_RR, MV_U, MV_MEM, MV_L, MV_XR1, MV_RY1, MV_XR2, MV_RY2, MV_XY, MV_IF, MV_IFU };

static void decode(dsp_core *d, uint32_t pc)
{
	uint32_t w = d->p[pc & (DSP_PSIZE - 1)], ext = d->p[(pc + 1) & (DSP_PSIZE - 1)];
	dsp_op *o = &d->op[pc & (DSP_PSIZE - 1)];
	uint32_t hi = w >> 16;
	memset(o, 0, sizeof(*o));
	o->size = 1;
	o->kind = K_PAR;
	o->alu = w & 0xff;
	if (w & 0x800000)
	{
		o->a = MV_XY;
		o->imm = w;
		return;
	}
	if ((w >> 20) >= 4 && (w >> 20) <= 7)
	{
		if ((w >> 20) == 4 && !((w >> 18) & 1)) { o->a = MV_L; o->imm = w; if (((w >> 14) & 1) && ea_has_ext((w >> 11) & 7, (w >> 8) & 7)) { o->size = 2; o->c = 1; } return; }
		o->a = MV_MEM; o->imm = w;
		if (((w >> 14) & 1) && ea_has_ext((w >> 11) & 7, (w >> 8) & 7)) o->size = 2;
		return;
	}
	if ((w >> 20) == 1)
	{
		o->a = (w >> 14) & 1 ? MV_RY1 : MV_XR1; o->imm = w;
		if (ea_has_ext((w >> 11) & 7, (w >> 8) & 7)) o->size = 2;
		return;
	}
	if ((w >> 20) == 2 || (w >> 20) == 3)
	{
		if ((w >> 8) == 0x2000) { o->a = MV_NONE; return; }
		if ((w >> 12) == 0x202) { o->a = MV_IF; o->b = (w >> 8) & 15; return; }
		if ((w >> 12) == 0x203) { o->a = MV_IFU; o->b = (w >> 8) & 15; return; }
		if ((w >> 13) == 0x102) { o->a = MV_U; o->b = (w >> 11) & 7; o->c = (w >> 8) & 7; return; }
		if ((w >> 18) == 0x8) { o->a = MV_RR; o->b = (w >> 13) & 0x1f; o->c = (w >> 8) & 0x1f; return; }
		o->a = MV_IMMS; o->b = (w >> 16) & 0x1f; o->imm = (w >> 8) & 0xff; return;
	}
	/* top nibble 0 */
	if ((w >> 17) == 4 && !((w >> 14) & 1))
	{
		o->a = (w >> 15) & 1 ? MV_RY2 : MV_XR2; o->imm = w;
		if (ea_has_ext((w >> 11) & 7, (w >> 8) & 7)) o->size = 2;
		return;
	}
	o->alu = 0;
	if ((w >> 17) == 4)       /* MOVEP 0000 100s W1.. */
	{
		int k = (w >> 6) & 3;
		if (k >= 2 || k == 1)
		{
			o->kind = K_MOVEP_EA; o->imm = w;
			if (ea_has_ext((w >> 11) & 7, (w >> 8) & 7)) o->size = 2;
			return;
		}
		o->kind = K_MOVEP_REG; o->imm = w; return;
	}
	if ((w >> 17) == 1 && (w >> 7) & 1) { o->kind = K_MOVE_DISP; o->imm = w; return; }
	if ((w & 0xfef880) == 0x0a7080) { o->kind = K_MOVE_DISP; o->imm = w; o->size = 2; o->c = 1; return; }
	switch (hi)
	{
	case 0x00:
		if (w == 0) { o->kind = K_NOP; return; }
		if (w == 0x00000c) { o->kind = K_RTS; return; }
		if (w == 0x000004) { o->kind = K_RTI; return; }
		if (w == 0x00008c) { o->kind = K_ENDDO; return; }
		if (w == 0x000200 || (w & 0xfffff0) == 0x000300 || (w & 0xfffff0) == 0x000210) { o->kind = K_DEBUG; return; }
		if (w == 0x000005 || w == 0x000084 || w == 0x000087 || w == 0x000086) { o->kind = K_ILLEGAL; return; }
		if ((w & 0xfc) == 0xb8) { o->kind = K_ANDI; o->b = w & 3; o->imm = (w >> 8) & 0xff; return; }
		if ((w & 0xfc) == 0xf8) { o->kind = K_ORI; o->b = w & 3; o->imm = (w >> 8) & 0xff; return; }
		if (w == 0x000203 || w == 0x000202) { o->kind = K_DO; o->b = 9; o->size = 2; o->imm = ext; o->c = w == 0x000202; return; }
		break;
	case 0x01:
		if (((w >> 14) & 3) == 1 && ((w >> 4) & 0xf) == 8) { o->kind = K_ALUIMM; o->b = w & 0xf; o->imm = (w >> 8) & 0x3f; return; }
		if ((w & 0xffff00) == 0x014000 && ((w >> 4) & 0xf) == 0xc) { o->kind = K_ALUIMM; o->b = w & 0xf; o->imm = ext; o->size = 2; return; }
		if ((w & 0xfff8f0) == 0x01d810) { o->kind = K_NORM; o->b = (w >> 8) & 7; o->c = (w >> 3) & 1; return; }
		if ((w & 0xffe0c0) == 0x0100c0) { o->kind = K_MPYS; o->imm = w; return; }
		if ((w & 0xffffc0) == 0x0141c0) { o->kind = K_MPYI; o->imm = w; o->size = 2; o->b = 0; return; }
		break;
	case 0x02: case 0x03:
		o->kind = K_TCC; o->imm = w; return;
	case 0x04:
		if ((w & 0xc000) == 0x0000) { o->kind = K_LUAREL; o->imm = w; return; }
		if ((w & 0xe000) == 0x4000 && (w & 0xe0) == 0) { o->kind = K_LUA; o->imm = w; return; }
		if ((w & 0x4000) && (w & 0xe0) == 0xa0) { o->kind = K_MOVEC_REG; o->imm = w; return; }
		if ((w & 0xc000) == 0x8000 && !((w >> 7) & 1)) { o->kind = K_BITBR; o->imm = w; o->size = 2; o->b = 4; return; }
		if (w & 0x4000) { o->kind = K_MOVEP_QREG; o->imm = w; return; }
		break;
	case 0x05:
		if ((w & 0xa0) == 0x20 && !(w & 0x80))
		{
			o->kind = K_MOVEC_EA; o->imm = w;
			if (((w >> 14) & 1) && ea_has_ext((w >> 11) & 7, (w >> 8) & 7)) o->size = 2;
			return;
		}
		if ((w & 0xe0) == 0xa0) { o->kind = K_MOVEC_IMM; o->imm = w; return; }
		if (!(w & 0x20)) { o->kind = K_BRANCH; o->imm = w; o->b = 1; return; }   /* short Bcc/BRA/BSR/BScc */
		break;
	case 0x06:
		/* low byte: 1ooo hhhh = #xxx form (count in iiiiiiii and hhhh); 0Soo 0000 = memory / register forms.
		   oo (bits 5-4): 00 DO, 01 DOR, 10 REP */
		o->imm = w;
		o->b = (w >> 4) & 3;
		o->kind = o->b == 2 ? K_REP : K_DO;
		if (o->kind == K_DO) o->size = 2;
		return;
	case 0x07:
		if ((w & 0xc0) == 0x80 || (w & 0xc0) == 0x00)
		{
			if (((w >> 14) & 1) == ((w >> 7) & 1)) { o->kind = K_MOVEM; o->imm = w; if (((w >> 14) & 1) && ea_has_ext((w >> 11) & 7, (w >> 8) & 7)) o->size = 2; return; }
		}
		o->kind = K_MOVEP_QEA; o->imm = w; if (ea_has_ext((w >> 11) & 7, (w >> 8) & 7)) o->size = 2; return;
	case 0x0a: case 0x0b:
	{
		int mode = (w >> 14) & 3;
		if (mode == 3)
		{
			if ((w & 0xff) == 0x80 || (w & 0xf0) == 0xa0) { o->kind = K_JUMP; o->imm = w; o->b = 2; if (ea_has_ext((w >> 11) & 7, (w >> 8) & 7)) o->size = 2; return; }
			if (((w >> 5) & 7) <= 1) { o->kind = K_BITJ; o->imm = w; o->size = 2; return; }
			o->kind = K_BITOP; o->imm = w; return;
		}
		if ((w >> 7) & 1) { o->kind = K_BITJ; o->imm = w; o->size = 2 + (mode == 1 && ea_has_ext((w >> 11) & 7, (w >> 8) & 7)); return; }
		o->kind = K_BITOP; o->imm = w; if (mode == 1 && ea_has_ext((w >> 11) & 7, (w >> 8) & 7)) o->size = 2; return;
	}
	case 0x0c: case 0x0d:
		if ((w >> 15) & 1) { o->kind = K_BITBR; o->imm = w; o->size = 2; return; }
		if ((w & 0xf000) == 0) { o->kind = K_JUMP; o->imm = w; o->b = 1; return; }
		if (hi == 0x0c)
		{
			if ((w >> 8) == 0x0c1d || (w >> 8) == 0x0c1c) { o->kind = K_SHIFTI; o->imm = w; return; }
			if ((w >> 8) == 0x0c1e)
			{
				int k = (w >> 5) & 7;
				if (k == 2 || k == 3) { o->kind = K_SHIFTR; o->imm = w; return; }
				if (k == 4 || k == 6) { o->kind = K_LSHIFTI; o->imm = w; return; }
				if ((w & 0xf0) == 0x10 || (w & 0xf0) == 0x30) { o->kind = K_LSHIFTR; o->imm = w; return; }
			}
			if ((w & 0xfffff0) == 0x0c1ff0) { o->kind = K_CMPU; o->imm = w; return; }
		}
		else
		{
			if ((w & 0xffff00) == 0x0d1000 && ((w & 0xc0) == 0x40 || (w & 0xf0) == 0x00 || w == 0x0d10c0 || w == 0x0d1080))
				{ o->kind = K_BRANCH; o->imm = w; o->b = 2; o->size = 2; return; }
			if ((w & 0xfff800) == 0x0d1800) { o->kind = K_BRANCH; o->imm = w; o->b = 3; return; }
		}
		break;
	case 0x0e: case 0x0f:
		o->kind = K_JUMP; o->imm = w; o->b = 1; return;
	}
	o->kind = K_UNKNOWN; o->imm = w;
}

/* ------------------------------------------------------------------ stack and loops */
static inline void push(dsp_core *d, uint32_t h, uint32_t l)
{
	d->sp = (d->sp + 1) & 15;
	d->ssh[d->sp] = h & 0xffffff; d->ssl[d->sp] = l & 0xffffff;
}
static inline void pop(dsp_core *d) { d->sp = (d->sp - 1) & 15; }

/* ------------------------------------------------------------------ parallel moves */
typedef struct pmove { int n; uint8_t space[2]; uint8_t reg[2]; uint8_t toreg[2]; uint32_t addr[2]; uint32_t val[2]; } pmove;

static uint32_t read_for_move(dsp_core *d, int code) { return reg_read(d, code); }

static void par_execute(dsp_core *d, dsp_op *o, uint32_t ext)
{
	uint32_t w = o->imm;
	/* phase 1: read move sources */
	int nm = 0;
	struct { int kind; int code; int space; uint32_t addr; uint32_t val; } mv[2];
	switch (o->a)
	{
	case MV_NONE: break;
	case MV_IF: if (cond(d, o->b)) { uint32_t sr = d->sr; alu(d, o->alu); d->sr = sr; } return;
	case MV_IFU: if (cond(d, o->b)) alu(d, o->alu); return;
	case MV_IMMS:
	{
		int code = o->b; uint32_t v = o->imm;
		if (code >= 0x04 && code <= 0x07) v <<= 16;
		else if (code == 0x0e || code == 0x0f) v = (uint32_t)(sx24(v << 16)) & 0xffffff;
		else if (code == 0x08 || code == 0x09 || code == 0x0a || code == 0x0b || code == 0x0c || code == 0x0d) { }
		mv[nm].kind = 1; mv[nm].code = code; mv[nm].val = v; nm++;
		break;
	}
	case MV_RR:
		mv[nm].kind = 1; mv[nm].code = o->c; mv[nm].val = read_for_move(d, o->b); nm++;
		break;
	case MV_U:
		ea(d, o->b, o->c, 0);
		break;
	case MV_MEM:
	{
		int space = (w >> 19) & 1, write_reg = (w >> 15) & 1, code = ((w >> 17) & 0x18) | ((w >> 16) & 7);
		uint32_t addr;
		if ((w >> 14) & 1)
		{
			int mmm = (w >> 11) & 7, rrr = (w >> 8) & 7;
			if (ea_is_imm(mmm, rrr)) { mv[nm].kind = 1; mv[nm].code = code; mv[nm].val = ext; nm++; break; }
			addr = ea(d, mmm, rrr, ext);
		}
		else addr = (w >> 8) & 0x3f;
		if (write_reg) { mv[nm].kind = 1; mv[nm].code = code; mv[nm].val = mem_read(d, space, addr); nm++; }
		else { mv[nm].kind = 2; mv[nm].space = space; mv[nm].addr = addr; mv[nm].val = read_for_move(d, code); nm++; }
		break;
	}
	case MV_L:
	{
		int L = ((w >> 17) & 4) | ((w >> 16) & 3), W = (w >> 15) & 1;
		uint32_t addr;
		if ((w >> 14) & 1) addr = ea(d, (w >> 11) & 7, (w >> 8) & 7, ext); else addr = (w >> 8) & 0x3f;
		{
			static const uint8_t hi_reg[8] = { 0x0c, 0x0d, 0x05, 0x07, 0x0e, 0x0f, 0x0e, 0x0f };
			static const uint8_t lo_reg[8] = { 0x08, 0x09, 0x04, 0x06, 0x08, 0x09, 0x0f, 0x0e };
			if (W)
			{
				uint32_t xv = mem_read(d, 0, addr), yv = mem_read(d, 1, addr);
				if (L == 4 || L == 5) { int a = L - 4; d->acc[a] = sx56(((int64_t)sx24(xv) << 24) | yv); }
				else { mv[nm].kind = 1; mv[nm].code = hi_reg[L]; mv[nm].val = xv; nm++; mv[nm].kind = 1; mv[nm].code = lo_reg[L]; mv[nm].val = yv; nm++; }
			}
			else
			{
				uint32_t xv, yv;
				if (L == 4 || L == 5)
				{
					int a = L - 4; int64_t v = d->acc[a];
					if (v > 0x7fffffffffffLL) { xv = 0x7fffff; yv = 0xffffff; d->sr |= SR_L; }
					else if (v < -0x800000000000LL) { xv = 0x800000; yv = 0; d->sr |= SR_L; }
					else { xv = (uint32_t)(v >> 24) & 0xffffff; yv = (uint32_t)v & 0xffffff; }
				}
				else { xv = read_for_move(d, hi_reg[L]); yv = read_for_move(d, lo_reg[L]); }
				mv[nm].kind = 2; mv[nm].space = 0; mv[nm].addr = addr; mv[nm].val = xv; nm++;
				mv[nm].kind = 2; mv[nm].space = 1; mv[nm].addr = addr; mv[nm].val = yv; nm++;
			}
		}
		break;
	}
	case MV_XR1:     /* 0001 ffdF W0MM MRRR : X:ea <-> ff ; S2 (A/B) -> D2 (Y0/Y1) */
	{
		static const uint8_t ff[4] = { 0x04, 0x05, 0x0e, 0x0f };
		int W = (w >> 15) & 1, mmm = (w >> 11) & 7, rrr = (w >> 8) & 7;
		uint32_t s2 = acc_read24(d, (w >> 17) & 1);
		if (ea_is_imm(mmm, rrr)) { mv[nm].kind = 1; mv[nm].code = ff[(w >> 18) & 3]; mv[nm].val = ext; nm++; }
		else
		{
			uint32_t addr = ea(d, mmm, rrr, ext);
			if (W) { mv[nm].kind = 1; mv[nm].code = ff[(w >> 18) & 3]; mv[nm].val = mem_read(d, 0, addr); nm++; }
			else { mv[nm].kind = 2; mv[nm].space = 0; mv[nm].addr = addr; mv[nm].val = read_for_move(d, ff[(w >> 18) & 3]); nm++; }
		}
		mv[nm].kind = 1; mv[nm].code = (w >> 16) & 1 ? 0x07 : 0x06; mv[nm].val = s2; nm++;
		break;
	}
	case MV_RY1:     /* 0001 deff W1MM MRRR : S1 (A/B) -> D1 (X0/X1) ; Y:ea <-> ff */
	{
		static const uint8_t ff[4] = { 0x06, 0x07, 0x0e, 0x0f };
		int W = (w >> 15) & 1, mmm = (w >> 11) & 7, rrr = (w >> 8) & 7;
		uint32_t s1 = acc_read24(d, (w >> 19) & 1);
		if (ea_is_imm(mmm, rrr)) { mv[nm].kind = 1; mv[nm].code = ff[(w >> 16) & 3]; mv[nm].val = ext; nm++; }
		else
		{
			uint32_t addr = ea(d, mmm, rrr, ext);
			if (W) { mv[nm].kind = 1; mv[nm].code = ff[(w >> 16) & 3]; mv[nm].val = mem_read(d, 1, addr); nm++; }
			else { mv[nm].kind = 2; mv[nm].space = 1; mv[nm].addr = addr; mv[nm].val = read_for_move(d, ff[(w >> 16) & 3]); nm++; }
		}
		mv[nm].kind = 1; mv[nm].code = (w >> 18) & 1 ? 0x05 : 0x04; mv[nm].val = s1; nm++;
		break;
	}
	case MV_XR2:     /* A -> X:ea ; X0 -> A */
	{
		int a = (w >> 16) & 1; uint32_t addr = ea(d, (w >> 11) & 7, (w >> 8) & 7, ext);
		mv[nm].kind = 2; mv[nm].space = 0; mv[nm].addr = addr; mv[nm].val = acc_read24(d, a); nm++;
		mv[nm].kind = 1; mv[nm].code = 0x0e + a; mv[nm].val = d->xr[0]; nm++;
		break;
	}
	case MV_RY2:     /* Y0 -> A ; A -> Y:ea */
	{
		int a = (w >> 16) & 1; uint32_t addr = ea(d, (w >> 11) & 7, (w >> 8) & 7, ext);
		mv[nm].kind = 2; mv[nm].space = 1; mv[nm].addr = addr; mv[nm].val = acc_read24(d, a); nm++;
		mv[nm].kind = 1; mv[nm].code = 0x0e + a; mv[nm].val = d->yr[0]; nm++;
		break;
	}
	case MV_XY:
	{
		static const uint8_t ee[4] = { 0x04, 0x05, 0x0e, 0x0f }, ff[4] = { 0x06, 0x07, 0x0e, 0x0f };
		int MM = (w >> 11) & 3, RRR = (w >> 8) & 7, mm = (w >> 20) & 3, rr = (w >> 13) & 3;
		int ry = rr | ((RRR & 4) ? 0 : 4);
		static const uint8_t mode[4] = { 4, 1, 2, 3 };
		uint32_t xa = ea(d, mode[MM], RRR, 0), ya = ea(d, mode[mm], ry, 0);
		if ((w >> 15) & 1) { mv[nm].kind = 1; mv[nm].code = ee[(w >> 18) & 3]; mv[nm].val = mem_read(d, 0, xa); nm++; }
		else { mv[nm].kind = 2; mv[nm].space = 0; mv[nm].addr = xa; mv[nm].val = read_for_move(d, ee[(w >> 18) & 3]); nm++; }
		if ((w >> 22) & 1) { mv[nm].kind = 1; mv[nm].code = ff[(w >> 16) & 3]; mv[nm].val = mem_read(d, 1, ya); nm++; }
		else { mv[nm].kind = 2; mv[nm].space = 1; mv[nm].addr = ya; mv[nm].val = read_for_move(d, ff[(w >> 16) & 3]); nm++; }
		break;
	}
	}
	/* phase 2: the ALU */
	alu(d, o->alu);
	/* phase 3: write move destinations */
	{
		int i;
		for (i = 0; i < nm; i++)
		{
			if (mv[i].kind == 1) reg_write(d, mv[i].code, mv[i].val);
			else mem_write(d, mv[i].space, mv[i].addr, mv[i].val);
		}
	}
}

/* ------------------------------------------------------------------ the other instructions */
static uint32_t bit_location_read(dsp_core *d, uint32_t w, uint32_t ext, int *space, uint32_t *addr, int *isreg)
{
	int mode = (w >> 14) & 3;
	*space = (w >> 6) & 1; *isreg = 0;
	if (mode == 1) *addr = ea(d, (w >> 11) & 7, (w >> 8) & 7, ext);
	else if (mode == 0) *addr = (w >> 8) & 0x3f;
	else if (mode == 2) *addr = 0xffffc0 + ((w >> 8) & 0x3f);
	else { *isreg = 1; *addr = (w >> 8) & 0x3f; return reg_read(d, (int)*addr); }
	return mem_read(d, *space, *addr);
}

static int execute(dsp_core *d)
{
	uint32_t pc = d->pc & (DSP_PSIZE - 1), ext, w;
	dsp_op *o;
	uint32_t next;
	if (!d->decoded[pc]) { decode(d, pc); d->decoded[pc] = 1; }
	o = &d->op[pc];
	ext = d->p[(pc + 1) & (DSP_PSIZE - 1)];
	w = o->imm;
	next = pc + o->size;
	d->count++;
	switch (o->kind)
	{
	case K_PAR: par_execute(d, o, ext); break;
	case K_NOP: case K_DEBUG: break;
	case K_RTS: next = d->ssh[d->sp & 15]; pop(d); break;
	case K_RTI: next = d->ssh[d->sp & 15]; d->sr = d->ssl[d->sp & 15]; pop(d); break;
	case K_ENDDO:
		d->sr = (d->sr & ~SR_LF) | (d->ssl[d->sp & 15] & SR_LF); pop(d);
		d->la = d->ssh[d->sp & 15]; d->lc = d->ssl[d->sp & 15]; pop(d);
		break;
	case K_ANDI: case K_ORI:
	{
		int shift = o->b == 0 ? 8 : o->b == 1 ? 0 : 16;
		if (o->b == 2) { if (o->kind == K_ANDI) d->omr &= o->imm | ~0xffu; else d->omr |= o->imm; break; }
		if (o->b == 3) { if (o->kind == K_ANDI) d->omr &= (o->imm << 8) | ~0xff00u; else d->omr |= o->imm << 8; break; }
		if (o->kind == K_ANDI) d->sr &= (o->imm << shift) | ~(0xffu << shift); else d->sr |= o->imm << shift;
		break;
	}
	case K_ALUIMM:
	{
		int k = o->b & 7, D = (o->b >> 3) & 1;
		uint32_t imm = o->size == 2 ? o->imm & 0xffffff : o->imm;
		int64_t v = o->size == 2 ? (int64_t)sx24(imm) << 24 : (int64_t)imm << 24;
		int64_t *dst = &d->acc[D];
		switch (k)
		{
		case 0: *dst = add56(d, *dst, v, 0, 0); break;
		case 4: *dst = add56(d, *dst, v, 0, 1); break;
		case 5: add56(d, *dst, v, 0, 1); break;
		case 2: case 3: case 6:
		{
			uint32_t a1 = (uint32_t)(*dst >> 24) & 0xffffff, r = k == 2 ? a1 | imm : k == 3 ? a1 ^ imm : a1 & imm;
			*dst = sx56((*dst & ~(0xffffffLL << 24)) | ((int64_t)r << 24));
			d->sr &= ~(SR_N | SR_Z | SR_V);
			if (r & 0x800000) d->sr |= SR_N;
			if (!r) d->sr |= SR_Z;
			break;
		}
		default: d->unknown_count++; d->last_unknown = w;
		}
		break;
	}
	case K_SHIFTI: case K_SHIFTR:
	{
		int S = (o->kind == K_SHIFTI) ? (w >> 7) & 1 : (w >> 4) & 1, D = w & 1, n;
		int left = o->kind == K_SHIFTI ? (w >> 8) == 0x0c1d : ((w >> 5) & 7) == 2;
		int64_t v = d->acc[S], r = v;
		if (o->kind == K_SHIFTI) n = (w >> 1) & 0x3f;
		else
		{
			static const uint8_t src[8] = { 0, 0, 0x0c, 0x0d, 0x04, 0x06, 0x05, 0x07 };
			n = (int)(reg_read(d, src[(w >> 1) & 7]) & 0x3f);
		}
		d->sr &= ~(SR_C | SR_V);
		if (n > 56) n = 56;
		if (n)
		{
			if (left)
			{
				int i;
				for (i = 0; i < n; i++)
				{
					int c = (int)((r >> 55) & 1);
					int64_t t = sx56(r << 1);
					if ((t < 0) != (r < 0)) d->sr |= SR_V | SR_L;
					r = t;
					d->sr = (d->sr & ~SR_C) | (c ? SR_C : 0);
				}
			}
			else
			{
				if ((v >> (n - 1)) & 1) d->sr |= SR_C;
				r = v >> n;
			}
		}
		d->acc[D] = r;
		flags_nzue(d, r);
		break;
	}
	case K_LSHIFTI: case K_LSHIFTR:
	{
		int D = w & 1, n, left;
		uint32_t a1 = (uint32_t)(d->acc[D] >> 24) & 0xffffff, r;
		if (o->kind == K_LSHIFTI) { n = (w >> 1) & 0x1f; left = ((w >> 5) & 7) == 4; }
		else
		{
			static const uint8_t src[8] = { 0, 0, 0x0c, 0x0d, 0x04, 0x06, 0x05, 0x07 };
			n = (int)(reg_read(d, src[(w >> 1) & 7]) & 0x1f); left = (w & 0xf0) == 0x10;
		}
		d->sr &= ~(SR_C | SR_V | SR_N | SR_Z);
		if (n)
		{
			if (left) { if ((a1 >> (24 - n)) & 1) d->sr |= SR_C; r = (a1 << n) & 0xffffff; }
			else { if ((a1 >> (n - 1)) & 1) d->sr |= SR_C; r = a1 >> n; }
		}
		else r = a1;
		if (r & 0x800000) d->sr |= SR_N;
		if (!r) d->sr |= SR_Z;
		d->acc[D] = sx56((d->acc[D] & ~(0xffffffLL << 24)) | ((int64_t)r << 24));
		break;
	}
	case K_MPYS:   /* mpy/mac/mpyr/macr (+/-)S,#n,D : 0000 0001 000s ssss 11QQ dkoo */
	{
		static const uint8_t q[4] = { 0x07, 0x04, 0x06, 0x05 };   /* y1 x0 y0 x1 */
		int D = (w >> 3) & 1, n = (w >> 8) & 0x1f;
		int64_t p = ((int64_t)sx24(reg_read(d, q[(w >> 4) & 3])) << 24) >> n;    /* the source times 2^-n */
		if (w & 4) p = -p;
		if ((w & 3) >= 2) d->acc[D] = sx56(d->acc[D] + p); else d->acc[D] = sx56(p);
		if (w & 1) d->acc[D] = rnd(d, d->acc[D]);
		d->sr &= ~SR_V; flags_nzue(d, d->acc[D]);
		break;
	}
	case K_MPYI:   /* mpyi/maci (+/-)#xxxx,S,D : 0000 0001 0100 0001 11qq dkoo */
	{
		static const uint8_t q[4] = { 0x04, 0x06, 0x05, 0x07 };
		int D = (w >> 3) & 1;
		int64_t p = ((int64_t)sx24(ext) * (int64_t)sx24(reg_read(d, q[(w >> 4) & 3]))) << 1;
		if (w & 4) p = -p;
		if ((w & 3) >= 2) d->acc[D] = sx56(d->acc[D] + p); else d->acc[D] = sx56(p);
		if (w & 1) d->acc[D] = rnd(d, d->acc[D]);
		d->sr &= ~SR_V; flags_nzue(d, d->acc[D]);
		break;
	}
	case K_TCC:
	{
		int cc = (w >> 12) & 15;
		if (cond(d, cc))
		{
			if ((w >> 16) == 0x02 && ((w >> 11) & 1)) { d->r[w & 7] = d->r[(w >> 8) & 7]; break; }
			{
				int JJJ = (w >> 4) & 7, D = (w >> 3) & 1;
				if (JJJ == 0) d->acc[D] = d->acc[D ^ 1];
				else { static const uint8_t s[8] = { 0, 0, 0, 0, 0x04, 0x06, 0x05, 0x07 }; acc_write24(d, D, reg_read(d, s[JJJ])); }
				if ((w >> 16) == 0x03) d->r[w & 7] = d->r[(w >> 8) & 7];
			}
		}
		break;
	}
	case K_LUAREL:
	{
		int32_t aa = (int32_t)((((w >> 11) & 7) << 4) | ((w >> 4) & 0xf));
		int rn = (w >> 8) & 7, dd = w & 0xf;
		uint32_t v;
		if (aa & 0x40) aa -= 0x80;
		v = (d->r[rn] + (uint32_t)aa) & 0xffffff;
		if (dd & 8) d->n[dd & 7] = v; else d->r[dd & 7] = v;
		break;
	}
	case K_LUA:
	{
		int mmm = (w >> 11) & 7, rrr = (w >> 8) & 7, dd = w & 0xf;
		uint32_t save = d->r[rrr], v;
		ea(d, mmm, rrr, 0); v = d->r[rrr]; d->r[rrr] = save;
		if (mmm == 5) v = mod_add(d, rrr, sx24(d->n[rrr]));
		if (dd & 8) d->n[dd & 7] = v; else d->r[dd & 7] = v;
		break;
	}
	case K_MOVEC_EA:
	{
		int c = 0x20 | (w & 0x1f), space = (w >> 6) & 1, W = (w >> 15) & 1;
		uint32_t addr;
		if ((w >> 14) & 1)
		{
			int mmm = (w >> 11) & 7, rrr = (w >> 8) & 7;
			if (ea_is_imm(mmm, rrr)) { reg_write(d, c, ext); break; }
			addr = ea(d, mmm, rrr, ext);
		}
		else addr = (w >> 8) & 0x3f;
		if (W) reg_write(d, c, mem_read(d, space, addr)); else mem_write(d, space, addr, reg_read(d, c));
		break;
	}
	case K_MOVEC_REG:
	{
		int c = 0x20 | (w & 0x1f), e = (w >> 8) & 0x3f;
		if ((w >> 15) & 1) reg_write(d, c, reg_read(d, e)); else reg_write(d, e, reg_read(d, c));
		break;
	}
	case K_MOVEC_IMM: reg_write(d, 0x20 | (w & 0x1f), (w >> 8) & 0xff); break;
	case K_MOVEM:
	{
		int W = (w >> 15) & 1, code = w & 0x3f;
		uint32_t addr;
		if ((w >> 14) & 1) addr = ea(d, (w >> 11) & 7, (w >> 8) & 7, ext); else addr = (w >> 8) & 0x3f;
		if (W) reg_write(d, code, mem_read(d, 2, addr)); else mem_write(d, 2, addr, reg_read(d, code));
		break;
	}
	case K_MOVEP_EA:   /* 0000 100s W1MM MRRR 1Spp pppp (X/Y:ea) or 01pp pppp (P:ea) */
	{
		int s = (w >> 16) & 1, W = (w >> 15) & 1, k = (w >> 6) & 3, mmm = (w >> 11) & 7, rrr = (w >> 8) & 7;
		uint32_t pp = 0xffffc0 + (w & 0x3f), v;
		int espace = k == 1 ? 2 : (w >> 6) & 1;
		if (W)
		{
			if (ea_is_imm(mmm, rrr)) v = ext; else v = mem_read(d, espace, ea(d, mmm, rrr, ext));
			mem_write(d, s, pp, v);
		}
		else { v = mem_read(d, s, pp); mem_write(d, espace, ea(d, mmm, rrr, ext), v); }
		break;
	}
	case K_MOVEP_REG:  /* 0000 100s W1dd dddd 00pp pppp */
	{
		int s = (w >> 16) & 1, W = (w >> 15) & 1; uint32_t pp = 0xffffc0 + (w & 0x3f);
		if (W) mem_write(d, s, pp, reg_read(d, (w >> 8) & 0x3f)); else reg_write(d, (w >> 8) & 0x3f, mem_read(d, s, pp));
		break;
	}
	case K_MOVEP_QEA:  /* 0000 0111 W1MM MRRR 0Sqq qqqq (X:qq) / W0MM MRRR 1Sqq qqqq (Y:qq) */
	{
		int W = (w >> 15) & 1, qspace = (w >> 14) & 1 ? 0 : 1, espace = (w >> 6) & 1, mmm = (w >> 11) & 7, rrr = (w >> 8) & 7;
		uint32_t qq = 0xffff80 + (w & 0x3f), v;
		if (W) { if (ea_is_imm(mmm, rrr)) v = ext; else v = mem_read(d, espace, ea(d, mmm, rrr, ext)); mem_write(d, qspace, qq, v); }
		else { v = mem_read(d, qspace, qq); mem_write(d, espace, ea(d, mmm, rrr, ext), v); }
		break;
	}
	case K_MOVEP_QREG: /* 0000 0100 W1dd dddd 1q0q qqqq (X) / 0q1q qqqq (Y) */
	{
		int W = (w >> 15) & 1, space = (w >> 7) & 1 ? 0 : 1;
		uint32_t qq = 0xffff80 + ((((w >> 6) & 1) << 5) | (w & 0x1f));
		if (W) mem_write(d, space, qq, reg_read(d, (w >> 8) & 0x3f)); else reg_write(d, (w >> 8) & 0x3f, mem_read(d, space, qq));
		break;
	}
	case K_BRANCH:
	{
		int32_t disp; int take, call = 0;
		if (o->b == 1)
		{
			int k = (w >> 10) & 3, cc = (w >> 12) & 15;
			disp = (int32_t)((((w >> 6) & 0xf) << 5) | (w & 0x1f)); if (disp & 0x100) disp -= 0x200;
			if (cc == 0 && k == 3) take = 1;            /* bra */
			else if (cc == 0 && k == 2) { take = 1; call = 1; }
			else if (k == 1) take = cond(d, cc);
			else { take = cond(d, cc); call = 1; }
			if (take) { if (call) push(d, next, d->sr); next = (pc + (uint32_t)disp) & 0xffffff; }
		}
		else if (o->b == 2)
		{
			disp = sx24(ext);
			if (w == 0x0d10c0) take = 1;
			else if (w == 0x0d1080) { take = 1; call = 1; }
			else if ((w & 0xc0) == 0x40) take = cond(d, w & 15);
			else { take = cond(d, w & 15); call = 1; }
			if (take) { if (call) push(d, next, d->sr); next = (pc + (uint32_t)disp) & 0xffffff; }
		}
		else
		{
			int rn = (w >> 8) & 7;
			disp = sx24(d->r[rn]);
			if ((w & 0xff) == 0xc0) take = 1; else if ((w & 0xff) == 0x80) { take = 1; call = 1; }
			else if ((w & 0xf0) == 0x40) take = cond(d, w & 15); else { take = cond(d, w & 15); call = 1; }
			if (take) { if (call) push(d, next, d->sr); next = (pc + (uint32_t)disp) & 0xffffff; }
		}
		break;
	}
	case K_JUMP:
	{
		uint32_t target; int take = 1, call = 0;
		if (o->b == 1)
		{
			target = w & 0xfff;
			switch (w >> 16)
			{
			case 0x0c: break;
			case 0x0d: call = 1; break;
			case 0x0e: take = cond(d, (w >> 12) & 15); break;
			case 0x0f: take = cond(d, (w >> 12) & 15); call = 1; break;
			}
		}
		else
		{
			int mmm = (w >> 11) & 7, rrr = (w >> 8) & 7;
			target = (mmm == 6 && rrr == 0) ? ext : ea(d, mmm, rrr, ext);
			call = (w >> 16) == 0x0b;
			if ((w & 0xf0) == 0xa0) take = cond(d, w & 15);
		}
		if (take) { if (call) push(d, next, d->sr); next = target & 0xffffff; }
		break;
	}
	case K_BITJ: case K_BITBR:
	{
		int space, isreg, bit = w & 0x1f, set, call;
		uint32_t addr, v, target;
		int32_t disp;
		if (o->kind == K_BITBR && o->b == 4)
		{
			/* brclr/brset #n,x/y:qq : 0000 0100 10qq qqqq 0S?b bbbb */
			space = (w >> 6) & 1; addr = 0xffff80 + ((w >> 8) & 0x3f); v = mem_read(d, space, addr);
			set = (w >> 5) & 1; call = 0;
		}
		else if (o->kind == K_BITBR)
		{
			int mode = (w >> 14) & 3;
			space = (w >> 6) & 1; set = (w >> 5) & 1; call = (w >> 16) == 0x0d;
			if (mode == 2)
			{
				if ((w >> 7) & 1) addr = (w >> 8) & 0x3f; else addr = ea(d, (w >> 11) & 7, (w >> 8) & 7, 0);
				v = mem_read(d, space, addr);
			}
			else
			{
				if ((w >> 7) & 1) v = reg_read(d, (w >> 8) & 0x3f);
				else v = mem_read(d, space, 0xffffc0 + ((w >> 8) & 0x3f));
			}
		}
		else
		{
			v = bit_location_read(d, w, ext, &space, &addr, &isreg);
			set = (w >> 5) & 1; call = (w >> 16) == 0x0b;
			if (((w >> 14) & 3) == 1 && ea_has_ext((w >> 11) & 7, (w >> 8) & 7)) { ext = d->p[(pc + 2) & (DSP_PSIZE - 1)]; }
		}
		d->sr = (d->sr & ~SR_C) | (((v >> bit) & 1) ? SR_C : 0);
		if ((((v >> bit) & 1) != 0) == (set != 0))
		{
			if (o->kind == K_BITBR) { disp = sx24(ext); target = (pc + (uint32_t)disp) & 0xffffff; }
			else target = ext & 0xffffff;
			if (call) push(d, next, d->sr);
			next = target;
		}
		break;
	}
	case K_BITOP:
	{
		int space, isreg, bit = w & 0x1f, k;
		uint32_t addr, v = bit_location_read(d, w, ext, &space, &addr, &isreg), nv = v;
		if (((w >> 14) & 3) == 3) k = ((w >> 16) == 0x0a) ? ((w >> 5) & 7) - 2 + 0 : ((w >> 5) & 7) - 2 + 2;   /* 0 bclr 1 bset 2 bchg 3 btst */
		else k = ((w >> 16) == 0x0a) ? ((w >> 5) & 1) : 2 + ((w >> 5) & 1);
		d->sr = (d->sr & ~SR_C) | (((v >> bit) & 1) ? SR_C : 0);
		switch (k)
		{
		case 0: nv = v & ~(1u << bit); break;
		case 1: nv = v | (1u << bit); break;
		case 2: nv = v ^ (1u << bit); break;
		case 3: break;
		}
		if (k != 3)
		{
			if (isreg) reg_write(d, (int)addr, nv);
			else mem_write(d, space, addr, nv);
		}
		break;
	}
	case K_DO: case K_REP:
	{
		uint32_t count, la;
		if (o->kind == K_DO && o->b == 9)
		{
			/* do/dor forever */
			push(d, d->la, d->lc);
			la = o->c ? (pc + ext) & 0xffffff : ext & 0xffffff;
			push(d, next, d->sr);
			d->la = la; d->lc = 0; d->sr |= SR_LF;
			break;
		}
		if (w & 0x80) count = ((w & 0xf) << 8) | ((w >> 8) & 0xff);
		else
		{
			int mode = (w >> 14) & 3;
			if (mode == 1) count = mem_read(d, (w >> 6) & 1, ea(d, (w >> 11) & 7, (w >> 8) & 7, 0));
			else if (mode == 0) count = mem_read(d, (w >> 6) & 1, (w >> 8) & 0x3f);
			else count = reg_read(d, (w >> 8) & 0x3f);
		}
		count &= 0xffffff;
		if (o->kind == K_REP)
		{
			d->rep_count = count;
			break;
		}
		la = o->b == 1 ? (pc + ext) & 0xffffff : ext & 0xffffff;
		if (count == 0) { next = la + 1; break; }
		push(d, d->la, d->lc);
		push(d, next, d->sr);
		d->la = la; d->lc = count; d->sr |= SR_LF;
		break;
	}
	case K_MOVE_DISP:
	{
		int32_t disp; int rn = (w >> 8) & 7, space, W, code;
		uint32_t addr;
		if (o->c)
		{
			/* 0000 101s 0111 0RRR 1WDD DDDD + xxxx */
			space = (w >> 16) & 1; W = (w >> 6) & 1; code = w & 0x3f; disp = sx24(ext);
		}
		else
		{
			disp = (int32_t)((((w >> 11) & 0x3f) << 1) | ((w >> 6) & 1)); if (disp & 0x40) disp -= 0x80;
			space = (w >> 5) & 1; W = (w >> 4) & 1; code = w & 0xf;
		}
		addr = (d->r[rn] + (uint32_t)disp) & 0xffffff;
		if (W) reg_write(d, code, mem_read(d, space, addr)); else mem_write(d, space, addr, reg_read(d, code));
		break;
	}
	case K_CMPU:
	{
		static const uint8_t src[8] = { 0, 0, 0, 0, 0x04, 0x06, 0x05, 0x07 };
		int D = w & 1, g = (w >> 1) & 7;
		uint64_t a = ((uint64_t)d->acc[D] >> 0) & 0xffffffffffffULL, b;
		if (g == 0) b = (uint64_t)d->acc[D ^ 1] & 0xffffffffffffULL;
		else b = (uint64_t)reg_read(d, src[g]) << 24;
		d->sr &= ~(SR_C | SR_V | SR_Z | SR_N);
		if (a < b) d->sr |= SR_C;
		if (a == b) d->sr |= SR_Z;
		if (((a - b) >> 47) & 1) d->sr |= SR_N;
		break;
	}
	case K_NORM:
	{
		int D = o->c, rn = o->b; int64_t v = d->acc[D];
		uint32_t sr = d->sr;
		flags_nzue(d, v);
		if (!(d->sr & SR_E) && (d->sr & SR_U) && !(d->sr & SR_Z)) { d->acc[D] = sx56(v << 1); d->r[rn] = (d->r[rn] - 1) & 0xffffff; }
		else if (d->sr & SR_E) { d->acc[D] = v >> 1; d->r[rn] = (d->r[rn] + 1) & 0xffffff; }
		(void)sr;
		flags_nzue(d, d->acc[D]);
		break;
	}
	case K_ILLEGAL: d->halted = 1; return -1;
	default:
		d->unknown_count++; d->last_unknown = w;
		break;
	}
	if (d->in_rep) { d->pc = next & 0xffffff; return 0; }
	if (o->kind == K_REP)
	{
		uint32_t i, rpc = next & (DSP_PSIZE - 1), save_lc = d->lc, count = d->rep_count;
		dsp_op *ro;
		if (!d->decoded[rpc]) { decode(d, rpc); d->decoded[rpc] = 1; }
		ro = &d->op[rpc];
		d->in_rep = 1;
		for (i = 0; i < count; i++) { d->pc = rpc; d->lc = count - i; execute(d); }
		d->in_rep = 0;
		d->lc = save_lc;
		pc = rpc; o = ro;
		next = rpc + ro->size;
	}
	/* end of a hardware loop? */
	if ((d->sr & SR_LF) && ((next - 1) & 0xffffff) == d->la && next == pc + o->size)
	{
		if (d->lc > 1 || d->lc == 0) { if (d->lc) d->lc--; next = d->ssh[d->sp & 15]; if (d->lc == 0 && 0) { } }
		else
		{
			d->sr = (d->sr & ~SR_LF) | (d->ssl[d->sp & 15] & SR_LF); pop(d);
			d->la = d->ssh[d->sp & 15]; d->lc = d->ssl[d->sp & 15]; pop(d);
		}
	}
	d->pc = next & 0xffffff;
	return 0;
}

void dsp_invalidate(dsp_core *d) { memset(d->decoded, 0, sizeof(d->decoded)); }

void dsp_reset(dsp_core *d)
{
	int i;
	uint8_t *scratch = d->scratch; uint32_t scratch_size = d->scratch_size;
	memset(d, 0, sizeof(*d));
	d->scratch = scratch; d->scratch_size = scratch_size;
	for (i = 0; i < 8; i++) d->m[i] = 0xffffff;
	d->sr = 0xc00300;
}

int dsp_call(dsp_core *d, uint32_t pc, uint32_t budget)
{
	uint32_t base_sp = d->sp, sr = d->sr, la = d->la, lc = d->lc, i;
	push(d, 0xfffff0, d->sr);                 /* return address no program lives at */
	d->pc = pc;
	for (i = 0; i < budget; i++)
	{
		if (d->pc == 0xfffff0) { return 0; }
		if (execute(d)) break;
	}
	/* it did not return: the control state goes back to what it was, so that no loop of this frame carries over */
	d->sp = base_sp; d->sr = sr; d->la = la; d->lc = lc; d->in_rep = 0;
	return -1;
}
/* one instruction (for analysis): 1 once the program has returned to dsp_call's sentinel */
int dsp_step(dsp_core *d)
{
	if (d->pc == 0xfffff0) return 1;
	execute(d);
	return d->pc == 0xfffff0;
}

const char *dsp_describe(dsp_core *d, uint32_t pc)
{
	static char text[64];
	snprintf(text, sizeof(text), "p:%04x = %06x %06x", pc & 0xffff, d->p[pc & (DSP_PSIZE - 1)], d->p[(pc + 1) & (DSP_PSIZE - 1)]);
	return text;
}

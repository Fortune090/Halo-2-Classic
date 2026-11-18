/* H2Classic: the console's audio processor in software (see apu.h).
 *
 * Voice processor (VP): every 32-sample frame at 48 kHz each active voice is decoded (PCM 8/16, Xbox ADPCM),
 * resampled by its pitch (4096 per octave, 0 = 48 kHz), filtered, shaped by its amplitude envelope and LFO, and added
 * into the 32 mixbins with the attenuation dsound programs into the hardware (ConvertVolumeValues: the voice volume
 * plus the mixbin's volume plus the 3D terms, in 1/64 dB steps, 0xfff = silent). A voice uses (channels + 1) / 2
 * hardware voices, each with an equal share of the mixbin slots; within one the even slots take the left channel and
 * the odd slots the right. A voice that sends to bin 31 while it has an output buffer feeds that buffer (a submix
 * destination, DSBCAPS_MIXIN). A 3D voice runs through the HRTF pair dsound chose: slots 0-1 take one ear, 2-3 the
 * other, the I3DL2 send (bin 10) the unfiltered voice.
 * Global processor (GP): the mixbins are written to the GP's mixbuffer and the game's own effects image runs on the
 * DSP56300 interpreter (apu_dsp.c) on a thread of its own, one frame behind the VP; bins 0-5 are the speakers and the
 * effect returns (DSBCAPS_FXIN) read its output of the frame before.
 * Output: waveOut (winmm, loaded at run time), or a clock when there is no device; H2N_SOUNDWAV records the mix. */
#include <windows.h>
#include <mmsystem.h>
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "apu.h"
#include "apu_dsp.h"
#include "h2n.h"

#define MAXV 512
#define FRAME 32
#define BLOCK_FRAMES 16                      /* 512 samples per output block */

typedef struct packet { const unsigned char *data; unsigned bytes, samples, cookie; } packet;

enum { EG_OFF, EG_DELAY, EG_ATTACK, EG_HOLD, EG_DECAY, EG_SUSTAIN, EG_RELEASE, EG_DONE };

typedef struct voice
{
	int used, kind, format, channels, rate, block_align, input_bin, is3d;
	const unsigned char *data; unsigned bytes;
	packet q[64]; int qn; double q_base;     /* absolute sample index of q[0]'s first sample */
	int discontinuity;
	int active, looping, paused, synch_wait, starved;
	double pos;
	int volume, nslots, pitch, output;
	unsigned char slot_bin[8];
	int bin_volume[32];
	/* amplitude envelope */
	int eg_mode, eg_delay, eg_attack, eg_hold, eg_decay, eg_release, eg_sustain;
	int eg_phase; double eg_level, eg_count, eg_from;
	int release_flush;                       /* a stream: flush everything when the release ends */
	/* LFOs: 0 multi (amplitude, cutoff, pitch), 1 pitch */
	int lfo_delay[2], lfo_delta[2], lfo_pitch[2], lfo_cut, lfo_amp, lfo_frames[2];
	unsigned lfo_phase[2];
	/* filter */
	int f_mode, f_q; unsigned f_c[4];
	float s_low[6], s_band[6], s2_low[6], s2_band[6], op_direct, op_room;
	/* 3D */
	int has3d;
	int a_front, a_rear, a_center, a_i3dl2, a_other;
	float hrtf_l[31], hrtf_r[31]; int itd;
	float hist[128]; int hist_pos;
	/* submix input */
	float in[FRAME]; int in_active, idle_frames;
	/* ADPCM block cache */
	int cache_block; const unsigned char *cache_src; short cache[6][64];
	/* interpolation: the two source samples around the position */
	double last_index; float prev[6], cur[6];
#ifdef H2N_ANALYSIS
	double an_src1, an_src2, an_67, an_31, an_in1, an_in2; unsigned an_n, an_in_n;   /* analysis builds: the last 5 s */
	float in_copy[FRAME];
#endif
} voice;

static CRITICAL_SECTION g_lock, g_gp_lock;
static voice g_v[MAXV];
static dsp_core *g_gp;
static int g_gp_loaded;
static float g_mix[32][FRAME];              /* the VP's mixbins for the frame being made */
static float g_gp_in[32][FRAME];            /* the frame handed to the GP */
static float g_multipass[32][FRAME];        /* the GP's mixbuffer after a finished frame (effect returns read it) */
static float g_mp_new[32][FRAME];           /* the GP's mixbuffer as the frame in progress leaves it */
static float g_speakers[6][FRAME];
static int g_headroom[32];
static apu_completion g_done[2048]; static int g_done_n;
static int g_started;
static HANDLE g_thread, g_gp_thread, g_gp_go, g_gp_done;
static FILE *g_wav; static unsigned g_wav_bytes;
static int g_output_channels = 2;
static double g_frames_mixed;
static unsigned g_stat_voices, g_stat_gp_over, g_stat_peak_voices, g_stat_starved, g_stat_underruns, g_stat_clipped;
static float g_volume = 1.0f;                /* H2N_VOLUME: the player's own master volume (1 = as the console) */
static int g_hrtf_mode = 1, g_no_gp;
static int g_meters;                         /* H2N_SOUNDLOG: the level of every mixbin and speaker every 5 s */
static double g_meter_in[32], g_meter_out[6], g_meter_peak[6];
static unsigned g_meter_frames;
static double g_gp_seconds, g_vp_seconds;
static LARGE_INTEGER g_freq;

/* ------------------------------------------------------------------ helpers */
/* the hardware's attenuation: hundredths of a decibel -> 1/64 dB steps (unsigned, so a gain above 0 dB is silence),
   0xfff and above silent */
static float att_gain(int att)
{
	unsigned hw = (unsigned)att << 6;
	hw /= 100;
	if (hw >= 0xfff) return 0.0f;
	if (!hw) return 1.0f;
	return (float)pow(10.0, -(double)hw / (64.0 * 20.0));
}
static inline int sx16(unsigned v) { return (int)(short)(v & 0xffff); }

static const int ima_step[89] = {
	7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118,
	130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963, 1060,
	1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484,
	7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767 };
static const int ima_index[16] = { -1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8 };

/* one Xbox ADPCM block (36 bytes per channel, 64 samples): a 4-byte header per channel (the block's first sample, the
   step index), then groups of 4 bytes (8 nibbles, low nibble first) of each channel in turn. The nibbles code the 63
   samples after the header's; the 64th nibble only pads the block (as OpenCE found against the console: decoding it
   in place of the header's sample puts a wrong sample in every 64, a buzz at 344 Hz in a 22 kHz sound). */
static void adpcm_block(const unsigned char *src, int channels, short out[6][64])
{
	int ch, g, i;
	for (ch = 0; ch < channels; ch++)
	{
		const unsigned char *h = src + ch * 4;
		int pred = (short)(h[0] | (h[1] << 8)), index = h[2], n = 1;
		if (index > 88) index = 88;
		out[ch][0] = (short)pred;
		for (g = 0; g < 8; g++)
		{
			const unsigned char *p = src + channels * 4 + (g * channels + ch) * 4;
			for (i = 0; i < 8 && n < 64; i++)
			{
				int nib = (p[i >> 1] >> ((i & 1) * 4)) & 15, step = ima_step[index], diff = step >> 3;
				if (nib & 1) diff += step >> 2;
				if (nib & 2) diff += step >> 1;
				if (nib & 4) diff += step;
				if (nib & 8) pred -= diff; else pred += diff;
				if (pred > 32767) pred = 32767; else if (pred < -32768) pred = -32768;
				index += ima_index[nib]; if (index < 0) index = 0; else if (index > 88) index = 88;
				out[ch][n++] = (short)pred;
			}
		}
	}
}

static unsigned samples_in(voice *v, unsigned bytes)
{
	if (v->format == APU_FMT_ADPCM) return v->block_align ? bytes / (unsigned)v->block_align * 64 : 0;
	if (v->format == APU_FMT_PCM8) return bytes / (unsigned)v->channels;
	if (v->format == APU_FMT_NONE) return 0;
	return bytes / (2u * (unsigned)v->channels);
}

/* the sample (all channels) at index i of a piece of data; returns 0 if outside */
static int read_sample(voice *v, const unsigned char *data, unsigned bytes, unsigned i, float *out)
{
	int ch;
	if (v->format == APU_FMT_ADPCM)
	{
		unsigned block = i / 64, at = i % 64;
		if ((block + 1) * (unsigned)v->block_align > bytes) return 0;
		if (v->cache_src != data || v->cache_block != (int)block)
		{
			adpcm_block(data + block * v->block_align, v->channels, v->cache);
			v->cache_src = data; v->cache_block = (int)block;
		}
		for (ch = 0; ch < v->channels; ch++) out[ch] = v->cache[ch][at] * (1.0f / 32768.0f);
		return 1;
	}
	if (v->format == APU_FMT_PCM8)
	{
		if ((i + 1) * (unsigned)v->channels > bytes) return 0;
		for (ch = 0; ch < v->channels; ch++) out[ch] = ((int)data[i * v->channels + ch] - 128) * (1.0f / 128.0f);
		return 1;
	}
	if (v->format == APU_FMT_NONE) return 0;
	if ((i + 1) * 2u * (unsigned)v->channels > bytes) return 0;
	for (ch = 0; ch < v->channels; ch++)
	{
		const unsigned char *p = data + (i * v->channels + ch) * 2;
		out[ch] = (short)(p[0] | (p[1] << 8)) * (1.0f / 32768.0f);
	}
	return 1;
}

/* source sample at an absolute index of a stream (across its queued packets) */
static int stream_sample(voice *v, double index, float *out)
{
	double base = v->q_base;
	int k;
	for (k = 0; k < v->qn; k++)
	{
		if (index < base + v->q[k].samples)
			return read_sample(v, v->q[k].data, v->q[k].bytes, (unsigned)(index - base), out);
		base += v->q[k].samples;
	}
	return 0;
}

static void complete(int idx, unsigned status, unsigned bytes, unsigned cookie)
{
	if (g_done_n < (int)(sizeof(g_done) / sizeof(g_done[0])))
	{
		g_done[g_done_n].voice = idx; g_done[g_done_n].cookie = cookie; g_done[g_done_n].status = status; g_done[g_done_n].completed = bytes;
		g_done_n++;
	}
}
/* CMcpxStream::Flush: every packet still queued completes as flushed, and the voice stops */
static void stream_flush_now(voice *v, int idx)
{
	int k;
	for (k = 0; k < v->qn; k++) complete(idx, 1, 0, v->q[k].cookie);
	v->qn = 0; v->q_base = 0; v->pos = 0; v->last_index = -1;
	v->active = 0; v->starved = 0; v->discontinuity = 0; v->release_flush = 0;
	v->eg_phase = EG_OFF;
}

/* ------------------------------------------------------------------ envelope and LFO */
static void eg_start(voice *v)
{
	v->eg_count = 0;
	switch (v->eg_mode)
	{
	case 0: v->eg_phase = EG_OFF; v->eg_level = 1.0; break;
	case 1: v->eg_phase = EG_DELAY; v->eg_level = 0.0; break;
	case 2: v->eg_phase = EG_ATTACK; v->eg_level = 0.0; break;
	default: v->eg_phase = EG_HOLD; v->eg_level = 1.0; break;
	}
}
/* the voice is activated: the envelope starts, the LFOs wait out their delays again */
static void activate(voice *v)
{
	v->active = 1; v->starved = 0; v->release_flush = 0;
	v->last_index = -1;
	eg_start(v);
	v->lfo_frames[0] = v->lfo_frames[1] = 0;
}
/* the release segment; returns 0 if there is none (the voice stops at once) */
static int eg_release(voice *v)
{
	if (v->eg_release <= 0) return 0;
	v->eg_from = v->eg_phase == EG_OFF ? 1.0 : v->eg_level;
	v->eg_phase = EG_RELEASE; v->eg_count = 0;
	return 1;
}
static float eg_step(voice *v)
{
	double len;
	switch (v->eg_phase)
	{
	case EG_OFF: return 1.0f;
	case EG_DELAY:
		if (v->eg_count++ >= v->eg_delay * 512.0) { v->eg_phase = EG_ATTACK; v->eg_count = 0; }
		return 0.0f;
	case EG_ATTACK:
		len = v->eg_attack * 512.0;
		if (len <= 0 || v->eg_count >= len) { v->eg_level = 1.0; v->eg_phase = EG_HOLD; v->eg_count = 0; }
		else v->eg_level = v->eg_count++ / len;
		return (float)v->eg_level;
	case EG_HOLD:
		if (v->eg_count++ >= v->eg_hold * 512.0) { v->eg_phase = EG_DECAY; v->eg_count = 0; }
		v->eg_level = 1.0; return 1.0f;
	case EG_DECAY:
	{
		double sus = v->eg_sustain / 255.0;
		len = v->eg_decay * 512.0;
		if (len <= 0 || v->eg_count >= len) { v->eg_level = sus; v->eg_phase = EG_SUSTAIN; }
		else v->eg_level = 1.0 + (sus - 1.0) * (v->eg_count++ / len);
		return (float)v->eg_level;
	}
	case EG_SUSTAIN: v->eg_level = v->eg_sustain / 255.0; return (float)v->eg_level;
	case EG_RELEASE:
		len = v->eg_release * 512.0;
		if (v->eg_count >= len) { v->eg_level = 0; v->eg_phase = EG_DONE; return 0.0f; }
		v->eg_level = v->eg_from * (1.0 - v->eg_count++ / len);
		return (float)v->eg_level;
	}
	return 0.0f;
}
static float lfo_value(voice *v, int k)
{
	float t = (v->lfo_phase[k] & 0xffff) / 65536.0f;     /* triangle: 0 -> 1 -> 0 -> -1 -> 0 over the 16-bit phase */
	if (t < 0.25f) return t * 4.0f;
	if (t < 0.75f) return 2.0f - t * 4.0f;
	return t * 4.0f - 4.0f;
}

/* ------------------------------------------------------------------ filters */
/* the DLS2 resonant low pass as a state variable filter: c0 = 4096 * log2(2 sin(pi f / 48000)) (0 is 8 kHz, the
   most the game asks for), c1 = 32768 / resonance */
static inline float svf(float *low, float *band, float in, float F, float q)
{
	float high;
	*low += F * *band;
	high = in - *low - q * *band;
	*band += F * high;
	return *low;
}
static inline float dls2(voice *v, int ch, float x, unsigned c0, unsigned c1, float cut_mod)
{
	float F = (float)pow(2.0, (sx16(c0) + cut_mod) / 4096.0), q = (c1 & 0xffff) / 32768.0f;
	if (F > 1.0f) F = 1.0f;
	if (q < 0.05f) q = 0.05f;
	return svf(&v->s_low[ch], &v->s_band[ch], x, F, q);
}
/* one parametric EQ band: centre c (as DLS2's c0), gain word g ((G - 1) * 8192, or 0xc000 + (1 - G) * 8192 for a cut) */
static inline float peq(float *low, float *band, float x, unsigned c, unsigned gw, float q)
{
	float F = (float)pow(2.0, sx16(c) / 4096.0), g;
	gw &= 0xffff;
	if (F > 1.0f) F = 1.0f;
	g = gw >= 0xc000 ? -(float)(gw - 0xc000) / 8192.0f : gw / 8192.0f;
	if (g == 0.0f) return x;
	svf(low, band, x, F, q);
	return x + g * *band * q;
}
static const float peq_q[8] = { 1.4142f, 1.0f, 0.7071f, 0.5f, 0.3536f, 0.25f, 0.1768f, 0.125f };

/* the voice's own filter on one channel (for a 3D voice the multi mode's second pair is the I3DL2 filter, applied in
   mix_3d) */
static float filter_sample(voice *v, int ch, float x, float cut_mod)
{
	switch (v->f_mode)
	{
	case 1: return dls2(v, ch, x, v->f_c[0], v->f_c[1], cut_mod);
	case 2:
		x = peq(&v->s_low[ch], &v->s_band[ch], x, v->f_c[0], v->f_c[1], peq_q[v->f_q & 7]);
		return peq(&v->s2_low[ch], &v->s2_band[ch], x, v->f_c[2], v->f_c[3], peq_q[v->f_q & 7]);
	case 3:
		if ((v->f_c[0] & 0xffff) || (v->f_c[1] & 0xffff)) x = dls2(v, ch, x, v->f_c[0], v->f_c[1], cut_mod);
		if (!v->has3d) x = peq(&v->s2_low[ch], &v->s2_band[ch], x, v->f_c[2], v->f_c[3], peq_q[v->f_q & 7]);
		return x;
	}
	return x;
}

/* ------------------------------------------------------------------ one frame of one voice */
/* up to 6 channels of FRAME samples; returns the channel count */
static int voice_render(voice *v, float out[6][FRAME])
{
	int i, ch, nch = v->channels;
	double step, lfo_p = 0;
	float amp_lfo = 1.0f, cut_mod = 0.0f;
	for (i = 0; i < 2; i++)
	{
		if (v->lfo_frames[i] < v->lfo_delay[i]) { v->lfo_frames[i]++; continue; }
		v->lfo_phase[i] += (unsigned)v->lfo_delta[i];
	}
	if (v->lfo_pitch[1]) lfo_p += lfo_value(v, 1) * v->lfo_pitch[1] * 32.0;
	if (v->lfo_pitch[0]) lfo_p += lfo_value(v, 0) * v->lfo_pitch[0] * 32.0;
	if (v->lfo_amp) amp_lfo = 1.0f + lfo_value(v, 0) * v->lfo_amp / 128.0f;
	if (v->lfo_cut) cut_mod = lfo_value(v, 0) * v->lfo_cut * 32.0f;
	if (v->kind == APU_KIND_MIXIN)
	{
		for (i = 0; i < FRAME; i++) out[0][i] = v->in[i];
		nch = 1;
	}
	else if (v->kind == APU_KIND_FXIN)
	{
		int b = v->input_bin & 31;
		for (i = 0; i < FRAME; i++) out[0][i] = g_multipass[b][i];
		nch = 1;
	}
	else
	{
		step = pow(2.0, (v->pitch + lfo_p) / 4096.0);
		for (i = 0; i < FRAME; i++)
		{
			double p = v->pos, index = floor(p);
			float frac = (float)(p - index);
			if (index != v->last_index)
			{
				float s0[6], s1[6];
				int ok0, ok1;
				if (v->kind == APU_KIND_STREAM) { ok0 = stream_sample(v, index, s0); ok1 = stream_sample(v, index + 1, s1); }
				else
				{
					unsigned n = samples_in(v, v->bytes);
					unsigned i0 = n ? (unsigned)index % n : 0, i1 = n ? (unsigned)(index + 1) % n : 0;
					ok0 = n && read_sample(v, v->data, v->bytes, i0, s0);
					if (!v->looping && index + 1 >= n) ok1 = 0; else ok1 = n && read_sample(v, v->data, v->bytes, i1, s1);
				}
				for (ch = 0; ch < nch; ch++) { v->prev[ch] = ok0 ? s0[ch] : 0.0f; v->cur[ch] = ok1 ? s1[ch] : (ok0 ? s0[ch] : 0.0f); }
				v->last_index = index;
			}
			for (ch = 0; ch < nch; ch++) out[ch][i] = v->prev[ch] + frac * (v->cur[ch] - v->prev[ch]);
			v->pos = p + step;
			if (v->kind == APU_KIND_BUFFER)
			{
				unsigned n = samples_in(v, v->bytes);
				if (!n || v->pos >= n)
				{
					if (n && v->looping) { v->pos = fmod(v->pos, (double)n); v->last_index = -1; }
					else
					{
						/* the end of a one-shot buffer: the voice stops and the next Play starts it over */
						v->active = 0; v->pos = 0; v->last_index = -1;
						for (i = i + 1; i < FRAME; i++) for (ch = 0; ch < nch; ch++) out[ch][i] = 0;
						break;
					}
				}
			}
		}
	}
	for (ch = 0; ch < nch; ch++)
		if (v->f_mode) for (i = 0; i < FRAME; i++) out[ch][i] = filter_sample(v, ch, out[ch][i], cut_mod);
	for (i = 0; i < FRAME; i++)
	{
		float e = eg_step(v) * amp_lfo;
		if (e != 1.0f) for (ch = 0; ch < nch; ch++) out[ch][i] *= e;
	}
	return nch;
}

static void stream_after_frame(voice *v, int idx)
{
	while (v->qn && v->pos >= v->q_base + v->q[0].samples)
	{
		v->q_base += v->q[0].samples;
		complete(idx, 0, v->q[0].bytes, v->q[0].cookie);
		memmove(&v->q[0], &v->q[1], (size_t)(v->qn - 1) * sizeof(packet));
		v->qn--;
	}
	if (!v->qn)
	{
		/* out of data: after a discontinuity the stream ends (OnEndOfStream), otherwise it starves and waits */
		if (v->discontinuity) { v->active = 0; v->starved = 0; v->discontinuity = 0; v->pos = v->q_base = 0; v->last_index = -1; }
		else { if (!v->starved) g_stat_starved++; v->starved = 1; }
	}
}

/* ------------------------------------------------------------------ mixing into the bins */
static void mix_bins(voice *v, float out[6][FRAME], int nch, voice *dst)
{
	int s, i;
	int nhw = (nch + 1) / 2, per = nhw ? v->nslots / nhw : 0;
	for (s = 0; s < v->nslots; s++)
	{
		int bin = v->slot_bin[s] & 31, h = per ? s / per : 0, ch;
		float g, *src, *to;
		if (h >= nhw) continue;
		ch = 2 * h + ((s - h * per) & 1);
		if (ch >= nch) ch = nch - 1;
		g = att_gain(-(v->bin_volume[bin] + v->volume));
		if (g == 0.0f) continue;
		src = out[ch];
		if (bin == 31) { if (!dst) continue; to = dst->in; dst->in_active = 1; }
		else to = g_mix[bin];
		for (i = 0; i < FRAME; i++) to[i] += src[i] * g;
#ifdef H2N_ANALYSIS
		if (bin == 6 || bin == 7) for (i = 0; i < FRAME; i++) v->an_67 += (double)src[i] * g * src[i] * g;
		if (bin == 31) for (i = 0; i < FRAME; i++) v->an_31 += (double)src[i] * g * src[i] * g;
#endif
	}
}
static int class_att(voice *v, int bin)
{
	return bin == 6 || bin == 7 ? v->a_front : bin == 8 || bin == 9 ? v->a_rear : bin == 2 ? v->a_center : bin == 10 ? v->a_i3dl2 : v->a_other;
}
/* a 3D voice: the multi mode's I3DL2 one-poles (direct and room), the HRTF pair on the direct path */
static void mix_3d(voice *v, float *x)
{
	float l[FRAME], r[FRAME], direct[FRAME], room[FRAME];
	int i, k, s;
	float ad = v->f_mode == 3 ? (v->f_c[2] & 0xffff) / 65536.0f : 0.0f, ar = v->f_mode == 3 ? (v->f_c[3] & 0xffff) / 65536.0f : 0.0f;
	for (i = 0; i < FRAME; i++)
	{
		float y = x[i];
		if (ad > 0.0f) { v->op_direct = (1.0f - ad) * y + ad * v->op_direct; direct[i] = v->op_direct; } else direct[i] = y;
		if (ar > 0.0f) { v->op_room = (1.0f - ar) * y + ar * v->op_room; room[i] = v->op_room; } else room[i] = y;
	}
	for (i = 0; i < FRAME; i++)
	{
		v->hist[v->hist_pos & 127] = direct[i];
		if (!g_hrtf_mode) l[i] = r[i] = direct[i];
		else
		{
			float al = 0, arr = 0;
			int dl = v->itd > 0 ? v->itd : 0, dr = v->itd < 0 ? -v->itd : 0;
			for (k = 0; k < 31; k++)
			{
				al += v->hrtf_l[k] * v->hist[(v->hist_pos - k - dl) & 127];
				arr += v->hrtf_r[k] * v->hist[(v->hist_pos - k - dr) & 127];
			}
			l[i] = al; r[i] = arr;
		}
		v->hist_pos++;
	}
	for (s = 0; s < v->nslots; s++)
	{
		int bin = v->slot_bin[s] & 31;
		float g = att_gain(-(v->bin_volume[bin] + v->volume) - class_att(v, bin)), *src;
		if (g == 0.0f || bin == 31) continue;
		src = s < 2 ? l : s < 4 ? r : bin == 10 ? room : direct;
		for (i = 0; i < FRAME; i++) g_mix[bin][i] += src[i] * g;
#ifdef H2N_ANALYSIS
		if (bin == 6 || bin == 7) for (i = 0; i < FRAME; i++) v->an_67 += (double)src[i] * g * src[i] * g;
#endif
	}
}

static int voice_running(voice *v)
{
	if (!v->used) return 0;
	if (v->kind == APU_KIND_MIXIN) return v->in_active || v->idle_frames < 8;
	if (!v->active || v->paused || v->synch_wait) return 0;
	if (v->kind == APU_KIND_STREAM && (v->starved || !v->qn)) return 0;
	return 1;
}

static void route(voice *v, float out[6][FRAME], int nch)
{
	voice *dst = v->output >= 0 && v->output < MAXV && g_v[v->output].used ? &g_v[v->output] : NULL;
	if (v->has3d) mix_3d(v, out[0]);
	else mix_bins(v, out, nch, dst);
}

static void frame_vp(void)
{
	int idx, n = 0;
	static float out[6][FRAME];
	memset(g_mix, 0, sizeof(g_mix));
	/* the sources first: buffers, streams and effect returns */
	for (idx = 0; idx < MAXV; idx++)
	{
		voice *v = &g_v[idx];
		int nch;
		if (!v->used || v->kind == APU_KIND_MIXIN) continue;
		if (v->kind == APU_KIND_STREAM && v->active && v->starved && v->qn) v->starved = 0;
		if (!voice_running(v)) continue;
		n++;
		nch = voice_render(v, out);
#ifdef H2N_ANALYSIS
		{
			int i2;
			for (i2 = 0; i2 < FRAME; i2++) { v->an_src1 += out[0][i2]; v->an_src2 += (double)out[0][i2] * out[0][i2]; }
			v->an_n += FRAME;
		}
#endif
		route(v, out, nch);
		if (v->kind == APU_KIND_STREAM) stream_after_frame(v, idx);
		if (v->eg_phase == EG_DONE)
		{
			/* the end of the release: the hardware turns the voice off; a stream is flushed (ServiceVoiceInterrupt) */
			if (v->kind == APU_KIND_STREAM) stream_flush_now(v, idx);
			else { v->active = 0; v->eg_phase = EG_OFF; }
		}
	}
	/* then the submix destinations */
	for (idx = 0; idx < MAXV; idx++)
	{
		voice *v = &g_v[idx];
		if (!v->used || v->kind != APU_KIND_MIXIN) continue;
#ifdef H2N_ANALYSIS
		memset(v->in_copy, 0, sizeof(v->in_copy));
#endif
		if (v->in_active) v->idle_frames = 0; else if (v->idle_frames < 1000) v->idle_frames++;
		if (voice_running(v))
		{
			int nch;
#ifdef H2N_ANALYSIS
			{
				int i2;
				for (i2 = 0; i2 < FRAME; i2++) { v->an_in1 += v->in[i2]; v->an_in2 += (double)v->in[i2] * v->in[i2]; }
				v->an_in_n += FRAME;
				memcpy(v->in_copy, v->in, sizeof(v->in_copy));
			}
#endif
			nch = voice_render(v, out);
			n++;
			route(v, out, nch);
		}
		memset(v->in, 0, sizeof(v->in));
		v->in_active = 0;
	}
	g_stat_voices = (unsigned)n;
	if ((unsigned)n > g_stat_peak_voices) g_stat_peak_voices = (unsigned)n;
#ifdef H2N_ANALYSIS
	{
		/* analysis builds, H2N_DSPDUMP=2: h2classic\\capture.raw, 10 channels of 16-bit samples at 48 kHz: bins 6 and 7, then the
		   inputs of the submix voices 82 to 89 */
		static FILE *cap;
		static int tried;
		const char *d = getenv("H2N_DSPDUMP");
		if (!tried && d && atoi(d) == 2) { tried = 1; CreateDirectoryW(L"h2classic", NULL); cap = h2n_real_fopen("h2classic\\capture.raw", "wb"); }
		if (cap)
		{
			static short buf[FRAME * 10];
			int i2, c;
			for (i2 = 0; i2 < FRAME; i2++)
				for (c = 0; c < 10; c++)
				{
					float x = c == 0 ? g_mix[6][i2] : c == 1 ? g_mix[7][i2] : g_v[80 + c].in_copy[i2];
					x *= 32767.0f;
					buf[i2 * 10 + c] = (short)(x > 32767.0f ? 32767 : x < -32768.0f ? -32768 : x);
				}
			fwrite(buf, sizeof(buf), 1, cap);
		}
	}
	{
		/* analysis builds: every 5 s, the voices that put the most into the 3D bins 6 and 7 */
		static unsigned frames;
		if (g_meters && ++frames == 7500)
		{
			int k, best[6] = { -1, -1, -1, -1, -1, -1 };
			frames = 0;
			for (idx = 0; idx < MAXV; idx++)
			{
				voice *m = &g_v[idx];
				if (m->used && m->kind == APU_KIND_MIXIN && m->an_in_n)
				{
					int j, shown = 0;
					h2n_log("sound submix %d: input rms %.0f dB, mean %+.4f; fed by:", idx, 10.0 * log10(m->an_in2 / m->an_in_n + 1e-20), m->an_in1 / m->an_in_n);
					for (j = 0; j < MAXV && shown < 12; j++)
						if (g_v[j].used && g_v[j].output == idx && g_v[j].an_31 > 0)
						{
							voice *f = &g_v[j];
							double n2 = f->an_n ? f->an_n : 1;
							shown++;
							h2n_log(" [%d: %.0f dB, src rms %.0f mean %+.4f, kind %d fmt %d %dch %dHz vol %d eg %d loop %d]", j, 10.0 * log10(f->an_31 / (7500.0 * FRAME) + 1e-20),
								10.0 * log10(f->an_src2 / n2 + 1e-20), f->an_src1 / n2, f->kind, f->format, f->channels, f->rate, f->volume, f->eg_mode, f->looping);
						}
					h2n_log("\n");
				}
			}
			for (idx = 0; idx < MAXV; idx++)
			{
				if (!g_v[idx].used || g_v[idx].an_67 <= 0) continue;
				for (k = 0; k < 6; k++)
					if (best[k] < 0 || g_v[idx].an_67 > g_v[best[k]].an_67) { memmove(best + k + 1, best + k, (5 - k) * sizeof(int)); best[k] = idx; break; }
			}
			for (k = 0; k < 6 && best[k] >= 0; k++)
			{
				voice *v = &g_v[best[k]];
				double n2 = v->an_n ? v->an_n : 1;
				h2n_log("sound voice %d: into bins 6/7 %.0f dB; source rms %.0f dB, mean %+.4f; kind %d format %d, %d ch, %d Hz, pitch %d, 3D %d; "
					"volume %d, bins %d/%d/%d/%d/%d (volumes %d %d), front %d rear %d room %d other %d; filter %d q %d c %08x %08x %08x %08x; eg %d, looping %d\n",
					best[k], 10.0 * log10(v->an_67 / (7500.0 * FRAME) + 1e-20), 10.0 * log10(v->an_src2 / n2 + 1e-20), v->an_src1 / n2,
					v->kind, v->format, v->channels, v->rate, v->pitch, v->has3d, v->volume, v->slot_bin[0], v->slot_bin[1], v->slot_bin[2], v->slot_bin[3], v->slot_bin[4],
					v->bin_volume[6], v->bin_volume[7], v->a_front, v->a_rear, v->a_i3dl2, v->a_other, v->f_mode, v->f_q, v->f_c[0], v->f_c[1], v->f_c[2], v->f_c[3], v->eg_mode, v->looping);
			}
			for (idx = 0; idx < MAXV; idx++)
			{
				g_v[idx].an_67 = g_v[idx].an_src1 = g_v[idx].an_src2 = g_v[idx].an_31 = g_v[idx].an_in1 = g_v[idx].an_in2 = 0;
				g_v[idx].an_n = g_v[idx].an_in_n = 0;
			}
		}
	}
#endif
}

/* ------------------------------------------------------------------ the global processor */
/* one GP frame on g_gp_in: the mixbuffer in, the effects, the mixbuffer out (g_multipass) and the speakers */
static void gp_frame(void)
{
	int b, i;
	EnterCriticalSection(&g_gp_lock);
	if (g_gp && g_gp_loaded && !g_no_gp)
	{
		for (b = 0; b < 32; b++)
		{
			float scale = 8388608.0f / (float)(1 << g_headroom[b]);
			uint32_t *x = &g_gp->x[0xc00 + b * FRAME];
			for (i = 0; i < FRAME; i++)
			{
				float s = g_gp_in[b][i] * scale;
				int v;
				if (s > 8388607.0f) s = 8388607.0f; else if (s < -8388608.0f) s = -8388608.0f;
				v = (int)s;
				x[i] = (unsigned)v & 0xffffff;
			}
		}
		{
#ifdef H2N_ANALYSIS
			static dsp_core *before;
			static int dumped;
			int dump = getenv("H2N_DSPDUMP") != NULL;
			if (dump && !dumped) { if (!before) before = (dsp_core *)malloc(sizeof(dsp_core)); if (before) *before = *g_gp; }
#endif
			if (dsp_call(g_gp, 0x171, 4000000))
			{
				g_stat_gp_over++;
				if (g_stat_gp_over <= 3) h2n_log("sound: the effects did not finish a frame in 4,000,000 instructions; at p:%04x %s\n", g_gp->pc, dsp_describe(g_gp, g_gp->pc));
#ifdef H2N_ANALYSIS
				if (dump && !dumped && before)
				{
					/* analysis builds only: the processor as it was at the start of the frame that did not finish */
					dumped = 1;
					apu_dump("h2classic\\dsp_stuck.bin", before, sizeof(dsp_core));
					apu_dump("h2classic\\dsp_stuck_scratch.bin", before->scratch, before->scratch_size);
				}
#endif
			}
		}
		for (b = 0; b < 32; b++)
		{
			uint32_t *x = &g_gp->x[0xc00 + b * FRAME];
			for (i = 0; i < FRAME; i++) g_mp_new[b][i] = (((int)(x[i] << 8)) >> 8) * (1.0f / 8388608.0f);
		}
		for (b = 0; b < 6; b++) memcpy(g_speakers[b], g_mp_new[b], sizeof(g_speakers[b]));
#ifdef H2N_ANALYSIS
		{
			/* analysis builds only (never sent): H2N_DSPDUMP=2 keeps the processor's state every 10 s for replaying here */
			static unsigned frames, n;
			const char *d = getenv("H2N_DSPDUMP");
			if (d && atoi(d) == 2 && ++frames % 15000 == 0 && n < 40)
			{
				char name[64];
				snprintf(name, sizeof(name), "h2classic\\dsp_state_%02u.bin", n++);
				apu_dump(name, g_gp, sizeof(dsp_core));
			}
		}
#endif
	}
	else
	{
		/* no effects image: the 3D bins go straight to the speakers */
		for (i = 0; i < FRAME; i++)
		{
			g_speakers[0][i] = g_gp_in[0][i] + g_gp_in[6][i]; g_speakers[1][i] = g_gp_in[1][i] + g_gp_in[7][i];
			g_speakers[2][i] = g_gp_in[2][i]; g_speakers[3][i] = g_gp_in[3][i];
			g_speakers[4][i] = g_gp_in[4][i] + g_gp_in[8][i]; g_speakers[5][i] = g_gp_in[5][i] + g_gp_in[9][i];
		}
		memcpy(g_mp_new, g_gp_in, sizeof(g_mp_new));
	}
	if (g_meters)
	{
		for (b = 0; b < 32; b++) for (i = 0; i < FRAME; i++) g_meter_in[b] += (double)g_gp_in[b][i] * g_gp_in[b][i];
		for (b = 0; b < 6; b++) for (i = 0; i < FRAME; i++)
		{
			double a = g_speakers[b][i] < 0 ? -g_speakers[b][i] : g_speakers[b][i];
			g_meter_out[b] += a * a;
			if (a > g_meter_peak[b]) g_meter_peak[b] = a;
		}
		if (++g_meter_frames == 7500)
		{
			/* every 5 s: the RMS level of each mixbin going into the effects and of each speaker coming out, in dB */
			char text[1024];
			int at = 0;
			double n = 7500.0 * FRAME;
			static const char *spk[6] = { "FL", "FR", "C", "LFE", "BL", "BR" };
			at += snprintf(text + at, sizeof(text) - at, "sound levels: mixbins");
			for (b = 0; b < 32; b++)
				if (g_meter_in[b] > n * 1e-9) at += snprintf(text + at, sizeof(text) - at, " %d:%.0f", b, 10.0 * log10(g_meter_in[b] / n));
			at += snprintf(text + at, sizeof(text) - at, "; speakers");
			for (b = 0; b < 6; b++)
				if (g_meter_out[b] > n * 1e-9) at += snprintf(text + at, sizeof(text) - at, " %s:%.0f (peak %.2f)", spk[b], 10.0 * log10(g_meter_out[b] / n), g_meter_peak[b]);
			h2n_log("%s dB\n", text);
			memset(g_meter_in, 0, sizeof(g_meter_in)); memset(g_meter_out, 0, sizeof(g_meter_out)); memset(g_meter_peak, 0, sizeof(g_meter_peak));
			g_meter_frames = 0;
		}
	}
	LeaveCriticalSection(&g_gp_lock);
}
static DWORD WINAPI gp_thread(LPVOID unused)
{
	(void)unused;
	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
	for (;;)
	{
		WaitForSingleObject(g_gp_go, INFINITE);
		gp_frame();
		SetEvent(g_gp_done);
	}
	return 0;
}

static short clip16(float s)
{
	s *= 32767.0f * g_volume;
	if (s > 32767.0f) { g_stat_clipped++; return 32767; }
	if (s < -32768.0f) { g_stat_clipped++; return -32768; }
	return (short)s;
}

/* 'frames' 32-sample frames into 16-bit interleaved output. The GP works on frame n on its own thread while the VP
   makes frame n + 1 here; the speakers of frame n go out after both, and the effect returns of frame n + 2 read the GP
   output of frame n (one frame later than the console's, 0.7 ms). */
static void render(short *pcm, int frames)
{
	int f, i;
	LARGE_INTEGER t0, t1, t2;
	for (f = 0; f < frames; f++)
	{
		QueryPerformanceCounter(&t0);
		SetEvent(g_gp_go);
		EnterCriticalSection(&g_lock);
		frame_vp();
		LeaveCriticalSection(&g_lock);
		QueryPerformanceCounter(&t1);
		WaitForSingleObject(g_gp_done, INFINITE);
		QueryPerformanceCounter(&t2);
		g_vp_seconds += (double)(t1.QuadPart - t0.QuadPart) / (double)g_freq.QuadPart;
		g_gp_seconds += (double)(t2.QuadPart - t0.QuadPart) / (double)g_freq.QuadPart;
		/* the GP's speakers are those of the frame before this one; its next input is this frame's mixbins */
		for (i = 0; i < FRAME; i++)
		{
			short *o = pcm + (f * FRAME + i) * g_output_channels;
			if (g_output_channels == 6)
			{
				int c;
				for (c = 0; c < 6; c++) o[c] = clip16(g_speakers[c][i]);
			}
			else
			{
				/* stereo: the front pair, the centre and the rear folded in at -3 dB */
				o[0] = clip16(g_speakers[0][i] + 0.7071f * (g_speakers[2][i] + g_speakers[4][i]));
				o[1] = clip16(g_speakers[1][i] + 0.7071f * (g_speakers[2][i] + g_speakers[5][i]));
			}
		}
		memcpy(g_gp_in, g_mix, sizeof(g_gp_in));
		memcpy(g_multipass, g_mp_new, sizeof(g_multipass));
		g_frames_mixed += 1;
	}
	if (g_wav)
	{
		fwrite(pcm, 2, (size_t)frames * FRAME * g_output_channels, g_wav);
		g_wav_bytes += (unsigned)frames * FRAME * g_output_channels * 2;
		if ((g_wav_bytes & 0xfffff) < (unsigned)frames * FRAME * g_output_channels * 2) { long at = ftell(g_wav); fseek(g_wav, 4, SEEK_SET); { unsigned v = 36 + g_wav_bytes; fwrite(&v, 4, 1, g_wav); } fseek(g_wav, 40, SEEK_SET); fwrite(&g_wav_bytes, 4, 1, g_wav); fseek(g_wav, at, SEEK_SET); }
	}
}

/* ------------------------------------------------------------------ output thread */
typedef MMRESULT (WINAPI *wo_open_t)(LPHWAVEOUT, UINT, LPCWAVEFORMATEX, DWORD_PTR, DWORD_PTR, DWORD);
typedef MMRESULT (WINAPI *wo_hdr_t)(HWAVEOUT, LPWAVEHDR, UINT);
static wo_open_t p_waveOutOpen; static wo_hdr_t p_waveOutPrepareHeader, p_waveOutWrite;

static void wav_header(FILE *f, unsigned bytes)
{
	unsigned rate = 48000, ch = (unsigned)g_output_channels, v;
	fseek(f, 0, SEEK_SET);
	fwrite("RIFF", 1, 4, f); v = 36 + bytes; fwrite(&v, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f);
	v = 16; fwrite(&v, 4, 1, f);
	{ unsigned short t = 1, c = (unsigned short)ch, ba = (unsigned short)(ch * 2), bits = 16; unsigned br = rate * ch * 2;
	  fwrite(&t, 2, 1, f); fwrite(&c, 2, 1, f); fwrite(&rate, 4, 1, f); fwrite(&br, 4, 1, f); fwrite(&ba, 2, 1, f); fwrite(&bits, 2, 1, f); }
	fwrite("data", 1, 4, f); fwrite(&bytes, 4, 1, f);
	fseek(f, 0, SEEK_END);
}

static DWORD WINAPI audio_thread(LPVOID unused)
{
	enum { NBUF = 6 };
	static short bufs[NBUF][BLOCK_FRAMES * FRAME * 6];
	WAVEHDR hdr[NBUF];
	HWAVEOUT wo = NULL;
	HANDLE ev = CreateEventA(NULL, FALSE, FALSE, NULL);
	const char *mode = getenv("H2N_SOUND");
	int device = !(mode && !strcmp(mode, "off"));
	(void)unused;
	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
	if (device)
	{
		HMODULE winmm = LoadLibraryA("winmm.dll");
		WAVEFORMATEX fmt;
		memset(&fmt, 0, sizeof(fmt));
		fmt.wFormatTag = WAVE_FORMAT_PCM; fmt.nChannels = (WORD)g_output_channels; fmt.nSamplesPerSec = 48000; fmt.wBitsPerSample = 16;
		fmt.nBlockAlign = (WORD)(fmt.nChannels * 2); fmt.nAvgBytesPerSec = 48000 * fmt.nBlockAlign;
		if (winmm)
		{
			p_waveOutOpen = (wo_open_t)GetProcAddress(winmm, "waveOutOpen");
			p_waveOutPrepareHeader = (wo_hdr_t)GetProcAddress(winmm, "waveOutPrepareHeader");
			p_waveOutWrite = (wo_hdr_t)GetProcAddress(winmm, "waveOutWrite");
		}
		if (!p_waveOutOpen || !p_waveOutPrepareHeader || !p_waveOutWrite || p_waveOutOpen(&wo, WAVE_MAPPER, &fmt, (DWORD_PTR)ev, 0, CALLBACK_EVENT) != MMSYSERR_NOERROR)
		{
			wo = NULL;
			if (g_output_channels == 6 && p_waveOutOpen && p_waveOutPrepareHeader && p_waveOutWrite)
			{
				h2n_log("sound: the audio device takes no 6-channel output; using stereo\n");
				g_output_channels = 2; fmt.nChannels = 2; fmt.nBlockAlign = 4; fmt.nAvgBytesPerSec = 48000 * 4;
				if (p_waveOutOpen(&wo, WAVE_MAPPER, &fmt, (DWORD_PTR)ev, 0, CALLBACK_EVENT) != MMSYSERR_NOERROR) wo = NULL;
			}
		}
		if (!wo) h2n_log("sound: no audio device could be opened; the sound runs on a clock instead\n");
		else h2n_log("sound: playing to the default audio device, 48 kHz, %d channels\n", g_output_channels);
	}
	else h2n_log("sound: H2N_SOUND=off, so the sound runs on a clock and nothing is played\n");
	if (g_wav) wav_header(g_wav, 0);
	if (wo)
	{
		int k;
		memset(hdr, 0, sizeof(hdr));
		for (k = 0; k < NBUF; k++)
		{
			render(bufs[k], BLOCK_FRAMES);
			hdr[k].lpData = (LPSTR)bufs[k]; hdr[k].dwBufferLength = BLOCK_FRAMES * FRAME * g_output_channels * 2;
			p_waveOutPrepareHeader(wo, &hdr[k], sizeof(WAVEHDR));
			p_waveOutWrite(wo, &hdr[k], sizeof(WAVEHDR));
		}
		for (;;)
		{
			int done = 0;
			WaitForSingleObject(ev, 50);
			for (k = 0; k < NBUF; k++) done += (hdr[k].dwFlags & WHDR_DONE) != 0;
			if (done == NBUF) g_stat_underruns++;          /* the device ran out: the mixer was late */
			for (k = 0; k < NBUF; k++)
				if (hdr[k].dwFlags & WHDR_DONE)
				{
					render(bufs[k], BLOCK_FRAMES);
					hdr[k].dwFlags &= ~WHDR_DONE;
					p_waveOutWrite(wo, &hdr[k], sizeof(WAVEHDR));
				}
		}
	}
	else
	{
		/* no device: keep to the wall clock */
		LARGE_INTEGER start, now;
		double done = 0;
		QueryPerformanceCounter(&start);
		for (;;)
		{
			double t;
			QueryPerformanceCounter(&now);
			t = (double)(now.QuadPart - start.QuadPart) / (double)g_freq.QuadPart;
			if (t * 48000.0 - done * FRAME >= BLOCK_FRAMES * FRAME)
			{
				render(bufs[0], BLOCK_FRAMES);
				done += BLOCK_FRAMES;
			}
			else Sleep(2);
		}
	}
	return 0;
}

/* ------------------------------------------------------------------ API */
int __cdecl apu_start(void)
{
	const char *wav = getenv("H2N_SOUNDWAV"), *sur = getenv("H2N_SURROUND"), *hrtf = getenv("H2N_HRTF"), *fx = getenv("H2N_SOUNDFX");
	if (g_started) return 1;
	g_started = 1;
	QueryPerformanceFrequency(&g_freq);
	InitializeCriticalSection(&g_lock);
	InitializeCriticalSection(&g_gp_lock);
	if (hrtf && !strcmp(hrtf, "0")) g_hrtf_mode = 0;
	g_no_gp = fx && !strcmp(fx, "0");
	g_meters = getenv("H2N_SOUNDLOG") != NULL;
	if (sur && atoi(sur)) g_output_channels = 6;
	{
		const char *vol = getenv("H2N_VOLUME");
		if (vol && *vol) { g_volume = (float)atof(vol); if (g_volume < 0.0f) g_volume = 0.0f; if (g_volume > 4.0f) g_volume = 4.0f; }
	}
	g_gp = (dsp_core *)calloc(1, sizeof(dsp_core));
	if (g_gp)
	{
		g_gp->scratch_size = 0x100000;
		g_gp->scratch = (unsigned char *)calloc(1, g_gp->scratch_size);
		dsp_reset(g_gp);
	}
	if (wav && *wav && strcmp(wav, "0"))
	{
		/* H2N_SOUNDWAV=1: the mix is also written to h2classic\h2classic_sound.wav (the exe writes nowhere else) */
		CreateDirectoryW(L"h2classic", NULL);
		g_wav = h2n_real_fopen("h2classic\\h2classic_sound.wav", "wb");
		if (g_wav) h2n_log("sound: recording the mix to h2classic\\h2classic_sound.wav\n");
		else h2n_log("sound: could not create h2classic\\h2classic_sound.wav for the recording\n");
	}
	g_gp_go = CreateEventA(NULL, FALSE, FALSE, NULL);
	g_gp_done = CreateEventA(NULL, FALSE, FALSE, NULL);
	g_gp_thread = h2n_start_thread(gp_thread, NULL);      /* the layer's own threads (not the game's: not logged) */
	g_thread = h2n_start_thread(audio_thread, NULL);
	return 1;
}
void __cdecl apu_lock(void) { EnterCriticalSection(&g_lock); }
void __cdecl apu_unlock(void) { LeaveCriticalSection(&g_lock); }

int __cdecl apu_gp_load(const unsigned *code, unsigned code_words, const unsigned *state, unsigned state_words)
{
	unsigned i;
	if (!g_gp || code_words > DSP_PSIZE - 0x171 || 0x80 + state_words > 0x1400) return 0;
	/* the state block runs past 0xc00 with zeros only: the mixbuffer there is written every frame anyway */
	if (0x80 + state_words > 0xc00)
	{
		for (i = 0xc00 - 0x80; i < state_words; i++) if (state[i] & 0xffffff) return 0;
		state_words = 0xc00 - 0x80;
	}
	EnterCriticalSection(&g_gp_lock);
	dsp_reset(g_gp);
	memset(g_gp->x, 0, sizeof(g_gp->x)); memset(g_gp->y, 0, sizeof(g_gp->y)); memset(g_gp->p, 0, sizeof(g_gp->p));
	memset(g_gp->scratch, 0, g_gp->scratch_size);
	for (i = 0; i < code_words; i++) g_gp->p[0x171 + i] = code[i] & 0xffffff;
	for (i = 0; i < state_words; i++) g_gp->x[0x80 + i] = state[i] & 0xffffff;
	dsp_invalidate(g_gp);
	g_gp_loaded = 1;
	LeaveCriticalSection(&g_gp_lock);
	return 1;
}
unsigned *__cdecl apu_gp_xmem(void) { return g_gp ? g_gp->x : NULL; }
unsigned *__cdecl apu_gp_ymem(void) { return g_gp ? g_gp->y : NULL; }
unsigned char *__cdecl apu_gp_scratch(void) { return g_gp ? g_gp->scratch : NULL; }
void __cdecl apu_gp_lock(void) { EnterCriticalSection(&g_gp_lock); }
void __cdecl apu_gp_unlock(void) { LeaveCriticalSection(&g_gp_lock); }
void __cdecl apu_gp_flags_changed(void) { }
void __cdecl apu_set_mixbin_headroom(int bin, int headroom) { if (bin >= 0 && bin < 32) g_headroom[bin] = headroom < 0 ? 0 : headroom > 7 ? 7 : headroom; }
void __cdecl apu_set_speakers(int surround) { (void)surround; }

int __cdecl apu_voice_new(int kind, int format, int channels, int rate, int block_align, int input_bin, int is3d)
{
	int i, found = -1;
	EnterCriticalSection(&g_lock);
	for (i = 0; i < MAXV; i++) if (!g_v[i].used) { found = i; break; }
	if (found >= 0)
	{
		voice *v = &g_v[found];
		memset(v, 0, sizeof(*v));
		v->used = 1; v->kind = kind; v->format = format; v->channels = channels < 1 ? 1 : channels > 6 ? 6 : channels;
		v->rate = rate; v->block_align = block_align; v->input_bin = input_bin; v->is3d = is3d;
		v->output = -1; v->cache_block = -1; v->last_index = -1;
		v->pitch = rate > 0 ? (int)floor(4096.0 * log(rate / 48000.0) / log(2.0) + 0.5) : 0;
		v->eg_phase = EG_OFF; v->eg_level = 1.0; v->eg_sustain = 255;
		v->idle_frames = 1000;
	}
	LeaveCriticalSection(&g_lock);
	return found;
}
#define V(v) (&g_v[(v) & (MAXV - 1)])
void __cdecl apu_voice_free(int v)
{
	int i;
	EnterCriticalSection(&g_lock);
	for (i = 0; i < MAXV; i++) if (g_v[i].used && g_v[i].output == v) g_v[i].output = -1;
	V(v)->used = 0; V(v)->qn = 0;
	LeaveCriticalSection(&g_lock);
}
void __cdecl apu_voice_set_buffer(int v, const void *data, unsigned bytes)
{
	voice *p = V(v);
	EnterCriticalSection(&g_lock);
	p->data = (const unsigned char *)data; p->bytes = bytes; p->cache_block = -1; p->cache_src = NULL; p->last_index = -1;
	LeaveCriticalSection(&g_lock);
}
void __cdecl apu_voice_set_format(int v, int format, int channels, int rate, int block_align)
{
	voice *p = V(v);
	EnterCriticalSection(&g_lock);
	p->format = format; p->channels = channels < 1 ? 1 : channels > 6 ? 6 : channels; p->rate = rate; p->block_align = block_align;
	p->cache_block = -1; p->cache_src = NULL; p->last_index = -1;
	LeaveCriticalSection(&g_lock);
}
void __cdecl apu_voice_play(int v, int looping)
{
	voice *p = V(v);
	EnterCriticalSection(&g_lock);
	p->looping = looping;
	if (!p->active) activate(p);
	LeaveCriticalSection(&g_lock);
}
void __cdecl apu_voice_stop(int v, int envelope)
{
	voice *p = V(v);
	EnterCriticalSection(&g_lock);
	if (p->active && !(envelope && eg_release(p))) p->active = 0;
	LeaveCriticalSection(&g_lock);
}
void __cdecl apu_voice_pause(int v, int mode)
{
	voice *p = V(v);
	EnterCriticalSection(&g_lock);
	if (mode == 0) { p->paused = 0; p->synch_wait = 0; }
	else if (mode == 2) { p->synch_wait = 1; p->paused = 0; }
	else { p->paused = 1; p->synch_wait = 0; }
	LeaveCriticalSection(&g_lock);
}
void __cdecl apu_synch_playback(void)
{
	int i;
	EnterCriticalSection(&g_lock);
	for (i = 0; i < MAXV; i++) if (g_v[i].used && g_v[i].synch_wait) g_v[i].synch_wait = 0;
	LeaveCriticalSection(&g_lock);
}
void __cdecl apu_voice_set_position(int v, unsigned byte_position)
{
	voice *p = V(v);
	EnterCriticalSection(&g_lock);
	p->pos = samples_in(p, byte_position); p->last_index = -1;
	LeaveCriticalSection(&g_lock);
}
unsigned __cdecl apu_voice_get_position(int v)
{
	voice *p = V(v); unsigned r;
	EnterCriticalSection(&g_lock);
	if (p->format == APU_FMT_ADPCM) r = (unsigned)(p->pos / 64) * (unsigned)p->block_align;
	else r = (unsigned)p->pos * (unsigned)p->channels * (p->format == APU_FMT_PCM8 ? 1u : 2u);
	LeaveCriticalSection(&g_lock);
	return r;
}
unsigned __cdecl apu_voice_status(int v)
{
	voice *p = V(v); unsigned s = 0;
	EnterCriticalSection(&g_lock);
	if (p->active)
	{
		s |= APU_STATUS_ACTIVE;
		if (p->looping) s |= APU_STATUS_LOOPING;
		if (p->paused || p->synch_wait) s |= APU_STATUS_PAUSED;
		if (p->kind == APU_KIND_STREAM && (p->starved || !p->qn)) s |= APU_STATUS_STARVED;
		if (!(s & (APU_STATUS_PAUSED | APU_STATUS_STARVED))) s |= APU_STATUS_PLAYING;
	}
	if (p->eg_phase == EG_DONE) s |= APU_STATUS_ENVDONE;
	LeaveCriticalSection(&g_lock);
	return s;
}
void __cdecl apu_voice_set_volume(int v, int volume) { V(v)->volume = volume; }
void __cdecl apu_voice_set_mixbins(int v, int count, const unsigned *bins, const int *volumes)
{
	voice *p = V(v); int i;
	EnterCriticalSection(&g_lock);
	if (count > 8) count = 8;
	if (count < 0) count = 0;
	p->nslots = count;
	for (i = 0; i < count; i++) p->slot_bin[i] = (unsigned char)(bins[i] & 31);
	for (i = 0; i < 32; i++) p->bin_volume[i] = volumes[i];
	LeaveCriticalSection(&g_lock);
}
void __cdecl apu_voice_set_pitch(int v, int pitch) { V(v)->pitch = pitch; }
void __cdecl apu_voice_set_output(int v, int destination) { V(v)->output = destination; }
void __cdecl apu_voice_set_eg(int v, int eg, int mode, int delay, int attack, int hold, int decay, int release, int sustain, int pitch_scale, int cutoff)
{
	voice *p = V(v);
	(void)pitch_scale; (void)cutoff;
	if (eg != 1) return;                     /* the multi-function envelope (pitch and cutoff) is not modelled */
	EnterCriticalSection(&g_lock);
	p->eg_mode = mode; p->eg_delay = delay; p->eg_attack = attack; p->eg_hold = hold; p->eg_decay = decay;
	p->eg_release = release; p->eg_sustain = sustain;
	LeaveCriticalSection(&g_lock);
}
void __cdecl apu_voice_set_lfo(int v, int lfo, int delay, int delta, int pitch_mod, int cutoff_range, int amp_mod)
{
	voice *p = V(v);
	int k = lfo ? 1 : 0;
	EnterCriticalSection(&g_lock);
	p->lfo_delay[k] = delay; p->lfo_delta[k] = delta; p->lfo_pitch[k] = pitch_mod;
	if (!k) { p->lfo_cut = cutoff_range; p->lfo_amp = amp_mod; }
	LeaveCriticalSection(&g_lock);
}
void __cdecl apu_voice_set_filter(int v, int mode, int q, unsigned c0, unsigned c1, unsigned c2, unsigned c3)
{
	voice *p = V(v);
	EnterCriticalSection(&g_lock);
	if (mode != p->f_mode)
	{
		memset(p->s_low, 0, sizeof(p->s_low)); memset(p->s_band, 0, sizeof(p->s_band));
		memset(p->s2_low, 0, sizeof(p->s2_low)); memset(p->s2_band, 0, sizeof(p->s2_band));
		p->op_direct = p->op_room = 0;
	}
	p->f_mode = mode & 3; p->f_q = q & 7; p->f_c[0] = c0; p->f_c[1] = c1; p->f_c[2] = c2; p->f_c[3] = c3;
	LeaveCriticalSection(&g_lock);
}
static float hrir_coefficient(unsigned char b)
{
	int e = (b >> 4) & 7, m = b & 15;
	float x = (e == 0 ? (float)m : (float)((16 + m) << (e - 1))) / 2048.0f;
	return b & 0x80 ? -x : x;
}
void __cdecl apu_voice_set_3d(int v, int front, int rear, int center, int i3dl2, int other,
	const void *filter_left, const void *filter_right, int itd)
{
	voice *p = V(v); int k;
	EnterCriticalSection(&g_lock);
	p->has3d = 1;
	p->a_front = front; p->a_rear = rear; p->a_center = center; p->a_i3dl2 = i3dl2; p->a_other = other;
	if (filter_left && filter_right)
	{
		const unsigned char *l = (const unsigned char *)filter_left, *r = (const unsigned char *)filter_right;
		/* 31 coefficients, each the hardware's 8-bit minifloat: sign, 3 bits of exponent, 4 of mantissa, over 2048
		   (e = 0: m / 2048; otherwise (16 + m) * 2^(e - 1) / 2048; 0x7f is 31/32). The bytes go to the voice processor as
		   they are (dsound writes them to the HRIR registers unchanged), so this is how the hardware reads them. */
		for (k = 0; k < 31; k++)
		{
			p->hrtf_l[k] = hrir_coefficient(l[k]);
			p->hrtf_r[k] = hrir_coefficient(r[k]);
		}
		p->itd = itd;
	}
	LeaveCriticalSection(&g_lock);
}
void __cdecl apu_voice_set_3d_off(int v) { V(v)->has3d = 0; }

int __cdecl apu_stream_submit(int v, const void *data, unsigned bytes, unsigned cookie)
{
	voice *p = V(v); int ok = 0;
	EnterCriticalSection(&g_lock);
	if (p->qn < 64)
	{
		packet *k = &p->q[p->qn++];
		k->data = (const unsigned char *)data; k->bytes = bytes; k->samples = samples_in(p, bytes); k->cookie = cookie;
#ifdef H2N_ANALYSIS
		if (g_meters && p->format == APU_FMT_ADPCM && p->block_align)
		{
			/* analysis builds: what the stream packets hold (H2N_SOUNDLOG): valid Xbox ADPCM block headers have a step index
			   of 0-88 and a zero fourth byte; the header samples trace the waveform */
			static int lines;
			if (lines < 400)
			{
				const unsigned char *d = (const unsigned char *)data;
				unsigned nb = bytes / (unsigned)p->block_align, b, bad = 0;
				int lo = 32767, hi = -32768, c;
				for (b = 0; b < nb; b++)
					for (c = 0; c < p->channels; c++)
					{
						const unsigned char *h = d + b * p->block_align + c * 4;
						int smp = (short)(h[0] | (h[1] << 8));
						if (h[2] > 88 || h[3]) bad++;
						if (smp < lo) lo = smp;
						if (smp > hi) hi = smp;
					}
				lines++;
				h2n_log("sound packet: voice %d, %u bytes at %p (%u blocks of %d, %d ch, %d Hz): %u bad headers; header samples %d..%d; first %d %d %d %d\n",
					v, bytes, data, nb, p->block_align, p->channels, p->rate, bad, lo, hi,
					nb > 0 ? (short)(d[0] | (d[1] << 8)) : 0, nb > 1 ? (short)(d[p->block_align] | (d[p->block_align + 1] << 8)) : 0,
					nb > 2 ? (short)(d[2 * p->block_align] | (d[2 * p->block_align + 1] << 8)) : 0, nb > 3 ? (short)(d[3 * p->block_align] | (d[3 * p->block_align + 1] << 8)) : 0);
			}
		}
#endif
		p->discontinuity = 0;
		if (!p->active)
		{
			if (p->qn == 1) { p->q_base = 0; p->pos = 0; }
			activate(p);
		}
		else if (p->starved) p->starved = 0;
		ok = 1;
	}
	LeaveCriticalSection(&g_lock);
	return ok;
}
int __cdecl apu_stream_queued(int v) { return V(v)->qn; }
void __cdecl apu_stream_discontinuity(int v)
{
	voice *p = V(v);
	EnterCriticalSection(&g_lock);
	if (p->qn) p->discontinuity = 1;
	else { p->active = 0; p->starved = 0; }
	LeaveCriticalSection(&g_lock);
}
void __cdecl apu_stream_flush(int v, int mode)
{
	voice *p = V(v);
	EnterCriticalSection(&g_lock);
	/* FlushEx with the envelope flag on an active voice: the release, then the flush when it ends (ServiceVoiceInterrupt) */
	if (mode == 2 && p->active && eg_release(p)) p->release_flush = 1;
	else if (!(mode == 2 && !p->active && !p->qn)) stream_flush_now(p, (int)(p - g_v));
	LeaveCriticalSection(&g_lock);
}
int __cdecl apu_completions(apu_completion *out, int most)
{
	int n;
	EnterCriticalSection(&g_lock);
	n = g_done_n < most ? g_done_n : most;
	memcpy(out, g_done, (size_t)n * sizeof(apu_completion));
	memmove(g_done, g_done + n, (size_t)(g_done_n - n) * sizeof(apu_completion));
	g_done_n -= n;
	LeaveCriticalSection(&g_lock);
	return n;
}
void __cdecl apu_dump(const char *name, const void *data, unsigned bytes)
{
	FILE *f;
	if (_strnicmp(name, "h2classic\\", 9)) return;
	CreateDirectoryW(L"h2classic", NULL);
	f = h2n_real_fopen(name, "wb");
	if (f) { fwrite(data, 1, bytes, f); fclose(f); }
}
void __cdecl apu_report(void)
{
	double secs = g_frames_mixed * FRAME / 48000.0;
	if (g_wav)
	{
		long at = ftell(g_wav);
		wav_header(g_wav, g_wav_bytes); fflush(g_wav); fseek(g_wav, at, SEEK_SET);
	}
	h2n_log("sound: %.1f s mixed; voices %u now, %u at most; effects image %s, %u frames over its budget; "
		"time per second of sound: voices %.0f ms, effects %.0f ms (more than 1000 cannot keep up); "
		"device ran dry %u times; streams starved %u times; samples clipped %u\n",
		secs, g_stat_voices, g_stat_peak_voices, g_gp_loaded ? "running" : "not loaded", g_stat_gp_over,
		secs > 0 ? g_vp_seconds * 1000.0 / secs : 0.0, secs > 0 ? g_gp_seconds * 1000.0 / secs : 0.0,
		g_stat_underruns, g_stat_starved, g_stat_clipped);
}

/* H2Classic: the console's audio processor (MCPX APU) as software - the interface between xdsound.cpp (the game's
 * DirectSound calls, built with the Xbox SDK's compiler) and apu.c (the mixer, built with zig cc). Plain C types only. */
#ifndef H2N_APU_H
#define H2N_APU_H
#ifdef __cplusplus
extern "C" {
#endif

#define APU_KIND_BUFFER  0
#define APU_KIND_STREAM  1
#define APU_KIND_MIXIN   2      /* a submix destination (DSBCAPS_MIXIN): input = what its sources send to bin 31 */
#define APU_KIND_FXIN    3      /* reads a mixbin after the effects (DSBCAPS_FXIN / FXIN2) */

#define APU_FMT_PCM8   0
#define APU_FMT_PCM16  1
#define APU_FMT_ADPCM  2
#define APU_FMT_NONE   3

#define APU_STATUS_PLAYING  0x1
#define APU_STATUS_LOOPING  0x2
#define APU_STATUS_PAUSED   0x4
#define APU_STATUS_STARVED  0x8
#define APU_STATUS_ENVDONE  0x10
#define APU_STATUS_ACTIVE   0x20     /* the voice is active (may still be paused or starved) */

typedef struct apu_completion
{
	int voice;
	unsigned cookie;           /* the stream packet's index in xdsound.cpp's table */
	unsigned status;           /* 0 played, 1 flushed */
	unsigned completed;        /* bytes */
} apu_completion;

int __cdecl apu_start(void);
void __cdecl apu_lock(void);
void __cdecl apu_unlock(void);

/* effects processor */
int __cdecl apu_gp_load(const unsigned *code, unsigned code_words, const unsigned *state, unsigned state_words);
unsigned *__cdecl apu_gp_xmem(void);
unsigned *__cdecl apu_gp_ymem(void);
unsigned char *__cdecl apu_gp_scratch(void);
void __cdecl apu_gp_lock(void);         /* held while several words of an effect's state change together */
void __cdecl apu_gp_unlock(void);
void __cdecl apu_gp_flags_changed(void);
void __cdecl apu_set_mixbin_headroom(int bin, int headroom);
void __cdecl apu_set_speakers(int surround);

/* voices */
int __cdecl apu_voice_new(int kind, int format, int channels, int rate, int block_align, int input_bin, int is3d);
void __cdecl apu_voice_free(int v);
void __cdecl apu_voice_set_buffer(int v, const void *data, unsigned bytes);
void __cdecl apu_voice_set_format(int v, int format, int channels, int rate, int block_align);
void __cdecl apu_voice_play(int v, int looping);
void __cdecl apu_voice_stop(int v, int envelope);
void __cdecl apu_voice_pause(int v, int mode);           /* 0 resume, 1 pause, 2 wait for synch, 3 pause no activate */
void __cdecl apu_synch_playback(void);
void __cdecl apu_voice_set_position(int v, unsigned byte_position);
unsigned __cdecl apu_voice_get_position(int v);
unsigned __cdecl apu_voice_status(int v);
void __cdecl apu_voice_set_volume(int v, int volume);    /* hundredths of a decibel, headroom already taken off */
/* the voice's mixbin slots (count, up to 8) and the volume of each of the 32 mixbins (hundredths of a decibel) */
void __cdecl apu_voice_set_mixbins(int v, int count, const unsigned *bins, const int *volumes);
void __cdecl apu_voice_set_pitch(int v, int pitch);      /* 4096 per octave, 0 = 48 kHz */
void __cdecl apu_voice_set_output(int v, int destination);
void __cdecl apu_voice_set_eg(int v, int eg, int mode, int delay, int attack, int hold, int decay, int release, int sustain, int pitch_scale, int cutoff);
void __cdecl apu_voice_set_lfo(int v, int lfo, int delay, int delta, int pitch_mod, int cutoff_range, int amp_mod);
void __cdecl apu_voice_set_filter(int v, int mode, int q, unsigned c0, unsigned c1, unsigned c2, unsigned c3);
/* 3D results for a voice, as dsound's ConvertVolumeValues sums them (hundredths of dB, <= 0): the XTLK front bins (6, 7),
   the XTLK rear bins (8, 9), the centre (2), the I3DL2 send (10) and every other bin; and the HRTF pair (two 31-tap
   filters of sign-and-magnitude bytes from the XBE's own table) with the interaural delay in samples (> 0 delays the
   left ear). The I3DL2 one-pole coefficients arrive as filter words c2 (direct) and c3 (room) in the multi mode. */
void __cdecl apu_voice_set_3d(int v, int front, int rear, int center, int i3dl2, int other,
	const void *filter_left, const void *filter_right, int itd);
void __cdecl apu_voice_set_3d_off(int v);

/* streams */
int __cdecl apu_stream_submit(int v, const void *data, unsigned bytes, unsigned cookie);
int __cdecl apu_stream_queued(int v);
void __cdecl apu_stream_discontinuity(int v);
void __cdecl apu_stream_flush(int v, int mode);          /* 0 now, 1 async, 2 envelope (async) */
int __cdecl apu_completions(apu_completion *out, int most);

/* diagnostics */
void __cdecl apu_report(void);
void __cdecl apu_dump(const char *name, const void *data, unsigned bytes);   /* a file under h2classic\ */

#ifdef __cplusplus
}
#endif
#endif

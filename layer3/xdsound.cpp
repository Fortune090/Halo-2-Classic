/* H2Classic: the Xbox DirectSound calls the game makes, compiled with the Xbox SDK's compiler and headers.
 *
 * The console's sound chip is apu.c (voices, mixbins, and the game's own effects image on a DSP interpreter); this
 * file is dsound.lib's side of it, written from that library's code (the XDK's dsound.lib and the copy linked into the
 * game): the settings each voice keeps (volume minus headroom, mixbin slots and per-bin volumes, output buffers and
 * their submix bin, filters, envelopes), stream packets and their completion callbacks, buffer and stream status, and
 * the effects image. The 3D sums (distance, cone, front/rear, I3DL2 direct and room, the HRTF filter pair) are worked
 * out by the retail game's own copies of dsound's math functions, called at their retail addresses, as are the DSP
 * image's decoder and the I3DL2 reverb's parameters, so that nothing of Microsoft's is in this exe. */
#include <xtl.h>
#ifndef H2N_VA                           /* (s10r) one program for both game versions: the address is picked at run time */
extern "C" int h2n_v15;                 /* main.c: 0 = game version 1.0 (the disc's default.xbe), 1 = 1.5 (the title update's) */
#define H2N_VA(v10, v15) (h2n_v15 ? (v15) : (v10))
#endif
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <float.h>
#include "apu.h"
inline void *operator new(size_t, void *where) { return where; }
inline void operator delete(void *, void *) { }

extern "C" {
void __cdecl h2n_log(const char *format, ...);
unsigned __cdecl h2n_next_sequence(void);
void *__cdecl h2n_calloc(unsigned size);
void __cdecl h2n_free(void *address);
int __cdecl h2n_retail_code_loaded(void);
double __cdecl h2n_seconds(void);
}

/* s9s: the movies' sound. Retail's Bink library (its code at 0x3e0400-0x3f5300) plays a movie's sound through these same
   calls; the first calls it makes are logged (a bounded number), told apart by where they return to. */
extern "C" void *_ReturnAddress(void);
#pragma intrinsic(_ReturnAddress)
static LONG g_bink_lines;
#define BINK_CALL ((DWORD)_ReturnAddress() - H2N_VA(0x3e0400u, 0x3ea4e0u) < 0x14f00u)     /* (s10s: 1.5's Bink code is at 0x3ea4e0) */
static bool bink_take(LONG *count, LONG limit) { return InterlockedIncrement(count) <= limit && InterlockedIncrement(&g_bink_lines) <= 80; }
#define BINK_NOTE(count, limit) if (BINK_CALL && bink_take(&count, limit)) h2n_log     /* (VC7.1 has no variadic macros) */

/* ------------------------------------------------------------------ retail's own functions (dsound.lib in the XBE) */
#define RETAIL_SET_LIGHT_HRTF4  H2N_VA(0x0037e126, 0x388206)   /* CHRTFSource::SetLightHRTF4Channel: fills the table below */
#define RETAIL_HRTF_TABLE       H2N_VA(0x003871c8, 0x3912a8)   /* CHRTFSource::m_vtable */
#define RETAIL_NORM_ORIENT      H2N_VA(0x0037db87, 0x387c67)   /* CFullHRTFListener::CalcNormOrient */
#define RETAIL_I3DL2_SOURCE     H2N_VA(0x0038368b, 0x38d76b)   /* CI3DL2Source::CalculateI3DL2 */
#define RETAIL_I3DL2_LISTENER   H2N_VA(0x003838a4, 0x38d984)   /* CI3DL2Listener::CalculateI3DL2 (thiscall) */
#define RETAIL_RAW_IIR2         H2N_VA(0x0037a032, 0x384112)   /* ConvertToRawIIR2 (output pointer in esi) */
#define RETAIL_TO_24BIT         H2N_VA(0x00379fc4, 0x3840a4)   /* DoubleTo24BitDWORD */
#define RETAIL_GENERATE_KEY     H2N_VA(0x00383ef2, 0x38dfd2)   /* XAudiopUtility_GenerateKey */
#define RETAIL_DECODE           H2N_VA(0x00383f1f, 0x38dfff)   /* XAudiopUtility_Decode */

typedef void (__stdcall *fn_void)(void);
typedef void (__stdcall *fn_norm_orient)(const D3DVECTOR *front, const D3DVECTOR *top, D3DVECTOR *out);
typedef void (__stdcall *fn_norm_pos)(DWORD mode, const D3DVECTOR *listener, const D3DVECTOR *source, D3DVECTOR *pos, FLOAT *distance);
typedef void (__stdcall *fn_polar)(DWORD mode, const D3DVECTOR *front, const D3DVECTOR *top, const D3DVECTOR *orient, const D3DVECTOR *pos, FLOAT distance, FLOAT *azimuth, FLOAT *elevation);
typedef void (__stdcall *fn_cone_angle)(const D3DVECTOR *cone, const D3DVECTOR *pos, FLOAT *angle);
typedef void (__stdcall *fn_distance_volume)(FLOAT rolloff, FLOAT minimum, FLOAT maximum, const FLOAT *curve, DWORD points, FLOAT distance, BOOL mute, LONG *volume);
typedef void (__stdcall *fn_cone_volume)(DWORD inside, DWORD outside, LONG outside_volume, FLOAT angle, LONG *volume);
typedef void (__stdcall *fn_front_rear)(FLOAT distance_factor, FLOAT distance, FLOAT azimuth, FLOAT elevation, BOOL four, LONG *front, LONG *rear);
typedef void (__stdcall *fn_center)(FLOAT azimuth, FLOAT elevation, LONG *a, LONG *b);
typedef void (__stdcall *fn_filter_pair)(FLOAT azimuth, FLOAT elevation, BOOL four, const BYTE **pair);
typedef void (__stdcall *fn_i3dl2_source)(FLOAT distance, FLOAT rolloff, FLOAT hf_reference, const void *source, LONG *direct, LONG *room, LONG *direct_hf, LONG *room_hf);
typedef void (__fastcall *fn_i3dl2_listener)(void *self, void *unused, const void *listener, void *state);
typedef DWORD (__stdcall *fn_to_24bit)(double value);
typedef void (__stdcall *fn_generate_key)(BYTE *key);
typedef int (__stdcall *fn_decode)(BYTE *salt, const BYTE *in, DWORD size, BYTE *out, BOOL header);

/* the light HRTF entries of CHRTFSource's table, in its order */
struct hrtf_table
{
	fn_norm_pos norm_pos;
	fn_polar polar;
	fn_cone_angle cone_angle;
	void *relative_velocity;
	fn_distance_volume distance_volume;
	fn_cone_volume cone_volume;
	fn_front_rear front_rear;
	fn_center center;
	void *doppler;
	fn_filter_pair filter_pair;
};
static hrtf_table g_hrtf;
static bool g_retail3d;

/* ------------------------------------------------------------------ the device */
#define SOUND_MAGIC 0x736e6432
struct ds_device { DWORD magic; LONG references; };
static ds_device g_device = { SOUND_MAGIC, 1 };
static bool g_created;
static DWORD g_game_thread;
static DWORD g_speaker_config;                 /* the dashboard's: stereo unless H2N_SURROUND */
static int g_hrtf_swap;
static int g_soundlog;                       /* H2N_SOUNDLOG=n: describe the first n 3D results in the log */

/* the listener (dsound's DS3DCALCLISTENER fields) */
static D3DVECTOR g_lpos, g_lvel, g_lfront = { 0, 0, 1 }, g_ltop = { 0, 1, 0 };
static FLOAT g_ldistance = 1.0f, g_lrolloff = 1.0f, g_ldoppler = 1.0f;
static bool g_listener_dirty = true;
/* dsound's I3DL2 listener: the generic preset until the game sets one; committed on the first CommitDeferredSettings */
static DSI3DL2LISTENER g_i3dl2_listener = { DSI3DL2_ENVIRONMENT_PRESET_DEFAULT };
static bool g_i3dl2_listener_dirty = true;

/* ------------------------------------------------------------------ voices */
#define VOICE_MAGIC 0x76736468
struct ds_voice
{
	DWORD magic; LONG references;
	bool is_stream;
	DWORD caps;
	int apu;
	int channels, format, rate, block_align, frame_bytes;
	/* CDirectSoundVoiceSettings */
	LONG volume;                               /* the volume set, minus the headroom */
	DWORD headroom;
	DWORD nbins; BYTE bins[8];
	LONG bin_volume[32];
	ds_voice *output;
	DWORD submix_bin;                          /* the bin a source sends to when this is its output buffer */
	DSFILTERDESC filter;
	/* 3D (DS3DCALCBUFFER and DS3DCALCI3DL2BUFFER) */
	bool is3d, has_center;
	D3DVECTOR pos, vel, cone;
	DWORD inside, outside; LONG outside_volume;
	FLOAT min_distance, max_distance, distance_factor, rolloff, doppler;
	DWORD mode;
	struct { DWORD flags; DSI3DL2BUFFER b; } i3dl2;
	bool dirty3d;
	/* the 3D results (DS3DCALCVOICEDATA) */
	LONG front, rear, center, room, base;
	LONG direct_hf, room_hf;
	ds_voice *next;
	struct ds_stream *owner;                   /* a stream's object */
};
static ds_voice *g_voices;
static ds_voice *g_by_apu[512];
static unsigned g_buffer_count, g_stream_count;

struct ds_buffer
{
	ds_voice v;
	BYTE *data; DWORD bytes; bool owns;
};

struct ds_packet { XMEDIAPACKET p; bool used; };
struct ds_stream : IDirectSoundStream
{
	ds_voice v;
	DWORD most;
	LPFNXMEDIAOBJECTCALLBACK callback; LPVOID context;
	ds_packet packets[64]; DWORD pending;

	STDMETHOD_(ULONG, AddRef)() { return (ULONG)++v.references; }
	STDMETHOD_(ULONG, Release)();
	STDMETHOD(GetInfo)(LPXMEDIAINFO pInfo);
	STDMETHOD(GetStatus)(LPDWORD pdwStatus);
	STDMETHOD(Process)(LPCXMEDIAPACKET pInputPacket, LPCXMEDIAPACKET pOutputPacket);
	STDMETHOD(Discontinuity)();
	STDMETHOD(Flush)();
};

static ds_voice *voice_of_buffer(LPDIRECTSOUNDBUFFER b) { ds_voice *v = (ds_voice *)b; return v && v->magic == VOICE_MAGIC ? v : NULL; }
static ds_stream *stream_of(LPDIRECTSOUNDSTREAM s) { ds_stream *p = static_cast<ds_stream *>(s); return p && p->v.magic == VOICE_MAGIC ? p : NULL; }

/* ------------------------------------------------------------------ settings pushed to the processor */
static void push_mixbins(ds_voice *v)
{
	unsigned bins[8]; int volumes[32];
	for (DWORD i = 0; i < 8; i++) bins[i] = i < v->nbins ? v->bins[i] : 0;
	for (int b = 0; b < 32; b++) volumes[b] = v->bin_volume[b];
	apu_voice_set_mixbins(v->apu, (int)v->nbins, bins, volumes);
}
static void push_volume(ds_voice *v) { apu_voice_set_volume(v->apu, v->volume); }
/* CMcpxVoiceClient::SetFilter: a 3D voice in bypass takes the multi mode, and its second pair of coefficients is the
   I3DL2 direct and room filter */
static void push_filter(ds_voice *v)
{
	DWORD mode = v->filter.dwMode & 3, c2 = v->filter.adwCoefficients[2], c3 = v->filter.adwCoefficients[3];
	if (v->is3d && g_retail3d)
	{
		if (!mode) mode = DSFILTER_MODE_MULTI;
		c2 = (DWORD)v->direct_hf & 0xffff; c3 = (DWORD)v->room_hf & 0xffff;
	}
	apu_voice_set_filter(v->apu, (int)mode, (int)(v->filter.dwQCoefficient & 7), v->filter.adwCoefficients[0], v->filter.adwCoefficients[1], c2, c3);
}

/* CDirectSoundVoiceSettings::IncludeSubMixBin: the bin appears once per channel */
static void include_submix_bin(ds_voice *v, DWORD bin)
{
	DWORD have = 0, want = (DWORD)v->channels;
	for (DWORD i = 0; i < v->nbins; i++) if (v->bins[i] == bin) have++;
	if (have >= want) return;
	if (v->nbins + (want - have) > 8) v->nbins = 8 - (want - have);
	for (DWORD k = have; k < want; k++) v->bins[v->nbins++] = (BYTE)bin;
}
static bool is_submix_destination(ds_voice *v) { return v && (v->caps & (DSBCAPS_MIXIN | DSBCAPS_FXIN)) != 0; }

/* CDirectSoundVoiceSettings::SetMixBins */
static void set_mixbins(ds_voice *v, LPCDSMIXBINS mixbins)
{
	static const DSMIXBINVOLUMEPAIR mono[2] = { { 0, 0 }, { 1, 0 } }, quad[4] = { { 0, 0 }, { 1, 0 }, { 4, 0 }, { 5, 0 } },
		six[6] = { { 0, 0 }, { 1, 0 }, { 2, 0 }, { 3, 0 }, { 4, 0 }, { 5, 0 } }, three_d[5] = { { 6, 0 }, { 8, 0 }, { 7, 0 }, { 9, 0 }, { 10, 0 } };
	DSMIXBINS fallback;
	if (!mixbins)
	{
		/* the defaults: 3D voices the XTLK pairs and I3DL2, a voice with an output buffer none, others by channels */
		if (v->is3d) { fallback.dwMixBinCount = 5; fallback.lpMixBinVolumePairs = three_d; }
		else if (v->output) { fallback.dwMixBinCount = 0; fallback.lpMixBinVolumePairs = mono; }
		else if (v->channels >= 6) { fallback.dwMixBinCount = 6; fallback.lpMixBinVolumePairs = six; }
		else if (v->channels >= 4) { fallback.dwMixBinCount = 4; fallback.lpMixBinVolumePairs = quad; }
		else { fallback.dwMixBinCount = 2; fallback.lpMixBinVolumePairs = mono; }
		mixbins = &fallback;
	}
	DWORD count = mixbins->dwMixBinCount > 8 ? 8 : mixbins->dwMixBinCount;
	for (DWORD i = 0; i < count; i++)
	{
		DWORD bin = mixbins->lpMixBinVolumePairs[i].dwMixBin & 31;
		v->bins[i] = (BYTE)bin;
		v->bin_volume[bin] = mixbins->lpMixBinVolumePairs[i].lVolume;
	}
	v->nbins = count;
	if (is_submix_destination(v->output)) include_submix_bin(v, v->output->submix_bin);
	if (v->is3d) v->has_center = count >= 5 && v->bins[4] == 2;
	push_mixbins(v);
}

static void voice_init(ds_voice *v, bool stream, DWORD caps, LPCWAVEFORMATEX format, DWORD input_bin)
{
	v->magic = VOICE_MAGIC; v->references = 1; v->is_stream = stream; v->caps = caps;
	v->channels = 1; v->format = APU_FMT_NONE; v->rate = 48000; v->block_align = 2; v->frame_bytes = 2;
	if (format)
	{
		v->channels = format->nChannels ? format->nChannels : 1;
		v->rate = format->nSamplesPerSec ? (int)format->nSamplesPerSec : 48000;
		v->block_align = format->nBlockAlign ? format->nBlockAlign : 2;
		if (format->wFormatTag == WAVE_FORMAT_XBOX_ADPCM) v->format = APU_FMT_ADPCM;
		else v->format = format->wBitsPerSample == 8 ? APU_FMT_PCM8 : APU_FMT_PCM16;
		v->frame_bytes = v->format == APU_FMT_PCM8 ? v->channels : 2 * v->channels;
	}
	v->is3d = (caps & DSBCAPS_CTRL3D) != 0;
	/* CDirectSoundVoiceSettings::Initialize: 2D voices keep 6 dB of headroom, 3D voices and submix destinations none */
	v->headroom = (caps & (DSBCAPS_MIXIN | DSBCAPS_FXIN | DSBCAPS_FXIN2)) || (caps & DSBCAPS_CTRL3D) ? 0 : 600;
	v->volume = -(LONG)v->headroom;
	v->submix_bin = caps & DSBCAPS_MIXIN ? 31 : input_bin;
	/* the defaults of a 3D buffer and its I3DL2 source */
	v->cone.x = 0; v->cone.y = 0; v->cone.z = 1;
	v->inside = 360; v->outside = 360; v->outside_volume = 0;
	v->min_distance = 1.0f; v->max_distance = 1000000000.0f; v->mode = DS3DMODE_NORMAL;
	v->distance_factor = 1.0f; v->rolloff = 1.0f; v->doppler = 1.0f;
	v->i3dl2.b.Occlusion.flLFRatio = 0.25f;
	v->dirty3d = true;
	int kind = stream ? APU_KIND_STREAM : caps & DSBCAPS_MIXIN ? APU_KIND_MIXIN : caps & (DSBCAPS_FXIN | DSBCAPS_FXIN2) ? APU_KIND_FXIN : APU_KIND_BUFFER;
	v->apu = apu_voice_new(kind, v->format, v->channels, v->rate, v->block_align, (int)input_bin, v->is3d);
	if (v->apu >= 0) g_by_apu[v->apu & 511] = v;
	push_volume(v);
	v->next = g_voices; g_voices = v;
}
static void voice_unlink(ds_voice *v)
{
	for (ds_voice **p = &g_voices; *p; p = &(*p)->next) if (*p == v) { *p = v->next; break; }
	for (ds_voice *o = g_voices; o; o = o->next) if (o->output == v) { o->output = NULL; apu_voice_set_output(o->apu, -1); }
	if (v->apu >= 0) { g_by_apu[v->apu & 511] = NULL; apu_voice_free(v->apu); }
	v->magic = 0;
}
/* ActivateVoice: a source that starts also starts the effect return it feeds */
static void activate_output(ds_voice *v)
{
	if (v->output && (v->output->caps & DSBCAPS_FXIN) && !(apu_voice_status(v->output->apu) & APU_STATUS_ACTIVE)) apu_voice_play(v->output->apu, 1);
}

/* ------------------------------------------------------------------ 3D */
static LONG clamp_volume(LONG v) { return v < -10000 ? -10000 : v > 0 ? 0 : v; }

/* CDirectSound3DCalculator::Calculate3D and GetVoiceData, then CMcpxVoiceClient::ConvertVolumeValues' sums and
   LoadHRTFFilter's pair, for one voice */
static void compute_3d(ds_voice *v)
{
	if (!v->is3d || !g_retail3d || v->apu < 0) return;
	D3DVECTOR orient, pos = { 0, 0, 0 };
	FLOAT distance = 0, azimuth = 0, elevation = 0, cone_angle = 0;
	LONG distance_volume = 0, cone_volume = 0, front = 0, rear = 0, center_a = 0, center_b = 0, direct = 0, room = 0, direct_hf = 0, room_hf = 0;
	BOOL four = (g_speaker_config & 0xffff) == DSSPEAKER_SURROUND || (g_speaker_config & DSSPEAKER_ENABLE_AC3);
	const BYTE *pair[2] = { NULL, NULL };
	((fn_norm_orient)RETAIL_NORM_ORIENT)(&g_lfront, &g_ltop, &orient);
	if (v->mode != DS3DMODE_DISABLE)
	{
		g_hrtf.norm_pos(v->mode, &g_lpos, &v->pos, &pos, &distance);
		g_hrtf.polar(v->mode, &g_lfront, &g_ltop, &orient, &pos, distance, &azimuth, &elevation);
		g_hrtf.cone_angle(&v->cone, &pos, &cone_angle);
		g_hrtf.distance_volume(g_lrolloff * v->rolloff, v->min_distance, v->max_distance, NULL, 0, distance,
			(v->caps & DSBCAPS_MUTE3DATMAXDISTANCE) != 0, &distance_volume);
		g_hrtf.cone_volume(v->inside, v->outside, v->outside_volume, cone_angle, &cone_volume);
		/* the front/rear split only with four speakers (the voice's flag bit 0) */
		g_hrtf.front_rear(g_ldistance * v->distance_factor, distance, azimuth, elevation, four && !(g_speaker_config & 0x80000000), &front, &rear);
		if (v->has_center) g_hrtf.center(azimuth, elevation, &center_a, &center_b);
		((fn_i3dl2_source)RETAIL_I3DL2_SOURCE)(distance, g_i3dl2_listener.flRoomRolloffFactor, g_i3dl2_listener.flHFReference, &v->i3dl2,
			&direct, &room, &direct_hf, &room_hf);
		if (v->has_center) room_hf = direct_hf;
		g_hrtf.filter_pair(azimuth, elevation, four, pair);
	}
	LONG base = clamp_volume(distance_volume + cone_volume);
	v->base = base;
	v->front = clamp_volume(direct + center_a + front + base);
	v->rear = clamp_volume(direct + rear + base);
	v->center = clamp_volume(direct + center_b + front + base);
	v->room = clamp_volume(room + base);
	v->direct_hf = direct_hf; v->room_hf = room_hf;
	if (pair[0] && pair[1])
	{
		/* as dsound loads the voice's HRIR registers: each tap's first byte from pair[0] (the left ear: the voice's first
		   two mixbins, 6 and 8), its second from pair[1] (the right ear: 7 and 9); the delay is pair[0]'s last byte, or
		   minus pair[1]'s when the azimuth is negative, and a positive delay is the left ear's. The table holds each
		   direction as (far ear, near ear) with the delay in the far ear's filter; the XBE's filter_pair hands them over
		   as (left, right): far first for a source on the right (azimuth >= 0, the listener's right being top x front).
		   H2N_HRTF=swap exchanges the ears and the delay, for comparison. */
		int itd = azimuth >= 0.0f ? pair[0][31] : -(int)pair[1][31];
		if (!g_hrtf_swap) apu_voice_set_3d(v->apu, v->front, v->rear, v->center, v->room, base, pair[0], pair[1], itd);
		else apu_voice_set_3d(v->apu, v->front, v->rear, v->center, v->room, base, pair[1], pair[0], -itd);
	}
	else apu_voice_set_3d(v->apu, v->front, v->rear, v->center, v->room, base, NULL, NULL, 0);
	push_filter(v);
	v->dirty3d = false;
	if (g_soundlog > 0)
	{
		g_soundlog--;
		h2n_log("sound 3D: voice %d at %.1f (azimuth %.2f, elevation %.2f): front %ld, rear %ld, center %ld, room %ld (distance and cone %ld); "
			"high frequencies direct %ld, room %ld; ear filters %s, delay %d\n", v->apu, distance, azimuth, elevation,
			v->front, v->rear, v->center, v->room, base, direct_hf, room_hf, pair[0] && pair[1] ? "yes" : "no",
			pair[0] && pair[1] ? (azimuth >= 0.0f ? pair[0][31] : -(int)pair[1][31]) : 0);
	}
}
static void commit_3d(bool all)
{
	for (ds_voice *v = g_voices; v; v = v->next) if (v->is3d && (all || v->dirty3d)) compute_3d(v);
	g_listener_dirty = false;
}
static void apply_3d(ds_voice *v, DWORD apply)
{
	v->dirty3d = true;
	if (apply == DS3D_IMMEDIATE) compute_3d(v);
}

/* ------------------------------------------------------------------ the effects image */
struct effect_map_raw { DWORD code, code_size, state, state_size, y, y_size, scratch, scratch_size; };
static struct { DSEFFECTIMAGEDESC d; DSEFFECTMAP more[31]; } g_image_desc;
static DWORD g_effect_count, g_i3dl2_index = 0xffffffff, g_crosstalk_index = 0xffffffff;
static bool g_image_loaded;

static DWORD *effect_state(DWORD effect, DWORD offset, DWORD size)
{
	if (!g_image_loaded || effect >= g_effect_count) return NULL;
	const DSEFFECTMAP *m = &g_image_desc.d.aEffectMaps[effect];
	if (offset + size > m->dwStateSize * 4) return NULL;
	return (DWORD *)m->lpvStateSegment + offset / 4;
}
static HRESULT get_effect_data(DWORD effect, DWORD offset, void *data, DWORD size)
{
	DWORD *s = effect_state(effect, offset, size);
	if (!s) { if (data) memset(data, 0, size); return DSERR_INVALIDCALL; }
	memcpy(data, s, size);
	return DS_OK;
}
static HRESULT set_effect_data(DWORD effect, DWORD offset, const void *data, DWORD size)
{
	DWORD *s = effect_state(effect, offset, size);
	if (!s) return DSERR_INVALIDCALL;
	const DWORD *from = (const DWORD *)data;
	for (DWORD i = 0; i < size / 4; i++) s[i] = from[i] & 0xffffff;     /* the GP's words are 24 bits */
	return DS_OK;
}

/* the I3DL2 reverb's parameters from a listener description, as XAudioSetEffectData and CommitI3DL2Listener do it */
static HRESULT set_i3dl2_reverb(DWORD effect, const DSI3DL2LISTENER *listener)
{
	struct { DWORD flags; DSI3DL2LISTENER l; } calc;
	DWORD state[0x220 / 4], self[16];
	if (!g_retail3d) return DSERR_INVALIDCALL;
	memset(state, 0, sizeof(state)); memset(self, 0, sizeof(self));
	if (FAILED(get_effect_data(effect, 0, state, 0x118))) return DSERR_INVALIDCALL;
	calc.flags = 0xfff; calc.l = *listener;
	((fn_i3dl2_listener)RETAIL_I3DL2_LISTENER)(self, NULL, &calc, state);
	state[0x10 / 4] |= 4;
	apu_gp_lock();
	set_effect_data(effect, 0x118, &state[0x118 / 4], 0x108);
	set_effect_data(effect, 0x10, &state[0x10 / 4], 4);
	apu_gp_unlock();
	return DS_OK;
}
static void raw_iir2(FLOAT frequency, FLOAT q, FLOAT gain, DWORD *words)
{
	DWORD fn = RETAIL_RAW_IIR2;
	__asm
	{
		push esi
		mov esi, words
		push gain
		push q
		push frequency
		call fn
		pop esi
	}
}

static bool load_image(const BYTE *image, DWORD image_size)
{
	if (image_size < 0x818 + 0x28) return false;
	const DWORD *h = (const DWORD *)(image + 0x800);
	DWORD code_words = h[1], state_offset = h[2], state_words = h[3];
	DWORD table = 0x818 + (code_words + state_words) * 4;
	if (table + 8 > image_size || state_offset + state_words * 4 > image_size) return false;
	DWORD count = *(const DWORD *)(image + table), scratch_total = *(const DWORD *)(image + table + 4);
	if (count == 0 || count > 32 || table + 8 + count * 32 + count * 8 > image_size) return false;
	const effect_map_raw *maps = (const effect_map_raw *)(image + table + 8);
	const BYTE *salts_in = image + table + 8 + count * 32;
	BYTE *code = (BYTE *)h2n_calloc(code_words * 4), salts[32 * 8], key[8];
	if (!code) return false;
	/* the code block as it is (the return at its end is not enciphered), then each effect deciphered over it */
	memcpy(code, image + 0x818, code_words * 4);
	/* each effect's code is enciphered with a salt of its own; the salts with the key itself (dsound's
	   ParseEffectImageInfo, done by the XBE's own XAudiopUtility functions) */
	((fn_generate_key)RETAIL_GENERATE_KEY)(key);
	((fn_decode)RETAIL_DECODE)(key, salts_in, count * 8, salts, FALSE);
	for (DWORD i = 0; i < count; i++)
	{
		DWORD at = maps[i].code - 0x818;
		if (maps[i].code < 0x818 || at + maps[i].code_size > code_words * 4) { h2n_free(code); return false; }
		((fn_decode)RETAIL_DECODE)(salts + i * 8, image + maps[i].code, maps[i].code_size, code + at, FALSE);
	}
#ifdef H2N_ANALYSIS
	if (getenv("H2N_DSPDUMP"))
	{
		/* analysis builds only (never sent: nothing of the XBE is written to disk by a build the user gets):
		   H2N_DSPDUMP=1 writes the deciphered code, for comparing (h2classic\\dsp_code.bin) */
		apu_dump("h2classic\\dsp_code.bin", code, code_words * 4);
	}
#endif
	if (!apu_gp_load((const unsigned *)code, code_words, (const unsigned *)(image + state_offset), state_words)) { h2n_free(code); return false; }
	h2n_free(code);
	unsigned *x = apu_gp_xmem(), *y = apu_gp_ymem();
	unsigned char *scratch = apu_gp_scratch();
	memset(&g_image_desc, 0, sizeof(g_image_desc));
	g_image_desc.d.dwEffectCount = count;
	g_image_desc.d.dwTotalScratchSize = scratch_total;
	for (DWORD i = 0; i < count; i++)
	{
		DSEFFECTMAP *m = &g_image_desc.d.aEffectMaps[i];
		m->lpvCodeSegment = NULL;
		m->dwCodeSize = maps[i].code_size / 4;
		m->lpvStateSegment = x + 0x80 + (maps[i].state - state_offset) / 4;
		m->dwStateSize = maps[i].state_size / 4;
		m->lpvYMemorySegment = y + maps[i].y / 4;
		m->dwYMemorySize = maps[i].y_size / 4;
		m->lpvScratchSegment = scratch + maps[i].scratch;
		m->dwScratchSize = maps[i].scratch_size / 4;
	}
	g_effect_count = count;
	g_image_loaded = true;
	return true;
}

/* ------------------------------------------------------------------ stream packet completions */
/* What the console's interrupt routine does for a finished packet (CMcpxStream::CompletePackets, XMOCompletePacket):
   the packet's slot is free again first, then its status and size are stored and the callback runs. Here that happens
   on the game's thread, at the start of the stream's GetStatus and in DirectSoundDoWork (and at once for a Flush). */
static int g_delivering;
static void complete_packet(ds_stream *s, DWORD cookie, DWORD status, DWORD completed)
{
	if (cookie >= 64 || !s->packets[cookie].used) return;
	XMEDIAPACKET p = s->packets[cookie].p;
	s->packets[cookie].used = false;
	if (s->pending) s->pending--;
	DWORD code = status ? (DWORD)XMEDIAPACKET_STATUS_FLUSHED : (DWORD)XMEDIAPACKET_STATUS_SUCCESS;
	if (p.pdwCompletedSize) *p.pdwCompletedSize = status ? 0 : completed;
	if (p.pdwStatus) *p.pdwStatus = code;
	if (s->callback) s->callback(s->context, p.pContext, code);
	else if (p.hCompletionEvent) SetEvent(p.hCompletionEvent);
}
static void deliver(bool force)
{
	apu_completion done[64];
	int n;
	if (!force && (g_delivering || GetCurrentThreadId() != g_game_thread)) return;
	g_delivering++;
	while ((n = apu_completions(done, 64)) > 0)
		for (int i = 0; i < n; i++)
		{
			ds_voice *v = g_by_apu[done[i].voice & 511];
			if (!v || !v->is_stream || !v->owner) continue;
			complete_packet(v->owner, done[i].cookie, done[i].status, done[i].completed);
		}
	g_delivering--;
}

/* ------------------------------------------------------------------ DirectSound */
static void start_device(void)
{
	const char *sur = getenv("H2N_SURROUND"), *hrtf = getenv("H2N_HRTF");
	g_created = true;
	g_game_thread = GetCurrentThreadId();
	g_speaker_config = sur && atoi(sur) ? (DSSPEAKER_SURROUND | DSSPEAKER_ENABLE_AC3) : DSSPEAKER_STEREO;
	g_hrtf_swap = hrtf && !strcmp(hrtf, "swap");
	{
		const char *log3d = getenv("H2N_SOUNDLOG");
		g_soundlog = log3d && *log3d ? (atoi(log3d) > 1 ? atoi(log3d) : 200) : 0;
	}
	apu_start();
	/* the game asks for the light 4-channel HRTF (DirectSoundUseLightHRTF4Channel): the XBE's own table of functions */
	if (h2n_retail_code_loaded())
	{
		((fn_void)RETAIL_SET_LIGHT_HRTF4)();
		memcpy(&g_hrtf, (const void *)RETAIL_HRTF_TABLE, sizeof(g_hrtf));
		g_retail3d = g_hrtf.norm_pos && g_hrtf.polar && g_hrtf.cone_angle && g_hrtf.distance_volume && g_hrtf.cone_volume &&
			g_hrtf.front_rear && g_hrtf.center && g_hrtf.filter_pair;
	}
	h2n_log("[%4u] DirectSoundCreate: the console's sound processor in software, %s speakers%s\n", h2n_next_sequence(),
		g_speaker_config ? "5.1" : "stereo", g_retail3d ? "" : "; no 3D (retail code is not loaded)");
}

STDAPI DirectSoundCreate(LPGUID pguidDeviceId, LPDIRECTSOUND *ppDirectSound, LPUNKNOWN pUnkOuter)
{
	if (!g_created) start_device();
	g_device.references++;
	*ppDirectSound = (LPDIRECTSOUND)&g_device;
	return DS_OK;
}
STDAPI_(ULONG) IDirectSound_Release(LPDIRECTSOUND pDirectSound) { if (g_device.references > 1) g_device.references--; return (ULONG)g_device.references; }
STDAPI IDirectSound_GetCaps(LPDIRECTSOUND pDirectSound, LPDSCAPS pdsc)
{
	pdsc->dwFree2DBuffers = 192 - (g_buffer_count < 128 ? g_buffer_count : 128); pdsc->dwFree3DBuffers = 64; pdsc->dwFreeBufferSGEs = 2047; pdsc->dwMemoryAllocated = 0;
	return DS_OK;
}
STDAPI IDirectSound_GetSpeakerConfig(LPDIRECTSOUND pDirectSound, LPDWORD pdwSpeakerConfig) { *pdwSpeakerConfig = g_speaker_config; return DS_OK; }
STDAPI IDirectSound_GetEffectData(LPDIRECTSOUND pDirectSound, DWORD dwEffectIndex, DWORD dwOffset, LPVOID pvData, DWORD dwDataSize)
{
	return get_effect_data(dwEffectIndex, dwOffset, pvData, dwDataSize);
}
STDAPI IDirectSound_SetEffectData(LPDIRECTSOUND pDirectSound, DWORD dwEffectIndex, DWORD dwOffset, LPCVOID pvData, DWORD dwDataSize, DWORD dwApply)
{
	return set_effect_data(dwEffectIndex, dwOffset, pvData, dwDataSize);
}
STDAPI IDirectSound_SetMixBinHeadroom(LPDIRECTSOUND pDirectSound, DWORD dwMixBin, DWORD dwHeadroom)
{
	apu_set_mixbin_headroom((int)dwMixBin, (int)dwHeadroom);
	return DS_OK;
}
STDAPI IDirectSound_SynchPlayback(LPDIRECTSOUND pDirectSound) { apu_synch_playback(); return DS_OK; }
STDAPI IDirectSound_CommitDeferredSettings(LPDIRECTSOUND pDirectSound)
{
	deliver(false);
	commit_3d(g_listener_dirty);
	/* CMcpxAPU::CommitI3DL2Listener: dsound's own I3DL2 listener goes to the I3DL2 reverb while it is marked changed,
	   which it is from the start */
	if (g_i3dl2_listener_dirty && g_image_loaded && g_i3dl2_index < g_effect_count) set_i3dl2_reverb(g_i3dl2_index, &g_i3dl2_listener);
	g_i3dl2_listener_dirty = false;
	return DS_OK;
}
static void listener_changed(DWORD apply) { g_listener_dirty = true; if (apply == DS3D_IMMEDIATE) commit_3d(true); }
STDAPI IDirectSound_SetPosition(LPDIRECTSOUND pDirectSound, FLOAT x, FLOAT y, FLOAT z, DWORD dwApply)
{
	g_lpos.x = x; g_lpos.y = y; g_lpos.z = z; listener_changed(dwApply); return DS_OK;
}
STDAPI IDirectSound_SetOrientation(LPDIRECTSOUND pDirectSound, FLOAT xFront, FLOAT yFront, FLOAT zFront, FLOAT xTop, FLOAT yTop, FLOAT zTop, DWORD dwApply)
{
	g_lfront.x = xFront; g_lfront.y = yFront; g_lfront.z = zFront; g_ltop.x = xTop; g_ltop.y = yTop; g_ltop.z = zTop;
	listener_changed(dwApply); return DS_OK;
}
STDAPI IDirectSound_SetDistanceFactor(LPDIRECTSOUND pDirectSound, FLOAT flDistanceFactor, DWORD dwApply) { g_ldistance = flDistanceFactor; listener_changed(dwApply); return DS_OK; }
STDAPI IDirectSound_SetDopplerFactor(LPDIRECTSOUND pDirectSound, FLOAT flDopplerFactor, DWORD dwApply) { g_ldoppler = flDopplerFactor; listener_changed(dwApply); return DS_OK; }
STDAPI IDirectSound_SetRolloffFactor(LPDIRECTSOUND pDirectSound, FLOAT flRolloffFactor, DWORD dwApply) { g_lrolloff = flRolloffFactor; listener_changed(dwApply); return DS_OK; }

STDAPI_(void) DirectSoundDoWork(void) { deliver(false); }

/* ------------------------------------------------------------------ buffers */
static HRESULT create_buffer(LPCDSBUFFERDESC description, LPDIRECTSOUNDBUFFER *out)
{
	if (!g_created) start_device();
	ds_buffer *b = (ds_buffer *)h2n_calloc(sizeof(ds_buffer));
	DWORD caps = description ? description->dwFlags : 0;
	voice_init(&b->v, false, caps, description ? description->lpwfxFormat : NULL, description ? description->dwInputMixBin & 31 : 0);
	set_mixbins(&b->v, description ? description->lpMixBins : NULL);
	if (description && description->dwBufferBytes)
	{
		b->bytes = description->dwBufferBytes; b->data = (BYTE *)h2n_calloc(b->bytes); b->owns = true;
		apu_voice_set_buffer(b->v.apu, b->data, b->bytes);
	}
	push_filter(&b->v);
	g_buffer_count++;
	*out = (LPDIRECTSOUNDBUFFER)b;
	return DS_OK;
}
STDAPI IDirectSound_CreateSoundBuffer(LPDIRECTSOUND pDirectSound, LPCDSBUFFERDESC pdsbd, LPDIRECTSOUNDBUFFER *ppBuffer, LPUNKNOWN pUnkOuter)
{
	HRESULT hr = create_buffer(pdsbd, ppBuffer);
	{ static LONG c_; BINK_NOTE(c_, 4)("[%4u] movie sound: buffer %p made: flags %08lx, %lu bytes, format %u, %u channels, %lu Hz, %u bits\n", h2n_next_sequence(), *ppBuffer,
		pdsbd ? pdsbd->dwFlags : 0, pdsbd ? pdsbd->dwBufferBytes : 0, pdsbd && pdsbd->lpwfxFormat ? pdsbd->lpwfxFormat->wFormatTag : 0,
		pdsbd && pdsbd->lpwfxFormat ? pdsbd->lpwfxFormat->nChannels : 0, pdsbd && pdsbd->lpwfxFormat ? pdsbd->lpwfxFormat->nSamplesPerSec : 0,
		pdsbd && pdsbd->lpwfxFormat ? pdsbd->lpwfxFormat->wBitsPerSample : 0); }
	return hr;
}
STDAPI DirectSoundCreateBuffer(LPCDSBUFFERDESC pdsbd, LPDIRECTSOUNDBUFFER *ppBuffer) { return create_buffer(pdsbd, ppBuffer); }

STDAPI_(ULONG) IDirectSoundBuffer_Release(LPDIRECTSOUNDBUFFER pBuffer)
{
	ds_buffer *b = (ds_buffer *)pBuffer;
	if (!voice_of_buffer(pBuffer)) return 0;
	LONG left = --b->v.references;
	if (!left)
	{
		apu_voice_stop(b->v.apu, 0);
		voice_unlink(&b->v);
		if (b->owns) h2n_free(b->data);
		h2n_free(b);
		if (g_buffer_count) g_buffer_count--;
	}
	return (ULONG)left;
}
STDAPI IDirectSoundBuffer_SetBufferData(LPDIRECTSOUNDBUFFER pBuffer, LPVOID pvBufferData, DWORD dwBufferBytes)
{
	ds_buffer *b = (ds_buffer *)pBuffer;
	if (!voice_of_buffer(pBuffer)) return DSERR_INVALIDCALL;
	{ static LONG c_; BINK_NOTE(c_, 4)("[%4u] movie sound: buffer %p takes the data at %p, %lu bytes\n", h2n_next_sequence(), pBuffer, pvBufferData, dwBufferBytes); }
	if (b->owns) { h2n_free(b->data); b->owns = false; }
	b->data = (BYTE *)pvBufferData; b->bytes = dwBufferBytes;
	apu_voice_set_buffer(b->v.apu, b->data, b->bytes);
	return DS_OK;
}
STDAPI IDirectSoundBuffer_Lock(LPDIRECTSOUNDBUFFER pBuffer, DWORD dwOffset, DWORD dwBytes, LPVOID *ppvAudioPtr1, LPDWORD pdwAudioBytes1,
	LPVOID *ppvAudioPtr2, LPDWORD pdwAudioBytes2, DWORD dwFlags)
{
	ds_buffer *b = (ds_buffer *)pBuffer;
	DWORD offset = dwFlags & DSBLOCK_FROMWRITECURSOR ? 0 : dwOffset, bytes = dwFlags & DSBLOCK_ENTIREBUFFER ? b->bytes : dwBytes;
	if (offset > b->bytes) offset = b->bytes;
	if (bytes > b->bytes) bytes = b->bytes;
	DWORD first = offset + bytes > b->bytes ? b->bytes - offset : bytes;
	{ static LONG c_; BINK_NOTE(c_, 6)("       movie sound: buffer %p locked at %lu for %lu bytes (flags %lx) at %.2f s\n", pBuffer, dwOffset, dwBytes, dwFlags, h2n_seconds()); }
	if (ppvAudioPtr1) *ppvAudioPtr1 = b->data ? b->data + offset : NULL;
	if (pdwAudioBytes1) *pdwAudioBytes1 = first;
	if (ppvAudioPtr2) *ppvAudioPtr2 = bytes > first ? b->data : NULL;
	if (pdwAudioBytes2) *pdwAudioBytes2 = bytes - first;
	return DS_OK;
}
STDAPI IDirectSoundBuffer_Play(LPDIRECTSOUNDBUFFER pBuffer, DWORD dwReserved1, DWORD dwReserved2, DWORD dwFlags)
{
	ds_buffer *b = (ds_buffer *)pBuffer;
	if (!voice_of_buffer(pBuffer)) return DSERR_INVALIDCALL;
	{ static LONG c_; BINK_NOTE(c_, 6)("[%4u] movie sound: buffer %p plays (flags %lx) at %.2f s\n", h2n_next_sequence(), pBuffer, dwFlags, h2n_seconds()); }
	/* submix destinations and effect returns always loop */
	if (b->v.caps & (DSBCAPS_MIXIN | DSBCAPS_FXIN | DSBCAPS_FXIN2)) dwFlags = DSBPLAY_LOOPING;
	if (dwFlags & DSBPLAY_FROMSTART) apu_voice_set_position(b->v.apu, 0);
	if (dwFlags & DSBPLAY_SYNCHPLAYBACK) apu_voice_pause(b->v.apu, 2);
	apu_voice_play(b->v.apu, (dwFlags & DSBPLAY_LOOPING) != 0);
	activate_output(&b->v);
	return DS_OK;
}
STDAPI IDirectSoundBuffer_StopEx(LPDIRECTSOUNDBUFFER pBuffer, REFERENCE_TIME rtTimeStamp, DWORD dwFlags)
{
	ds_buffer *b = (ds_buffer *)pBuffer;
	if (!voice_of_buffer(pBuffer)) return DSERR_INVALIDCALL;
	apu_voice_stop(b->v.apu, (dwFlags & DSBSTOPEX_ENVELOPE) != 0);
	return DS_OK;
}
STDAPI IDirectSoundBuffer_Pause(LPDIRECTSOUNDBUFFER pBuffer, DWORD dwPause)
{
	ds_buffer *b = (ds_buffer *)pBuffer;
	if (!voice_of_buffer(pBuffer)) return DSERR_INVALIDCALL;
	/* CMcpxBuffer::Pause acts on an active voice only */
	if (apu_voice_status(b->v.apu) & APU_STATUS_ACTIVE) apu_voice_pause(b->v.apu, dwPause == DSBPAUSE_PAUSE ? 1 : dwPause == DSBPAUSE_SYNCHPLAYBACK ? 2 : 0);
	return DS_OK;
}
STDAPI IDirectSoundBuffer_GetStatus(LPDIRECTSOUNDBUFFER pBuffer, LPDWORD pdwStatus)
{
	ds_buffer *b = (ds_buffer *)pBuffer;
	if (!voice_of_buffer(pBuffer)) { *pdwStatus = 0; return DSERR_INVALIDCALL; }
	unsigned s = apu_voice_status(b->v.apu);
	DWORD out = 0;
	if (s & APU_STATUS_ACTIVE)
	{
		out = s & APU_STATUS_PAUSED ? DSBSTATUS_PAUSED : DSBSTATUS_PLAYING;
		if (s & APU_STATUS_LOOPING) out |= DSBSTATUS_LOOPING;
	}
	*pdwStatus = out;
	{ static LONG c_; BINK_NOTE(c_, 4)("       movie sound: buffer %p status %lx\n", pBuffer, out); }
	return DS_OK;
}
STDAPI IDirectSoundBuffer_SetCurrentPosition(LPDIRECTSOUNDBUFFER pBuffer, DWORD dwPlayCursor)
{
	ds_buffer *b = (ds_buffer *)pBuffer;
	if (!voice_of_buffer(pBuffer)) return DSERR_INVALIDCALL;
	apu_voice_set_position(b->v.apu, dwPlayCursor);
	return DS_OK;
}
/* s9s: three buffer calls the game's own code never makes, but the Bink library's DirectSound output does (movies), each
   as the console's dsound does it (read off the XBE's own copy) */
STDAPI IDirectSoundBuffer_Stop(LPDIRECTSOUNDBUFFER pBuffer)
{
	/* 0x37b703 -> CMcpxBuffer::Stop(0): at once, no release envelope (StopEx's DSBSTOPEX_IMMEDIATE) */
	ds_buffer *b = (ds_buffer *)pBuffer;
	{ static LONG c_; BINK_NOTE(c_, 4)("[%4u] movie sound: buffer %p stopped at %.2f s\n", h2n_next_sequence(), pBuffer, h2n_seconds()); }
	if (!voice_of_buffer(pBuffer)) return DSERR_INVALIDCALL;
	apu_voice_stop(b->v.apu, 0);
	return DS_OK;
}
STDAPI IDirectSoundBuffer_GetCurrentPosition(LPDIRECTSOUNDBUFFER pBuffer, LPDWORD pdwCurrentPlayCursor, LPDWORD pdwCurrentWriteCursor)
{
	/* 0x37b777 -> CMcpxBuffer::GetCurrentPosition (0x382918): while the voice plays, the play cursor is where it is and the
	   write cursor 32 samples ahead of it (0x37f43e: in the buffer's own bytes - nBlockAlign a sample, or a block per 64
	   samples of ADPCM), wrapped at the buffer's end; a voice that is not playing reports the place it stopped for both.
	   (The console also takes a larger lead from its buffer settings at +0x14 when one is set; none is, for a buffer made
	   with IDirectSound_CreateSoundBuffer.) */
	ds_buffer *b = (ds_buffer *)pBuffer;
	if (!voice_of_buffer(pBuffer))
	{
		if (pdwCurrentPlayCursor) *pdwCurrentPlayCursor = 0;
		if (pdwCurrentWriteCursor) *pdwCurrentWriteCursor = 0;
		return DSERR_INVALIDCALL;
	}
	DWORD play = apu_voice_get_position(b->v.apu), write = play;
	if (b->bytes && play >= b->bytes) play %= b->bytes;
	write = play;
	if (apu_voice_status(b->v.apu) & APU_STATUS_ACTIVE)
	{
		DWORD lead = b->v.format == APU_FMT_ADPCM ? (32u >> 6) * (DWORD)b->v.block_align : 32u * (DWORD)b->v.block_align;
		write = play + lead;
		if (b->bytes) write %= b->bytes;
	}
	if (pdwCurrentPlayCursor) *pdwCurrentPlayCursor = play;
	if (pdwCurrentWriteCursor) *pdwCurrentWriteCursor = write;
	{ static LONG c_; BINK_NOTE(c_, 8)("       movie sound: buffer %p is at %lu (write %lu) at %.2f s%s\n", pBuffer, play, write, h2n_seconds(), apu_voice_status(b->v.apu) & APU_STATUS_ACTIVE ? "" : ", not playing"); }
	return DS_OK;
}
STDAPI IDirectSoundBuffer_SetFrequency(LPDIRECTSOUNDBUFFER pBuffer, DWORD dwFrequency)
{
	/* 0x37c5c8 -> CDirectSoundVoice::SetFrequency: the pitch for that rate (4096 a octave, 0 at 48 kHz - the same rounding
	   the voice was started with); 0 (DSBFREQUENCY_ORIGINAL) goes back to the rate of the buffer's own format */
	ds_buffer *b = (ds_buffer *)pBuffer;
	if (!voice_of_buffer(pBuffer)) return DSERR_INVALIDCALL;
	double rate = dwFrequency ? (double)dwFrequency : (double)b->v.rate;
	int pitch = rate > 0 ? (int)floor(4096.0 * log(rate / 48000.0) / log(2.0) + 0.5) : 0;
	apu_voice_set_pitch(b->v.apu, pitch < DSBPITCH_MIN ? DSBPITCH_MIN : pitch > DSBPITCH_MAX ? DSBPITCH_MAX : pitch);
	static LONG told;
	if (InterlockedIncrement(&told) <= 6)       /* only the movies' sound (Bink) sets a frequency: a line for its first buffers */
		h2n_log("[%4u] sound: a buffer's frequency set to %lu Hz (pitch %d): the movie's sound is starting\n", h2n_next_sequence(), dwFrequency, pitch);
	return DS_OK;
}

/* the settings both buffers and streams have */
static HRESULT voice_set_volume(ds_voice *v, LONG volume) { v->volume = volume - (LONG)v->headroom; push_volume(v); return DS_OK; }
static HRESULT voice_set_headroom(ds_voice *v, DWORD headroom) { v->volume += (LONG)v->headroom - (LONG)headroom; v->headroom = headroom; push_volume(v); return DS_OK; }
static HRESULT voice_set_mixbin_volumes(ds_voice *v, LPCDSMIXBINS mixbins)
{
	if (!mixbins) return DSERR_INVALIDCALL;
	for (DWORD i = 0; i < mixbins->dwMixBinCount; i++) v->bin_volume[mixbins->lpMixBinVolumePairs[i].dwMixBin & 31] = mixbins->lpMixBinVolumePairs[i].lVolume;
	push_mixbins(v);
	return DS_OK;
}
static HRESULT voice_set_filter(ds_voice *v, LPCDSFILTERDESC filter)
{
	if (filter) v->filter = *filter;
	push_filter(v);
	return DS_OK;
}
/* CMcpxVoiceClient::GetVoiceProperties: what the hardware holds per slot (the whole attenuation in 1/64 dB, as
   hundredths of a decibel again), unused slots as bin -1 at -10000 */
static HRESULT voice_properties(ds_voice *v, LPDSVOICEPROPS p)
{
	memset(p, 0, sizeof(*p));
	p->dwMixBinCount = v->nbins;
	for (DWORD i = 0; i < 8; i++)
	{
		if (i >= v->nbins) { p->MixBinVolumePairs[i].dwMixBin = 0xffffffff; p->MixBinVolumePairs[i].lVolume = -10000; continue; }
		DWORD bin = v->bins[i];
		LONG att = -(v->bin_volume[bin] + v->volume);
		if (v->is3d && g_retail3d)
			att -= bin == 6 || bin == 7 ? v->front : bin == 8 || bin == 9 ? v->rear : bin == 2 ? v->center : bin == 10 ? v->room : v->base;
		DWORD hw = ((DWORD)att << 6) / 100;
		if (hw > 0xfff) hw = 0xfff;
		p->MixBinVolumePairs[i].dwMixBin = bin;
		p->MixBinVolumePairs[i].lVolume = -(LONG)((hw * 100) >> 6);
	}
	return DS_OK;
}

STDAPI IDirectSoundBuffer_SetVolume(LPDIRECTSOUNDBUFFER pBuffer, LONG lVolume) { ds_voice *v = voice_of_buffer(pBuffer); return v ? voice_set_volume(v, lVolume) : DSERR_INVALIDCALL; }
STDAPI IDirectSoundBuffer_SetHeadroom(LPDIRECTSOUNDBUFFER pBuffer, DWORD dwHeadroom) { ds_voice *v = voice_of_buffer(pBuffer); return v ? voice_set_headroom(v, dwHeadroom) : DSERR_INVALIDCALL; }
STDAPI IDirectSoundBuffer_SetMixBins(LPDIRECTSOUNDBUFFER pBuffer, LPCDSMIXBINS pMixBins) { ds_voice *v = voice_of_buffer(pBuffer); if (!v) return DSERR_INVALIDCALL; set_mixbins(v, pMixBins); return DS_OK; }
STDAPI IDirectSoundBuffer_SetMixBinVolumes(LPDIRECTSOUNDBUFFER pBuffer, LPCDSMIXBINS pMixBins) { ds_voice *v = voice_of_buffer(pBuffer); return v ? voice_set_mixbin_volumes(v, pMixBins) : DSERR_INVALIDCALL; }
STDAPI IDirectSoundBuffer_SetFilter(LPDIRECTSOUNDBUFFER pBuffer, LPCDSFILTERDESC pFilterDesc) { ds_voice *v = voice_of_buffer(pBuffer); return v ? voice_set_filter(v, pFilterDesc) : DSERR_INVALIDCALL; }
STDAPI IDirectSoundBuffer_GetVoiceProperties(LPDIRECTSOUNDBUFFER pBuffer, LPDSVOICEPROPS pVoiceProps) { ds_voice *v = voice_of_buffer(pBuffer); return v ? voice_properties(v, pVoiceProps) : DSERR_INVALIDCALL; }
STDAPI IDirectSoundBuffer_SetPosition(LPDIRECTSOUNDBUFFER pBuffer, FLOAT x, FLOAT y, FLOAT z, DWORD dwApply)
{
	ds_voice *v = voice_of_buffer(pBuffer); if (!v) return DSERR_INVALIDCALL;
	v->pos.x = x; v->pos.y = y; v->pos.z = z; apply_3d(v, dwApply); return DS_OK;
}
STDAPI IDirectSoundBuffer_SetMaxDistance(LPDIRECTSOUNDBUFFER pBuffer, FLOAT flMaxDistance, DWORD dwApply) { ds_voice *v = voice_of_buffer(pBuffer); if (!v) return DSERR_INVALIDCALL; v->max_distance = flMaxDistance; apply_3d(v, dwApply); return DS_OK; }
STDAPI IDirectSoundBuffer_SetMinDistance(LPDIRECTSOUNDBUFFER pBuffer, FLOAT flMinDistance, DWORD dwApply) { ds_voice *v = voice_of_buffer(pBuffer); if (!v) return DSERR_INVALIDCALL; v->min_distance = flMinDistance; apply_3d(v, dwApply); return DS_OK; }
STDAPI IDirectSoundBuffer_SetRolloffFactor(LPDIRECTSOUNDBUFFER pBuffer, FLOAT flRolloffFactor, DWORD dwApply) { ds_voice *v = voice_of_buffer(pBuffer); if (!v) return DSERR_INVALIDCALL; v->rolloff = flRolloffFactor; apply_3d(v, dwApply); return DS_OK; }
STDAPI IDirectSoundBuffer_SetDopplerFactor(LPDIRECTSOUNDBUFFER pBuffer, FLOAT flDopplerFactor, DWORD dwApply) { ds_voice *v = voice_of_buffer(pBuffer); if (!v) return DSERR_INVALIDCALL; v->doppler = flDopplerFactor; apply_3d(v, dwApply); return DS_OK; }
STDAPI IDirectSoundBuffer_SetConeOutsideVolume(LPDIRECTSOUNDBUFFER pBuffer, LONG lConeOutsideVolume, DWORD dwApply) { ds_voice *v = voice_of_buffer(pBuffer); if (!v) return DSERR_INVALIDCALL; v->outside_volume = lConeOutsideVolume; apply_3d(v, dwApply); return DS_OK; }
STDAPI IDirectSoundBuffer_SetI3DL2Source(LPDIRECTSOUNDBUFFER pBuffer, LPCDSI3DL2BUFFER pds3db, DWORD dwApply)
{
	ds_voice *v = voice_of_buffer(pBuffer); if (!v || !pds3db) return DSERR_INVALIDCALL;
	v->i3dl2.b = *pds3db; apply_3d(v, dwApply); return DS_OK;
}

/* ------------------------------------------------------------------ streams */
STDAPI IDirectSound_CreateSoundStream(LPDIRECTSOUND pDirectSound, LPCDSSTREAMDESC pdssd, LPDIRECTSOUNDSTREAM *ppStream, LPUNKNOWN pUnkOuter)
{
	if (!g_created) start_device();
	ds_stream *s = new (h2n_calloc(sizeof(ds_stream))) ds_stream;
	s->v.owner = s;
	voice_init(&s->v, true, pdssd ? pdssd->dwFlags : 0, pdssd ? pdssd->lpwfxFormat : NULL, 0);
	set_mixbins(&s->v, pdssd ? pdssd->lpMixBins : NULL);
	push_filter(&s->v);
	s->most = pdssd && pdssd->dwMaxAttachedPackets ? pdssd->dwMaxAttachedPackets : 1;
	if (s->most > 64) s->most = 64;
	s->callback = pdssd ? pdssd->lpfnCallback : NULL; s->context = pdssd ? pdssd->lpvContext : NULL;
	g_stream_count++;
	*ppStream = s;
	return DS_OK;
}
ULONG STDMETHODCALLTYPE ds_stream::Release()
{
	LONG left = --v.references;
	if (!left)
	{
		apu_stream_flush(v.apu, 0);
		deliver(true);
		voice_unlink(&v);
		if (g_stream_count) g_stream_count--;
		h2n_free(this);
	}
	return (ULONG)left;
}
HRESULT STDMETHODCALLTYPE ds_stream::GetInfo(LPXMEDIAINFO pInfo)
{
	memset(pInfo, 0, sizeof(*pInfo));
	pInfo->dwFlags = XMO_STREAMF_FIXED_SAMPLE_SIZE | XMO_STREAMF_INPUT_ASYNC;
	pInfo->dwInputSize = (DWORD)v.block_align;
	return S_OK;
}
HRESULT STDMETHODCALLTYPE ds_stream::GetStatus(LPDWORD pdwStatus)
{
	deliver(false);
	unsigned s = apu_voice_status(v.apu);
	DWORD out = pending < most ? DSSTREAMSTATUS_READY : 0;
	/* CMcpxStream::GetStatus: an active voice that is paused or starved is not "playing" */
	if (s & APU_STATUS_ACTIVE)
	{
		if (s & (APU_STATUS_PAUSED | APU_STATUS_STARVED)) out |= DSSTREAMSTATUS_PAUSED | (s & APU_STATUS_STARVED ? DSSTREAMSTATUS_STARVED : 0);
		else out |= DSSTREAMSTATUS_PLAYING;
	}
	*pdwStatus = out;
	return S_OK;
}
HRESULT STDMETHODCALLTYPE ds_stream::Process(LPCXMEDIAPACKET pInputPacket, LPCXMEDIAPACKET pOutputPacket)
{
	DWORD slot;
	if (!pInputPacket) return E_INVALIDARG;
	if (pending >= most) return DSERR_INVALIDCALL;
	for (slot = 0; slot < 64 && packets[slot].used; slot++) ;
	if (slot >= 64) return DSERR_INVALIDCALL;
	/* XMOAcceptPacket */
	if (pInputPacket->pdwCompletedSize) *pInputPacket->pdwCompletedSize = 0;
	if (pInputPacket->pdwStatus) *pInputPacket->pdwStatus = (DWORD)XMEDIAPACKET_STATUS_PENDING;
	packets[slot].p = *pInputPacket; packets[slot].used = true; pending++;
	if (!apu_stream_submit(v.apu, pInputPacket->pvBuffer, pInputPacket->dwMaxSize, slot))
	{
		packets[slot].used = false; pending--;
		return DSERR_INVALIDCALL;
	}
	activate_output(&v);
	return S_OK;
}
HRESULT STDMETHODCALLTYPE ds_stream::Discontinuity() { apu_stream_discontinuity(v.apu); return S_OK; }
HRESULT STDMETHODCALLTYPE ds_stream::Flush()
{
	/* CMcpxStream::Flush: the queued packets complete as flushed before this returns */
	apu_stream_flush(v.apu, 0);
	deliver(true);
	return S_OK;
}

static ds_voice *voice_of_stream(LPDIRECTSOUNDSTREAM s) { ds_stream *p = stream_of(s); return p ? &p->v : NULL; }
STDAPI IDirectSoundStream_SetVolume(LPDIRECTSOUNDSTREAM pStream, LONG lVolume) { ds_voice *v = voice_of_stream(pStream); return v ? voice_set_volume(v, lVolume) : DSERR_INVALIDCALL; }
STDAPI IDirectSoundStream_SetHeadroom(LPDIRECTSOUNDSTREAM pStream, DWORD dwHeadroom) { ds_voice *v = voice_of_stream(pStream); return v ? voice_set_headroom(v, dwHeadroom) : DSERR_INVALIDCALL; }
STDAPI IDirectSoundStream_SetMixBins(LPDIRECTSOUNDSTREAM pStream, LPCDSMIXBINS pMixBins) { ds_voice *v = voice_of_stream(pStream); if (!v) return DSERR_INVALIDCALL; set_mixbins(v, pMixBins); return DS_OK; }
STDAPI IDirectSoundStream_SetMixBinVolumes(LPDIRECTSOUNDSTREAM pStream, LPCDSMIXBINS pMixBins) { ds_voice *v = voice_of_stream(pStream); return v ? voice_set_mixbin_volumes(v, pMixBins) : DSERR_INVALIDCALL; }
STDAPI IDirectSoundStream_SetFilter(LPDIRECTSOUNDSTREAM pStream, LPCDSFILTERDESC pFilterDesc) { ds_voice *v = voice_of_stream(pStream); return v ? voice_set_filter(v, pFilterDesc) : DSERR_INVALIDCALL; }
STDAPI IDirectSoundStream_GetVoiceProperties(LPDIRECTSOUNDSTREAM pStream, LPDSVOICEPROPS pVoiceProps) { ds_voice *v = voice_of_stream(pStream); return v ? voice_properties(v, pVoiceProps) : DSERR_INVALIDCALL; }
STDAPI IDirectSoundStream_SetPitch(LPDIRECTSOUNDSTREAM pStream, LONG lPitch)
{
	ds_voice *v = voice_of_stream(pStream); if (!v) return DSERR_INVALIDCALL;
	apu_voice_set_pitch(v->apu, lPitch < DSBPITCH_MIN ? DSBPITCH_MIN : lPitch > DSBPITCH_MAX ? DSBPITCH_MAX : lPitch);
	return DS_OK;
}
STDAPI IDirectSoundStream_SetLFO(LPDIRECTSOUNDSTREAM pStream, LPCDSLFODESC pLFODesc)
{
	ds_voice *v = voice_of_stream(pStream); if (!v || !pLFODesc) return DSERR_INVALIDCALL;
	apu_voice_set_lfo(v->apu, (int)pLFODesc->dwLFO, (int)pLFODesc->dwDelay, (int)pLFODesc->dwDelta, pLFODesc->lPitchModulation,
		pLFODesc->lFilterCutOffRange, pLFODesc->lAmplitudeModulation);
	return DS_OK;
}
STDAPI IDirectSoundStream_SetEG(LPDIRECTSOUNDSTREAM pStream, LPCDSENVELOPEDESC pEnvelopeDesc)
{
	ds_voice *v = voice_of_stream(pStream); if (!v || !pEnvelopeDesc) return DSERR_INVALIDCALL;
	const DSENVELOPEDESC *e = pEnvelopeDesc;
	apu_voice_set_eg(v->apu, (int)e->dwEG, (int)e->dwMode, (int)e->dwDelay, (int)e->dwAttack, (int)e->dwHold, (int)e->dwDecay, (int)e->dwRelease,
		(int)e->dwSustain, e->lPitchScale, e->lFilterCutOff);
	return DS_OK;
}
/* CDirectSoundVoiceSettings::SetOutputBuffer: the old destination's bin leaves the slots; a new submix destination
   takes all of them, once per channel, at 1/channels */
STDAPI IDirectSoundStream_SetOutputBuffer(LPDIRECTSOUNDSTREAM pStream, LPDIRECTSOUNDBUFFER pOutputBuffer)
{
	ds_voice *v = voice_of_stream(pStream); if (!v) return DSERR_INVALIDCALL;
	ds_voice *dest = voice_of_buffer(pOutputBuffer);
	if (is_submix_destination(v->output))
	{
		DWORD keep = 0, old = v->output->submix_bin;
		for (DWORD i = 0; i < v->nbins; i++) if (v->bins[i] != old) v->bins[keep++] = v->bins[i];
		v->nbins = keep;
	}
	v->output = dest;
	if (is_submix_destination(dest))
	{
		v->nbins = 0;
		include_submix_bin(v, dest->submix_bin);
		v->bin_volume[dest->submix_bin] = (LONG)(2000.0 * log10(1.0 / v->channels));
	}
	apu_voice_set_output(v->apu, dest && (dest->caps & DSBCAPS_MIXIN) ? dest->apu : -1);
	push_mixbins(v);
	return DS_OK;
}
STDAPI IDirectSoundStream_Pause(LPDIRECTSOUNDSTREAM pStream, DWORD dwPause)
{
	ds_voice *v = voice_of_stream(pStream); if (!v) return DSERR_INVALIDCALL;
	apu_voice_pause(v->apu, dwPause == DSSTREAMPAUSE_RESUME ? 0 : dwPause == DSSTREAMPAUSE_SYNCHPLAYBACK ? 2 : 1);
	return DS_OK;
}
STDAPI IDirectSoundStream_FlushEx(LPDIRECTSOUNDSTREAM pStream, REFERENCE_TIME rtTimeStamp, DWORD dwFlags)
{
	ds_stream *s = stream_of(pStream); if (!s) return DSERR_INVALIDCALL;
	if (!dwFlags) return s->Flush();
	/* with the envelope: the release, and the flush when it is over; without: the voice stops now and the
	   packets complete as flushed at the next chance */
	apu_stream_flush(s->v.apu, dwFlags & (DSSTREAMFLUSHEX_ENVELOPE | DSSTREAMFLUSHEX_ENVELOPE2) ? 2 : 1);
	return DS_OK;
}

/* ------------------------------------------------------------------ effects */
/* an image with no effects in it: what the game is given when its own cannot be run (it then plays without effects) */
static DSEFFECTIMAGEDESC g_no_effects;
STDAPI XAudioDownloadEffectsImage(LPCSTR pszImageName, LPCDSEFFECTIMAGELOC pImageLoc, DWORD dwFlags, LPDSEFFECTIMAGEDESC *ppImageDesc)
{
	if (!g_created) start_device();
	if (ppImageDesc) *ppImageDesc = &g_no_effects;
	if (!(dwFlags & XAUDIO_DOWNLOADFX_XBESECTION) || !pszImageName)
	{
		h2n_log("[%4u] XAudioDownloadEffectsImage: only images in the XBE's sections are known here; no effects\n", h2n_next_sequence());
		return S_OK;
	}
	const BYTE *image = (const BYTE *)XLoadSection(pszImageName);
	if (!image || !h2n_retail_code_loaded())
	{
		h2n_log("[%4u] XAudioDownloadEffectsImage(%s): %s; no effects\n", h2n_next_sequence(), pszImageName,
			image ? "retail code is not loaded, so the image cannot be deciphered" : "no such section");
		return S_OK;
	}
	if (!load_image(image, 0x7060))
	{
		h2n_log("[%4u] XAudioDownloadEffectsImage(%s): the image is not one this build understands; no effects\n", h2n_next_sequence(), pszImageName);
		return S_OK;
	}
	g_i3dl2_index = pImageLoc ? pImageLoc->dwI3DL2ReverbIndex : 0xffffffff;
	g_crosstalk_index = pImageLoc ? pImageLoc->dwCrosstalkIndex : 0xffffffff;
	h2n_log("[%4u] XAudioDownloadEffectsImage(%s): %lu effects on the DSP interpreter (I3DL2 reverb %ld, crosstalk %ld)\n", h2n_next_sequence(),
		pszImageName, g_effect_count, (long)g_i3dl2_index, (long)g_crosstalk_index);
	if (ppImageDesc) *ppImageDesc = &g_image_desc.d;
	return S_OK;
}
/* XAudioSetEffectData: a high-level description turned into the effect's parameters by the XBE's own functions */
STDAPI XAudioSetEffectData(DWORD dwEffectIndex, LPCDSFX_HIGH_LEVEL_EFFECT_DESCRIPTION pDesc, LPDSFX_RAW_EFFECT_DESCRIPTION pRawDesc)
{
	if (!pDesc || !g_image_loaded || !g_retail3d) return DSERR_INVALIDCALL;
	switch (pDesc->effectType)
	{
	case DSFX_EFFECT_TYPE_I3DL2REVERB:
		return set_i3dl2_reverb(dwEffectIndex, &pDesc->I3DL2Reverb);
	case DSFX_EFFECT_TYPE_DISTORTION:
	{
		DWORD words[11];
		words[0] = ((fn_to_24bit)RETAIL_TO_24BIT)((double)pDesc->Distortion.flGain);
		raw_iir2(pDesc->Distortion.flPreFilterFrequency, pDesc->Distortion.flPreFilterQ, pDesc->Distortion.flPreFilterGain, words + 1);
		raw_iir2(pDesc->Distortion.flPostFilterFrequency, pDesc->Distortion.flPostFilterQ, pDesc->Distortion.flPostFilterGain, words + 6);
		apu_gp_lock(); HRESULT hr = set_effect_data(dwEffectIndex, 0x1c, words, sizeof(words)); apu_gp_unlock();
		if (pRawDesc) { pRawDesc->effectType = pDesc->effectType; memcpy(&pRawDesc->Distortion, words + 1, 10 * sizeof(DWORD)); }
		return hr;
	}
	case DSFX_EFFECT_TYPE_IIR2:
	{
		DWORD words[5];
		raw_iir2(pDesc->IIR2.flFrequency, pDesc->IIR2.flQ, pDesc->IIR2.flGain, words);
		apu_gp_lock(); HRESULT hr = set_effect_data(dwEffectIndex, 0x1c, words, sizeof(words)); apu_gp_unlock();
		if (pRawDesc) { pRawDesc->effectType = pDesc->effectType; memcpy(&pRawDesc->IIR2, words, sizeof(words)); }
		return hr;
	}
	}
	return DSERR_INVALIDCALL;
}

/* ------------------------------------------------------------------ WMA */
/* The game's music is WMA. Its decoder is the XBE's own (dsound.lib's CWMAXMediaObject and the WMADEC section), run
   at its retail address: WmaCreateInMemoryDecoder's body at 0x387db0 (the exported entry 0x387e31 leads here, to this
   function). H2N_WMA=off gives the game a decoder that only reads its input and returns silence. */
#define RETAIL_WMA_CREATE H2N_VA(0x00387db0, 0x391e90)
typedef HRESULT (__stdcall *fn_wma_create)(LPFNWMAXMODATACALLBACK, LPVOID, DWORD, LPWAVEFORMATEX, LPXMEDIAOBJECT *);
static unsigned g_wma_decoders;
/* the silent stand-in: reads its input through the game's callback, at the rate the sound would have taken it */
struct null_wma : XMediaObject
{
	LONG references;
	LPFNWMAXMODATACALLBACK callback; LPVOID context;
	DWORD offset;
	WAVEFORMATEX format;
	STDMETHOD_(ULONG, AddRef)() { return (ULONG)++references; }
	STDMETHOD_(ULONG, Release)() { LONG left = --references; if (!left) h2n_free(this); return (ULONG)left; }
	STDMETHOD(GetInfo)(LPXMEDIAINFO pInfo) { memset(pInfo, 0, sizeof(*pInfo)); pInfo->dwOutputSize = 4096; return S_OK; }
	STDMETHOD(GetStatus)(LPDWORD pdwStatus) { *pdwStatus = XMO_STATUSF_ACCEPT_OUTPUT_DATA; return S_OK; }
	STDMETHOD(Process)(LPCXMEDIAPACKET pInputPacket, LPCXMEDIAPACKET pOutputPacket)
	{
		LPVOID data = NULL;
		DWORD produced = 0;
		if (!pOutputPacket) return S_OK;
		if (callback)
		{
			DWORD pcm_rate = format.nSamplesPerSec * (format.nChannels ? format.nChannels : 2) * 2;
			DWORD want = pcm_rate ? (DWORD)((double)pOutputPacket->dwMaxSize * format.nAvgBytesPerSec / pcm_rate) : 2048;
			DWORD got;
			if (want < 64) want = 64;
			got = callback(context, offset, want, &data);
			offset += got;
			if (got) produced = pOutputPacket->dwMaxSize;
		}
		if (produced && pOutputPacket->pvBuffer) memset(pOutputPacket->pvBuffer, 0, produced);
		if (pOutputPacket->pdwCompletedSize) *pOutputPacket->pdwCompletedSize = produced;
		if (pOutputPacket->pdwStatus) *pOutputPacket->pdwStatus = XMEDIAPACKET_STATUS_SUCCESS;
		return S_OK;
	}
	STDMETHOD(Discontinuity)() { return S_OK; }
	STDMETHOD(Flush)() { offset = 0; return S_OK; }
};
static HRESULT silent_wma_decoder(LPFNWMAXMODATACALLBACK pfnCallback, LPVOID pvContext, DWORD dwYieldRate, LPWAVEFORMATEX pwfxCompressed, LPXMEDIAOBJECT *ppMediaObject)
{
	null_wma *w = new (h2n_calloc(sizeof(null_wma))) null_wma;
	w->references = 1; w->callback = pfnCallback; w->context = pvContext; w->offset = 0;
	memset(&w->format, 0, sizeof(w->format));
	w->format.nChannels = 2; w->format.nSamplesPerSec = 44100; w->format.nAvgBytesPerSec = 16000; w->format.wBitsPerSample = 16;
	if (pfnCallback)
	{
		static const BYTE stream_properties[16] = { 0x91, 0x07, 0xdc, 0xb7, 0xb7, 0xa9, 0xcf, 0x11, 0x8e, 0xe6, 0x00, 0xc0, 0x0c, 0x20, 0x53, 0x65 };
		LPVOID data = NULL;
		DWORD got = pfnCallback(pvContext, 0, 4096, &data);
		const BYTE *bytes = (const BYTE *)data;
		for (DWORD i = 0; bytes && i + 78 + sizeof(WAVEFORMATEX) <= got; i++)
			if (!memcmp(bytes + i, stream_properties, 16)) { memcpy(&w->format, bytes + i + 78, sizeof(WAVEFORMATEX)); break; }
		if (!w->format.nSamplesPerSec || !w->format.nAvgBytesPerSec) { w->format.nChannels = 2; w->format.nSamplesPerSec = 44100; w->format.nAvgBytesPerSec = 16000; }
	}
	if (pwfxCompressed) *pwfxCompressed = w->format;
	*ppMediaObject = w;
	return S_OK;
}
STDAPI WmaCreateInMemoryDecoder(LPFNWMAXMODATACALLBACK pfnCallback, LPVOID pvContext, DWORD dwYieldRate, LPWAVEFORMATEX pwfxCompressed, LPXMEDIAOBJECT *ppMediaObject)
{
	const char *mode = getenv("H2N_WMA");
	HRESULT hr;
	bool retail = h2n_retail_code_loaded() && !(mode && !strcmp(mode, "off"));
	if (retail) hr = ((fn_wma_create)RETAIL_WMA_CREATE)(pfnCallback, pvContext, dwYieldRate, pwfxCompressed, ppMediaObject);
	else hr = silent_wma_decoder(pfnCallback, pvContext, dwYieldRate, pwfxCompressed, ppMediaObject);
	if (g_wma_decoders++ < 4)
		h2n_log("[%4u] WmaCreateInMemoryDecoder: %s decoder, %08lx%s; format %04x, %u channels, %lu Hz, %lu bytes/s\n", h2n_next_sequence(),
			retail ? "the XBE's own" : "a silent", (unsigned long)hr, g_wma_decoders == 4 ? " (no more of these are logged)" : "",
			pwfxCompressed ? pwfxCompressed->wFormatTag : 0, pwfxCompressed ? pwfxCompressed->nChannels : 0,
			pwfxCompressed ? pwfxCompressed->nSamplesPerSec : 0, pwfxCompressed ? pwfxCompressed->nAvgBytesPerSec : 0);
	return hr;
}

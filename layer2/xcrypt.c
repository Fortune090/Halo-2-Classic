/* xcrypt.c - the console kernel's crypto calls that the XBE's own network library uses (s9u, H2N_XNET=retail):
 * XcSHAInit/Update/Final (SHA-1), XcHMAC (HMAC-SHA1), XcDESKeyParity, XcKeyTable + XcBlockCrypt + XcBlockCryptCBC
 * (DES, and three-key triple DES, EDE), XcModExp (A = B^C mod D, little-endian dwords), XcRC4Key/XcRC4Crypt.
 * The algorithms are the standard ones (FIPS 180, RFC 2104, FIPS 46-3); the argument lists are the kernel's.
 * No Windows calls, so the file also builds on its own for the test vectors (scripts/xcrypt_test.c). */
#include <stdint.h>
#include <string.h>

#ifdef _WIN32
#define XAPI __stdcall
#else
#define XAPI
#endif

/* ---- SHA-1: the caller's context (the kernel's is 116 bytes) holds state, count and the partial block ---- */
typedef struct { uint32_t h[5]; uint32_t count_lo, count_hi; uint8_t block[64]; } sha_ctx;      /* 92 bytes */
static uint32_t rol(uint32_t v, int n) { return (v << n) | (v >> (32 - n)); }
static void sha_block(sha_ctx *c, const uint8_t *p)
{
	uint32_t w[80], a = c->h[0], b = c->h[1], d = c->h[3], e = c->h[4], cc = c->h[2], t;
	for (int i = 0; i < 16; i++) w[i] = (uint32_t)p[4 * i] << 24 | (uint32_t)p[4 * i + 1] << 16 | (uint32_t)p[4 * i + 2] << 8 | p[4 * i + 3];
	for (int i = 16; i < 80; i++) w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
	for (int i = 0; i < 80; i++)
	{
		uint32_t f, k;
		if (i < 20) { f = (b & cc) | (~b & d); k = 0x5a827999; }
		else if (i < 40) { f = b ^ cc ^ d; k = 0x6ed9eba1; }
		else if (i < 60) { f = (b & cc) | (b & d) | (cc & d); k = 0x8f1bbcdc; }
		else { f = b ^ cc ^ d; k = 0xca62c1d6; }
		t = rol(a, 5) + f + e + k + w[i]; e = d; d = cc; cc = rol(b, 30); b = a; a = t;
	}
	c->h[0] += a; c->h[1] += b; c->h[2] += cc; c->h[3] += d; c->h[4] += e;
}
void XAPI h2n_XcSHAInit(uint8_t *context)
{
	sha_ctx *c = (sha_ctx *)context;
	memset(c, 0, sizeof(*c));
	c->h[0] = 0x67452301; c->h[1] = 0xefcdab89; c->h[2] = 0x98badcfe; c->h[3] = 0x10325476; c->h[4] = 0xc3d2e1f0;
}
void XAPI h2n_XcSHAUpdate(uint8_t *context, const uint8_t *input, uint32_t length)
{
	sha_ctx *c = (sha_ctx *)context;
	uint32_t used = c->count_lo & 63;
	if ((c->count_lo += length) < length) c->count_hi++;
	while (length)
	{
		uint32_t take = 64 - used < length ? 64 - used : length;
		memcpy(c->block + used, input, take);
		used += take; input += take; length -= take;
		if (used == 64) { sha_block(c, c->block); used = 0; }
	}
}
void XAPI h2n_XcSHAFinal(uint8_t *context, uint8_t *digest)
{
	sha_ctx *c = (sha_ctx *)context;
	uint32_t hi = (c->count_hi << 3) | (c->count_lo >> 29), lo = c->count_lo << 3;
	uint8_t pad = 0x80, zero = 0, len[8];
	for (int i = 0; i < 4; i++) { len[i] = (uint8_t)(hi >> (24 - 8 * i)); len[4 + i] = (uint8_t)(lo >> (24 - 8 * i)); }
	h2n_XcSHAUpdate(context, &pad, 1);
	while ((c->count_lo & 63) != 56) h2n_XcSHAUpdate(context, &zero, 1);
	h2n_XcSHAUpdate(context, len, 8);
	for (int i = 0; i < 20; i++) digest[i] = (uint8_t)(c->h[i / 4] >> (24 - 8 * (i % 4)));
}
/* HMAC-SHA1 of data followed by data2, with the key material as the key */
void XAPI h2n_XcHMAC(const uint8_t *key, uint32_t key_length, const uint8_t *data, uint32_t data_length,
	const uint8_t *data2, uint32_t data2_length, uint8_t *mac)
{
	uint8_t k[64], pad[64], inner[20], ctx[sizeof(sha_ctx)];
	memset(k, 0, sizeof(k));
	if (key_length > 64) { h2n_XcSHAInit(ctx); h2n_XcSHAUpdate(ctx, key, key_length); h2n_XcSHAFinal(ctx, k); }
	else if (key_length) memcpy(k, key, key_length);
	for (int i = 0; i < 64; i++) pad[i] = k[i] ^ 0x36;
	h2n_XcSHAInit(ctx); h2n_XcSHAUpdate(ctx, pad, 64);
	if (data && data_length) h2n_XcSHAUpdate(ctx, data, data_length);
	if (data2 && data2_length) h2n_XcSHAUpdate(ctx, data2, data2_length);
	h2n_XcSHAFinal(ctx, inner);
	for (int i = 0; i < 64; i++) pad[i] = k[i] ^ 0x5c;
	h2n_XcSHAInit(ctx); h2n_XcSHAUpdate(ctx, pad, 64); h2n_XcSHAUpdate(ctx, inner, 20); h2n_XcSHAFinal(ctx, mac);
}

/* ---- DES ---- */
static const uint8_t PC1[56] = { 57,49,41,33,25,17,9,1,58,50,42,34,26,18,10,2,59,51,43,35,27,19,11,3,60,52,44,36,
	63,55,47,39,31,23,15,7,62,54,46,38,30,22,14,6,61,53,45,37,29,21,13,5,28,20,12,4 };
static const uint8_t PC2[48] = { 14,17,11,24,1,5,3,28,15,6,21,10,23,19,12,4,26,8,16,7,27,20,13,2,
	41,52,31,37,47,55,30,40,51,45,33,48,44,49,39,56,34,53,46,42,50,36,29,32 };
static const uint8_t SHIFTS[16] = { 1,1,2,2,2,2,2,2,1,2,2,2,2,2,2,1 };
static const uint8_t IP[64] = { 58,50,42,34,26,18,10,2,60,52,44,36,28,20,12,4,62,54,46,38,30,22,14,6,64,56,48,40,32,24,16,8,
	57,49,41,33,25,17,9,1,59,51,43,35,27,19,11,3,61,53,45,37,29,21,13,5,63,55,47,39,31,23,15,7 };
static const uint8_t PERM[32] = { 16,7,20,21,29,12,28,17,1,15,23,26,5,18,31,10,2,8,24,14,32,27,3,9,19,13,30,6,22,11,4,25 };
static const uint8_t SBOX[8][64] = {
	{14,4,13,1,2,15,11,8,3,10,6,12,5,9,0,7,0,15,7,4,14,2,13,1,10,6,12,11,9,5,3,8,4,1,14,8,13,6,2,11,15,12,9,7,3,10,5,0,15,12,8,2,4,9,1,7,5,11,3,14,10,0,6,13},
	{15,1,8,14,6,11,3,4,9,7,2,13,12,0,5,10,3,13,4,7,15,2,8,14,12,0,1,10,6,9,11,5,0,14,7,11,10,4,13,1,5,8,12,6,9,3,2,15,13,8,10,1,3,15,4,2,11,6,7,12,0,5,14,9},
	{10,0,9,14,6,3,15,5,1,13,12,7,11,4,2,8,13,7,0,9,3,4,6,10,2,8,5,14,12,11,15,1,13,6,4,9,8,15,3,0,11,1,2,12,5,10,14,7,1,10,13,0,6,9,8,7,4,15,14,3,11,5,2,12},
	{7,13,14,3,0,6,9,10,1,2,8,5,11,12,4,15,13,8,11,5,6,15,0,3,4,7,2,12,1,10,14,9,10,6,9,0,12,11,7,13,15,1,3,14,5,2,8,4,3,15,0,6,10,1,13,8,9,4,5,11,12,7,2,14},
	{2,12,4,1,7,10,11,6,8,5,3,15,13,0,14,9,14,11,2,12,4,7,13,1,5,0,15,10,3,9,8,6,4,2,1,11,10,13,7,8,15,9,12,5,6,3,0,14,11,8,12,7,1,14,2,13,6,15,0,9,10,4,5,3},
	{12,1,10,15,9,2,6,8,0,13,3,4,14,7,5,11,10,15,4,2,7,12,9,5,6,1,13,14,0,11,3,8,9,14,15,5,2,8,12,3,7,0,4,10,1,13,11,6,4,3,2,12,9,5,15,10,11,14,1,7,6,0,8,13},
	{4,11,2,14,15,0,8,13,3,12,9,7,5,10,6,1,13,0,11,7,4,9,1,10,14,3,5,12,2,15,8,6,1,4,11,13,12,3,7,14,10,15,6,8,0,5,9,2,6,11,13,8,1,4,10,7,9,5,0,15,14,2,3,12},
	{13,2,8,4,6,15,11,1,10,9,3,14,5,0,12,7,1,15,13,8,10,3,7,4,12,5,6,11,0,14,9,2,7,11,4,1,9,12,14,2,0,6,10,13,15,3,5,8,2,1,14,7,4,10,8,13,15,12,9,0,3,5,6,11} };
static uint32_t g_sp[8][64];
static int g_sp_made;
static void sp_make(void)
{
	for (int s = 0; s < 8; s++)
		for (int v = 0; v < 64; v++)
		{
			int row = ((v >> 4) & 2) | (v & 1), col = (v >> 1) & 15;
			uint32_t out = (uint32_t)SBOX[s][row * 16 + col] << (28 - 4 * s), p = 0;
			for (int i = 0; i < 32; i++) if (out & (1u << (31 - (PERM[i] - 1)))) p |= 1u << (31 - i);
			g_sp[s][v] = p;
		}
	g_sp_made = 1;
}
static uint64_t bits_permute(uint64_t in, const uint8_t *table, int out_bits, int in_bits)
{
	uint64_t out = 0;
	for (int i = 0; i < out_bits; i++) if (in >> (in_bits - table[i]) & 1) out |= (uint64_t)1 << (out_bits - 1 - i);
	return out;
}
/* one DES key schedule: 16 subkeys, each 8 chunks of 6 bits (128 bytes) */
static void des_schedule(uint8_t *table, const uint8_t *key)
{
	uint64_t k = 0, cd;
	uint32_t c, d;
	for (int i = 0; i < 8; i++) k = k << 8 | key[i];
	cd = bits_permute(k, PC1, 56, 64);
	c = (uint32_t)(cd >> 28) & 0xfffffff; d = (uint32_t)cd & 0xfffffff;
	for (int r = 0; r < 16; r++)
	{
		for (int s = 0; s < SHIFTS[r]; s++) { c = ((c << 1) | (c >> 27)) & 0xfffffff; d = ((d << 1) | (d >> 27)) & 0xfffffff; }
		uint64_t sub = bits_permute((uint64_t)c << 28 | d, PC2, 48, 56);
		for (int j = 0; j < 8; j++) table[r * 8 + j] = (uint8_t)(sub >> (42 - 6 * j)) & 0x3f;
	}
}
static uint32_t des_f(uint32_t r, const uint8_t *sub)
{
	uint32_t out = 0;
	for (int i = 0; i < 8; i++)
	{
		int s = (4 * i - 1 + 32) & 31;
		uint32_t rot = (r << s) | (s ? r >> (32 - s) : 0);
		out |= g_sp[i][((rot >> 26) & 0x3f) ^ sub[i]];
	}
	return out;
}
static void des_block(const uint8_t *table, uint8_t *block, int encrypt)
{
	uint64_t v = 0, ip;
	uint32_t l, r;
	for (int i = 0; i < 8; i++) v = v << 8 | block[i];
	ip = bits_permute(v, IP, 64, 64);
	l = (uint32_t)(ip >> 32); r = (uint32_t)ip;
	for (int round = 0; round < 16; round++)
	{
		uint32_t t = r;
		r = l ^ des_f(r, table + 8 * (encrypt ? round : 15 - round));
		l = t;
	}
	v = (uint64_t)r << 32 | l;
	{	/* the final permutation is the inverse of IP */
		uint64_t out = 0;
		for (int i = 0; i < 64; i++) if (v >> (63 - i) & 1) out |= (uint64_t)1 << (64 - IP[i]);
		v = out;
	}
	for (int i = 0; i < 8; i++) block[i] = (uint8_t)(v >> (56 - 8 * i));
}
/* cipher 0: DES (8-byte key, a 128-byte table); 1: triple DES, three keys (24 bytes, 384-byte table; the network
   library gives it 0x180) */
void XAPI h2n_XcKeyTable(uint32_t cipher, uint8_t *table, const uint8_t *key)
{
	if (!g_sp_made) sp_make();
	des_schedule(table, key);
	if (cipher) { des_schedule(table + 128, key + 8); des_schedule(table + 256, key + 16); }
}
static void crypt_block(uint32_t cipher, const uint8_t *table, uint8_t *b, int encrypt)
{
	if (!cipher) { des_block(table, b, encrypt); return; }
	if (encrypt) { des_block(table, b, 1); des_block(table + 128, b, 0); des_block(table + 256, b, 1); }
	else { des_block(table + 256, b, 0); des_block(table + 128, b, 1); des_block(table, b, 0); }
}
/* op 1 encrypts, 0 decrypts */
void XAPI h2n_XcBlockCrypt(uint32_t cipher, uint8_t *output, const uint8_t *input, const uint8_t *table, uint32_t op)
{
	uint8_t b[8];
	memcpy(b, input, 8); crypt_block(cipher, table, b, op != 0); memcpy(output, b, 8);
}
/* CBC over whole 8-byte blocks; the feedback block is chained through and left holding the last cipher block */
void XAPI h2n_XcBlockCryptCBC(uint32_t cipher, uint32_t length, uint8_t *output, const uint8_t *input, const uint8_t *table,
	uint32_t op, uint8_t *feedback)
{
	uint8_t b[8], c[8];
	for (uint32_t n = 0; n + 8 <= length; n += 8)
	{
		memcpy(b, input + n, 8);
		if (op)
		{
			for (int i = 0; i < 8; i++) b[i] ^= feedback[i];
			crypt_block(cipher, table, b, 1);
			memcpy(feedback, b, 8);
		}
		else
		{
			memcpy(c, b, 8);
			crypt_block(cipher, table, b, 0);
			for (int i = 0; i < 8; i++) b[i] ^= feedback[i];
			memcpy(feedback, c, 8);
		}
		memcpy(output + n, b, 8);
	}
}
/* odd parity in every byte */
void XAPI h2n_XcDESKeyParity(uint8_t *key, uint32_t length)
{
	for (uint32_t i = 0; i < length; i++)
	{
		uint8_t v = key[i] & 0xfe, ones = 0;
		for (int b = 1; b < 8; b++) ones += (v >> b) & 1;
		key[i] = v | ((ones & 1) ? 0 : 1);
	}
}

/* ---- A = B^C mod D, numbers of n little-endian dwords; 1 when done ---- */
static int big_cmp(const uint32_t *a, const uint32_t *b, uint32_t n)
{
	for (uint32_t i = n; i--;) if (a[i] != b[i]) return a[i] < b[i] ? -1 : 1;
	return 0;
}
static void big_sub(uint32_t *a, const uint32_t *b, uint32_t n)
{
	uint64_t borrow = 0;
	for (uint32_t i = 0; i < n; i++) { uint64_t t = (uint64_t)a[i] - b[i] - borrow; a[i] = (uint32_t)t; borrow = (t >> 32) & 1; }
}
/* r = a * b mod m (all below m), by shifts and adds from the top bit of a */
static void big_mulmod(uint32_t *r, const uint32_t *a, const uint32_t *b, const uint32_t *m, uint32_t n, uint32_t *t)
{
	memset(t, 0, (n + 1) * 4);
	for (int bit = (int)n * 32 - 1; bit >= 0; bit--)
	{
		uint32_t carry = 0;
		for (uint32_t i = 0; i <= n; i++) { uint32_t v = t[i]; t[i] = (v << 1) | carry; carry = v >> 31; }
		if (t[n] || big_cmp(t, m, n) >= 0) { big_sub(t, m, n); t[n] = 0; }
		if (a[bit / 32] >> (bit % 32) & 1)
		{
			uint64_t c = 0;
			for (uint32_t i = 0; i < n; i++) { c += (uint64_t)t[i] + b[i]; t[i] = (uint32_t)c; c >>= 32; }
			t[n] += (uint32_t)c;
			if (t[n] || big_cmp(t, m, n) >= 0) { big_sub(t, m, n); t[n] = 0; }
		}
	}
	memcpy(r, t, n * 4);
}
uint32_t XAPI h2n_XcModExp(uint32_t *a, const uint32_t *b, const uint32_t *c, const uint32_t *d, uint32_t n)
{
	uint32_t base[64], result[64], tmp[65];
	if (!n || n > 64) return 0;
	memcpy(base, b, n * 4);
	while (big_cmp(base, d, n) >= 0) big_sub(base, d, n);         /* (B is below D in every use seen; this keeps it so) */
	memset(result, 0, n * 4); result[0] = 1;
	if (n == 1 && d[0] == 1) result[0] = 0;
	for (int bit = (int)n * 32 - 1; bit >= 0; bit--)
	{
		big_mulmod(result, result, result, d, n, tmp);
		if (c[bit / 32] >> (bit % 32) & 1) big_mulmod(result, result, base, d, n, tmp);
	}
	memcpy(a, result, n * 4);
	return 1;
}

/* ---- RC4 (the key structure: 256-byte state, then i and j) ---- */
void XAPI h2n_XcRC4Key(uint8_t *state, uint32_t key_length, const uint8_t *key)
{
	uint8_t j = 0, t;
	for (int i = 0; i < 256; i++) state[i] = (uint8_t)i;
	for (int i = 0; i < 256; i++) { j = (uint8_t)(j + state[i] + key[i % key_length]); t = state[i]; state[i] = state[j]; state[j] = t; }
	state[256] = state[257] = 0;
}
void XAPI h2n_XcRC4Crypt(uint8_t *state, uint32_t length, uint8_t *data)
{
	uint8_t i = state[256], j = state[257], t;
	for (uint32_t n = 0; n < length; n++)
	{
		i++; j = (uint8_t)(j + state[i]); t = state[i]; state[i] = state[j]; state[j] = t;
		data[n] ^= state[(uint8_t)(state[i] + state[j])];
	}
	state[256] = i; state[257] = j;
}

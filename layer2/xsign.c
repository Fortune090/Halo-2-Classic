/* H2Classic: the signatures of saved files.
 *
 * The game signs every file it saves (profiles, saved games, variants, its preferences) and checks the signature when
 * it reads the file back; a file whose signature is not the one it computes is shown as damaged. The signature is the
 * console's: XCalculateSignatureBegin, Update and End of the XAPI library. What they do is read from the library's own
 * code inside the game (retail 0x2d6459-0x2d680c):
 *
 *   Begin(flags)           = BeginEx(flags, the title id in the XBE's certificate)
 *   BeginEx(flags, title)  flags 0 or 1 (a saved game; 1 = bound to one console): a 0x7c-byte context holding the flags,
 *                          the key and a SHA-1 state. The key is the kernel's XboxSignatureKey when the title id is the
 *                          certificate's own, else the entry of XboxAlternateSignatureKeys that belongs to that
 *                          alternate title id (none: error 5). Then the inner half of HMAC-SHA1 with that 16-byte key.
 *                          flags 2 or 4 (content): a plain SHA-1, with the context's flags set to 2.
 *                          flags 8 (online): through three pointers of the online library; anything else: error 50.
 *   Update                 more data into the SHA-1 state
 *   End(signature)         the outer half of the HMAC. With flag 1 the result is signed once more:
 *                          HMAC-SHA1(the console's hard disk key, the 20 bytes), so that the file is good on one console only.
 *
 * The two keys are the kernel's. The kernel makes them when it starts the title, from the certificate inside the XBE
 * and from a key of its own, the certificate key (the same on every retail console):
 *
 *   XboxSignatureKey = the first 16 bytes of HMAC-SHA1(certificate key, the certificate's signature key)
 *   and the same for each of the 16 alternate signature keys and for the LAN key.
 *
 * That key of the kernel is not in this exe, as nothing of the console's or the game's is. It is read from a file the
 * user supplies: h2classic\keys.bin (or the file H2N_CERTKEYFILE names), in one of these forms:
 *   16 bytes                 the certificate key
 *   32 bytes                 an EEPROM key, then the certificate key (the keys.bin of Cxbx-Reloaded)
 *   text, 32 or 64 hex digits  the same two forms written out (spaces, commas and 0x are skipped)
 * H2N_CERTKEY=<32 hex digits> does the same without a file. Without the key the signatures are made with a key of
 * zeros: files saved here are good here, but a file saved on a console shows as damaged here and the other way round.
 *
 * The hard disk key (flag 1) belongs to one console; this "console" has a hard disk key of zeros. The game uses flag 1
 * only for its preferences file, which never leaves the console. */
#include "h2n.h"

/* ---- SHA-1, as the kernel's XcSHAInit, XcSHAUpdate and XcSHAFinal ---- */
typedef struct sha_state { DWORD h[5]; DWORD count_low, count_high; unsigned char block[64]; } sha_state;

static DWORD rol(DWORD v, int n) { return (v << n) | (v >> (32 - n)); }
static void sha_block(sha_state *s, const unsigned char *p)
{
	DWORD w[80], a = s->h[0], b = s->h[1], c = s->h[2], d = s->h[3], e = s->h[4], t;
	int i;
	for (i = 0; i < 16; i++) w[i] = ((DWORD)p[i * 4] << 24) | ((DWORD)p[i * 4 + 1] << 16) | ((DWORD)p[i * 4 + 2] << 8) | p[i * 4 + 3];
	for (; i < 80; i++) w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
	for (i = 0; i < 80; i++)
	{
		if (i < 20) t = ((b & c) | (~b & d)) + 0x5a827999u;
		else if (i < 40) t = (b ^ c ^ d) + 0x6ed9eba1u;
		else if (i < 60) t = ((b & c) | (b & d) | (c & d)) + 0x8f1bbcdcu;
		else t = (b ^ c ^ d) + 0xca62c1d6u;
		t += rol(a, 5) + e + w[i];
		e = d; d = c; c = rol(b, 30); b = a; a = t;
	}
	s->h[0] += a; s->h[1] += b; s->h[2] += c; s->h[3] += d; s->h[4] += e;
}
static void sha_init(sha_state *s)
{
	s->h[0] = 0x67452301u; s->h[1] = 0xefcdab89u; s->h[2] = 0x98badcfeu; s->h[3] = 0x10325476u; s->h[4] = 0xc3d2e1f0u;
	s->count_low = 0; s->count_high = 0;
}
static void sha_update(sha_state *s, const unsigned char *data, size_t size)
{
	size_t have = s->count_low & 63;
	s->count_low += (DWORD)size; if (s->count_low < (DWORD)size) s->count_high++;
	while (size)
	{
		size_t take = 64 - have;
		if (take > size) take = size;
		memcpy(s->block + have, data, take);
		have += take; data += take; size -= take;
		if (have == 64) { sha_block(s, s->block); have = 0; }
	}
}
static void sha_final(sha_state *s, unsigned char *digest)
{
	static const unsigned char padding[64] = { 0x80 };
	unsigned char length[8];
	DWORD low = s->count_low << 3, high = (s->count_high << 3) | (s->count_low >> 29);
	int i;
	for (i = 0; i < 4; i++) { length[i] = (unsigned char)(high >> (24 - 8 * i)); length[4 + i] = (unsigned char)(low >> (24 - 8 * i)); }
	sha_update(s, padding, ((s->count_low & 63) < 56 ? 56 : 120) - (s->count_low & 63));
	sha_update(s, length, 8);
	for (i = 0; i < 20; i++) digest[i] = (unsigned char)(s->h[i / 4] >> (24 - 8 * (i & 3)));
}

/* ---- the two halves of HMAC-SHA1, as XShaHmacInitialize (retail 0x2d6459) and XShaHmacComputeFinal (0x2d64c0) ---- */
static void hmac_begin(const unsigned char *key, size_t key_size, sha_state *s)
{
	unsigned char pad[64];
	size_t i;
	if (key_size > 64) key_size = 64;
	memset(pad, 0, sizeof(pad)); memcpy(pad, key, key_size);
	for (i = 0; i < 64; i++) pad[i] ^= 0x36;
	sha_init(s);
	sha_update(s, pad, 64);
}
static void hmac_end(sha_state *s, const unsigned char *key, size_t key_size, unsigned char *digest)
{
	unsigned char pad[64 + 20];
	size_t i;
	if (key_size > 64) key_size = 64;
	memset(pad, 0, 64); memcpy(pad, key, key_size);
	for (i = 0; i < 64; i++) pad[i] ^= 0x5c;
	sha_final(s, pad + 64);
	sha_init(s);
	sha_update(s, pad, sizeof(pad));
	sha_final(s, digest);
}
/* the kernel's XcHMAC: one call, two pieces of data */
static void hmac(const unsigned char *key, size_t key_size, const unsigned char *data1, size_t size1, const unsigned char *data2, size_t size2, unsigned char *digest)
{
	sha_state s;
	hmac_begin(key, key_size, &s);
	if (size1) sha_update(&s, data1, size1);
	if (size2) sha_update(&s, data2, size2);
	hmac_end(&s, key, key_size, digest);
}

/* ---- the kernel's keys for this title ---- */
static unsigned char g_certificate_key[16];             /* the kernel's own; from the user's file, else zeros */
static unsigned char g_hard_disk_key[16];               /* XboxHDKey: this console's is zeros */
static unsigned char g_signature_key[16];               /* XboxSignatureKey */
static unsigned char g_alternate_signature_keys[16][16];/* XboxAlternateSignatureKeys */
static unsigned char g_lan_key[16];                     /* XboxLANKey (system link: H2N_XNET=retail hands it to the XBE's network library, xkernel.c) */
static DWORD g_title_id, g_alternate_title_ids[16];
static int g_keys_ready, g_have_certificate_key;

static int hex_value(int c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; }
/* hex digits from text into bytes; separators and "0x" are skipped. Returns the number of bytes, or -1 for anything else in the text */
static int parse_hex(const unsigned char *text, size_t size, unsigned char *out, size_t capacity)
{
	size_t i, digits = 0;
	for (i = 0; i < size; i++)
	{
		int c = text[i], v;
		if (c == '0' && i + 1 < size && (text[i + 1] == 'x' || text[i + 1] == 'X') && !(digits & 1)) { i++; continue; }
		if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == ',' || c == '-' || c == ':' || c == ';' || c == '{' || c == '}') continue;
		v = hex_value(c);
		if (v < 0 || digits / 2 >= capacity) return -1;
		if (digits & 1) out[digits / 2] |= (unsigned char)v; else out[digits / 2] = (unsigned char)(v << 4);
		digits++;
	}
	return digits & 1 ? -1 : (int)(digits / 2);
}
static void read_certificate_key(void)
{
	const char *direct = getenv("H2N_CERTKEY"), *name = getenv("H2N_CERTKEYFILE");
	unsigned char data[600], bytes[32];
	size_t size;
	int count;
	FILE *f;
	if (direct && *direct)
	{
		count = parse_hex((const unsigned char *)direct, strlen(direct), bytes, sizeof(bytes));
		if (count == 16) { memcpy(g_certificate_key, bytes, 16); g_have_certificate_key = 1; h2n_log("saved files: the console's certificate key is the one in H2N_CERTKEY\n"); return; }
		h2n_log("*** saved files: H2N_CERTKEY is not 32 hex digits; it is left out\n");
	}
	if (!name || !*name) name = "h2classic\\keys.bin";
	f = h2n_real_fopen(name, "rb");
	if (!f)
	{
		if (getenv("H2N_CERTKEYFILE")) h2n_log("*** saved files: the key file %s could not be opened\n", name);
		return;
	}
	size = fread(data, 1, sizeof(data), f);
	fclose(f);
	if (size == 16) { memcpy(g_certificate_key, data, 16); g_have_certificate_key = 1; h2n_log("saved files: the console's certificate key was read from %s (16 bytes)\n", name); return; }
	if (size == 32) { memcpy(g_certificate_key, data + 16, 16); g_have_certificate_key = 1; h2n_log("saved files: the console's certificate key was read from %s (32 bytes: the second key in it)\n", name); return; }
	count = parse_hex(data, size, bytes, sizeof(bytes));
	if (count == 16 || count == 32)
	{
		memcpy(g_certificate_key, bytes + count - 16, 16); g_have_certificate_key = 1;
		h2n_log("saved files: the console's certificate key was read from %s (text, %d hex digits%s)\n", name, count * 2, count == 32 ? ": the second key in it" : "");
		return;
	}
	h2n_log("*** saved files: %s is %u bytes and is neither 16 or 32 bytes nor 32 or 64 hex digits; it is left out\n", name, (unsigned)size);
}

/* called once at start with the user's XBE: the certificate in it gives the title's ids and raw keys */
unsigned char *h2n_lan_key(void) { return g_lan_key; }
int h2n_have_certificate_key(void) { return g_have_certificate_key; }

void h2n_title_keys(const unsigned char *xbe, size_t size)
{
	DWORD base, certificate;
	unsigned char digest[20];
	int i;
	memcpy(&base, xbe + 0x104, 4); memcpy(&certificate, xbe + 0x118, 4);
	certificate -= base;
	if (certificate > size || size - certificate < 0x1d0) { h2n_log("*** saved files: the XBE's certificate is not where its header says; signatures use keys of zeros\n"); g_keys_ready = 1; return; }
	read_certificate_key();
	memcpy(&g_title_id, xbe + certificate + 8, 4);
	memcpy(g_alternate_title_ids, xbe + certificate + 0x5c, sizeof(g_alternate_title_ids));
	hmac(g_certificate_key, 16, xbe + certificate + 0xb0, 16, NULL, 0, digest); memcpy(g_lan_key, digest, 16);
	hmac(g_certificate_key, 16, xbe + certificate + 0xc0, 16, NULL, 0, digest); memcpy(g_signature_key, digest, 16);
	for (i = 0; i < 16; i++) { hmac(g_certificate_key, 16, xbe + certificate + 0xd0 + i * 16, 16, NULL, 0, digest); memcpy(g_alternate_signature_keys[i], digest, 16); }
	g_keys_ready = 1;
	{
		/* H2N_SIGCHECK=<a saved file>: is its signature (its last 20 bytes) the one made here over the rest of it? A profile or
		   saved game copied from a console answers whether the key that was given is the console's. The file is only read. */
		const char *name = getenv("H2N_SIGCHECK");
		FILE *f = name && *name ? h2n_real_fopen(name, "rb") : NULL;
		if (name && *name && !f) h2n_log("*** saved files: H2N_SIGCHECK: %s could not be opened\n", name);
		if (f)
		{
			static unsigned char data[1 << 20];
			size_t have = fread(data, 1, sizeof(data), f);
			fclose(f);
			if (have <= 20 || have == sizeof(data)) h2n_log("*** saved files: H2N_SIGCHECK: %s is %u bytes; a signed file of 21 bytes to 1 MB is expected\n", name, (unsigned)have);
			else
			{
				hmac(g_signature_key, 16, data, have - 20, NULL, 0, digest);
				h2n_log("saved files: H2N_SIGCHECK: %s (%u bytes): its signature is %02x%02x%02x%02x...%02x%02x, the one made here is %02x%02x%02x%02x...%02x%02x: %s\n", name, (unsigned)have,
					data[have - 20], data[have - 19], data[have - 18], data[have - 17], data[have - 2], data[have - 1], digest[0], digest[1], digest[2], digest[3], digest[18], digest[19],
					!memcmp(digest, data + have - 20, 20) ? "THE SAME (the key is right for files of this title)"
					: g_have_certificate_key ? "DIFFERENT (the key that was given is not the console's certificate key, or the file was changed after it was signed)"
					: "DIFFERENT (as expected without the console's certificate key)");
			}
		}
	}
	if (g_have_certificate_key)
		h2n_log("saved files: signed and checked as on the console (title %08lx); files saved here and files saved on a console can stand in for each other\n", g_title_id);
	else
		h2n_log("saved files: no certificate key was given (h2classic\\keys.bin), so files are signed with a key of zeros: a file saved on a console shows as\n"
			"       damaged here, and a file saved here shows as damaged on a console. The launcher's \"Generate Xbox keys.bin file\" makes it.\n");
}

/* ---- the three calls ---- */
struct calcsig { DWORD flags; const unsigned char *key; sha_state sha; };

HANDLE __stdcall h2n_XCalculateSignatureBeginEx(DWORD flags, DWORD title_id) __asm__("_XCalculateSignatureBeginEx@8");
HANDLE __stdcall h2n_XCalculateSignatureBeginEx(DWORD flags, DWORD title_id)
{
	struct calcsig *c;
	if (flags <= 1)
	{
		const unsigned char *key = NULL;
		int i;
		if (title_id == g_title_id) key = g_signature_key;
		else
			for (i = 0; i < 16 && g_alternate_title_ids[i]; i++)
				if (g_alternate_title_ids[i] == title_id) { key = g_alternate_signature_keys[i]; break; }
		if (!key) { SetLastError(ERROR_ACCESS_DENIED); return INVALID_HANDLE_VALUE; }
		c = malloc(sizeof(*c));
		if (!c) { SetLastError(ERROR_NOT_ENOUGH_MEMORY); return INVALID_HANDLE_VALUE; }
		c->flags = flags; c->key = key;
		hmac_begin(key, 16, &c->sha);
		return (HANDLE)c;
	}
	if (flags == 2 || flags == 4)
	{
		c = malloc(sizeof(*c));
		if (!c) { SetLastError(ERROR_NOT_ENOUGH_MEMORY); return INVALID_HANDLE_VALUE; }
		c->flags = 2; c->key = NULL;
		sha_init(&c->sha);
		return (HANDLE)c;
	}
	if (flags == 8) { static int told; if (!told) { told = 1; h2n_log("*** saved files: a signature of the online kind (flag 8) was asked for; this build has no online library, so it is refused\n"); } }
	SetLastError(ERROR_NOT_SUPPORTED);
	return INVALID_HANDLE_VALUE;
}
HANDLE __stdcall h2n_XCalculateSignatureBegin(DWORD flags) __asm__("_XCalculateSignatureBegin@4");
HANDLE __stdcall h2n_XCalculateSignatureBegin(DWORD flags) { return h2n_XCalculateSignatureBeginEx(flags, g_title_id); }

DWORD __stdcall h2n_XCalculateSignatureUpdate(HANDLE handle, const BYTE *data, ULONG size) __asm__("_XCalculateSignatureUpdate@12");
DWORD __stdcall h2n_XCalculateSignatureUpdate(HANDLE handle, const BYTE *data, ULONG size)
{
	struct calcsig *c = (struct calcsig *)handle;
	if (c->flags > 2 && c->flags != 4) return ERROR_NOT_SUPPORTED;
	sha_update(&c->sha, data, size);
	return ERROR_SUCCESS;
}

DWORD __stdcall h2n_XCalculateSignatureEnd(HANDLE handle, void *signature) __asm__("_XCalculateSignatureEnd@8");
DWORD __stdcall h2n_XCalculateSignatureEnd(HANDLE handle, void *signature)
{
	struct calcsig *c = (struct calcsig *)handle;
	unsigned char result[20];
	DWORD bytes = c->sha.count_low;
	if (c->flags > 2 && c->flags != 4) return ERROR_NOT_SUPPORTED;
	if (c->flags <= 1)
	{
		if (signature)
		{
			hmac_end(&c->sha, c->key, 16, result);
			bytes -= 64;                                    /* the key's block is not part of what was signed */
			if (c->flags & 1) hmac(g_hard_disk_key, 16, result, 20, NULL, 0, result);     /* XapiConvertSignatureToNonRoamable (retail 0x2d6532) */
			memcpy(signature, result, 20);
		}
	}
	else if (signature) { sha_final(&c->sha, result); memcpy(signature, result, 20); }
	if (signature)
		h2n_log("[%4u] signature over %lu bytes (%s): %02x%02x%02x%02x %02x%02x%02x%02x ...%02x%02x\n", h2n_next_sequence(), bytes,
			c->flags == 0 ? "a saved file that can move between consoles" : c->flags == 1 ? "a file of this console only" : "content",
			result[0], result[1], result[2], result[3], result[4], result[5], result[6], result[7], result[18], result[19]);
	free(c);
	return ERROR_SUCCESS;
}

DWORD __stdcall h2n_XCalculateSignatureGetSize(DWORD flags) __asm__("_XCalculateSignatureGetSize@4");
DWORD __stdcall h2n_XCalculateSignatureGetSize(DWORD flags) { return flags <= 2 || flags == 4 ? 20 : flags == 8 ? 100 : 0; }

/* ---- the layer's own check of the above (H2N_SELFTEST): known answers for SHA-1 and HMAC-SHA1 ---- */
int h2n_sign_selftest(void)
{
	static const unsigned char abc_digest[20] = { 0xa9, 0x99, 0x3e, 0x36, 0x47, 0x06, 0x81, 0x6a, 0xba, 0x3e, 0x25, 0x71, 0x78, 0x50, 0xc2, 0x6c, 0x9c, 0xd0, 0xd8, 0x9d };
	static const unsigned char long_digest[20] = { 0x84, 0x98, 0x3e, 0x44, 0x1c, 0x3b, 0xd2, 0x6e, 0xba, 0xae, 0x4a, 0xa1, 0xf9, 0x51, 0x29, 0xe5, 0xe5, 0x46, 0x70, 0xf1 };
	static const unsigned char hmac1[20] = { 0xb6, 0x17, 0x31, 0x86, 0x55, 0x05, 0x72, 0x64, 0xe2, 0x8b, 0xc0, 0xb6, 0xfb, 0x37, 0x8c, 0x8e, 0xf1, 0x46, 0xbe, 0x00 };
	static const unsigned char hmac2[20] = { 0xef, 0xfc, 0xdf, 0x6a, 0xe5, 0xeb, 0x2f, 0xa2, 0xd2, 0x74, 0x16, 0xd5, 0xf1, 0x84, 0xdf, 0x9c, 0x25, 0x9a, 0x7c, 0x79 };
	unsigned char digest[20], key[20];
	sha_state s;
	int failures = 0;
	sha_init(&s); sha_update(&s, (const unsigned char *)"abc", 3); sha_final(&s, digest);
	if (memcmp(digest, abc_digest, 20)) failures++;
	sha_init(&s); sha_update(&s, (const unsigned char *)"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", 56); sha_final(&s, digest);
	if (memcmp(digest, long_digest, 20)) failures++;
	memset(key, 0x0b, 20); hmac(key, 20, (const unsigned char *)"Hi There", 8, NULL, 0, digest);
	if (memcmp(digest, hmac1, 20)) failures++;
	hmac((const unsigned char *)"Jefe", 4, (const unsigned char *)"what do ya want ", 16, (const unsigned char *)"for nothing?", 12, digest);
	if (memcmp(digest, hmac2, 20)) failures++;
	return failures;
}

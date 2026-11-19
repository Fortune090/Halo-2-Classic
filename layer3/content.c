/* H2Classic (s10o): the map packs and Halo 2's title update on H2Classic's hard disk, the way an Xbox keeps them once
 * they are installed.
 *
 * On the console T: is Halo 2's own folder on the hard disk, E:\TDATA\4d530064\; here it is .\h2classic\T\ (every Xbox
 * drive but the disc is a folder under .\h2classic\, xapi.c). An installed pack is the folder T:\$c\<its offer id>\
 * (ContentMeta.xbx, ContentImage.xbx, patch.lvl, patch_v3.lvl and its maps); the title update is T:\$u\ (ContentMeta.xbx,
 * default.xbe, downloader.xbe).
 *
 * At start the layer looks through Roms\DLC for packs and title updates in the forms they were handed out:
 *   - the Multiplayer Map Pack disc's files: demos\dlc\00000001 ... (its own title updates, demos\update\Updates\*.03 ...,
 *     are title update 3, not 1.5),
 *   - an installer's folder such as "Halo 2 DLC": content\c\4d53006400000001 ... and content\u5English,
 *   - a copy of a console's TDATA\4d530064: $c\4d53006400000001 ... and $u,
 * and copies onto H2Classic's hard disk what is not there yet, as the disc's installer does on a console. A pack is any
 * folder whose ContentMeta.xbx is Halo 2's ("XCSF" at 0x14, title 4d530064 at 0x24) with an offer id (0x28, 8 bytes); a
 * title update is such a folder with no offer id, next to a default.xbe. Only the last title update - 5, the one that
 * shows "1.5" on the start screen (certificate version 0x803) - goes into T:\$u. The user's files are only ever read.
 *
 * v1.0 does what the "1.0 TU Skip" build does on the console: it never restarts into the title update (H2Classic does not
 * run XAPI's start-up, whose XapipUpdateRebootIfNecessary does that) and the packs are not offered to it (XFindFirstContent
 * answers "none", xsave.cpp). They are for the 1.5 build. */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "h2n.h"

DWORD h2n_crc32(const unsigned char *p, size_t n);      /* main.c */

#define U5_SIZE 4919296u                /* title update 5 ("1.5"): its default.xbe */
#define U5_VERSION 0x803u               /* ... its certificate's version */
#define U5_ENGLISH_CRC 0xa5b0b715u      /* ... the English one, whole file */

typedef HANDLE (WINAPI *find_first_fn)(LPCSTR, LPWIN32_FIND_DATAA);
typedef BOOL (WINAPI *find_next_fn)(HANDLE, LPWIN32_FIND_DATAA);
typedef BOOL (WINAPI *create_dir_fn)(LPCSTR, LPSECURITY_ATTRIBUTES);
typedef BOOL (WINAPI *copy_fn)(LPCSTR, LPCSTR, BOOL);
typedef BOOL (WINAPI *attrs_fn)(LPCSTR, GET_FILEEX_INFO_LEVELS, LPVOID);
static find_first_fn g_first;
static find_next_fn g_next;
static create_dir_fn g_mkdir;
static copy_fn g_copy;
static attrs_fn g_attrs;
static void functions(void)
{
	HMODULE k = GetModuleHandleA("kernel32.dll");                /* the PC's own, not the ones the layer gives the game */
	g_first = (find_first_fn)GetProcAddress(k, "FindFirstFileA");
	g_next = (find_next_fn)GetProcAddress(k, "FindNextFileA");
	g_mkdir = (create_dir_fn)GetProcAddress(k, "CreateDirectoryA");
	g_copy = (copy_fn)GetProcAddress(k, "CopyFileA");
	g_attrs = (attrs_fn)GetProcAddress(k, "GetFileAttributesExA");
}
static int file_size(const char *path, ULONGLONG *size)
{
	WIN32_FILE_ATTRIBUTE_DATA a;
	if (!g_attrs(path, GetFileExInfoStandard, &a) || (a.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) return 0;
	*size = ((ULONGLONG)a.nFileSizeHigh << 32) | a.nFileSizeLow;
	return 1;
}
static unsigned char *read_file(const char *path, size_t limit, size_t *size)
{
	FILE *f = h2n_real_fopen(path, "rb");
	unsigned char *data;
	long n;
	if (!f) return NULL;
	fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
	if (n <= 0 || (size_t)n > limit) { fclose(f); return NULL; }
	data = malloc((size_t)n);
	if (data && fread(data, 1, (size_t)n, f) != (size_t)n) { free(data); data = NULL; }
	fclose(f);
	if (data) *size = (size_t)n;
	return data;
}

/* a ContentMeta.xbx of Halo 2's: its offer id (0 for a title update) and the [Default] section's Name= */
static int content_meta(const char *path, ULONGLONG *offer, char *name, size_t name_size)
{
	size_t n = 0, i, k = 0;
	unsigned char *d = read_file(path, 0x10000, &n);
	int ok = 0;
	name[0] = 0;
	if (d && n >= 0x40 && !memcmp(d + 0x14, "XCSF", 4) && *(const DWORD *)(d + 0x24) == 0x4d530064)
	{
		DWORD text = *(const DWORD *)(d + 0x34), bytes = *(const DWORD *)(d + 0x38);
		*offer = *(const ULONGLONG *)(d + 0x28);
		ok = 1;
		if (text < n && bytes <= n - text)
		{
			const WCHAR *w = (const WCHAR *)(d + text);
			size_t count = bytes / 2;
			for (i = 0; i + 5 <= count; i++)
				if (w[i] == 'N' && w[i + 1] == 'a' && w[i + 2] == 'm' && w[i + 3] == 'e' && w[i + 4] == '=')
				{
					for (i += 5; i < count && w[i] != '\r' && w[i] != '\n' && w[i] && k + 1 < name_size; i++)
						name[k++] = w[i] < 128 ? (char)w[i] : '?';
					break;
				}
			name[k] = 0;
		}
	}
	free(d);
	return ok;
}

static void make_dirs(const char *path)                         /* every folder of path, from the first */
{
	char part[MAX_PATH];
	size_t i, n = strlen(path);
	for (i = 0; i <= n && i < sizeof(part); i++)
	{
		if (path[i] == '\\' || path[i] == 0) { memcpy(part, path, i); part[i] = 0; if (i) g_mkdir(part, NULL); }
	}
}
/* copies the folder's files that the target lacks or has at another size; returns the bytes copied, or -1 on a failure */
static LONGLONG copy_folder(const char *from, const char *to, const char *skip_prefix)
{
	WIN32_FIND_DATAA e;
	char pattern[MAX_PATH], src[MAX_PATH], dst[MAX_PATH];
	HANDLE f;
	LONGLONG copied = 0;
	make_dirs(to);
	snprintf(pattern, sizeof(pattern), "%s\\*", from);
	f = g_first(pattern, &e);
	if (f == INVALID_HANDLE_VALUE) return -1;
	do
	{
		ULONGLONG have = 0, want = ((ULONGLONG)e.nFileSizeHigh << 32) | e.nFileSizeLow;
		if (e.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
		if (skip_prefix && !_strnicmp(e.cFileName, skip_prefix, strlen(skip_prefix))) continue;
		snprintf(src, sizeof(src), "%s\\%s", from, e.cFileName);
		snprintf(dst, sizeof(dst), "%s\\%s", to, e.cFileName);
		if (file_size(dst, &have) && have == want) continue;
		if (!g_copy(src, dst, FALSE)) { h2n_log("*** map packs: could not copy %s to %s (error %lu)\n", src, dst, GetLastError()); copied = -1; break; }
		copied += (LONGLONG)want;
	} while (g_next(f, &e));
	FindClose(f);
	return copied;
}

/* (s10p, s10r) the last step of the console's installer, XInstallContentSignatures (v1.0 0x2d2ce1, read off its code),
   as the console's own installer of downloaded content calls it (0x3b19e5: title id, no source file, the installed
   ContentMeta.xbx, flags 3). On the header of the ContentMeta.xbx that is now on the hard disk it
     - sets bit 0 of the word at 0x20 when bit 1 of its flags is set (0x2d2e2b: or dword [header+0x20], 1; cleared
       otherwise). XFindFirstContent only offers a pack to the game when that bit is set (xsave.cpp). A pack as it is
       handed out has it clear (the installer folder's packs 1 to 3: with build 39 the game was offered pack 4 only);
       a console's own copy has it set;
     - signs the header for that console with XapiComputeContentHeaderSignature (0x2d2938): HMAC-SHA1 of the file's bytes
       from 0x14 to the header's end (its size at 0x18; 0x88 for a pack, 0x6c for the title update), keyed with
       XComputeContentSignatureKey (0x2d2919) = HMAC-SHA1(the console's hard disk key, the title id). The game checks that
       signature when it loads a pack's signatures (XLoadContentSignaturesEx -> XapiLoadContentMetadataHeader), so a
       pack copied from another console's TDATA is not good on this one until it is signed for it;
     - writes the header back (at most 0x88 bytes).
   H2Classic's console has a hard disk key of zeros (xkernel.c). Only the copy on H2Classic's hard disk is written, and
   only its header. The title update is not installed by that function (pack = 0): its header is only signed.
   Returns 1 if it wrote the header, 0 if the header already was as the installer leaves it, -1 if it could not. */
void __stdcall h2n_XcHMAC(const unsigned char *key, unsigned key_length, const unsigned char *data, unsigned data_length,
	const unsigned char *data2, unsigned data2_length, unsigned char *mac);  /* xcrypt.c */
const unsigned char *h2n_hd_key(void);                          /* xkernel.c */
static int sign_for_this_console(const char *path, int pack)
{
	size_t n = 0;
	unsigned char *d = read_file(path, 0x10000, &n), key[20], sig[20];
	DWORD size, title;
	FILE *f;
	int result = -1, changed = 0;
	if (!d) return -1;
	size = n >= 0x1c ? *(const DWORD *)(d + 0x18) : 0;
	if (n >= 0x1c && !memcmp(d + 0x14, "XCSF", 4) && size >= 0x1c && size <= n)      /* a title update's header is 0x6c bytes */
	{
		if (pack && size >= 0x24 && !(*(DWORD *)(d + 0x20) & 1)) { *(DWORD *)(d + 0x20) |= 1; changed = 1; }
		title = *(const DWORD *)(d + 0x24);
		h2n_XcHMAC(h2n_hd_key(), 16, (const unsigned char *)&title, 4, NULL, 0, key);
		h2n_XcHMAC(key, 20, d + 0x14, size - 0x14, NULL, 0, sig);
		if (memcmp(sig, d, 20)) { memcpy(d, sig, 20); changed = 1; }
		if (!changed) result = 0;
		else if ((f = h2n_real_fopen(path, "r+b")) != NULL)
		{
			size_t header = size < 0x88 ? size : 0x88;
			if (fwrite(d, 1, header, f) == header) result = 1;
			if (fclose(f)) result = -1;
		}
	}
	free(d);
	return result;
}
static const char *signed_text(int how)
{
	return how > 0 ? "; its header marked installed and signed for H2Classic's console, as the console's installer does" : how < 0 ? "; *** its ContentMeta.xbx could NOT be signed for H2Classic's console" : "";
}

/* what was found in Roms\DLC */
static unsigned g_packs, g_packs_new, g_updates_other, g_images;
static ULONGLONG g_offers[16];
static char g_u5_dir[MAX_PATH], g_u5_file[MAX_PATH];
static int g_u5_english;

static void list_maps(const char *dir, char *out, size_t size)
{
	WIN32_FIND_DATAA e;
	char pattern[MAX_PATH];
	size_t k = 0;
	HANDLE f;
	out[0] = 0;
	snprintf(pattern, sizeof(pattern), "%s\\*.map", dir);
	f = g_first(pattern, &e);
	if (f == INVALID_HANDLE_VALUE) return;
	do k += (size_t)snprintf(out + k, k < size ? size - k : 0, "%s%s", k ? ", " : "", e.cFileName); while (k < size && g_next(f, &e));
	FindClose(f);
}
static void pack(const char *dir, ULONGLONG offer, const char *name)
{
	char to[MAX_PATH], maps[300];
	LONGLONG copied;
	unsigned i;
	int how = 0;
	for (i = 0; i < g_packs && i < 16; i++) if (g_offers[i] == offer) return;        /* the same pack from another source */
	if (g_packs < 16) g_offers[g_packs] = offer;
	g_packs++;
	snprintf(to, sizeof(to), ".\\h2classic\\T\\$c\\%08lx%08lx", (unsigned long)(offer >> 32), (unsigned long)offer);
	copied = copy_folder(dir, to, NULL);
	list_maps(dir, maps, sizeof(maps));
	if (copied > 0) g_packs_new++;
	if (copied >= 0) { char meta[MAX_PATH]; snprintf(meta, sizeof(meta), "%s\\ContentMeta.xbx", to); how = sign_for_this_console(meta, 1); }
	h2n_log("    T:\\$c\\%08lx%08lx  %s (%s): %s%s%s\n", (unsigned long)(offer >> 32), (unsigned long)offer, name[0] ? name : "a pack", maps,
		copied < 0 ? "NOT installed" : copied > 0 ? "installed from " : "already on H2Classic's hard disk", copied > 0 ? dir : "", signed_text(how));
}
static void update(const char *dir)
{
	WIN32_FIND_DATAA e;
	char pattern[MAX_PATH], path[MAX_PATH];
	HANDLE f;
	snprintf(pattern, sizeof(pattern), "%s\\default.xbe*", dir);
	f = g_first(pattern, &e);
	if (f == INVALID_HANDLE_VALUE) return;
	do
	{
		size_t n = 0;
		unsigned char *x;
		if (e.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
		snprintf(path, sizeof(path), "%s\\%s", dir, e.cFileName);
		x = e.nFileSizeLow == U5_SIZE && !e.nFileSizeHigh ? read_file(path, U5_SIZE, &n) : NULL;
		if (x && n == U5_SIZE)
		{
			DWORD base = *(const DWORD *)(x + 0x104), cert = *(const DWORD *)(x + 0x118) - base;
			if (cert + 0xb0 <= n && *(const DWORD *)(x + cert + 8) == 0x4d530064 && *(const DWORD *)(x + cert + 0xac) == U5_VERSION)
			{
				int english = h2n_crc32(x, n) == U5_ENGLISH_CRC;
				if (!g_u5_dir[0] || (english && !g_u5_english))
				{
					snprintf(g_u5_dir, sizeof(g_u5_dir), "%s", dir);
					snprintf(g_u5_file, sizeof(g_u5_file), "%s", path);
					g_u5_english = english;
				}
			}
			else g_updates_other++;
		}
		else g_updates_other++;
		free(x);
	} while (g_next(f, &e));
	FindClose(f);
}
static void scan(const char *dir, int depth)
{
	WIN32_FIND_DATAA e;
	char pattern[MAX_PATH], path[MAX_PATH];
	HANDLE f;
	if (depth > 8) return;
	snprintf(path, sizeof(path), "%s\\ContentMeta.xbx", dir);
	{
		ULONGLONG offer = 0, size = 0;
		char name[80];
		if (file_size(path, &size) && content_meta(path, &offer, name, sizeof(name)))
		{
			if (offer) pack(dir, offer, name);
			else update(dir);
		}
	}
	snprintf(pattern, sizeof(pattern), "%s\\*", dir);
	f = g_first(pattern, &e);
	if (f == INVALID_HANDLE_VALUE) return;
	do
	{
		size_t n = strlen(e.cFileName);
		if (!(e.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
		{
			if (n > 4 && !_stricmp(e.cFileName + n - 4, ".iso")) { g_images++; h2n_log("    %s\\%s: a disc image - not read yet (its files, unpacked into Roms\\DLC, are)\n", dir, e.cFileName); }
			continue;
		}
		if (!strcmp(e.cFileName, ".") || !strcmp(e.cFileName, "..")) continue;
		snprintf(path, sizeof(path), "%s\\%s", dir, e.cFileName);
		scan(path, depth + 1);
	} while (g_next(f, &e));
	FindClose(f);
}

void h2n_content_install(const char *dlc_folder)
{
	WIN32_FILE_ATTRIBUTE_DATA a;
	functions();
	if (!g_first || !g_next || !g_mkdir || !g_copy || !g_attrs) return;
	if (!g_attrs(dlc_folder, GetFileExInfoStandard, &a) || !(a.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
	{
		h2n_log("map packs: no %s folder (the map packs and the 1.5 title update go there)\n", dlc_folder);
		return;
	}
	h2n_log("map packs: looking through %s; H2Classic's hard disk keeps them as the Xbox's does (h2classic\\T is TDATA\\4d530064)\n", dlc_folder);
	scan(dlc_folder, 0);
	if (g_u5_dir[0])
	{
		char to[] = ".\\h2classic\\T\\$u";
		char dst[MAX_PATH];
		ULONGLONG have = 0;
		LONGLONG copied = copy_folder(g_u5_dir, to, "default.xbe");     /* ContentMeta.xbx and downloader.xbe */
		snprintf(dst, sizeof(dst), "%s\\default.xbe", to);
		if (copied >= 0 && !(file_size(dst, &have) && have == U5_SIZE))
		{
			if (g_copy(g_u5_file, dst, FALSE)) copied += U5_SIZE;
			else { h2n_log("*** map packs: could not copy %s to %s (error %lu)\n", g_u5_file, dst, GetLastError()); copied = -1; }
		}
		int how = copied >= 0 ? sign_for_this_console(".\\h2classic\\T\\$u\\ContentMeta.xbx", 0) : 0;
		h2n_log("    T:\\$u  title update 5 (\"1.5\"%s): %s%s%s\n", g_u5_english ? ", English" : "",
			copied < 0 ? "NOT installed" : copied > 0 ? "installed from " : "already on H2Classic's hard disk", copied > 0 ? g_u5_file : "", signed_text(how));
	}
	h2n_log("map packs: %u pack%s found (%u copied now)%s%s; %u other title update file%s (not 1.5) and %u disc image%s left as they are\n",
		g_packs, g_packs == 1 ? "" : "s", g_packs_new, g_u5_dir[0] ? ", and the 1.5 title update" : "", g_u5_dir[0] ? "" : " (no 1.5 title update)",
		g_updates_other, g_updates_other == 1 ? "" : "s", g_images, g_images == 1 ? "" : "s");
	h2n_log("    v1.0 runs as the \"1.0 TU Skip\" build does on the console: the title update is not started and the packs are not offered to it\n");
}

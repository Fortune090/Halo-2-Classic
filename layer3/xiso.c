/* xiso.c - the game's files read straight out of an Xbox disc image (.iso), so that the exe can be started next to one
 * instead of next to an unpacked game folder.
 *
 * An Xbox disc's file system (XDVDFS): 2048-byte sectors; at sector 32 a volume descriptor that starts and ends with
 * "MICROSOFT*XBOX*MEDIA" and holds the root directory's sector and size. A directory is a binary tree of entries, each:
 *     u16 left (offset of the lower names' entry, in 4-byte units from the directory's start; 0 = none)
 *     u16 right (the same for the higher names)
 *     u32 first sector, u32 size in bytes, u8 attributes (0x10 = a directory), u8 name length, the name
 * An image made from the game partition alone starts with that file system; a dump of the whole disc has the video
 * partition first and the game partition 0x18300000 bytes in (other offsets are known for later disc kinds; they cost
 * nothing to try).
 *
 * The image is only ever opened for reading. A file "opened" from it is a handle on the image itself, placed at the
 * file's first byte; xapi.c's ReadFile, SetFilePointer and GetFileSize keep the game inside the file's own bytes.
 * Windows' own file functions are fetched by name: the layer defines several of them for the game (see xapi.c).
 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "h2n.h"

#define SECTOR 2048u
static char g_iso_path[600];
static HANDLE g_iso = INVALID_HANDLE_VALUE;      /* the layer's own handle, for looking things up */
static ULONGLONG g_iso_base;                     /* where the game partition starts in the image */
static DWORD g_root_sector, g_root_size;
static CRITICAL_SECTION g_iso_lock;
static int g_iso_ready;
struct iso_handle { HANDLE handle; ULONGLONG base; DWORD size; };
static struct iso_handle g_iso_handles[64];

static FARPROC real(const char *name) { return GetProcAddress(GetModuleHandleA("kernel32.dll"), name); }
static HANDLE real_open(const char *path, DWORD flags)
{
	static HANDLE (WINAPI *create)(const char *, DWORD, DWORD, void *, DWORD, DWORD, HANDLE);
	if (!create) create = (void *)real("CreateFileA");
	return create(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, flags, NULL);
}
static int read_at(HANDLE h, ULONGLONG offset, void *buffer, DWORD size)
{
	static BOOL (WINAPI *read)(HANDLE, void *, DWORD, DWORD *, OVERLAPPED *);
	OVERLAPPED where;
	DWORD got = 0;
	if (!read) read = (void *)real("ReadFile");
	memset(&where, 0, sizeof(where));
	where.Offset = (DWORD)offset; where.OffsetHigh = (DWORD)(offset >> 32);
	return read(h, buffer, size, &got, &where) && got == size;
}

int h2n_iso_active(void) { return g_iso != INVALID_HANDLE_VALUE; }
const char *h2n_iso_path(void) { return g_iso_path; }

/* is this file an Xbox disc image? If so it becomes the disc */
int h2n_iso_use(const char *path)
{
	static const ULONGLONG bases[] = { 0, 0x18300000ull, 0x0fd90000ull, 0x02080000ull };
	unsigned char sector[SECTOR];
	HANDLE h = real_open(path, 0);
	unsigned i;
	if (h == INVALID_HANDLE_VALUE) return 0;
	for (i = 0; i < sizeof(bases) / sizeof(bases[0]); i++)
	{
		if (!read_at(h, bases[i] + 32 * SECTOR, sector, SECTOR)) continue;
		if (memcmp(sector, "MICROSOFT*XBOX*MEDIA", 20) || memcmp(sector + SECTOR - 20, "MICROSOFT*XBOX*MEDIA", 20)) continue;
		if (!g_iso_ready) { InitializeCriticalSection(&g_iso_lock); g_iso_ready = 1; }
		if (g_iso != INVALID_HANDLE_VALUE) CloseHandle(g_iso);
		g_iso = h; g_iso_base = bases[i];
		memcpy(&g_root_sector, sector + 20, 4); memcpy(&g_root_size, sector + 24, 4);
		snprintf(g_iso_path, sizeof(g_iso_path), "%s", path);
		return 1;
	}
	CloseHandle(h);
	return 0;
}

/* one name in one directory: walk the tree (every entry is looked at; a directory is a few sectors at most) */
static int find_in_directory(DWORD sector, DWORD size, const char *name, size_t length, DWORD *out_sector, DWORD *out_size, DWORD *out_attributes)
{
	unsigned char *d;
	unsigned stack[512], top = 0, visited = 0;
	int found = 0;
	if (!size || size > 4u * 1024 * 1024) return 0;
	d = malloc(size);
	if (!d) return 0;
	if (!read_at(g_iso, g_iso_base + (ULONGLONG)sector * SECTOR, d, size)) { free(d); return 0; }
	stack[top++] = 0;
	while (top && !found && visited++ < 65536)
	{
		unsigned at = stack[--top], left, right, name_length;
		if (at + 14 > size) continue;
		left = d[at] | (d[at + 1] << 8); right = d[at + 2] | (d[at + 3] << 8);
		if (left == 0xffff && right == 0xffff) continue;                     /* padding: no entry here */
		name_length = d[at + 13];
		if (at + 14 + name_length > size) continue;
		if (name_length == length && !_strnicmp((const char *)d + at + 14, name, length))
		{
			memcpy(out_sector, d + at + 4, 4); memcpy(out_size, d + at + 8, 4); *out_attributes = d[at + 12];
			found = 1;
			break;
		}
		if (left && left != 0xffff && top < 511) stack[top++] = left * 4;
		if (right && right != 0xffff && top < 511) stack[top++] = right * 4;
	}
	free(d);
	return found;
}

/* a path below the disc's root ("maps\\shared.map"; "" is the root itself) */
int h2n_iso_find(const char *rel, ULONGLONG *offset, DWORD *size, DWORD *attributes)
{
	DWORD sector = g_root_sector, bytes = g_root_size, attr = 0x10;
	int ok = 1;
	if (!h2n_iso_active()) return 0;
	EnterCriticalSection(&g_iso_lock);
	while (ok && rel && *rel)
	{
		size_t n;
		while (*rel == '\\' || *rel == '/') rel++;
		if (!*rel) break;
		n = strcspn(rel, "\\/");
		if (!(attr & 0x10)) { ok = 0; break; }                                /* a file cannot hold a name */
		ok = find_in_directory(sector, bytes, rel, n, &sector, &bytes, &attr);
		rel += n;
	}
	LeaveCriticalSection(&g_iso_lock);
	if (!ok) return 0;
	if (offset) *offset = g_iso_base + (ULONGLONG)sector * SECTOR;
	if (size) *size = bytes;
	if (attributes) *attributes = attr;
	return 1;
}

unsigned char *h2n_iso_read(const char *rel, size_t *size)
{
	ULONGLONG offset; DWORD bytes, attr;
	unsigned char *data;
	if (!h2n_iso_find(rel, &offset, &bytes, &attr) || (attr & 0x10)) return NULL;
	data = malloc(bytes ? bytes : 1);
	if (!data) return NULL;
	EnterCriticalSection(&g_iso_lock);
	if (bytes && !read_at(g_iso, offset, data, bytes)) { free(data); data = NULL; }
	LeaveCriticalSection(&g_iso_lock);
	if (data && size) *size = bytes;
	return data;
}

/* the game opens a file of the disc: its own handle on the image, standing at the file's first byte */
HANDLE h2n_iso_open(const char *rel, DWORD flags)
{
	ULONGLONG offset; DWORD bytes, attr;
	LARGE_INTEGER to;
	HANDLE h;
	unsigned i;
	if (!h2n_iso_find(rel, &offset, &bytes, &attr) || (attr & 0x10)) { SetLastError(ERROR_FILE_NOT_FOUND); return INVALID_HANDLE_VALUE; }
	h = real_open(g_iso_path, flags);
	if (h == INVALID_HANDLE_VALUE) return h;
	to.QuadPart = (LONGLONG)offset;
	SetFilePointerEx(h, to, NULL, FILE_BEGIN);
	EnterCriticalSection(&g_iso_lock);
	for (i = 0; i < 64 && g_iso_handles[i].handle && g_iso_handles[i].handle != h; i++) { }
	if (i < 64) { g_iso_handles[i].handle = h; g_iso_handles[i].base = offset; g_iso_handles[i].size = bytes; }
	LeaveCriticalSection(&g_iso_lock);
	if (i == 64) { CloseHandle(h); SetLastError(ERROR_TOO_MANY_OPEN_FILES); return INVALID_HANDLE_VALUE; }
	SetLastError(0);
	return h;
}
int h2n_iso_handle(HANDLE h, ULONGLONG *base, DWORD *size)
{
	unsigned i;
	int found = 0;
	if (!g_iso_ready || !h || h == INVALID_HANDLE_VALUE) return 0;
	EnterCriticalSection(&g_iso_lock);
	for (i = 0; i < 64; i++)
		if (g_iso_handles[i].handle == h) { if (base) *base = g_iso_handles[i].base; if (size) *size = g_iso_handles[i].size; found = 1; break; }
	LeaveCriticalSection(&g_iso_lock);
	return found;
}
/* Windows gave this handle value out again for another file: it is no longer one of the image's */
void h2n_iso_forget(HANDLE h)
{
	unsigned i;
	if (!g_iso_ready) return;
	EnterCriticalSection(&g_iso_lock);
	for (i = 0; i < 64; i++) if (g_iso_handles[i].handle == h) g_iso_handles[i].handle = NULL;
	LeaveCriticalSection(&g_iso_lock);
}

/* the first disc image in a folder that holds a default.xbe */
int h2n_iso_search(const char *folder)
{
	static HANDLE (WINAPI *first)(const char *, WIN32_FIND_DATAA *);
	WIN32_FIND_DATAA data;
	char pattern[600], path[600];
	HANDLE find;
	int found = 0;
	if (!first) first = (void *)real("FindFirstFileA");
	snprintf(pattern, sizeof(pattern), "%s\\*.iso", folder);
	find = first(pattern, &data);
	if (find == INVALID_HANDLE_VALUE) return 0;
	do
	{
		snprintf(path, sizeof(path), "%s\\%s", folder, data.cFileName);
		if (h2n_iso_use(path))
		{
			/* (s10o) Halo 2's own disc only: a map pack's or an installer's disc image (Roms\DLC) has a default.xbe too */
			size_t n = 0;
			unsigned char *xbe = h2n_iso_find("default.xbe", NULL, NULL, NULL) ? h2n_iso_read("default.xbe", &n) : NULL;
			int halo2 = 0;
			if (xbe && n >= 0x200)
			{
				DWORD base = *(const DWORD *)(xbe + 0x104), cert = *(const DWORD *)(xbe + 0x118) - base;
				halo2 = cert + 12 <= n && *(const DWORD *)(xbe + cert + 8) == 0x4d530064;
			}
			free(xbe);
			if (halo2) { found = 1; break; }
			CloseHandle(g_iso); g_iso = INVALID_HANDLE_VALUE;
		}
	} while (FindNextFileA(find, &data));
	FindClose(find);
	return found;
}

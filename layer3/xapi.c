/* H2Classic: memory, console settings and files (the XAPI and kernel calls the game makes directly) */
#include "h2n.h"
void h2n_pad_rumble_stop(void);

/* ---- console settings ---- */
DWORD __stdcall h2n_XGetVideoStandard(void) __asm__("_XGetVideoStandard@0");
DWORD __stdcall h2n_XGetVideoStandard(void) { return 1; }                     /* NTSC-M */
DWORD __stdcall h2n_XGetVideoFlags(void) __asm__("_XGetVideoFlags@0");
int h2n_env_int(const char *name, int otherwise);
/* what the console's dashboard is set to: 4:3 unless H2N_WIDE=1, which is the dashboard's "widescreen" (XC_VIDEO_FLAGS_WIDESCREEN).
   The game then draws its 640x480 for a 16:9 screen by itself (wider view, HUD placed for it), as it does on a console */
/* s9y: the widescreen factor the game's projection multiplies the 4:3 frame's aspect by (function_2fb70, retail's
   "movss xmm5, [4/3]" at 0x2fc88): 4/3 for a 16:9 picture; 16/9 with H2N_ASPECT=21:9 (64:27), as Spooky Ghost v2.0.4
   reads it and the xemu Halo 2 build sets it (it points that instruction at the game's own 16/9, 0x44a070) */
static float g_ws_factor;                 /* (s10u: outside the function, so that Aspect Ratio can change while the game runs) */
#define factor g_ws_factor
float __cdecl h2n_ws_factor(void)
{
	if (factor == 0.0f)
	{
		const char *a = getenv("H2N_ASPECT");
		float w = 0, h = 0;
		factor = 4.0f / 3.0f;
		if (a && (!strcmp(a, "21:9") || !strcmp(a, "64:27"))) factor = 16.0f / 9.0f;      /* as Spooky Ghost and the xemu build: 64:27 */
		else if (a && sscanf(a, "%f:%f", &w, &h) == 2 && w > 0 && h > 0)
		{	/* s9z: any shape up to 32:9 (5120x1440): the picture's width over the 4:3 frame's */
			factor = w / h * 0.75f;
			if (factor < 4.0f / 3.0f) factor = 4.0f / 3.0f;
			if (factor > 8.0f / 3.0f) factor = 8.0f / 3.0f;
		}
	}
	return factor;
}
#undef factor
static int g_video_wide = -1;
DWORD __stdcall h2n_XGetVideoFlags(void) { if (g_video_wide < 0) g_video_wide = h2n_env_int("H2N_WIDE", 0) ? 1 : 0; return (DWORD)g_video_wide; }
/* (s10u) Aspect Ratio changed in H2Classic Settings while the game runs (main.c h2n_live_poll, after H2N_WIDE and
 * H2N_ASPECT were set again). The game asks the console for its video flags once, when it starts (function_123b0), and
 * keeps "widescreen" in one byte (0x485ac2; 1.5: 0x4922e2), which its projection, its split-screen layout, its
 * letterbox and the Widescreen Fix read every frame. That byte is given the new setting, as function_123b0 would have
 * left it had the console been set so at the start; the factor is read again from H2N_ASPECT at its next use. */
void __cdecl h2n_aspect_live(void)
{
	g_ws_factor = 0.0f; g_video_wide = -1;
	*(volatile unsigned char *)(size_t)H2N_VA(0x485AC2, 0x4922e2) = (unsigned char)(h2n_XGetVideoFlags() & 1);
}
DWORD __stdcall h2n_XGetLanguage(void) __asm__("_XGetLanguage@0");
DWORD __stdcall h2n_XGetLanguage(void) { return 1; }                          /* English */
DWORD __stdcall h2n_XGetGameRegion(void) __asm__("_XGetGameRegion@0");
DWORD __stdcall h2n_XGetGameRegion(void) { return 1; }                        /* North America */
DWORD __stdcall h2n_XGetLaunchInfo(DWORD *type, void *data) __asm__("_XGetLaunchInfo@8");
DWORD __stdcall h2n_XGetLaunchInfo(DWORD *type, void *data) { (void)type; (void)data; return 1168; }   /* ERROR_NOT_FOUND: started from the dashboard */
/* XMountSecondaryUtilityDrive (XDK xapilib, retail 0x2d5d0a; the first call of the game's start-up, function_12000):
   picks a second cache partition, records it in the cache table on partition 0, FORMATS IT EVERY TIME
   (_XapiFormatFATVolumeEx at 0x2d5f07 has no condition in front of it), mounts it (_XapiValidateDiskPartitionEx) and
   links it as \??\N:. So on a console N: is empty at every start: the main menu's cache slot n:\cache002.map, the map
   slots cache003-005, savegame0/1.bin and n:\sgf are gone, map_location_get(mainmenu) is "none", and function_18eaa0
   copies the main menu through function_163890, which plays the intro (flags 0x3c6) and holds the main loop and the
   user interface until the movie ends or is skipped. Here N: is the folder .\h2classic\N: emptied the same way at the
   same moment. A link inside it (junction, symbolic link) is removed as a link, never followed. */
static FARPROC real(const char *name);
static unsigned g_format_files;
static ULONGLONG g_format_bytes;
static void format_folder(const char *folder, int depth)
{
	static HANDLE (WINAPI *find_first)(const char *, WIN32_FIND_DATAA *);
	static BOOL (WINAPI *find_next)(HANDLE, WIN32_FIND_DATAA *);
	static BOOL (WINAPI *find_close)(HANDLE);
	static BOOL (WINAPI *delete_file)(const char *);
	static BOOL (WINAPI *remove_directory)(const char *);
	static BOOL (WINAPI *set_attributes)(const char *, DWORD);
	WIN32_FIND_DATAA data;
	char pattern[600], path[600];
	HANDLE search;
	if (!find_first)
	{
		find_first = (void *)real("FindFirstFileA"); find_next = (void *)real("FindNextFileA"); find_close = (void *)real("FindClose");
		delete_file = (void *)real("DeleteFileA"); remove_directory = (void *)real("RemoveDirectoryA");
		set_attributes = (void *)real("SetFileAttributesA");
	}
	if (depth > 16) return;
	snprintf(pattern, sizeof(pattern), "%s\\*", folder);
	search = find_first(pattern, &data);
	if (search == INVALID_HANDLE_VALUE) return;
	do
	{
		if (!strcmp(data.cFileName, ".") || !strcmp(data.cFileName, "..")) continue;
		snprintf(path, sizeof(path), "%s\\%s", folder, data.cFileName);
		if (data.dwFileAttributes & FILE_ATTRIBUTE_READONLY) set_attributes(path, data.dwFileAttributes & ~FILE_ATTRIBUTE_READONLY);
		if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
		{
			if (!(data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) format_folder(path, depth + 1);
			remove_directory(path);
		}
		else if (delete_file(path))
		{
			g_format_files++;
			g_format_bytes += ((ULONGLONG)data.nFileSizeHigh << 32) | data.nFileSizeLow;
		}
	}
	while (find_next(search, &data));
	find_close(search);
}
BOOL __stdcall h2n_XMountSecondaryUtilityDrive(void) __asm__("_XMountSecondaryUtilityDrive@0");
BOOL __stdcall h2n_XMountSecondaryUtilityDrive(void)
{
	DWORD error = GetLastError();
	format_folder(".\\h2classic\\N", 0);
	h2n_log("N: (the second utility drive) formatted, as the console does at every start: %u files (%llu MB) removed\n",
		g_format_files, g_format_bytes >> 20);
	SetLastError(error);
	return TRUE;
}
DWORD __stdcall h2n_XGetAutoLogonFlag(void) __asm__("_XGetAutoLogonFlag@0");
DWORD __stdcall h2n_XGetAutoLogonFlag(void) { return 0; }

/* ---- files: Xbox drive letters become folders. D: (the disc) is the game folder and is never opened for writing;
   every other drive is a folder under .\h2classic\ next to where the exe is started ---- */
static FARPROC real(const char *name) { return GetProcAddress(GetModuleHandleA("kernel32.dll"), name); }
static int g_file_lines;
int h2n_xbox_path(const char *in, char *out, size_t size)   /* returns 1 when the path is on the disc */
{
	if (in && in[0] && in[1] == ':')
	{
		char drive = (char)(in[0] & ~0x20);
		const char *rest = in + 2;
		while (*rest == '\\' || *rest == '/') rest++;
		if (drive == 'D') { snprintf(out, size, "%s\\%s", g_disc_root, rest); return 1; }
		snprintf(out, size, ".\\h2classic\\%c\\%s", drive, rest);
		return 0;
	}
	snprintf(out, size, "%s", in ? in : "");
	return 0;
}
static const char *disc_relative(const char *in)      /* "d:\\maps\\x.map" -> "maps\\x.map" */
{
	const char *rest = in + 2;
	while (*rest == '\\' || *rest == '/') rest++;
	return rest;
}
/* ---- what the game does with each file it has open: totals for the periodic report, and every failed read or
   write as it happens (a map that does not load shows up here first) ---- */
struct open_file { HANDLE handle; char name[80]; unsigned reads, writes, failures; ULONGLONG read_bytes, written_bytes; int reported; };
static struct open_file g_files[96];
static CRITICAL_SECTION g_files_lock;
static LONG g_files_ready, g_io_lines;
static void files_lock(void)
{
	if (InterlockedCompareExchange(&g_files_ready, 1, 0) == 0) { InitializeCriticalSection(&g_files_lock); InterlockedExchange(&g_files_ready, 2); }
	while (g_files_ready != 2) Sleep(0);
	EnterCriticalSection(&g_files_lock);
}
static void file_opened(HANDLE h, const char *name)
{
	struct open_file *slot = NULL;
	files_lock();
	for (int i = 0; i < 96; i++) if (g_files[i].handle == h) { slot = &g_files[i]; break; }       /* the handle value is in use again */
	if (!slot) for (int i = 0; i < 96; i++) if (!g_files[i].handle) { slot = &g_files[i]; break; }
	if (!slot) for (int i = 0; i < 96; i++) if (!g_files[i].reads && !g_files[i].writes) { slot = &g_files[i]; break; }
	if (slot) { memset(slot, 0, sizeof(*slot)); slot->handle = h; snprintf(slot->name, sizeof(slot->name), "%s", name); }
	LeaveCriticalSection(&g_files_lock);
}
static void file_used(HANDLE h, int write, DWORD size, const OVERLAPPED *where, BOOL ok, DWORD error)
{
	char name[80] = "(a handle this layer did not open)";
	int failed = !ok && error != ERROR_IO_PENDING && error != ERROR_HANDLE_EOF;
	h2n_note_activity();
	files_lock();
	for (int i = 0; i < 96; i++)
		if (g_files[i].handle == h)
		{
			struct open_file *f = &g_files[i];
			if (write) { f->writes++; f->written_bytes += size; } else { f->reads++; f->read_bytes += size; }
			f->failures += failed; f->reported = 0;
			snprintf(name, sizeof(name), "%s", f->name);
			break;
		}
	LeaveCriticalSection(&g_files_lock);
	if (failed && InterlockedIncrement(&g_io_lines) <= 60)
		h2n_log("       file: %s of %lu bytes at offset %08lx%08lx FAILED, error %lu: %s\n", write ? "write" : "read", size,
			where ? where->OffsetHigh : 0, where ? where->Offset : 0, error, name);
}
/* one line per file that was read or written since the last report */
void h2n_files_report(void)
{
	char lines[2048];
	size_t used = 0;
	lines[0] = 0;
	files_lock();
	for (int i = 0; i < 96 && used + 200 < sizeof(lines); i++)
	{
		struct open_file *f = &g_files[i];
		if (!f->handle || f->reported || (!f->reads && !f->writes)) continue;
		f->reported = 1;
		used += (size_t)snprintf(lines + used, sizeof(lines) - used, "       files so far: %s: %u reads (%.1f MB), %u writes (%.1f MB)%s\n", f->name,
			f->reads, f->read_bytes / 1048576.0, f->writes, f->written_bytes / 1048576.0, f->failures ? ", with failures" : "");
	}
	LeaveCriticalSection(&g_files_lock);
	if (used) h2n_log("%s", lines);
}
BOOL __stdcall h2n_ReadFile(HANDLE h, void *buffer, DWORD size, DWORD *got, OVERLAPPED *where) __asm__("_ReadFile@20");
BOOL __stdcall h2n_ReadFile(HANDLE h, void *buffer, DWORD size, DWORD *got, OVERLAPPED *where)
{
	static BOOL (WINAPI *read)(HANDLE, void *, DWORD, DWORD *, OVERLAPPED *);
	BOOL ok;
	DWORD error;
	if (!read) read = (void *)real("ReadFile");
	{
		/* a file inside a disc image: the handle is on the image, and the game must stay inside the file's own bytes */
		ULONGLONG base; DWORD file_size;
		if (h2n_iso_handle(h, &base, &file_size))
		{
			ULONGLONG position;
			if (where) position = ((ULONGLONG)where->OffsetHigh << 32) | where->Offset;
			else { LARGE_INTEGER zero, now; zero.QuadPart = 0; now.QuadPart = 0; SetFilePointerEx(h, zero, &now, FILE_CURRENT); position = (ULONGLONG)now.QuadPart - base; }
			if (position >= file_size)
			{
				if (got) *got = 0;
				file_used(h, 0, size, where, where ? FALSE : TRUE, where ? ERROR_HANDLE_EOF : 0);
				if (where) { SetLastError(ERROR_HANDLE_EOF); return FALSE; }
				return TRUE;
			}
			if (size > file_size - position) size = (DWORD)(file_size - position);
			if (where)
			{
				DWORD low = where->Offset, high = where->OffsetHigh;
				ULONGLONG in_image = base + position;
				where->Offset = (DWORD)in_image; where->OffsetHigh = (DWORD)(in_image >> 32);
				ok = read(h, buffer, size, got, where);
				error = ok ? 0 : GetLastError();
				where->Offset = low; where->OffsetHigh = high;
				file_used(h, 0, size, where, ok, error);
				if (!ok) SetLastError(error);
				return ok;
			}
		}
	}
	ok = read(h, buffer, size, got, where);
	error = ok ? 0 : GetLastError();
	file_used(h, 0, size, where, ok, error);
	if (!ok) SetLastError(error);
	return ok;
}
BOOL __stdcall h2n_WriteFile(HANDLE h, const void *buffer, DWORD size, DWORD *put, OVERLAPPED *where) __asm__("_WriteFile@20");
BOOL __stdcall h2n_WriteFile(HANDLE h, const void *buffer, DWORD size, DWORD *put, OVERLAPPED *where)
{
	static BOOL (WINAPI *write)(HANDLE, const void *, DWORD, DWORD *, OVERLAPPED *);
	BOOL ok;
	DWORD error;
	if (!write) write = (void *)real("WriteFile");
	ok = write(h, buffer, size, put, where);
	error = ok ? 0 : GetLastError();
	file_used(h, 1, size, where, ok, error);
	if (!ok) SetLastError(error);
	return ok;
}

/* the two other calls the game makes on an open file that depend on where a disc image's file starts and ends */
DWORD __stdcall h2n_SetFilePointer(HANDLE h, LONG distance, LONG *distance_high, DWORD method) __asm__("_SetFilePointer@16");
DWORD __stdcall h2n_SetFilePointer(HANDLE h, LONG distance, LONG *distance_high, DWORD method)
{
	ULONGLONG base; DWORD file_size;
	LARGE_INTEGER to, now;
	to.QuadPart = distance_high ? (LONGLONG)(((ULONGLONG)(DWORD)*distance_high << 32) | (DWORD)distance) : (LONGLONG)distance;
	if (h2n_iso_handle(h, &base, &file_size))
	{
		if (method == FILE_BEGIN) to.QuadPart += (LONGLONG)base;
		else if (method == FILE_END) { to.QuadPart += (LONGLONG)(base + file_size); method = FILE_BEGIN; }
		if (!SetFilePointerEx(h, to, &now, method)) return INVALID_SET_FILE_POINTER;
		now.QuadPart -= (LONGLONG)base;
	}
	else if (!SetFilePointerEx(h, to, &now, method)) return INVALID_SET_FILE_POINTER;
	if (distance_high) *distance_high = now.HighPart;
	SetLastError(0);                      /* a low half of 0xffffffff is a failure only with an error set */
	return now.LowPart;
}
DWORD __stdcall h2n_GetFileSize(HANDLE h, DWORD *size_high) __asm__("_GetFileSize@8");
DWORD __stdcall h2n_GetFileSize(HANDLE h, DWORD *size_high)
{
	ULONGLONG base; DWORD file_size;
	LARGE_INTEGER size;
	if (h2n_iso_handle(h, &base, &file_size)) { if (size_high) *size_high = 0; return file_size; }
	if (!GetFileSizeEx(h, &size)) return INVALID_FILE_SIZE;
	if (size_high) *size_high = (DWORD)size.HighPart;
	SetLastError(0);
	return size.LowPart;
}

static void make_parents(char *path)
{
	for (char *p = path + 2; *p; p++)
		if (*p == '\\') { *p = 0; CreateDirectoryA(path, NULL); *p = '\\'; }
}
HANDLE __stdcall h2n_CreateFileA(const char *name, DWORD access, DWORD share, void *security, DWORD disposition, DWORD flags, HANDLE model)
	__asm__("_CreateFileA@28");
HANDLE __stdcall h2n_CreateFileA(const char *name, DWORD access, DWORD share, void *security, DWORD disposition, DWORD flags, HANDLE model)
{
	static HANDLE (WINAPI *create)(const char *, DWORD, DWORD, void *, DWORD, DWORD, HANDLE);
	char path[600];
	int disc = h2n_xbox_path(name, path, sizeof(path));
	HANDLE h;
	if (!create) create = (void *)real("CreateFileA");
	if (disc && ((access & (GENERIC_WRITE | GENERIC_ALL)) || disposition != OPEN_EXISTING))
	{
		if (g_file_lines++ < 600) h2n_log("       file: %s  REFUSED (the game's own files are never opened for writing)\n", name);
		SetLastError(ERROR_ACCESS_DENIED);
		return INVALID_HANDLE_VALUE;
	}
	if (!disc && disposition != OPEN_EXISTING) make_parents(path);
	if (disc && h2n_iso_active())
	{
		h = h2n_iso_open(disc_relative(name), flags & ~0x20000000u);
		if (h != INVALID_HANDLE_VALUE) file_opened(h, name);
		{
			DWORD error = GetLastError();
			if (g_file_lines++ < 600) h2n_log("       file: %s -> inside %s  %s\n", name, h2n_iso_path(), h == INVALID_HANDLE_VALUE ? "NOT FOUND" : "opened");
			SetLastError(error);
		}
		return h;
	}
	h = create(path, access, share | FILE_SHARE_READ, security, disposition, flags & ~0x20000000u /* no unbuffered I/O rules */, model);
	if (h != INVALID_HANDLE_VALUE) h2n_iso_forget(h);                 /* this handle value may have been a disc image's file before */
	if (h != INVALID_HANDLE_VALUE && !disc && (access & GENERIC_WRITE))
	{
		/* the game sets its map cache files to their full size (1.4 GB in all) before filling them: as sparse files
		   they take only what is really written */
		DWORD returned;
		DeviceIoControl(h, 0x000900c4 /* FSCTL_SET_SPARSE */, NULL, 0, NULL, 0, &returned, NULL);
	}
	if (h != INVALID_HANDLE_VALUE) file_opened(h, name);
	{
		DWORD error = GetLastError();
		if (g_file_lines++ < 600) h2n_log("       file: %s -> %s  %s\n", name, path, h == INVALID_HANDLE_VALUE ? "NOT FOUND" : "opened");
		SetLastError(error);              /* the game looks at it (ERROR_ALREADY_EXISTS after "open or create") */
	}
	return h;
}
DWORD __stdcall h2n_GetFileAttributesA(const char *name) __asm__("_GetFileAttributesA@4");
DWORD __stdcall h2n_GetFileAttributesA(const char *name)
{
	static DWORD (WINAPI *get)(const char *);
	char path[600];
	if (!get) get = (void *)real("GetFileAttributesA");
	if (h2n_xbox_path(name, path, sizeof(path)) && h2n_iso_active())
	{
		DWORD attributes;
		if (!h2n_iso_find(disc_relative(name), NULL, NULL, &attributes)) { SetLastError(ERROR_FILE_NOT_FOUND); return INVALID_FILE_ATTRIBUTES; }
		return (attributes & 0x10 ? FILE_ATTRIBUTE_DIRECTORY : 0) | FILE_ATTRIBUTE_READONLY;
	}
	return get(path);
}
BOOL __stdcall h2n_GetFileAttributesExA(const char *name, int level, void *data) __asm__("_GetFileAttributesExA@12");
BOOL __stdcall h2n_GetFileAttributesExA(const char *name, int level, void *data)
{
	static BOOL (WINAPI *get)(const char *, int, void *);
	char path[600];
	if (!get) get = (void *)real("GetFileAttributesExA");
	if (h2n_xbox_path(name, path, sizeof(path)) && h2n_iso_active())
	{
		WIN32_FILE_ATTRIBUTE_DATA *out = data;
		DWORD attributes, size;
		if (level != 0 || !h2n_iso_find(disc_relative(name), NULL, &size, &attributes)) { SetLastError(ERROR_FILE_NOT_FOUND); return FALSE; }
		memset(out, 0, sizeof(*out));
		out->dwFileAttributes = (attributes & 0x10 ? FILE_ATTRIBUTE_DIRECTORY : 0) | FILE_ATTRIBUTE_READONLY;
		out->nFileSizeLow = attributes & 0x10 ? 0 : size;
		return TRUE;
	}
	return get(path, level, data);
}
HANDLE __stdcall h2n_FindFirstFileA(const char *name, void *data) __asm__("_FindFirstFileA@8");
HANDLE __stdcall h2n_FindFirstFileA(const char *name, void *data)
{
	static HANDLE (WINAPI *find)(const char *, void *);
	char path[600];
	if (!find) find = (void *)real("FindFirstFileA");
	if (h2n_xbox_path(name, path, sizeof(path)) && h2n_iso_active())
	{
		/* the game is not known to list the disc; if it ever does, this line says what it asked for */
		static int told;
		if (!told++) h2n_log("*** file: the game asked for a list of files on the disc (%s); with a disc image that is not answered yet\n", name);
		SetLastError(ERROR_FILE_NOT_FOUND);
		return INVALID_HANDLE_VALUE;
	}
	return find(path, data);
}
BOOL __stdcall h2n_CreateDirectoryA(const char *name, void *security) __asm__("_CreateDirectoryA@8");
BOOL __stdcall h2n_CreateDirectoryA(const char *name, void *security)
{
	static BOOL (WINAPI *create)(const char *, void *);
	char path[600];
	if (!create) create = (void *)real("CreateDirectoryA");
	if (h2n_xbox_path(name, path, sizeof(path))) { SetLastError(ERROR_ACCESS_DENIED); return FALSE; }
	make_parents(path);
	return create(path, security);
}
BOOL __stdcall h2n_DeleteFileA(const char *name) __asm__("_DeleteFileA@4");
BOOL __stdcall h2n_DeleteFileA(const char *name)
{
	static BOOL (WINAPI *delete_file)(const char *);
	char path[600];
	if (!delete_file) delete_file = (void *)real("DeleteFileA");
	if (h2n_xbox_path(name, path, sizeof(path))) { SetLastError(ERROR_ACCESS_DENIED); return FALSE; }
	return delete_file(path);
}
BOOL __stdcall h2n_RemoveDirectoryA(const char *name) __asm__("_RemoveDirectoryA@4");
BOOL __stdcall h2n_RemoveDirectoryA(const char *name)
{
	static BOOL (WINAPI *remove_directory)(const char *);
	char path[600];
	if (!remove_directory) remove_directory = (void *)real("RemoveDirectoryA");
	if (h2n_xbox_path(name, path, sizeof(path))) { SetLastError(ERROR_ACCESS_DENIED); return FALSE; }
	return remove_directory(path);
}
BOOL __stdcall h2n_SetFileAttributesA(const char *name, DWORD attributes) __asm__("_SetFileAttributesA@8");
BOOL __stdcall h2n_SetFileAttributesA(const char *name, DWORD attributes)
{
	static BOOL (WINAPI *set)(const char *, DWORD);
	char path[600];
	if (!set) set = (void *)real("SetFileAttributesA");
	if (h2n_xbox_path(name, path, sizeof(path))) { SetLastError(ERROR_ACCESS_DENIED); return FALSE; }
	return set(path, attributes);
}
BOOL __stdcall h2n_GetDiskFreeSpaceExA(const char *name, void *a, void *b, void *c) __asm__("_GetDiskFreeSpaceExA@16");
BOOL __stdcall h2n_GetDiskFreeSpaceExA(const char *name, void *a, void *b, void *c)
{
	static BOOL (WINAPI *get)(const char *, void *, void *, void *);
	(void)name;
	if (!get) get = (void *)real("GetDiskFreeSpaceExA");
	return get(".\\", a, b, c);
}
/* ---- the C library's fopen: the game (Havok's file streams, the launch file) names Xbox paths here too ---- */
FILE *h2n_real_fopen(const char *name, const char *mode)        /* for the layer's own files: no drive translation */
{
	static FILE *(__cdecl *open_file)(const char *, const char *);
	if (!open_file)
	{
		HMODULE crt = GetModuleHandleA("ucrtbase.dll");
		if (!crt) crt = GetModuleHandleA("api-ms-win-crt-stdio-l1-1-0.dll");
		if (!crt) crt = GetModuleHandleA("msvcrt.dll");
		open_file = crt ? (void *)GetProcAddress(crt, "fopen") : NULL;
		if (!open_file) h2n_fatal("the C library's fopen was not found");
	}
	return open_file(name, mode);
}
FILE *__cdecl h2n_fopen(const char *name, const char *mode) __asm__("_fopen");
FILE *__cdecl h2n_fopen(const char *name, const char *mode)
{
	char path[600];
	int disc = h2n_xbox_path(name, path, sizeof(path));
	int writes = mode && strpbrk(mode, "wa+") != NULL;
	FILE *f;
	if (disc && writes)
	{
		if (g_file_lines++ < 600) h2n_log("       file: %s  REFUSED (the game's own files are never opened for writing)\n", name);
		return NULL;
	}
	if (!disc && writes && name && name[0] && name[1] == ':') make_parents(path);
	if (disc && h2n_iso_active())
	{
		/* the C library needs a real file: a (small) file of the disc image is copied out under h2classic\D first */
		size_t size = 0;
		unsigned char *bytes = h2n_iso_read(disc_relative(name), &size);
		f = NULL;
		if (bytes)
		{
			FILE *copy;
			snprintf(path, sizeof(path), ".\\h2classic\\D\\%s", disc_relative(name));
			make_parents(path);
			copy = h2n_real_fopen(path, "wb");
			if (copy) { fwrite(bytes, 1, size, copy); fclose(copy); f = h2n_real_fopen(path, mode); }
			free(bytes);
		}
		if (g_file_lines++ < 600) h2n_log("       file: %s -> inside %s  %s (fopen \"%s\")\n", name, h2n_iso_path(), f ? "opened (a copy under h2classic\\D)" : "NOT FOUND", mode ? mode : "");
		return f;
	}
	f = h2n_real_fopen(path, mode);
	if (g_file_lines++ < 600) h2n_log("       file: %s -> %s  %s (fopen \"%s\")\n", name, path, f ? "opened" : "NOT FOUND", mode ? mode : "");
	return f;
}

/* ---- virtual memory: the console's is all below 2 GB. MEM_TOP_DOWN, which the game asks for nearly everywhere,
   would hand out addresses just under 4 GB in a large-address program, where the console keeps its physical
   memory views, so it is dropped ---- */
void *h2n_real_VirtualAlloc(void *address, SIZE_T size, DWORD type, DWORD protect)
{
	static void *(WINAPI *allocate)(void *, SIZE_T, DWORD, DWORD);
	if (!allocate) allocate = (void *)real("VirtualAlloc");
	return allocate(address, size, type, protect);
}
void *__stdcall h2n_VirtualAlloc(void *address, SIZE_T size, DWORD type, DWORD protect) __asm__("_VirtualAlloc@16");
void *__stdcall h2n_VirtualAlloc(void *address, SIZE_T size, DWORD type, DWORD protect)
{
	static LONG warned;
	void *p = h2n_real_VirtualAlloc(address, size, type & ~(DWORD)MEM_TOP_DOWN, protect);
	if (!p) h2n_log("[%4u] VirtualAlloc(%p, %lu bytes, type %lx) FAILED, error %lu\n", h2n_next_sequence(), address, (unsigned long)size, type, GetLastError());
	else if ((ULONG_PTR)p >= 0x80000000u && InterlockedExchange(&warned, 1) == 0)
		h2n_log("[%4u] note: VirtualAlloc returned %p, above 2 GB (the console never does)\n", h2n_next_sequence(), p);
	return p;
}

/* ---- what the game prints for a debugger goes to the log ---- */
void __stdcall h2n_OutputDebugStringA(const char *text) __asm__("_OutputDebugStringA@4");
void __stdcall h2n_OutputDebugStringA(const char *text)
{
	static LONG lines;
	size_t n;
	if (!text || InterlockedIncrement(&lines) > 400) return;
	n = strlen(text);
	while (n && (text[n - 1] == '\n' || text[n - 1] == '\r')) n--;
	h2n_log("       debug text: %.*s\n", (int)(n > 500 ? 500 : n), text);
}

/* ---- leaving the game for another program (the dashboard, the demos, the downloader): on the console the call
   does not come back, and the game spins after it ---- */
DWORD __stdcall h2n_XLaunchNewImageA(const char *image, void *launch_data) __asm__("_XLaunchNewImageA@8");
DWORD __stdcall h2n_XLaunchNewImageA(const char *image, void *launch_data)
{
	const DWORD *d = (const DWORD *)launch_data;
	h2n_log("\n=== the game asked the console to start %s", image ? image : "the dashboard");
	if (!image && d && !IsBadReadPtr(d, 16)) h2n_log(" (reason %lu, context %lx, parameters %lx %lx)", d[0], d[1], d[2], d[3]);
	h2n_log(": on a console the game ends here, so this build stops ===\n");
	h2n_pad_rumble_stop();
	ExitProcess(0);
	return 0;
}

/* ---- small console facts ---- */
DWORD __stdcall h2n_XGetDiskClusterSizeA(const char *root) __asm__("_XGetDiskClusterSizeA@4");
DWORD __stdcall h2n_XGetDiskClusterSizeA(const char *root) { (void)root; return 16384; }      /* the hard disk's */

/* ---- network bits that have an answer without a network: random bytes, and "no address" ---- */
int __stdcall h2n_XNetRandom(BYTE *buffer, UINT count) __asm__("_h2n_pc_XNetRandom@8");
int __stdcall h2n_XNetRandom(BYTE *buffer, UINT count)
{
	static BOOLEAN (WINAPI *generate)(void *, ULONG);
	static int looked;
	if (!looked)
	{
		HMODULE advapi = LoadLibraryA("advapi32.dll");
		generate = advapi ? (void *)GetProcAddress(advapi, "SystemFunction036") : NULL;       /* RtlGenRandom */
		looked = 1;
	}
	if (!generate || !generate(buffer, count))
	{
		LARGE_INTEGER t;
		QueryPerformanceCounter(&t);
		for (UINT i = 0; i < count; i++) { t.QuadPart = t.QuadPart * 6364136223846793005ll + 1442695040888963407ll; buffer[i] = (BYTE)(t.QuadPart >> 33); }
	}
	return 0;
}
/* XNetGetTitleXnAddr and the rest of XNet: xnet.c (s9t) */

void __stdcall h2n_XMemFreeDefault(void *address, DWORD attributes) __asm__("_XMemFreeDefault@8");
void __cdecl h2n_free(void *address);
void __stdcall h2n_XMemFreeDefault(void *address, DWORD attributes) { (void)attributes; h2n_free(address); }

/* ---- device type tables the input code names (their contents are the kernel's; only their addresses are used) ---- */
DWORD XDEVICE_TYPE_GAMEPAD_TABLE[4], XDEVICE_TYPE_MEMORY_UNIT_TABLE[4];
DWORD XDEVICE_TYPE_VOICE_HEADPHONE_TABLE[4], XDEVICE_TYPE_VOICE_MICROPHONE_TABLE[4];
DWORD g_dwDirectSoundDeltaPanicCount;

/* ---- signatures: the game signs its saved files and checks them when it reads them back. That is in xsign.c: the
   console's own method, with the title's keys made the way the console's kernel makes them. ---- */

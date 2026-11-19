/* H2Classic: the console's saved games (which is how Halo 2 keeps its profiles), compiled with the Xbox SDK's compiler
 * and headers so that the structures the game reads are the SDK's own.
 *
 * As on the console, a saved game is a folder in the root of its drive, named with twelve hex digits and holding
 * SaveMeta.xbx, a UTF-16 text file (byte order mark first) whose "Name=" line is the saved game's name. The drive
 * itself is a folder under .\h2classic\ : the file calls made here are the game's own (CreateFileA, FindFirstFileA,
 * ...), which the layer maps there, so nothing in this file knows a Windows path.
 *
 * Downloaded content (the map packs) is reported as "none" for now, and Xbox Live as having no accounts. */
#include <xtl.h>
#include <xonline.h>
#include <stddef.h>
extern "C" int h2n_v15;                 /* (s10r) main.c: 0 = game version 1.0, 1 = 1.5 */

extern "C" {
void __cdecl h2n_log(const char *format, ...);
unsigned __cdecl h2n_next_sequence(void);
void *__cdecl h2n_calloc(unsigned size);
void __cdecl h2n_free(void *address);
}

#define FIND_MAGIC 0x646e6946u          /* "Find" */
struct save_find { DWORD magic; HANDLE find; char root[4]; };
static unsigned g_lines;

/* ---- small text helpers (no C library needed) ---- */
static unsigned text_copy(char *to, unsigned size, const char *from)
{
	unsigned n = 0;
	if (!size) return 0;
	while (from[n] && n + 1 < size) { to[n] = from[n]; n++; }
	to[n] = 0;
	return n;
}
static WCHAR fold(WCHAR c) { return (c >= 'A' && c <= 'Z') ? (WCHAR)(c + 32) : c; }
static bool wide_same(const WCHAR *a, const WCHAR *b)
{
	while (*a && fold(*a) == fold(*b)) { a++; b++; }
	return fold(*a) == fold(*b);
}
static unsigned wide_length(const WCHAR *s) { unsigned n = 0; while (s[n]) n++; return n; }
static const char *plain(const WCHAR *name, char *out, unsigned size)     /* for the log */
{
	unsigned n = 0;
	while (name[n] && n + 1 < size) { out[n] = (name[n] >= 32 && name[n] < 127) ? (char)name[n] : '?'; n++; }
	out[n] = 0;
	return out;
}
/* the root of a drive that can hold saved games: "u:\" (never the disc) */
static bool root_take(const char *path, char *root)
{
	char letter;
	if (!path) return false;
	letter = (char)(path[0] | 32);
	if (letter < 'a' || letter > 'z' || letter == 'd' || path[1] != ':') return false;
	if (path[2] && !((path[2] == '\\' || path[2] == '/') && !path[3])) return false;
	root[0] = path[0]; root[1] = ':'; root[2] = '\\'; root[3] = 0;
	return true;
}

/* ---- SaveMeta.xbx ---- */
static bool meta_read(const char *directory, WCHAR *name)
{
	char path[MAX_PATH];
	WCHAR text[512];
	DWORD got = 0;
	unsigned n = text_copy(path, sizeof(path), directory), count, i = 0;
	BOOL ok;
	HANDLE f;
	text_copy(path + n, sizeof(path) - n, "SaveMeta.xbx");
	f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (f == INVALID_HANDLE_VALUE) return false;
	ok = ReadFile(f, text, sizeof(text), &got, NULL);
	CloseHandle(f);
	if (!ok) return false;
	count = got / 2;
	if (count && text[0] == 0xfeff) i = 1;
	while (i < count)
	{
		if (i + 5 <= count && text[i] == 'N' && text[i + 1] == 'a' && text[i + 2] == 'm' && text[i + 3] == 'e' && text[i + 4] == '=')
		{
			unsigned k = 0;
			for (i += 5; i < count && text[i] && text[i] != '\r' && text[i] != '\n' && k < MAX_GAMENAME - 1; i++) name[k++] = text[i];
			name[k] = 0;
			return true;
		}
		while (i < count && text[i] != '\n') i++;
		i++;
	}
	return false;
}
static bool meta_write(const char *directory, const WCHAR *name, DWORD flags)
{
	static const WCHAR key[] = L"Name=", no_copy[] = L"NoCopy=1\r\n";
	char path[MAX_PATH];
	WCHAR text[1 + 5 + MAX_GAMENAME + 2 + 10];
	unsigned n = text_copy(path, sizeof(path), directory), k = 0, i;
	DWORD written = 0;
	BOOL ok;
	HANDLE f;
	text_copy(path + n, sizeof(path) - n, "SaveMeta.xbx");
	/* s10n: the console's file starts with the UTF-16 byte order mark (a SaveMeta.xbx made by a console's XAPI does), and
	   the game's own XAPI - the one a console or xemu runs - lists no saved game whose file lacks it (checked on xemu: a
	   profile made here showed in its list only with the mark) */
	text[k++] = 0xfeff;
	for (i = 0; i < 5; i++) text[k++] = key[i];
	for (i = 0; name[i] && i < MAX_GAMENAME - 1; i++) text[k++] = name[i];
	text[k++] = '\r'; text[k++] = '\n';
	if (flags & XSAVEGAME_NOCOPY) for (i = 0; i < 10; i++) text[k++] = no_copy[i];
	f = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (f == INVALID_HANDLE_VALUE) return false;
	ok = WriteFile(f, text, k * sizeof(WCHAR), &written, NULL);
	CloseHandle(f);
	return ok && written == k * sizeof(WCHAR);
}

/* ---- walking a drive's saved games ---- */
static bool find_take(const save_find *f, const WIN32_FIND_DATAA *entry, XGAME_FIND_DATA *out)
{
	char directory[MAX_PATH];
	WCHAR name[MAX_GAMENAME];
	unsigned n, i;
	if (!(entry->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || entry->cFileName[0] == '.') return false;
	n = text_copy(directory, sizeof(directory), f->root);
	n += text_copy(directory + n, sizeof(directory) - n, entry->cFileName);
	text_copy(directory + n, sizeof(directory) - n, "\\");
	if (!meta_read(directory, name)) return false;
	out->wfd = *entry;
	text_copy(out->szSaveGameDirectory, sizeof(out->szSaveGameDirectory), directory);
	for (i = 0; name[i]; i++) out->szSaveGameName[i] = name[i];
	out->szSaveGameName[i] = 0;
	return true;
}
static save_find *find_open(const char *root, XGAME_FIND_DATA *out)
{
	char pattern[8];
	WIN32_FIND_DATAA entry;
	save_find *f = (save_find *)h2n_calloc(sizeof(save_find));
	f->magic = FIND_MAGIC;
	text_copy(f->root, sizeof(f->root), root);
	pattern[0] = root[0]; pattern[1] = ':'; pattern[2] = '\\'; pattern[3] = '*'; pattern[4] = 0;
	f->find = FindFirstFileA(pattern, &entry);
	if (f->find != INVALID_HANDLE_VALUE)
	{
		do
			if (find_take(f, &entry, out)) return f;
		while (FindNextFileA(f->find, &entry));
		FindClose(f->find);
	}
	h2n_free(f);
	return NULL;
}
static bool find_next(save_find *f, XGAME_FIND_DATA *out)
{
	WIN32_FIND_DATAA entry;
	while (FindNextFileA(f->find, &entry))
		if (find_take(f, &entry, out)) return true;
	return false;
}
static void find_close(save_find *f)
{
	FindClose(f->find);
	f->magic = 0;
	h2n_free(f);
}
/* the folder of the saved game with this name, if the drive has one */
static bool save_lookup(const char *root, const WCHAR *name, char *directory)
{
	XGAME_FIND_DATA data;
	bool found = false;
	save_find *f = find_open(root, &data);
	if (!f) return false;
	do
		if (wide_same(data.szSaveGameName, name)) { text_copy(directory, MAX_PATH, data.szSaveGameDirectory); found = true; break; }
	while (find_next(f, &data));
	find_close(f);
	return found;
}

extern "C" {

HANDLE WINAPI XFindFirstSaveGame(LPCSTR lpRootPathName, PXGAME_FIND_DATA pFindGameData)
{
	char root[4], shown[64];
	save_find *f;
	if (!root_take(lpRootPathName, root) || !pFindGameData) { SetLastError(ERROR_PATH_NOT_FOUND); return INVALID_HANDLE_VALUE; }
	f = find_open(root, pFindGameData);
	if (g_lines < 12)
	{
		g_lines++;
		if (f) h2n_log("[%4u] saved games on %s: the first is \"%s\" in %s\n", h2n_next_sequence(), root, plain(pFindGameData->szSaveGameName, shown, sizeof(shown)), pFindGameData->szSaveGameDirectory);
		else h2n_log("[%4u] saved games on %s: none\n", h2n_next_sequence(), root);
	}
	if (!f) { SetLastError(ERROR_FILE_NOT_FOUND); return INVALID_HANDLE_VALUE; }
	return (HANDLE)f;
}

BOOL WINAPI XFindNextSaveGame(HANDLE hFindGame, PXGAME_FIND_DATA pFindGameData)
{
	save_find *f = (save_find *)hFindGame;
	if (!f || hFindGame == INVALID_HANDLE_VALUE || f->magic != FIND_MAGIC || !pFindGameData) { SetLastError(ERROR_INVALID_HANDLE); return FALSE; }
	if (find_next(f, pFindGameData)) return TRUE;
	SetLastError(ERROR_NO_MORE_FILES);
	return FALSE;
}

BOOL WINAPI XFindClose(HANDLE hFind)
{
	save_find *f = (save_find *)hFind;
	if (f && hFind != INVALID_HANDLE_VALUE && *(DWORD *)hFind == 0x434f4e54u)        /* a content search (XFindFirstContent) */
	{
		HANDLE find = ((HANDLE *)hFind)[1];
		*(DWORD *)hFind = 0;
		FindClose(find);
		h2n_free(hFind);
		return TRUE;
	}
	if (!f || hFind == INVALID_HANDLE_VALUE || f->magic != FIND_MAGIC) { SetLastError(ERROR_INVALID_HANDLE); return FALSE; }
	find_close(f);
	return TRUE;
}

DWORD WINAPI XCreateSaveGame(LPCSTR lpRootPathName, LPCWSTR lpSaveGameName, DWORD dwCreationDisposition, DWORD dwCreateFlags, LPSTR lpPathBuffer, UINT uSize)
{
	static const char digits[] = "0123456789ABCDEF";
	char root[4], directory[MAX_PATH], shown[64];
	bool exists;
	DWORD result = ERROR_SUCCESS;
	if (!root_take(lpRootPathName, root) || !lpSaveGameName || !lpSaveGameName[0] || wide_length(lpSaveGameName) >= MAX_GAMENAME)
		return ERROR_INVALID_PARAMETER;
	exists = save_lookup(root, lpSaveGameName, directory);
	if (exists && dwCreationDisposition == CREATE_NEW) result = ERROR_ALREADY_EXISTS;
	else if (!exists && dwCreationDisposition == OPEN_EXISTING) result = ERROR_FILE_NOT_FOUND;
	else if (!exists)
	{
		/* a new folder: twelve hex digits from the name, moved on if another saved game already has them */
		unsigned __int64 hash = 0xcbf29ce484222325ui64;
		unsigned i, attempt;
		for (i = 0; lpSaveGameName[i]; i++)
		{
			WCHAR c = fold(lpSaveGameName[i]);
			hash = (hash ^ (c & 0xff)) * 0x100000001b3ui64;
			hash = (hash ^ (c >> 8)) * 0x100000001b3ui64;
		}
		result = ERROR_CANNOT_MAKE;
		for (attempt = 0; attempt < 256; attempt++, hash++)
		{
			unsigned n = text_copy(directory, sizeof(directory), root);
			for (i = 0; i < 12; i++) directory[n + i] = digits[(unsigned)(hash >> (44 - 4 * i)) & 15];
			directory[n + 12] = 0;
			if (GetFileAttributesA(directory) != 0xffffffffu) continue;          /* taken */
			if (!CreateDirectoryA(directory, NULL)) { result = GetLastError(); if (!result) result = ERROR_CANNOT_MAKE; break; }
			directory[n + 12] = '\\'; directory[n + 13] = 0;
			result = meta_write(directory, lpSaveGameName, dwCreateFlags) ? ERROR_SUCCESS : ERROR_CANNOT_MAKE;
			break;
		}
	}
	if (result == ERROR_SUCCESS)
	{
		if (!lpPathBuffer || uSize < 17) result = ERROR_INSUFFICIENT_BUFFER;      /* "u:\0123456789AB\" and its end */
		else text_copy(lpPathBuffer, uSize, directory);
	}
	if (g_lines < 40)
	{
		g_lines++;
		h2n_log("[%4u] XCreateSaveGame(%s, \"%s\", %s): %s%s\n", h2n_next_sequence(), root, plain(lpSaveGameName, shown, sizeof(shown)),
			dwCreationDisposition == CREATE_NEW ? "new" : dwCreationDisposition == OPEN_EXISTING ? "existing" : "open or create",
			result == ERROR_SUCCESS ? directory : result == ERROR_ALREADY_EXISTS ? "already there" : result == ERROR_FILE_NOT_FOUND ? "not there" : "FAILED",
			result == ERROR_SUCCESS && !exists ? " (created)" : "");
	}
	return result;
}

DWORD WINAPI XDeleteSaveGame(LPCSTR lpRootPathName, LPCWSTR lpSaveGameName)
{
	char root[4], directory[MAX_PATH], path[MAX_PATH], shown[64];
	WIN32_FIND_DATAA entry;
	HANDLE find;
	unsigned n;
	DWORD result = ERROR_SUCCESS;
	if (!root_take(lpRootPathName, root) || !lpSaveGameName) return ERROR_INVALID_PARAMETER;
	if (!save_lookup(root, lpSaveGameName, directory)) return ERROR_FILE_NOT_FOUND;
	/* the files in the saved game's own folder, then the folder (it lives under .\h2classic\, never in the game's files) */
	n = text_copy(path, sizeof(path), directory);
	text_copy(path + n, sizeof(path) - n, "*");
	find = FindFirstFileA(path, &entry);
	if (find != INVALID_HANDLE_VALUE)
	{
		do
			if (!(entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
			{
				text_copy(path + n, sizeof(path) - n, entry.cFileName);
				DeleteFileA(path);
			}
		while (FindNextFileA(find, &entry));
		FindClose(find);
	}
	n = text_copy(path, sizeof(path), directory);
	if (n && path[n - 1] == '\\') path[n - 1] = 0;
	if (!RemoveDirectoryA(path)) { result = GetLastError(); if (!result) result = ERROR_DIR_NOT_EMPTY; }
	h2n_log("[%4u] XDeleteSaveGame(%s, \"%s\"): %s %s\n", h2n_next_sequence(), root, plain(lpSaveGameName, shown, sizeof(shown)), directory,
		result == ERROR_SUCCESS ? "removed" : "could NOT be removed");
	return result;
}

/* ---- downloaded content ----
   v1.0 does what the "1.0 TU Skip" build does on the console: the packs are not offered to it ("none").
   v1.5 (s10p): XAPI's own way (findcont.obj; XFindFirstContent 0x33080a and its check 0x330659 in v1.0's XBE, read off
   its code): the folders <root>$C\* (FindFirstFileA, so T: is .\h2classic\T); one counts when it is a folder with a
   16-character name whose ContentMeta.xbx has an 'XCSF' header of 0x88 bytes with bit 0 of its word at 0x20 set, whose
   flags (0x30) share a bit with the filter (when one is given), and whose text (at [0x34], [0x38] bytes) names it.
   The offering id is the header's 0x28, the directory <root>$C\<name> (no backslash at the end), the display name the
   "Name=" line of the console language's section ([EN] for English, else [Default]). The handle is XAPI's too: 'TNOC'. */
#define CONTENT_MAGIC 0x434f4e54u       /* "TNOC" */
static unsigned text_length(const char *s) { unsigned n = 0; while (s[n]) n++; return n; }
static bool wide_prefix(const WCHAR *a, const WCHAR *b, unsigned n) { for (unsigned i = 0; i < n; i++) if (a[i] != b[i]) return false; return true; }
struct content_find { DWORD magic; HANDLE find; DWORD filter; DWORD length; char path[MAX_PATH]; };
static unsigned g_content_lines;
static bool content_name(HANDLE h, DWORD offset, DWORD size, WCHAR *out, unsigned max)
{
	static const char *const sections[] = { "Default", "EN", "JA", "DE", "FR", "ES", "IT", "KO", "TW", "PT" };
	WCHAR text[2048], want[16], fallback[128];
	DWORD got = 0, language = XGetLanguage(), i, n;
	bool in_want = false, in_default = false, have_fallback = false;
	if (size < 4 || size > sizeof(text) - 2) return false;
	if (SetFilePointer(h, (LONG)offset, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER) return false;
	if (!ReadFile(h, text, size, &got, NULL) || got != size) return false;
	n = size / 2; text[n] = 0;
	if (text[0] != 0xfeff) return false;
	const char *section = language < sizeof(sections) / sizeof(sections[0]) ? sections[language] : "Default";
	for (i = 0; section[i] && i < 15; i++) want[i] = (WCHAR)section[i];
	want[i] = 0;
	for (i = 1; i < n; )
	{
		DWORD end = i, k;
		while (end < n && text[end] != '\r' && text[end] != '\n') end++;
		if (text[i] == '[')
		{
			DWORD close = i + 1;
			while (close < end && text[close] != ']') close++;
			in_want = close - i - 1 == wide_length(want) && wide_prefix(text + i + 1, want, close - i - 1);
			in_default = close - i - 1 == 7 && wide_prefix(text + i + 1, L"Default", 7);
		}
		else if ((in_want || in_default) && end - i > 5 && wide_prefix(text + i, L"Name=", 5))
		{
			WCHAR *to = in_want ? out : fallback;
			for (k = 0; k + 1 < max && i + 5 + k < end; k++) to[k] = text[i + 5 + k];
			to[k] = 0;
			if (in_want) return true;
			have_fallback = true;
		}
		i = end;
		while (i < n && (text[i] == '\r' || text[i] == '\n')) i++;
	}
	if (!have_fallback) return false;
	for (i = 0; i + 1 < max && fallback[i]; i++) out[i] = fallback[i];
	out[i] = 0;
	return true;
}
static bool content_take(content_find *f, PXCONTENT_FIND_DATA data)
{
	char meta[MAX_PATH];
	BYTE header[0x88];
	DWORD got = 0;
	unsigned n;
	HANDLE h;
	bool ok;
	if (!(data->wfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || text_length(data->wfd.cFileName) != 16) return false;
	n = text_copy(data->szContentDirectory, MAX_PATH, f->path);
	n += text_copy(data->szContentDirectory + n, MAX_PATH - n, data->wfd.cFileName);
	if (n >= 0xf4) return false;
	text_copy(meta, sizeof(meta), data->szContentDirectory);
	text_copy(meta + n, sizeof(meta) - n, "\\ContentMeta.xbx");
	h = CreateFileA(meta, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (h == INVALID_HANDLE_VALUE) return false;
	ok = ReadFile(h, header, sizeof(header), &got, NULL) && got == sizeof(header) && *(DWORD *)(header + 0x14) == 0x46534358u;
	if (ok)
	{
		data->qwOfferingId = *(ULONGLONG *)(header + 0x28);
		data->dwFlags = *(DWORD *)(header + 0x30);
		ok = (*(DWORD *)(header + 0x20) & 1) && (!f->filter || (data->dwFlags & f->filter));
	}
	data->szDisplayName[0] = 0;
	if (ok) ok = content_name(h, *(DWORD *)(header + 0x34), *(DWORD *)(header + 0x38), data->szDisplayName, MAX_CONTENT_DISPLAY_NAME);
	CloseHandle(h);
	if (ok && g_content_lines < 16)
	{
		char shown[80];
		g_content_lines++;
		h2n_log("[%4u] downloaded content: %s (offer %08lx%08lx), \"%s\"\n", h2n_next_sequence(), data->szContentDirectory,
			(DWORD)(data->qwOfferingId >> 32), (DWORD)data->qwOfferingId, plain(data->szDisplayName, shown, sizeof(shown)));
	}
	return ok;
}
HANDLE WINAPI XFindFirstContent(LPCSTR lpRootPathName, DWORD dwFlagFilter, PXCONTENT_FIND_DATA pFindContentData)
{
	content_find *f;
	if (!h2n_v15)
	{	/* game version 1.0: as the "1.0 TU Skip" build on the console, the packs are not offered to it */
		static bool said;
		if (!said) { said = true; h2n_log("[%4u] downloaded content: game version 1.0 is offered none (as the 1.0 TU Skip build)\n", h2n_next_sequence()); }
		SetLastError(ERROR_FILE_NOT_FOUND);
		return INVALID_HANDLE_VALUE;
	}
	char pattern[MAX_PATH];
	unsigned n;
	HANDLE find;
	if (!lpRootPathName || !pFindContentData || text_length(lpRootPathName) + 4 >= MAX_PATH) { SetLastError(ERROR_INVALID_PARAMETER); return INVALID_HANDLE_VALUE; }
	f = (content_find *)h2n_calloc(sizeof(*f));
	if (!f) { SetLastError(ERROR_NOT_ENOUGH_MEMORY); return INVALID_HANDLE_VALUE; }
	f->magic = CONTENT_MAGIC; f->filter = dwFlagFilter;
	n = text_copy(f->path, sizeof(f->path), lpRootPathName);
	n += text_copy(f->path + n, sizeof(f->path) - n, "$C\\");
	f->length = n;
	text_copy(pattern, sizeof(pattern), f->path);
	text_copy(pattern + n, sizeof(pattern) - n, "*");
	find = FindFirstFileA(pattern, &pFindContentData->wfd);
	if (find != INVALID_HANDLE_VALUE)
	{
		bool ok = content_take(f, pFindContentData);
		while (!ok && FindNextFileA(find, &pFindContentData->wfd)) ok = content_take(f, pFindContentData);
		if (ok) { f->find = find; return (HANDLE)f; }
		FindClose(find);
	}
	if (g_content_lines < 16) { g_content_lines++; h2n_log("[%4u] downloaded content in %s: none\n", h2n_next_sequence(), f->path); }
	h2n_free(f);
	SetLastError(ERROR_NO_MORE_FILES);
	return INVALID_HANDLE_VALUE;
}
BOOL WINAPI XFindNextContent(HANDLE hFindContent, PXCONTENT_FIND_DATA pFindContentData)
{
	content_find *f = (content_find *)hFindContent;
	if (!h2n_v15) { SetLastError(ERROR_NO_MORE_FILES); return FALSE; }
	if (!f || hFindContent == INVALID_HANDLE_VALUE || f->magic != CONTENT_MAGIC || !pFindContentData) { SetLastError(ERROR_INVALID_HANDLE); return FALSE; }
	while (FindNextFileA(f->find, &pFindContentData->wfd))
		if (content_take(f, pFindContentData)) return TRUE;
	SetLastError(ERROR_NO_MORE_FILES);
	return FALSE;
}

/* ---- Xbox Live accounts on the console: none ---- */
HRESULT WINAPI XOnlineGetUsers(PXONLINE_USER pUsers, DWORD *pdwUsers)
{
	static bool said;
	(void)pUsers;
	if (pdwUsers) *pdwUsers = 0;
	if (!said) { said = true; h2n_log("[%4u] Xbox Live accounts on this console: none\n", h2n_next_sequence()); }
	return S_OK;
}

}

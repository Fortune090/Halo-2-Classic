/* H2Classic: a self-test of the layer, run instead of the game when H2N_SELFTEST is set. It makes the same Xbox calls
 * the game makes (this file is compiled like the game, with the Xbox SDK's compiler and headers) and checks what
 * comes back; everything it creates is under .\h2classic\ and is removed again. */
#include <xtl.h>
#include <xonline.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

extern "C" {
void __cdecl h2n_log(const char *format, ...);
int __cdecl h2n_sign_selftest(void);
}
static int g_failed, g_checked;
static void check(bool ok, const char *what)
{
	g_checked++;
	if (!ok) g_failed++;
	h2n_log("    %-6s %s\n", ok ? "ok" : "FAILED", what);
}
static int count_saves(const char *root, WCHAR *last_name)
{
	XGAME_FIND_DATA data;
	int count = 0;
	HANDLE find = XFindFirstSaveGame(root, &data);
	if (find == INVALID_HANDLE_VALUE) return 0;
	do { count++; if (last_name) wcscpy(last_name, data.szSaveGameName); } while (XFindNextSaveGame(find, &data));
	XFindClose(find);
	return count;
}

extern "C" int __cdecl h2n_selftest(void)
{
	char first[MAX_PATH] = "", again[MAX_PATH] = "", second[MAX_PATH] = "", path[MAX_PATH];
	WCHAR name[MAX_GAMENAME];
	static const WCHAR odd[] = { 'S', 0xe9, 'r', 'a', ' ', 0x30cf, 0x30ed, 0 };
	XGAME_FIND_DATA data;
	XCONTENT_FIND_DATA content;
	XONLINE_USER users[XONLINE_MAX_STORED_ONLINE_USERS];
	DWORD user_count = 0xdeadbeef, written = 0;
	HANDLE file;
	FILE *stream;
	BYTE *memory;

	h2n_log("self-test of the layer\n  saved games\n");
	check(XFindFirstSaveGame("", &data) == INVALID_HANDLE_VALUE, "no drive given: nothing found");
	check(XFindFirstSaveGame("d:\\", &data) == INVALID_HANDLE_VALUE, "the disc holds no saved games");
	check(count_saves("u:\\", NULL) == 0, "an empty drive has none");
	check(XCreateSaveGame("u:\\", L"Self Test", OPEN_EXISTING, 0, first, sizeof(first)) == ERROR_FILE_NOT_FOUND, "opening one that is not there fails");
	check(XCreateSaveGame("u:\\", L"Self Test", CREATE_NEW, 0, first, sizeof(first)) == ERROR_SUCCESS && strlen(first) == 16 && first[15] == '\\', "creating one gives u:\\<12 digits>\\");
	check(XCreateSaveGame("u:\\", L"Self Test", CREATE_NEW, 0, again, sizeof(again)) == ERROR_ALREADY_EXISTS, "creating it twice is refused");
	check(XCreateSaveGame("u:\\", L"self test", OPEN_EXISTING, 0, again, sizeof(again)) == ERROR_SUCCESS && !strcmp(first, again), "opening it finds the same folder");
	check(XCreateSaveGame("u:\\", odd, CREATE_NEW, 0, second, sizeof(second)) == ERROR_SUCCESS && strcmp(first, second), "a second one gets its own folder");
	check(count_saves("u:\\", NULL) == 2, "the drive now lists two");
	sprintf(path, "%sprofile", first);
	file = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	check(file != INVALID_HANDLE_VALUE && WriteFile(file, "data", 4, &written, NULL) && written == 4, "a file can be written inside it");
	if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
	check(XDeleteSaveGame("u:\\", L"Self Test") == ERROR_SUCCESS, "deleting the first");
	name[0] = 0;
	check(count_saves("u:\\", name) == 1 && !wcscmp(name, odd), "leaves the second, with its name intact");
	check(XDeleteSaveGame("u:\\", odd) == ERROR_SUCCESS && count_saves("u:\\", NULL) == 0, "deleting the second leaves none");
	check(XDeleteSaveGame("u:\\", odd) == ERROR_FILE_NOT_FOUND, "deleting it again reports it missing");
	SetLastError(0);
	check(XFindFirstContent("t:\\", 0, &content) == INVALID_HANDLE_VALUE && GetLastError() == ERROR_FILE_NOT_FOUND, "no downloaded content");
	check(XOnlineGetUsers(users, &user_count) == S_OK && user_count == 0, "no Xbox Live accounts");
	check(XGetDiskClusterSize("u:\\") == 16384, "the hard disk's cluster size");

	h2n_log("  the game's own files are read-only\n");
	check(CreateFileA("d:\\selftest.bin", GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL) == INVALID_HANDLE_VALUE, "CreateFile for writing on d: is refused");
	check(CreateFileA("d:\\default.xbe", GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL) == INVALID_HANDLE_VALUE, "opening a game file read-write is refused");
	file = CreateFileA("d:\\default.xbe", GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	check(file != INVALID_HANDLE_VALUE, "opening a game file for reading works");
	if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
	check(fopen("d:\\selftest.bin", "wb") == NULL, "fopen for writing on d: is refused");
	check(!DeleteFileA("d:\\default.xbe") && !CreateDirectoryA("d:\\selftest", NULL), "deleting or creating on d: is refused");
	stream = fopen("z:\\selftest.txt", "wb");
	check(stream != NULL, "fopen on another drive goes under .\\h2classic");
	if (stream) { fputs("x", stream); fclose(stream); }
	check(GetFileAttributesA("z:\\selftest.txt") != 0xffffffff && DeleteFileA("z:\\selftest.txt"), "and the file is where the file calls look for it");

	h2n_log("  memory\n");
	memory = (BYTE *)VirtualAlloc(NULL, 0x10000, MEM_COMMIT | MEM_TOP_DOWN, PAGE_READWRITE);
	check(memory && (DWORD)memory < 0x80000000, "VirtualAlloc gives memory below 2 GB");
	if (memory) VirtualFree(memory, 0, MEM_RELEASE);
	memory = (BYTE *)XPhysicalAlloc(0x4000, MAXULONG_PTR, 0, PAGE_READWRITE);
	check(memory && (DWORD)memory >= 0x80000000 && (DWORD)memory < 0x88000000, "XPhysicalAlloc gives memory in the console's physical range");
	if (memory)
	{
		BYTE *alias = (BYTE *)((DWORD)memory | 0xf0000000);
		DWORD *words = (DWORD *)(alias + 0x200);
		int i;
		memory[0x123] = 0x5a;
		check(alias[0x123] == 0x5a, "what is written at 0x80000000+ is read back at 0xF0000000+");
		for (i = 0; i < 64; i++) words[i] = 0x1000 + i;                  /* as the game fills a locked texture */
		memset(alias + 0x400, 0x77, 0x100);
		check(((DWORD *)(memory + 0x200))[63] == 0x103f && memory[0x4ff] == 0x77, "and what is written at 0xF0000000+ is read back at 0x80000000+");
		XPhysicalFree(memory);
	}
	h2n_log("  the signatures of saved files\n");
	check(h2n_sign_selftest() == 0, "SHA-1 and HMAC-SHA1 give the published answers for the published test data");
	{
		/* a file signed, then checked, the way the game does it; and the two kinds of signature differ */
		static const BYTE data[480] = { 1, 2, 3 };
		BYTE first[XCALCSIG_SIGNATURE_SIZE], second[XCALCSIG_SIGNATURE_SIZE], bound[XCALCSIG_SIGNATURE_SIZE];
		HANDLE h = XCalculateSignatureBegin(0);
		bool ok = h != INVALID_HANDLE_VALUE && XCalculateSignatureUpdate(h, data, 100) == ERROR_SUCCESS && XCalculateSignatureUpdate(h, data + 100, 380) == ERROR_SUCCESS
			&& XCalculateSignatureEnd(h, (PXCALCSIG_SIGNATURE)first) == ERROR_SUCCESS;
		h = XCalculateSignatureBegin(0);
		ok = ok && h != INVALID_HANDLE_VALUE && XCalculateSignatureUpdate(h, data, 480) == ERROR_SUCCESS && XCalculateSignatureEnd(h, (PXCALCSIG_SIGNATURE)second) == ERROR_SUCCESS;
		check(ok && !memcmp(first, second, sizeof(first)), "a saved file's signature is the same however the data is fed in");
		h = XCalculateSignatureBegin(XCALCSIG_FLAG_NON_ROAMABLE);
		ok = h != INVALID_HANDLE_VALUE && XCalculateSignatureUpdate(h, data, 480) == ERROR_SUCCESS && XCalculateSignatureEnd(h, (PXCALCSIG_SIGNATURE)bound) == ERROR_SUCCESS;
		check(ok && memcmp(first, bound, sizeof(first)) != 0, "the signature that binds a file to one console is a different one");
		check(XCalculateSignatureBegin(0x40) == INVALID_HANDLE_VALUE && GetLastError() == ERROR_NOT_SUPPORTED, "an unknown kind of signature is refused with error 50");
	}
	h2n_log("  the watchdog (the main thread now does nothing for 7 seconds: one report is expected)\n");
	Sleep(7000);
	h2n_log("self-test: %d of %d checks failed\n", g_failed, g_checked);
	{
		/* H2N_SELFTEST=hang: stay here, so that the watchdog's stop (the time limit plus 30 seconds) can be seen */
		const char *mode = getenv("H2N_SELFTEST");
		if (mode && !strcmp(mode, "hang")) { h2n_log("  (H2N_SELFTEST=hang: the main thread now waits for the watchdog to stop the run)\n"); Sleep(INFINITE); }
	}
	return g_failed;
}

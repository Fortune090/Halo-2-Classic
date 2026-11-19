/* H2Classic: Halo 2's own code as a Windows program.
 *
 * What is linked together:
 *   - the game and Havok code: the halo2-decompiled source, compiled with the Xbox SDK's compiler as ordinary objects;
 *   - this layer, in place of the Xbox libraries;
 *   - an empty block covering the retail image's whole address range (the exe is linked at 0x10000, like the game).
 * The game's data is not taken from the source. At start-up the layer reads the user's own default.xbe (v1.0),
 * puts its data sections at their original addresses, applies what retail's start-up initialisers would have written
 * (worked out ahead of time by running them in an emulator), and re-aims every pointer to code inside that data at our
 * build's functions. Every data symbol of the game code that stands for retail data was linked to the retail address. */
#include "h2n.h"
#ifndef H2N_VA                           /* (s10r) one program for both game versions: the address is picked at run time */
extern int h2n_v15;                     /* main.c: 0 = game version 1.0 (the disc's default.xbe), 1 = 1.5 (the title update's) */
#define H2N_VA(v10, v15) (h2n_v15 ? (v15) : (v10))
#define H2N_VA2(v10, v15) { (v10), (v15) }   /* in a table: both, picked with [h2n_v15] */
#endif
void h2n_pad_rumble_stop(void);

extern char h2n_retail_range[], h2n_retail_range_end[];
struct h2n_section { const char *name; DWORD va, vsize, rsize, crc; };
extern const struct h2n_section h2n_sections[];
extern const DWORD h2n_delta[];
extern const DWORD h2n_fixups[];
extern const struct h2n_section h2n15_sections[];       /* (s10r) game version 1.5's (native15's retail_tables.S) */
extern const DWORD h2n15_delta[], h2n15_fixups[];
#define h2n_sections (h2n_v15 ? h2n15_sections : h2n_sections)
#define h2n_delta (h2n_v15 ? h2n15_delta : h2n_delta)
#define h2n_fixups (h2n_v15 ? h2n15_fixups : h2n_fixups)
extern const char *const h2n_stub_names[];
extern const unsigned h2n_stub_count;
extern const DWORD h2n_stub_returns[];
extern const LONG h2n_stub_cleared[];
extern char __ImageBase;

static FILE *g_log;
static unsigned g_hits[2048];
static unsigned g_sequence;
static CRITICAL_SECTION g_log_lock;
static int g_log_lock_ready;
/* (s10f) the game's files - the Xbox's D: drive: default.xbe and the maps folder, or a disc image (.iso) of them - are in the
   Roms folder next to the exe (relative to the current folder, like h2classic\ and the log: the launcher, the test launcher
   and Explorer all start the exe in its own folder). A first argument names another folder or image.
   (s10o) The folder is Roms (it was Rom): the game in Roms\Halo 2 or in Roms itself, the map packs in Roms\DLC; the
   Rom folder of earlier builds is still found. */
char g_disc_root[400] = "Roms";
static int g_disc_named;                /* the game folder or image was named on the command line */

/* ---- what a local finds on the stack before the game has written it ----
   On the console the dead part of the stack (below the stack pointer) holds only what the game's own earlier calls
   left there. Here the layer and Windows use the same stack (logging, file calls, waits), and what they leave differs
   from one system to the next, so a local that is read before it is written behaves differently on each: stage 3
   stopped on the user's Windows, and not under Wine, on exactly that. The dead part of the stack is therefore filled
   with a known value wherever nothing of the game's can be in it: when the game starts, when a thread starts, at every
   presented frame, before every vertical-blank callback and after every line the layer logs.
     H2N_STACK=zero    (default) zeros: the most benign value, and the same everywhere
     H2N_STACK=poison  0x0BADBAD1: a use of such a local stops at once and the report names it
     H2N_STACK=off     leave the stack as it is */
#define STACK_POISON 0x0badbad1u
static int g_stack_mode = 1;
__attribute__((noinline)) void h2n_scrub_stack(unsigned bytes)
{
	ULONG_PTR sp, top, low, floor;
	DWORD value = g_stack_mode == 2 ? STACK_POISON : 0;
	if (!g_stack_mode) return;
	__asm__ volatile("movl %%esp, %0" : "=r"(sp));
	top = (sp - 32) & ~(ULONG_PTR)3;            /* nothing below the stack pointer is in use, and nothing here pushes */
	floor = (ULONG_PTR)*(void **)((char *)NtCurrentTeb() + 0xe0c) + 0x10000;      /* the stack's reserve starts here: keep well clear of its last pages */
	low = top - bytes;
	if (low < floor) low = floor;
	if (low >= top) return;
	/* the stack grows a page at a time, and only when its pages are touched in order, downwards */
	for (ULONG_PTR page = (top - 1) & ~(ULONG_PTR)0xfff; page + 0x1000 > low; page -= 0x1000) (void)*(volatile char *)page;
	for (volatile DWORD *p = (volatile DWORD *)low; (ULONG_PTR)p < top; p++) *p = value;
}
void h2n_set_stack_mode(const char *mode)
{
	if (!mode) return;
	if (!_stricmp(mode, "off")) g_stack_mode = 0;
	else if (!_stricmp(mode, "poison")) g_stack_mode = 2;
	else g_stack_mode = 1;
}
const char *h2n_stack_mode_name(void) { return g_stack_mode == 2 ? "filled with 0x0BADBAD1 (H2N_STACK=poison)" : g_stack_mode ? "zeroed" : "left alone (H2N_STACK=off)"; }

static void log_line(const char *format, va_list arguments)
{
	va_list copy;
	if (g_log_lock_ready) EnterCriticalSection(&g_log_lock);
	va_copy(copy, arguments);
	if (g_log) { vfprintf(g_log, format, copy); fflush(g_log); }
	va_end(copy);
	vfprintf(stdout, format, arguments); fflush(stdout);
	if (g_log_lock_ready) LeaveCriticalSection(&g_log_lock);
}
void h2n_log(const char *format, ...)
{
	va_list arguments;
	va_start(arguments, format);
	log_line(format, arguments);
	va_end(arguments);
	h2n_note_activity();
	h2n_scrub_stack(0x8000);        /* what printing left behind */
}
int h2n_console_attach(void);
void h2n_fatal(const char *format, ...)
{
	char text[1024];
	va_list arguments;
	va_start(arguments, format);
	vsnprintf(text, sizeof(text), format, arguments);
	va_end(arguments);
	h2n_log("\n*** cannot continue: %s\n", text);
	{
		const char *quiet = getenv("H2N_NOLAUNCHER");
		if (!h2n_console_attach() && !(quiet && atoi(quiet)))
		{
			char box[1200];
			snprintf(box, sizeof(box), "H2Classic cannot continue:\n\n%s\n\n(h2classic_log.txt has the details.)", text);
			MessageBoxA(NULL, box, "H2Classic", MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
		}
	}
	h2n_pad_rumble_stop();
	ExitProcess(2);
}
int h2n_env_int(const char *name, int otherwise) { const char *v = getenv(name); return v && *v ? (int)strtol(v, NULL, 0) : otherwise; }
/* s9s, H2N_SKIPINTRO=1: the main loop (function_12b690, unknown_12b690.cpp) starts with the game's own "play the intro" flag
   (main_globals +0x28) clear instead of set, as the game itself has it once the intro has played: the first map load
   (loading.cpp) and main_play_intro_movie (0x12bf90) then pass the intro by. Read once, when the main loop starts. */
int __cdecl h2n_intro_wanted(void)
{
	int skip = h2n_env_int("H2N_SKIPINTRO", 0) != 0;
	if (skip) h2n_log("H2N_SKIPINTRO: the intro movie is left out (the game's \"play the intro\" flag starts clear)\n");
	return !skip;
}
unsigned h2n_next_sequence(void) { return (unsigned)InterlockedIncrement((LONG *)&g_sequence); }
unsigned h2n_run_seconds(void);
int h2n_is_retail_address(DWORD address) { return address >= RETAIL_BASE && address < RETAIL_END; }

void __cdecl h2n_stub_hit(unsigned index)
{
	/* a real library function would leave its own working values just below the caller's frame; a stand-in leaves
	   the known fill value there instead (see h2n_scrub_stack) */
	volatile DWORD residue[256];
	if (g_stack_mode) for (int i = 0; i < 256; i++) residue[i] = g_stack_mode == 2 ? STACK_POISON : 0;
	if (index < 2048 && InterlockedIncrement((LONG *)&g_hits[index]) == 1)
	{
		char what[96];
		if (h2n_stub_returns[index]) snprintf(what, sizeof(what), "returns the failure code %08lx", h2n_stub_returns[index]);
		else snprintf(what, sizeof(what), "returns 0");
		if (h2n_stub_cleared[index] > 0) snprintf(what + strlen(what), sizeof(what) - strlen(what), ", clears its %ld output%s", h2n_stub_cleared[index], h2n_stub_cleared[index] == 1 ? "" : "s");
		h2n_log("[%4u] first call: %s (stand-in: %s)\n", h2n_next_sequence(), h2n_stub_names[index], what);
	}
}

/* ---- the retail data, in place ---- */
void __cdecl h2n_lods_retail(void);
void __cdecl h2n_animation_retail(void);
void __cdecl h2n_look_retail(void);
void h2n_load_retail_code(const BYTE *xbe, size_t size);
int h2n_retail_code_loaded(void);
static DWORD crc32_of(const unsigned char *p, size_t n);
DWORD h2n_crc32(const unsigned char *p, size_t n) { return crc32_of(p, n); }
static DWORD crc32_of(const unsigned char *p, size_t n)
{
	static DWORD table[256];
	DWORD c = 0xffffffffu;
	if (!table[1])
		for (DWORD i = 0; i < 256; i++) { DWORD v = i; for (int k = 0; k < 8; k++) v = (v >> 1) ^ (0xedb88320u & (0u - (v & 1))); table[i] = v; }
	while (n--) c = table[(c ^ *p++) & 255] ^ (c >> 8);
	return ~c;
}

/* (s10f) The exe is a Windows program now (no console window of its own: the user's request). Started from a console
   (PowerShell, the test launcher's window), its messages still go there: it joins that console. Started from the launcher
   or Explorer there is none, and a message that ends the run is shown in a message box too (not in test runs:
   H2N_NOLAUNCHER=1, which the test launcher and the test scripts set). */
static int g_console = -1;
int h2n_console_attach(void)
{
	if (g_console < 0)
	{
		g_console = AttachConsole(ATTACH_PARENT_PROCESS) ? 1 : 0;
		if (g_console) { freopen("CONOUT$", "w", stdout); freopen("CONOUT$", "w", stderr); }
	}
	return g_console;
}
void h2n_tell_user(const char *text)
{
	const char *quiet = getenv("H2N_NOLAUNCHER");
	printf("%s\n", text); fflush(stdout);
	if (!h2n_console_attach() && !(quiet && atoi(quiet)))
		MessageBoxA(NULL, text, "H2Classic", MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
}

/* runs before any of the game code's own start-up initialisers (it sits ahead of them in the initialiser table):
   the retail range is part of our code section, so it starts out read-only */
/* ---- (s10r) one program for both game versions ----
   The exe is linked as game version 1.0: every reference of the compiled game code (and of this layer) to the game's
   data holds 1.0's address. The build links the same objects a second time against 1.5's addresses (the 1.5 title
   update keeps the same data elsewhere, and some structures grew) and writes every place where the two links differ
   into the exe's .h2nv15 section: address, length, 1.5's bytes (scripts/v15list.py). When the game version is 1.5 those
   places are rewritten here, before any of the game code's own initialisers and before main; from then on the program is
   the 1.5 build. Nothing is rewritten for 1.0.
   The version: H2N_VERSION (1.0 or 1.5; the test launcher sets it), else game_version in h2classic\settings.ini (H2Classic
   Settings writes it), else 1.0. Read with Windows' own calls: the C library and the log are not up yet. */
int h2n_v15;
static char g_version_from[48] = "nothing asks for 1.5";
extern const DWORD h2n_v15_list[];                       /* v15list.S: 'H2V5', runs, bytes; then the runs */
extern DWORD h2n_retail_ftol_at, h2n_retail_4c450_at, h2n_retail_1237e5_at;      /* helpers.S */
static int version_wanted(void)
{
	HMODULE k = GetModuleHandleA("kernel32.dll");
	DWORD (WINAPI *env)(const char *, char *, DWORD) = (void *)GetProcAddress(k, "GetEnvironmentVariableA");
	HANDLE (WINAPI *create)(const char *, DWORD, DWORD, void *, DWORD, DWORD, HANDLE) = (void *)GetProcAddress(k, "CreateFileA");
	BOOL (WINAPI *read)(HANDLE, void *, DWORD, DWORD *, void *) = (void *)GetProcAddress(k, "ReadFile");
	BOOL (WINAPI *close)(HANDLE) = (void *)GetProcAddress(k, "CloseHandle");
	static char text[8192];
	char value[16];
	DWORD n = env ? env("H2N_VERSION", value, sizeof(value)) : 0, got = 0;
	HANDLE f;
	char *line;
	if (n && n < sizeof(value)) { lstrcpynA(g_version_from, "H2N_VERSION", sizeof(g_version_from)); return !lstrcmpA(value, "1.5"); }
	n = env ? env("H2N_SETTINGS", value, sizeof(value)) : 0;
	if (n && n < sizeof(value) && (!lstrcmpiA(value, "off") || !lstrcmpA(value, "0"))) return 0;      /* the test launcher's settings only */
	if (!create || !read || !close) return 0;
	f = create(".\\h2classic\\settings.ini", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
	if (f == INVALID_HANDLE_VALUE) return 0;
	if (!read(f, text, sizeof(text) - 1, &got, NULL)) got = 0;
	close(f);
	text[got] = 0;
	for (line = text; *line; )
	{
		char *end = line, *key = line, *v;
		while (*end && *end != '\n' && *end != '\r') end++;
		while (*key == ' ' || *key == '\t') key++;
		if (end - key > 13 && (key[12] == '=' || key[12] == ' ' || key[12] == '\t'))
		{
			char name[13];
			lstrcpynA(name, key, 13);
			if (!lstrcmpiA(name, "game_version"))
			{
				v = key + 12;
				while (v < end && (*v == ' ' || *v == '\t' || *v == '=')) v++;
				lstrcpynA(g_version_from, "game_version in h2classic\\settings.ini", sizeof(g_version_from));
				return end - v >= 3 && v[0] == '1' && v[1] == '.' && v[2] == '5' && (end - v == 3 || v[3] == ' ' || v[3] == '\t');
			}
		}
		line = end;
		while (*line == '\n' || *line == '\r') line++;
	}
	return 0;
}
static unsigned g_v15_runs, g_v15_bytes;
static void v15_apply(void)
{
	const DWORD *t = h2n_v15_list;
	const BYTE *base = (const BYTE *)GetModuleHandleA(NULL), *p;
	const IMAGE_NT_HEADERS *nt = (const IMAGE_NT_HEADERS *)(base + ((const IMAGE_DOS_HEADER *)base)->e_lfanew);
	const IMAGE_SECTION_HEADER *s = IMAGE_FIRST_SECTION(nt);
	DWORD old[32], i, sections = nt->FileHeader.NumberOfSections;
	if (t[0] != 0x35563248u || sections > 32)             /* "H2V5": the build did not put the list into this exe */
	{
		h2n_tell_user("H2Classic: this exe has no list for game version 1.5 (it was not built whole); game version 1.0 runs.");
		ExitProcess(2);
	}
	for (i = 0; i < sections; i++)
		VirtualProtect((void *)(base + s[i].VirtualAddress), s[i].Misc.VirtualSize, s[i].Characteristics & IMAGE_SCN_MEM_EXECUTE ? PAGE_EXECUTE_READWRITE : PAGE_READWRITE, &old[i]);
	for (i = 0, p = (const BYTE *)(t + 3); i < t[1]; i++)
	{
		DWORD at = *(const DWORD *)p, length = *(const DWORD *)(p + 4);
		memcpy((void *)(ULONG_PTR)at, p + 8, length);
		p += 8 + ((length + 3) & ~3u);
	}
	g_v15_runs = t[1]; g_v15_bytes = t[2];
	for (i = 0; i < sections; i++) VirtualProtect((void *)(base + s[i].VirtualAddress), s[i].Misc.VirtualSize, old[i], &old[i]);
	FlushInstructionCache(GetCurrentProcess(), NULL, 0);
	h2n_retail_ftol_at = 0x32cc0c; h2n_retail_4c450_at = 0x4c440; h2n_retail_1237e5_at = 0x1262f5;      /* (helpers.S: 1.5's) */
}
static void __cdecl early_init(void)
{
	DWORD old;
	h2n_console_attach();
	h2n_v15 = version_wanted();
	if (h2n_v15) v15_apply();
	if ((DWORD)(ULONG_PTR)h2n_retail_range != RETAIL_FIRST || (DWORD)(ULONG_PTR)h2n_retail_range_end != RETAIL_RESERVED_END)
	{
		char text[200];
		snprintf(text, sizeof(text), "H2Classic: the exe is not laid out as built (retail range at %p)", (void *)h2n_retail_range);
		h2n_tell_user(text);
		ExitProcess(2);
	}
	VirtualProtect((void *)RETAIL_FIRST, RETAIL_RESERVED_END - RETAIL_FIRST, PAGE_READWRITE, &old);
}
__attribute__((section(".CRT$XCB"), used)) static void (__cdecl *const early_init_entry)(void) = early_init;

void h2n_net_setup(const unsigned char *xbe, size_t size);
static void xbe_headers_for_retail(const BYTE *xbe, size_t size);
void h2n_title_keys(const unsigned char *xbe, size_t size);
/* (s10j) THE MAIN MENU'S SIXTH ITEM, EXIT (main_menu.cpp: six records and six item widgets, its text, what A on it does).
 * Three entries of retail's tables go with the list's new size: the list's item count (slot 19 of its table 0x458a70:
 * retail's 0x230612 answers 5), and the deleting destructors of the list and of the screen (slot 0 of 0x458a70 and of
 * 0x453828: retail's are folded with other lists' and screens' of the five-item layout, so they would take apart the
 * wrong members) - each now ours, compiled from main_menu.cpp with six items. Each table is recognised first by an entry
 * that must already be ours (the list's text, slot 20; the screen's v19, slot 19); if either is not, EXIT is left out
 * and the menu is retail's five items' layout with six records (the sixth reached by scrolling). */
extern char g_main_menu_list_text[] __asm__("?v20@c_main_menu_list@@UAEXPAVc_class_1a2c81@@J@Z");
extern char g_main_menu_screen_shown[] __asm__("?v19@c_main_menu_screen@@UAEXXZ");
extern char g_main_menu_list_delete[] __asm__("??_Gc_main_menu_list@@UAEPAXI@Z");
extern char g_main_menu_screen_delete[] __asm__("??_Gc_main_menu_screen@@UAEPAXI@Z");
static long __attribute__((fastcall)) main_menu_item_count(void *list) { (void)list; return 6; }
static void main_menu_six(void)
{
	DWORD *list_table = (DWORD *)H2N_VA(0x458a70, 0x464c90), *screen_table = (DWORD *)H2N_VA(0x453828, 0x45f4f8);
	if (list_table[20] != (DWORD)(ULONG_PTR)g_main_menu_list_text || screen_table[19] != (DWORD)(ULONG_PTR)g_main_menu_screen_shown)
	{
		h2n_log("main menu: its tables are not as expected (%08lx, %08lx): EXIT is not shown in full\n", list_table[20], screen_table[19]);
		return;
	}
	list_table[19] = (DWORD)(ULONG_PTR)main_menu_item_count;
	list_table[0] = (DWORD)(ULONG_PTR)g_main_menu_list_delete;
	screen_table[0] = (DWORD)(ULONG_PTR)g_main_menu_screen_delete;
	h2n_log("main menu: six items, the sixth EXIT (closes the program)\n");
}

static void load_retail_data(const char *xbe_path)
{
	FILE *f = xbe_path ? h2n_real_fopen(xbe_path, "rb") : NULL;
	unsigned char *xbe;
	long size;
	DWORD base, count, headers, old;
	unsigned loaded = 0, bytes = 0, runs = 0, fixed = 0;
	if (!xbe_path)
	{
		/* the disc is a disc image: its default.xbe is read from inside it */
		size_t n = 0;
		xbe = h2n_iso_read("default.xbe", &n);
		if (!xbe) h2n_fatal("the disc image %s has no default.xbe that could be read", h2n_iso_path());
		size = (long)n;
		xbe_path = "the disc image's default.xbe";
	}
	else
	{
		if (!f)
		{
			char full[600];
			if (!GetFullPathNameA(g_disc_root, sizeof(full), full, NULL)) snprintf(full, sizeof(full), "%s", g_disc_root);
			/* (s10q) game version 1.5 is the title update's own default.xbe, which content.c copies from Roms\DLC */
			if (h2n_v15) h2n_fatal("Halo 2's 1.5 title update was not found (%s).\n    Game version 1.5 runs the title update's own default.xbe (4,919,296 bytes), which H2Classic copies from Roms\\DLC.\n"
				"    Put the title update there in one of the forms it was handed out: an installer's folder (content\\u5English with\n"
				"    its default.xbe and contentmeta.xbx) or a copy of a console's TDATA\\4d530064 folder (its $u folder).\n"
				"    Game version 1.0 (Game Version in H2Classic Settings) does not need it.", xbe_path);
			if (!g_disc_named)
				h2n_fatal("Halo 2's files were not found in the Roms folder (%s).\n    Put the v1.0 game's files (default.xbe and the maps folder), or a disc image of them (.iso), in Roms\\Halo 2 or in Roms.\n"
					"    Another folder or disc image can be named instead:  H2Classic.exe \"C:\\Games\\Halo 2\"", full);
			h2n_fatal("default.xbe not found at %s, and no Xbox disc image (.iso) with the game is in that folder.\n    Name the folder that holds the v1.0 game files or a disc image of them:  H2Classic.exe \"C:\\Games\\Halo 2\"\n"
				"    (a second argument can name the default.xbe itself)", xbe_path);
		}
		fseek(f, 0, SEEK_END); size = ftell(f); fseek(f, 0, SEEK_SET);
		xbe = malloc((size_t)size);
		if (!xbe || fread(xbe, 1, (size_t)size, f) != (size_t)size) h2n_fatal("could not read %s", xbe_path);
		fclose(f);
	}
	if (size < 0x1000 || memcmp(xbe, "XBEH", 4)) h2n_fatal("%s is not an Xbox executable", xbe_path);
	base = *(DWORD *)(xbe + 0x104); count = *(DWORD *)(xbe + 0x11c); headers = *(DWORD *)(xbe + 0x120) - base;
	for (const struct h2n_section *want = h2n_sections; want->name; want++)
	{
		int found = 0;
		for (DWORD i = 0; i < count && !found; i++)
		{
			const DWORD *h = (const DWORD *)(xbe + headers + i * 56);
			const char *name = (const char *)(xbe + h[5] - base);
			if (h[1] != want->va || strcmp(name, want->name)) continue;
			found = 1;
			if (h[4] != want->rsize || h[3] + h[4] > (DWORD)size || crc32_of(xbe + h[3], h[4]) != want->crc)
				h2n_fatal("the %s section of %s is not the %s game's.\n    This build takes its data from the unmodified %s"
					"; name that file as the second argument", want->name, xbe_path, H2N_GAME_VERSION, H2N_GAME_XBE);
			memcpy((void *)want->va, xbe + h[3], h[4]);
			loaded++; bytes += h[4];
		}
		if (!found) h2n_fatal("%s has no %s section at %08lx: it is not the %s", xbe_path, want->name, want->va, H2N_GAME_XBE);
	}
	/* what retail's 407 start-up initialisers leave behind */
	for (const DWORD *d = h2n_delta; d[0]; d = (const DWORD *)((const char *)(d + 2) + ((d[1] + 3) & ~3u)))
	{
		memcpy((void *)d[0], d + 2, d[1]);
		runs++;
	}
	*(DWORD *)H2N_VA(0x557c88, 0x56aecc) = GetCurrentThreadId();      /* retail initialiser 0x377ef0: the main thread's id */
	/* pointers to code, re-aimed at our functions; each is checked against the retail address it should hold */
	for (const DWORD *p = h2n_fixups; p[0]; p += 3)
	{
		if (*(DWORD *)p[0] != p[1]) h2n_fatal("data at %08lx holds %08lx, expected the retail address %08lx", p[0], *(DWORD *)p[0], p[1]);
		*(DWORD *)p[0] = p[2];
		fixed++;
	}
	h2n_log("game data in place: %u sections (%u bytes) from %s, %u initialiser writes, %u code pointers re-aimed\n",
		loaded, bytes, xbe_path, runs, fixed);
	/* retail's code, for the functions the decompilation does not have (H2N_RETAIL=off leaves it out: then anything
	   that still jumps or points into it stops at once) */
	{
		const char *mode = getenv("H2N_RETAIL");
		if (!(mode && !_stricmp(mode, "off"))) h2n_load_retail_code(xbe, (size_t)size);
	}
	if (h2n_retail_code_loaded()) xbe_headers_for_retail(xbe, (size_t)size);
	main_menu_six();                        /* (s10j) the main menu's sixth item, EXIT (after retail's code, which also writes table entries) */
	h2n_lods_retail();                      /* (s10l) Increased LODs in retail's code: the instances' fade, the objects' level of detail */
	h2n_animation_retail();                 /* (s10m) the rocket launcher's animation fix in retail's code as well */
	h2n_look_retail();                      /* (s10v) the mouse: retail's look and aim assist calls go through the layer */
	h2n_title_keys(xbe, (size_t)size);      /* the keys saved files are signed with come from the certificate in the XBE */
	if (h2n_env_int("H2N_SKIPATTRACT", 0) && h2n_retail_code_loaded() && *(BYTE *)H2N_VA(0x223a21, 0x22a431) == 0x55)
	{	/* s9w: no attract movie when the menu stands idle: retail's attract_mode_play_movie (0x223a21, called by 0x1479c3 when
		   0x223976 says the menu has been idle long enough) returns at once */
		DWORD was;
		VirtualProtect((void *)H2N_VA(0x223a21, 0x22a431), 1, PAGE_EXECUTE_READWRITE, &was);
		*(BYTE *)H2N_VA(0x223a21, 0x22a431) = 0xc3;
		VirtualProtect((void *)H2N_VA(0x223a21, 0x22a431), 1, was, &was);
		h2n_log("H2N_SKIPATTRACT=1: the attract movie is not played when the menu stands idle\n");
	}
	h2n_net_setup(xbe, (size_t)size);       /* the game's socket and XNet calls: the PC's, or (H2N_XNET=retail) the XBE's library */
	free(xbe);
	VirtualProtect((void *)RETAIL_FIRST, RETAIL_CODE_END - RETAIL_FIRST, h2n_retail_code_loaded() ? PAGE_EXECUTE_READWRITE : PAGE_NOACCESS, &old);
}

/* ---- the XBE's headers, for retail's library code (s10p) ----
   On the console the kernel maps the XBE's headers at 0x10000 (its base address), and the XAPI, XOnline and XNet
   libraries read them there: the certificate's address (0x10118: title id, version, keys), the initialisation flags
   (0x10124), the sections (0x1011c, 0x10120), the stack and heap sizes. This program's own headers are at 0x10000, so
   those reads read a copy of the XBE's headers instead: the 39 places in each version's code that read 0x10100-0x1017f
   (every one there is: a disassembly of the code sections and a search of their bytes find the same 39) read the same
   field of the copy. The copy's pointers into the headers (certificate, section headers, names, library versions,
   logo) point into the copy, as they point into the mapped headers on the console. */
void *__cdecl h2n_calloc(unsigned size);       /* crt.c */
static const struct { DWORD at[2]; WORD field; } g_header_reads[39] = {       /* (at: 1.0's, 1.5's) */
	{ H2N_VA2(0x2d092e, 0x2da81e), 0x118 }, { H2N_VA2(0x2d0af0, 0x2da9e0), 0x118 }, { H2N_VA2(0x2d0af5, 0x2da9e5), 0x108 },
	{ H2N_VA2(0x2d0d8c, 0x2dac7c), 0x118 }, { H2N_VA2(0x2d1129, 0x2db019), 0x118 }, { H2N_VA2(0x2d12ad, 0x2db19d), 0x118 },
	{ H2N_VA2(0x2d1388, 0x2db278), 0x118 }, { H2N_VA2(0x2d1ae2, 0x2db9d2), 0x118 }, { H2N_VA2(0x2d1b8a, 0x2dba7a), 0x138 },
	{ H2N_VA2(0x2d1b90, 0x2dba80), 0x134 }, { H2N_VA2(0x2d1ba7, 0x2dba97), 0x124 }, { H2N_VA2(0x2d1bfd, 0x2dbaed), 0x124 },
	{ H2N_VA2(0x2d1c05, 0x2dbaf5), 0x118 }, { H2N_VA2(0x2d1c30, 0x2dbb20), 0x124 }, { H2N_VA2(0x2d1c53, 0x2dbb43), 0x118 },
	{ H2N_VA2(0x2d21d1, 0x2dc0c1), 0x130 }, { H2N_VA2(0x2d2c9a, 0x2dcb8a), 0x118 }, { H2N_VA2(0x2d2e24, 0x2dcd14), 0x118 },
	{ H2N_VA2(0x2d2fa6, 0x2dce96), 0x118 }, { H2N_VA2(0x2d5747, 0x2df7c5), 0x11c }, { H2N_VA2(0x2d574d, 0x2df7cb), 0x120 },
	{ H2N_VA2(0x2d5936, 0x2df9b4), 0x118 }, { H2N_VA2(0x2d5bb6, 0x2dfc34), 0x124 }, { H2N_VA2(0x2d5cef, 0x2dfd6d), 0x124 },
	{ H2N_VA2(0x2d5d14, 0x2dfd92), 0x118 }, { H2N_VA2(0x2d5eb6, 0x2dff34), 0x124 }, { H2N_VA2(0x2d65ab, 0x2e062b), 0x118 },
	{ H2N_VA2(0x2d6766, 0x2e07e6), 0x118 }, { H2N_VA2(0x3a0948, 0x3aaa28), 0x118 }, { H2N_VA2(0x3a1cf2, 0x3abdd2), 0x118 },
	{ H2N_VA2(0x3b02e7, 0x3ba3c7), 0x118 }, { H2N_VA2(0x3b0365, 0x3ba445), 0x118 }, { H2N_VA2(0x3b03da, 0x3ba4ba), 0x118 },
	{ H2N_VA2(0x3b03fd, 0x3ba4dd), 0x118 }, { H2N_VA2(0x3b0415, 0x3ba4f5), 0x118 }, { H2N_VA2(0x3b057e, 0x3ba65e), 0x124 },
	{ H2N_VA2(0x3d0e1a, 0x3da281), 0x118 }, { H2N_VA2(0x3d55ea, 0x3dea51), 0x118 }, { H2N_VA2(0x3df491, 0x3e9571), 0x118 },
};
static void xbe_headers_for_retail(const BYTE *xbe, size_t size)
{
	static const WORD pointers[] = { 0x118, 0x120, 0x14c, 0x150, 0x154, 0x15c, 0x164, 0x168, 0x16c, 0x170 };
	DWORD base = *(const DWORD *)(xbe + 0x104), length = *(const DWORD *)(xbe + 0x108), sections, at, delta, old;
	BYTE *copy;
	unsigned i, k;
	if (length < 0x178 || length > size || length > 0x10000) h2n_fatal("the XBE's headers are %lu bytes", length);
	copy = (BYTE *)h2n_calloc(length);
	memcpy(copy, xbe, length);
	delta = (DWORD)(ULONG_PTR)copy - base;
	for (i = 0; i < sizeof(pointers) / sizeof(pointers[0]); i++)
	{
		DWORD *p = (DWORD *)(copy + pointers[i]);
		if (*p >= base && *p - base < length) *p += delta;
	}
	sections = *(const DWORD *)(xbe + 0x11c);
	at = *(const DWORD *)(xbe + 0x120) - base;
	if (at > length || sections > (length - at) / 0x38) h2n_fatal("the XBE's section headers are not inside its headers");
	for (i = 0; i < sections; i++)
		for (k = 0x14; k <= 0x20; k += 4)          /* name, then (0x18 is the count itself) the shared pages' counts */
		{
			DWORD *p = (DWORD *)(copy + at + i * 0x38 + k);
			if (k != 0x18 && *p >= base && *p - base < length) *p += delta;
		}
	for (i = 0, k = 0; i < sizeof(g_header_reads) / sizeof(g_header_reads[0]); i++)
	{
		BYTE *operand = (BYTE *)(ULONG_PTR)g_header_reads[i].at[h2n_v15];
		DWORD want = base + g_header_reads[i].field, now = (DWORD)(ULONG_PTR)(copy + g_header_reads[i].field);
		unsigned back;
		if (*(DWORD *)operand != want)
		{	/* the read is the first instruction of a library function that jumps to this program's own (retail.c wrote
			   the jump over it): it never runs */
			for (back = 1; back <= 2; back++)
				if (operand[-(int)back] == 0xe9)
				{
					DWORD to = (DWORD)(ULONG_PTR)(operand - back) + 5 + *(DWORD *)(operand - back + 1);
					if (to < RETAIL_FIRST || to >= RETAIL_CODE_END) break;
				}
			if (back > 2) h2n_fatal("retail code at %08lx reads %08lx, expected the XBE header's %08lx", (DWORD)(ULONG_PTR)operand, *(DWORD *)operand, want);
			k++;
			continue;
		}
		VirtualProtect(operand, 4, PAGE_EXECUTE_READWRITE, &old);
		*(DWORD *)operand = now;
		VirtualProtect(operand, 4, old, &old);
	}
	h2n_log("the XBE's headers (%lu bytes) are at %p for the %u places retail's libraries read them (%u more are in library functions replaced by this program's own)\n",
		length, copy, i - k, k);
}

/* ---- XLoadSection: the game asks for a few of its own sections by name ---- */
void *__stdcall h2n_XLoadSectionA(const char *name) __asm__("_XLoadSectionA@4");
void *__stdcall h2n_XLoadSectionA(const char *name)
{
	for (const struct h2n_section *s = h2n_sections; s->name; s++)
		if (!strcmp(s->name, name)) { h2n_log("[%4u] XLoadSection(%s) -> %08lx\n", h2n_next_sequence(), name, s->va); return (void *)s->va; }
	h2n_log("[%4u] XLoadSection(%s): not one of the sections this build loads\n", h2n_next_sequence(), name);
	return NULL;
}
BOOL __stdcall h2n_XFreeSectionA(const char *name) __asm__("_XFreeSectionA@4");
BOOL __stdcall h2n_XFreeSectionA(const char *name) { (void)name; return TRUE; }

/* ---- stopping ---- */
static const char *region_of(DWORD a)
{
	if (a >= RETAIL_FIRST && a < RETAIL_CODE_END) return h2n_retail_code_loaded() ? "(retail code)" : "retail CODE address (retail's code is not loaded: something still points at it)";
	if (a >= RETAIL_CODE_END && a < RETAIL_END) return "retail data address";
	if (a >= 0x80000000u && a < 0x84000000u) return "game memory block";
	if (a < 0x10000) return "null pointer";
	if (a >= 0x0bad0000u && a < 0x0baf0000u) return "(the stack-poison value: a local was used before anything was stored in it)";
	return "";
}
/* is this an address in the code we built (after the block that stands where retail's image was)? */
static int is_our_code(DWORD a)
{
	static DWORD code_end;
	if (!code_end)
	{
		const BYTE *base = (const BYTE *)GetModuleHandleA(NULL);
		const IMAGE_NT_HEADERS *nt = (const IMAGE_NT_HEADERS *)(base + ((const IMAGE_DOS_HEADER *)base)->e_lfanew);
		const IMAGE_SECTION_HEADER *s = IMAGE_FIRST_SECTION(nt);
		code_end = (DWORD)(ULONG_PTR)base + s->VirtualAddress + s->Misc.VirtualSize;      /* .text is the first section */
	}
	return a >= RETAIL_RESERVED_END && a < code_end;
}
/* a value on a stack that points just after a call instruction in our code: text and numbers that merely fall in
   the code's address range are left out */
static int is_return_address(DWORD a)
{
	const BYTE *p = (const BYTE *)a;
	if (h2n_retail_code_loaded() && a >= 0x12007 && a < RETAIL_CODE_END) ;         /* retail code in place counts too */
	else if (!is_our_code(a) || !is_our_code(a - 7)) return 0;
	if (p[-5] == 0xe8) return 1;                                              /* call rel32 */
	for (int n = 2; n <= 7; n++) if (p[-n] == 0xff && (p[-n + 1] & 0x38) == 0x10) return 1;      /* call r/m32 */
	return 0;
}
static LONG WINAPI crash_filter(EXCEPTION_POINTERS *e)
{
	CONTEXT *c = e->ContextRecord;
	DWORD touched = e->ExceptionRecord->NumberParameters >= 2 ? (DWORD)e->ExceptionRecord->ExceptionInformation[1] : 0;
	h2n_log("\n*** stopped: exception %08lx at %08lx, touching %08lx %s\n", e->ExceptionRecord->ExceptionCode, (unsigned long)c->Eip,
		(unsigned long)touched, region_of(touched));
	if (h2n_is_retail_address((DWORD)c->Eip) || *region_of((DWORD)c->Eip) == '(') h2n_log("    the program counter itself is a %s\n", region_of((DWORD)c->Eip));
	{
		const DWORD regs[7] = { c->Eax, c->Ebx, c->Ecx, c->Edx, c->Esi, c->Edi, c->Ebp };
		for (int i = 0; i < 7; i++)
			if (regs[i] == STACK_POISON) { h2n_log("    a register holds the stack-poison value: a local was used before anything was stored in it\n"); break; }
	}
	h2n_log("    eax %08lx ebx %08lx ecx %08lx edx %08lx esi %08lx edi %08lx ebp %08lx esp %08lx  thread %lu\n",
		c->Eax, c->Ebx, c->Ecx, c->Edx, c->Esi, c->Edi, c->Ebp, c->Esp, GetCurrentThreadId());
	{
		DWORD *stack = (DWORD *)c->Esp;
		h2n_log("    stack (anything that looks like a return address into our code):");
		for (int i = 0, shown = 0; i < 600 && shown < 16; i++)
		{
			if (IsBadReadPtr(stack + i, 4)) break;
			if (is_return_address(stack[i])) { h2n_log(" %08lx", stack[i]); shown++; }
		}
	}
	h2n_log("\n    %u Xbox calls were logged before this point\n", g_sequence);
	h2n_write_stop_file(e, "exception");
	h2n_pad_rumble_stop();                  /* (s10l) after the stop file: a pad is not left vibrating */
	ExitProcess(3);
	return EXCEPTION_EXECUTE_HANDLER;
}

/* (diagnostics) what on the calling thread's stack looks like a return address into our code, as one log line */
void __cdecl h2n_log_callers(const char *what)
{
	volatile DWORD *stack = (volatile DWORD *)__builtin_frame_address(0);
	char line[700];
	int used = snprintf(line, sizeof(line), "       %s - called from:", what);
	for (int i = 0, shown = 0; i < 6000 && shown < 30 && used < (int)sizeof(line) - 12; i++)
	{
		if (IsBadReadPtr((const void *)(stack + i), 4)) break;
		if (is_return_address(stack[i])) { used += snprintf(line + used, sizeof(line) - used, " %08lx", (unsigned long)stack[i]); shown++; }
	}
	h2n_log("%s\n", line);
}

/* where another thread is: its program counter and whatever on its stack looks like a return address into our code.
   Everything is collected while the thread is held and written only after it runs again, so that this can never
   wait on a lock the held thread owns. */
void h2n_report_thread(HANDLE thread, const char *why)
{
	CONTEXT c;
	DWORD found[14];
	int shown = 0, ok;
	char line[700];
	size_t used;
	memset(&c, 0, sizeof(c));
	c.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
	if (SuspendThread(thread) == (DWORD)-1) return;
	ok = GetThreadContext(thread, &c);
	if (ok)
	{
		DWORD *stack = (DWORD *)c.Esp;
		for (int i = 0; i < 600 && shown < 14; i++)
		{
			if (IsBadReadPtr(stack + i, 4)) break;
			if (is_return_address(stack[i])) found[shown++] = stack[i];
		}
	}
	ResumeThread(thread);
	if (!ok) return;
	used = (size_t)snprintf(line, sizeof(line), "    %s: %08lx (esp %08lx); return addresses:", why, c.Eip, c.Esp);
	for (int i = 0; i < shown && used + 10 < sizeof(line); i++) used += (size_t)snprintf(line + used, sizeof(line) - used, " %08lx", found[i]);
	h2n_log("%s\n", line);
}
void h2n_watch_main_thread(void);

/* threads the game starts: logged, so a stop on one of them can be told apart */
HANDLE __stdcall h2n_CreateThread(void *security, SIZE_T stack, LPTHREAD_START_ROUTINE start, void *parameter, DWORD flags, DWORD *id) __asm__("_CreateThread@24");
static HANDLE (WINAPI *real_create_thread(void))(void *, SIZE_T, LPTHREAD_START_ROUTINE, void *, DWORD, DWORD *)
{
	static HANDLE (WINAPI *create)(void *, SIZE_T, LPTHREAD_START_ROUTINE, void *, DWORD, DWORD *);
	if (!create) create = (void *)GetProcAddress(GetModuleHandleA("kernel32.dll"), "CreateThread");
	return create;
}
/* a thread of the layer's own (the vertical blank): not one of the game's, so not logged */
HANDLE h2n_start_thread(LPTHREAD_START_ROUTINE start, void *parameter)
{
	DWORD id = 0;
	return real_create_thread()(NULL, 0, start, parameter, 0, &id);
}
struct game_thread { LPTHREAD_START_ROUTINE start; void *parameter; };
static DWORD WINAPI game_thread_entry(void *context)
{
	struct game_thread g = *(struct game_thread *)context;
	free(context);
	h2n_scrub_stack(0x20000);
	return g.start(g.parameter);
}
HANDLE __stdcall h2n_CreateThread(void *security, SIZE_T stack, LPTHREAD_START_ROUTINE start, void *parameter, DWORD flags, DWORD *id)
{
	DWORD local = 0;
	HANDLE h;
	struct game_thread *g = malloc(sizeof(*g));
	g->start = start; g->parameter = parameter;
	h = real_create_thread()(security, stack < 0x40000 ? 0x40000 : stack, game_thread_entry, g, flags, id ? id : &local);
	h2n_log("[%4u] the game started a thread: entry %08lx, id %lu\n", h2n_next_sequence(), (unsigned long)(ULONG_PTR)start, id ? *id : local);
	return h;
}

int __cdecl h2n_selftest(void);
int h2n_physmem_alias(void);

/* H2N_BREAK=<address>[,<address>...] (hex, addresses in this exe's code from its link map, or in retail's code): the run stops the first
   time one of them is reached, with the usual report and stop file, which show who called it and with what. A way
   to ask "does the game get here, and from where?" of a build that is already out. */
static void set_breaks(const char *list)
{
	while (list && *list)
	{
		char *end;
		DWORD a = strtoul(list, &end, 16), old;
		if (end == list) break;
		int retail = h2n_retail_code_loaded() && a >= RETAIL_FIRST && a < RETAIL_CODE_END;     /* (s10p) retail's code in place too */
		if ((is_our_code(a) || retail) && VirtualProtect((void *)(ULONG_PTR)a, 1, PAGE_EXECUTE_READWRITE, &old))
		{
			*(BYTE *)(ULONG_PTR)a = 0xcc;
			VirtualProtect((void *)(ULONG_PTR)a, 1, old, &old);
			FlushInstructionCache(GetCurrentProcess(), (void *)(ULONG_PTR)a, 1);
			h2n_log("the run will stop when the game reaches %08lx (H2N_BREAK)\n", a);
		}
		else h2n_log("H2N_BREAK: %08lx is not an address in this exe's code\n", a);
		list = *end ? end + 1 : end;
	}
}

/* one line about the system, and where this run keeps its own files */
static void describe_system(void)
{
	char here[400] = "", full_disc[400] = "";
	HMODULE ntdll = GetModuleHandleA("ntdll.dll");
	LONG (WINAPI *get_version)(OSVERSIONINFOW *) = ntdll ? (void *)GetProcAddress(ntdll, "RtlGetVersion") : NULL;
	const char *(__cdecl *wine_version)(void) = ntdll ? (void *)GetProcAddress(ntdll, "wine_get_version") : NULL;
	OSVERSIONINFOW v;
	SYSTEM_INFO s;
	BOOL wow64 = FALSE;
	memset(&v, 0, sizeof(v)); v.dwOSVersionInfoSize = sizeof(v);
	if (get_version) get_version(&v);
	GetNativeSystemInfo(&s);
	IsWow64Process(GetCurrentProcess(), &wow64);
	h2n_log("system: Windows %lu.%lu build %lu%s%s, %s, %lu processors; second memory view at 0xF0000000: %s\n",
		v.dwMajorVersion, v.dwMinorVersion, v.dwBuildNumber, wine_version ? " under Wine " : "", wine_version ? wine_version() : "",
		wow64 ? "64-bit" : "32-bit", s.dwNumberOfProcessors, h2n_physmem_alias() ? "yes" : "no (accesses are redirected)");
	GetCurrentDirectoryA(sizeof(here), here);
	GetFullPathNameA(g_disc_root, sizeof(full_disc), full_disc, NULL);
	h2n_log("the console's other drives are folders under %s\\h2classic ; this log is %s\\h2classic_log.txt\n", here, here);
	if (!_stricmp(here, full_disc))
		h2n_log("    (that is inside the game folder; the game's own files are only ever read)\n");
}
void __fastcall game_main(void) __asm__("?function_12190@@YIXXZ");   /* retail 0x12190: the game's own entry */
extern unsigned char pacing_code[] __asm__("?function_12b0e0@@YIMXZ");   /* retail 0x12b0e0: the frame pacing (see timing.c, H2N_UNLOCK) */
extern unsigned char h2n_pace_clamp; int h2n_unlock_wanted(void); extern short h2n_refresh; extern unsigned short h2n_pace_min; extern int h2n_follow_display;

static int g_settings_read;
/* ---- h2classic\settings.ini (s9w): what the launcher saves; read here before anything looks at an H2N_ value, and turned
 * into those values. A value already in the environment wins (the test launcher and command lines set them that way). ---- */
/* (s10u) The file is read again when H2Classic Settings changes it while the game runs (h2n_live_poll below). The rule
 * stays "a value already in the environment wins": the names that were in the environment at the start are kept, and a
 * later pass leaves them alone; of the others it notes which kind of setting changed. */
enum { LIVE_PICTURE = 1, LIVE_ASPECT = 2, LIVE_PACING = 4, LIVE_FPS = 8, LIVE_AA = 16, LIVE_INPUT = 32 };
#define ENV_MOST 80                      /* (s10v: the keys and the mouse's settings are among them) */
static char g_env_kept[ENV_MOST][24], g_env_ours[ENV_MOST][24];
static unsigned g_env_kept_count, g_env_ours_count, g_live_changed;
static int g_live_pass;
static int named(char (*list)[24], unsigned count, const char *name) { unsigned i; for (i = 0; i < count; i++) if (!strcmp(list[i], name)) return 1; return 0; }
static void setting(const char *name, const char *value)
{
	const char *now = getenv(name);
	char text[300];
	if (!g_live_pass)
	{
		if (now)
		{	/* from the environment (the test launcher, a command line): it wins, now and later */
			if (!named(g_env_ours, g_env_ours_count, name) && !named(g_env_kept, g_env_kept_count, name) && g_env_kept_count < ENV_MOST) snprintf(g_env_kept[g_env_kept_count++], 24, "%s", name);
			return;
		}
		if (g_env_ours_count < ENV_MOST) snprintf(g_env_ours[g_env_ours_count++], 24, "%s", name);
	}
	else
	{
		if (named(g_env_kept, g_env_kept_count, name) || (now && !strcmp(now, value))) return;
		g_live_changed |= !strcmp(name, "H2N_WIDE") || !strcmp(name, "H2N_ASPECT") ? LIVE_ASPECT : !strcmp(name, "H2N_UNLOCK") || !strcmp(name, "H2N_VSYNC") ? LIVE_PACING
			: !strcmp(name, "H2N_FPS") ? LIVE_FPS : !strcmp(name, "H2N_AA") || !strcmp(name, "H2N_SMAA") ? LIVE_AA
			: !strcmp(name, "H2N_RENDER") || !strcmp(name, "H2N_RENDERSCALE") || !strcmp(name, "H2N_WINDOW_SIZE") || !strcmp(name, "H2N_DISPLAY") ? LIVE_PICTURE
			: !strncmp(name, "H2N_KEY_", 8) || !strncmp(name, "H2N_MOUSE_", 10) || !strcmp(name, "H2N_KBM") ? LIVE_INPUT : 0;
	}
	snprintf(text, sizeof(text), "%s=%s", name, value); _putenv(text);
}
static int g_settings_skipped;
static void h2n_settings_apply(void)
{
	FILE *f;
	char line[300], width[16] = "", height[16] = "";
	int any = 0;
	const char *use = getenv("H2N_SETTINGS");
	/* (s10f) H2N_SETTINGS=off: the test launcher's own settings are the whole set; settings.ini (the release launcher's) is
	   not read, so what is unticked there stays off (the user: "settings.ini overrides launcher.json") */
	if (use && (!_stricmp(use, "off") || !strcmp(use, "0"))) { g_settings_skipped = 1; return; }
	f = h2n_real_fopen(".\\h2classic\\settings.ini", "r");
	if (!f) return;
	while (fgets(line, sizeof(line), f))
	{
		char *key = line, *value, *end;
		while (*key == ' ' || *key == '\t') key++;
		if (*key == ';' || *key == '#' || *key == '[' || !(value = strchr(key, '='))) continue;
		*value++ = 0;
		for (end = value + strlen(value); end > value && (end[-1] == '\n' || end[-1] == '\r' || end[-1] == ' ' || end[-1] == '\t'); ) *--end = 0;
		for (end = key + strlen(key); end > key && (end[-1] == ' ' || end[-1] == '\t'); ) *--end = 0;
		while (*value == ' ' || *value == '\t') value++;
		any = 1;
		if (!_stricmp(key, "render_multiplier")) setting("H2N_RENDER", value);      /* (s10t) 1 to 10 times the game's 640x480; (s10z) to 20 */
		else if (!_stricmp(key, "render_scale")) setting("H2N_RENDERSCALE", value);  /* (a settings.ini from before s10t: a percentage) */
		else if (!_stricmp(key, "window_width")) snprintf(width, sizeof(width), "%s", value);
		else if (!_stricmp(key, "window_height")) snprintf(height, sizeof(height), "%s", value);
		else if (!_stricmp(key, "widescreen")) { setting("H2N_WIDE", !_stricmp(value, "4:3") || !strcmp(value, "0") ? "0" : "1"); setting("H2N_ASPECT", value); }
		else if (!_stricmp(key, "display_mode")) setting("H2N_DISPLAY", value);
		else if (!_stricmp(key, "unlock_performance")) setting("H2N_UNLOCK", value);
		else if (!_stricmp(key, "vsync")) setting("H2N_VSYNC", atoi(value) ? "on" : "off");
		else if (!_stricmp(key, "anti_aliasing")) setting("H2N_AA", value);      /* (s10u: "off" is set too, so that a later change away from it is seen as one) */
		else if (!_stricmp(key, "smaa_quality")) setting("H2N_SMAA", value);          /* (s10g) low, medium, high, ultra */
		else if (!_stricmp(key, "fps_counter")) setting("H2N_FPS", value);
		/* (s10v) Keyboard + Mouse Settings: key_<button>=<key>,<key> (Windows key numbers; 256 is the mouse wheel; 0 is none) */
		else if (!_strnicmp(key, "key_", 4) && strlen(key) < 20)
		{
			char name[24]; unsigned i;
			snprintf(name, sizeof(name), "H2N_KEY_%s", key + 4);
			for (i = 8; name[i]; i++) if (name[i] >= 'a' && name[i] <= 'z') name[i] -= 32;
			setting(name, value);
		}
		else if (!_stricmp(key, "keyboard_mouse")) setting("H2N_KBM", value);      /* Enable Keyboard + Mouse */
		else if (!_stricmp(key, "mouse_sensitivity")) setting("H2N_MOUSE_SENS", value);
		else if (!_stricmp(key, "mouse_zoom_sensitivity")) setting("H2N_MOUSE_ZOOM", value);
		else if (!_stricmp(key, "mouse_invert")) setting("H2N_MOUSE_INVERT", value);
		else if (g_live_pass) continue;                 /* (s10u) the rest is read when the game starts only */
		else if (!_stricmp(key, "dlss_preset")) setting("H2N_DLSS_PRESET", value);    /* (s10g) k, j (or default) */
		else if (!_stricmp(key, "skip_intro")) setting("H2N_SKIPINTRO", value);
		else if (!_stricmp(key, "skip_attract")) setting("H2N_SKIPATTRACT", value);
		else if (!_stricmp(key, "network")) setting("H2N_XNET", value);
		else if (!_stricmp(key, "network_peer")) setting("H2N_XNET_PEER", value);
		else if (!_stricmp(key, "network_port")) setting("H2N_XNET_PORT", value);
		else if (!_stricmp(key, "network_link")) setting("H2N_XNET_LINK", value);       /* (s10x) lan (the default: players are found by themselves), tunnel (network_peer, network_port) */
		else if (!_stricmp(key, "network_mac")) setting("H2N_XNET_MAC", value);         /* the console address, aa:bb:cc:dd:ee:ff (otherwise made from this PC) */
	}
	fclose(f);
	if (width[0] && height[0]) { char size[40]; snprintf(size, sizeof(size), "%sx%s", width, height); setting("H2N_WINDOW_SIZE", size); }
	if (any) g_settings_read = 1;
}

/* ---- (s10u) settings changed while the game runs ----
 * H2Classic Settings may stay open after Start Game, or be opened while the game runs. The game makes a named event when
 * it starts; Settings finds it (that is how it knows the game is running, and greys out what only a start can take) and
 * sets it each time it has saved settings.ini. Once a frame, between two frames (gfx9.c, after the frame is shown), the
 * event is looked at; when set, the file is read again and what changed is applied: the window and the picture
 * (Display Mode, Window Size, Render Scale, Aspect Ratio), Unlock Performance, VSync, the FPS counter, and Anti-Aliasing
 * other than a change to or from DLSS. Game Version and the two video settings are for the next start. With
 * H2N_SETTINGS=off (the test launcher) the file is not this run's, and nothing is taken from it. ---- */
static HANDLE g_live_event;
void __cdecl h2n_aspect_live(void);    /* xapi.c */
void __cdecl h2n_pacing_live(void);    /* timing.c */
void __cdecl gx_live(void);            /* gfx9.c */
void __cdecl gx_fps_live(void);
void __cdecl gx_aa_live(void);
void __cdecl h2n_input_live(void);     /* input.c */
static void h2n_live_start(void)
{
	g_live_event = CreateEventA(NULL, FALSE, FALSE, "H2Classic_settings_changed");
}
void __cdecl h2n_live_poll(void)
{
	static int told;
	if (!g_live_event || WaitForSingleObject(g_live_event, 0) != WAIT_OBJECT_0) return;
	if (g_settings_skipped)
	{
		if (!told) { told = 1; h2n_log("settings: h2classic\\settings.ini changed, but this run does not use it (H2N_SETTINGS=off): nothing is taken from it\n"); }
		return;
	}
	g_live_changed = 0;
	g_live_pass = 1; h2n_settings_apply(); g_live_pass = 0;
	if (!g_live_changed) return;
	h2n_log("settings: h2classic\\settings.ini changed while the game runs:%s%s%s%s%s%s\n", g_live_changed & LIVE_PICTURE ? " the window or the render scale;" : "",
		g_live_changed & LIVE_ASPECT ? " the aspect ratio;" : "", g_live_changed & LIVE_PACING ? " Unlock Performance or VSync;" : "",
		g_live_changed & LIVE_FPS ? " the FPS counter;" : "", g_live_changed & LIVE_AA ? " anti-aliasing;" : "",
		g_live_changed & LIVE_INPUT ? " the keys or the mouse;" : "");
	if (g_live_changed & LIVE_PACING) h2n_pacing_live();
	if (g_live_changed & LIVE_ASPECT) h2n_aspect_live();
	if (g_live_changed & (LIVE_PICTURE | LIVE_ASPECT)) gx_live();
	if (g_live_changed & LIVE_FPS) gx_fps_live();
	if (g_live_changed & LIVE_AA) gx_aa_live();
	if (g_live_changed & LIVE_INPUT) h2n_input_live();
}

/* (s10b) a first start with no h2classic\settings.ini (not started from the launcher, which always writes the file before
 * it starts the game): the launcher is opened instead, so the settings are chosen before the game runs. H2N_NOLAUNCHER=1
 * (the test launcher, test scripts) goes straight on. Returns 1 when the launcher was started. */
static int open_launcher_first(void)
{
	FILE *f;
	char exe[MAX_PATH], line[MAX_PATH + 4], *slash;
	STARTUPINFOA si;
	PROCESS_INFORMATION pi;
	BOOL (WINAPI *create)(const char *, char *, void *, void *, BOOL, DWORD, void *, const char *, STARTUPINFOA *, PROCESS_INFORMATION *);
	if (getenv("H2N_NOLAUNCHER") && atoi(getenv("H2N_NOLAUNCHER"))) return 0;
	if ((f = h2n_real_fopen(".\\h2classic\\settings.ini", "r"))) { fclose(f); return 0; }
	GetModuleFileNameA(NULL, exe, sizeof(exe));
	if (!(slash = strrchr(exe, '\\'))) return 0;
	snprintf(slash + 1, sizeof(exe) - (size_t)(slash + 1 - exe), "H2Classic Settings.exe");
	if ((f = h2n_real_fopen(exe, "rb")) == NULL) return 0;          /* no launcher here: go on with the defaults */
	fclose(f);
	snprintf(line, sizeof(line), "\"%s\"", exe);
	memset(&si, 0, sizeof(si)); si.cb = sizeof(si);
	create = (void *)GetProcAddress(GetModuleHandleA("kernel32.dll"), "CreateProcessA");
	if (!create || !create(exe, line, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) return 0;
	CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
	printf("H2Classic: no settings yet (h2classic\\settings.ini): H2Classic Settings is open to choose them; Start Game there starts the game.\n");
	return 1;
}

/* ---- H2N_UNLOCK: the frame pacing's three instructions (timing.c says what they are for). (s10u) A function, called at the
 * start when Unlock Performance is on and again when the setting changes while the game runs: on = the three re-aimed at
 * the layer's values, as before; off = the game's own bytes back, kept from before the first change. ---- */
void h2n_pacing_patch(int on)
{
	/* the one instruction of the frame pacing that reads "a game is in progress" before the raise to two refreshes:
	   mov dl, [ecx+0x1120] ; test dl, dl ; je .. ; cmp dword [ecx+8], 3   (retail 0x12b172) -> mov dl, [h2n_pace_clamp] */
	static const unsigned char want[] = { 0x8a, 0x91, 0x20, 0x11, 0x00, 0x00, 0x84, 0xd2, 0x74 };
	static const unsigned char then[] = { 0x83, 0x79, 0x08, 0x03 };
	/* s9o, as Spooky Ghost's v2.0.8 / v2.0.9 for a refresh rate above 60 (timing.c, h2n_display_rate): the 30 Hz hold
	   cmp ax, 2 ; jg +4 ; mov ax, 2  (retail 0x12b191) -> mov ax, [h2n_pace_min] ; 4 nops, and the read that turns
	   vblanks into seconds  movsx edx, word [0x485ac0]  (retail 0x12b1f6) -> movsx edx, word [h2n_refresh] */
	static const unsigned char hold[] = { 0x66, 0x3d, 0x02, 0x00, 0x7f, 0x04, 0x66, 0xb8, 0x02, 0x00 };
	static unsigned char *at, *at_hold, *at_rate;
	static unsigned found, found_hold, found_rate;
	static unsigned char old_at[6], old_hold[sizeof(hold)], old_rate[7];
	static int searched, patched;
	DWORD old;
	if (!searched)
	{
		unsigned char rate[] = { 0x0f, 0xbf, 0x15, 0xc0, 0x5a, 0x48, 0x00 };      /* (1.0's g_485ac0; s10r: 1.5's is 0x4922e0) */
		unsigned n;
		{ DWORD rate_at = H2N_VA(0x485ac0, 0x4922e0); memcpy(rate + 3, &rate_at, 4); }
		searched = 1;
		for (n = 0; n < 0x180; n++)
			if (!memcmp(pacing_code + n, want, sizeof(want)) && !memcmp(pacing_code + n + sizeof(want) + 1, then, sizeof(then))) { at = pacing_code + n; found++; }
		for (n = 0; n + sizeof(hold) <= 0x1bd; n++)
		{
			if (!memcmp(pacing_code + n, hold, sizeof(hold))) { at_hold = pacing_code + n; found_hold++; }
			if (!memcmp(pacing_code + n, rate, sizeof(rate))) { at_rate = pacing_code + n; found_rate++; }
		}
		if (found == 1) memcpy(old_at, at, sizeof(old_at));
		if (found_hold == 1) memcpy(old_hold, at_hold, sizeof(old_hold));
		if (found_rate == 1) memcpy(old_rate, at_rate, sizeof(old_rate));
	}
	if (!on)
	{
		if (!patched) return;
		VirtualProtect(at, sizeof(old_at), PAGE_EXECUTE_READWRITE, &old); memcpy(at, old_at, sizeof(old_at)); VirtualProtect(at, sizeof(old_at), old, &old);
		if (h2n_follow_display)
		{
			VirtualProtect(at_hold, sizeof(old_hold), PAGE_EXECUTE_READWRITE, &old); memcpy(at_hold, old_hold, sizeof(old_hold)); VirtualProtect(at_hold, sizeof(old_hold), old, &old);
			VirtualProtect(at_rate, sizeof(old_rate), PAGE_EXECUTE_READWRITE, &old); memcpy(at_rate, old_rate, sizeof(old_rate)); VirtualProtect(at_rate, sizeof(old_rate), old, &old);
		}
		FlushInstructionCache(GetCurrentProcess(), pacing_code, 0x1bd);
		h2n_follow_display = 0; patched = 0;
		h2n_log("frame rate: Unlock Performance is off - the game's own frame pacing (its bytes are back as they were)\n");
		return;
	}
	if (patched) return;
	if (found == 1)
	{
		DWORD address = (DWORD)(size_t)&h2n_pace_clamp;
		VirtualProtect(at, 6, PAGE_EXECUTE_READWRITE, &old);
		at[0] = 0x8a; at[1] = 0x15; memcpy(at + 2, &address, 4);
		VirtualProtect(at, 6, old, &old);
		FlushInstructionCache(GetCurrentProcess(), at, 6);
		patched = 1;
		h2n_log("frame rate: H2N_UNLOCK - frames are shown as soon as they are ready (one a vertical blank); the game's ticks stay 30 a second\n");
		if (found_hold == 1 && found_rate == 1)
		{
			DWORD min_address = (DWORD)(size_t)&h2n_pace_min, rate_address = (DWORD)(size_t)&h2n_refresh;
			VirtualProtect(at_hold, sizeof(hold), PAGE_EXECUTE_READWRITE, &old);
			at_hold[0] = 0x66; at_hold[1] = 0xa1; memcpy(at_hold + 2, &min_address, 4); memset(at_hold + 6, 0x90, 4);
			VirtualProtect(at_hold, sizeof(hold), old, &old);
			VirtualProtect(at_rate, 7, PAGE_EXECUTE_READWRITE, &old);
			memcpy(at_rate + 3, &rate_address, 4);
			VirtualProtect(at_rate, 7, old, &old);
			FlushInstructionCache(GetCurrentProcess(), pacing_code, 0x1bd);
			h2n_follow_display = 1;
		}
		else h2n_log("*** frame rate: the frame pacing's 30 Hz hold or refresh read was found %u and %u times instead of once; the vertical blank stays the console's 59.94\n", found_hold, found_rate);
	}
	else h2n_log("*** frame rate: H2N_UNLOCK was asked for, but the frame pacing's instruction was found %u times instead of once; the game's own pacing is left alone\n", found);
}
/* (s10w) The program works in the screen's own pixels. Without this, on a screen Windows scales (125 %, 150 % ...) a window's
 * size is counted in scaled units and Windows stretches the finished picture to the real pixels - one more resizing of
 * every frame, and "the screen's size" would be too small. Per monitor (version 2: Windows 10 1703 and later), else the
 * older call for the whole system; both through user32 by name, as older Windows has neither. H2N_DPI=0 leaves it as it was. */
static void dpi_aware(void)
{
	HMODULE user = GetModuleHandleA("user32.dll");
	BOOL (WINAPI *context)(HANDLE) = user ? (void *)GetProcAddress(user, "SetProcessDpiAwarenessContext") : NULL;
	BOOL (WINAPI *plain)(void) = user ? (void *)GetProcAddress(user, "SetProcessDPIAware") : NULL;
	const char *off = getenv("H2N_DPI");
	if (off && !strcmp(off, "0")) return;
	if (context && context((HANDLE)(LONG_PTR)-4)) return;      /* DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 */
	if (plain) plain();
}
int main(int argc, char **argv)
{
	char xbe_path[600];
	dpi_aware();                            /* (before any window or message box of this program) */
	if (open_launcher_first()) return 0;
	InitializeCriticalSection(&g_log_lock); g_log_lock_ready = 1;
	g_log = h2n_real_fopen("h2classic_log.txt", "w");
	SetUnhandledExceptionFilter(crash_filter);
	h2n_settings_apply();                   /* (s9w: h2classic\settings.ini, before any H2N_ value is read) */
	h2n_live_start();                       /* (s10u: the event H2Classic Settings finds a running game by, and sets when the file changed) */
	h2n_set_stack_mode(getenv("H2N_STACK"));
	if (argc > 1)
	{
		size_t n;
		snprintf(g_disc_root, sizeof(g_disc_root), "%s", argv[1]);
		g_disc_named = 1;
		n = strlen(g_disc_root);
		while (n && (g_disc_root[n - 1] == '\\' || g_disc_root[n - 1] == '/')) g_disc_root[--n] = 0;
	}
	if (argc > 2) snprintf(xbe_path, sizeof(xbe_path), "%s", argv[2]);
	else snprintf(xbe_path, sizeof(xbe_path), "%s\\default.xbe", g_disc_root);
	h2n_log("H2Classic %s: Halo 2's own code as a Windows program; %u Xbox calls are stand-ins\n", H2N_BUILD, h2n_stub_count);
	if (h2n_v15) h2n_log("game version: 1.5 (%s): %u places of this program rewritten for it (%u bytes)\n", g_version_from, g_v15_runs, g_v15_bytes);
	else h2n_log("game version: 1.0 (%s)\n", g_version_from);
	if (g_settings_read) h2n_log("settings: h2classic\\settings.ini (values set in the environment win)\n");
	if (g_settings_skipped) h2n_log("settings: h2classic\\settings.ini not read (H2N_SETTINGS=off: the test launcher's settings only)\n");
	{
		/* the disc as a disc image: named as the first argument, or found in the game folder when that has no default.xbe */
		size_t n = strlen(g_disc_root);
		if (n > 4 && !_stricmp(g_disc_root + n - 4, ".iso"))
		{
			char *slash;
			if (!h2n_iso_use(g_disc_root)) h2n_fatal("%s is not an Xbox disc image (no Xbox file system was found in it)", g_disc_root);
			if (!h2n_iso_find("default.xbe", NULL, NULL, NULL)) h2n_fatal("the disc image %s has no default.xbe", g_disc_root);
			slash = strrchr(g_disc_root, '\\'); if (!slash) slash = strrchr(g_disc_root, '/');
			if (slash) *slash = 0; else snprintf(g_disc_root, sizeof(g_disc_root), ".");
		}
		else if (argc == 2)
		{
			FILE *probe = h2n_real_fopen(xbe_path, "rb");
			if (probe) fclose(probe); else h2n_iso_search(g_disc_root);
		}
		else if (argc < 2)
		{
			/* (s10o) the first of Roms\Halo 2, Roms and Rom that holds the game's default.xbe or a disc image of Halo 2 */
			static const char *const roots[] = { "Roms\\Halo 2", "Roms", "Rom" };
			int i, chosen = 0;
			for (i = 0; i < 3 && !chosen; i++)
			{
				char probe_path[600];
				FILE *probe;
				snprintf(probe_path, sizeof(probe_path), "%s\\default.xbe", roots[i]);
				probe = h2n_real_fopen(probe_path, "rb");
				if (probe) { fclose(probe); chosen = 1; }
				else if (h2n_iso_search(roots[i])) chosen = 1;
				if (chosen)
				{
					snprintf(g_disc_root, sizeof(g_disc_root), "%s", roots[i]);
					snprintf(xbe_path, sizeof(xbe_path), "%s", probe_path);
				}
			}
		}
	}
	if (h2n_iso_active()) h2n_log("game files (the Xbox's D: drive) are read from inside the disc image: %s\n    (the image is only ever read)\n", h2n_iso_path());
	else
	{
		char full[600];
		if (!GetFullPathNameA(g_disc_root, sizeof(full), full, NULL)) snprintf(full, sizeof(full), "%s", g_disc_root);
		h2n_log("game files (the Xbox's D: drive) are read from: %s\n", full);
	}
	{	/* (s10o) the map packs and the 1.5 title update: from Roms\DLC onto H2Classic's hard disk (content.c) */
		void h2n_content_install(const char *dlc_folder);
		const char *dlc = getenv("H2N_DLC");
		h2n_content_install(dlc && *dlc ? dlc : "Roms\\DLC");
	}
	describe_system();
	if (h2n_run_seconds()) h2n_log("this test build stops by itself: once nothing new has happened for 20 seconds, or after %u seconds (H2N_SECONDS=0 lets it run)\n", h2n_run_seconds());
	if (h2n_v15)
	{
		/* (s10p) game version 1.5 runs the title update's own default.xbe, which content.c put on H2Classic's hard disk
		   from Roms\DLC (on the console 1.0 starts it from T:\$u); the disc (D:) stays the game folder or disc image */
		if (argc <= 2) snprintf(xbe_path, sizeof(xbe_path), "h2classic\\T\\$u\\default.xbe");
		h2n_log("the game's code and data: the 1.5 title update, %s\n", xbe_path);
		load_retail_data(xbe_path);
	}
	else load_retail_data(h2n_iso_active() && argc <= 2 ? NULL : xbe_path);
	h2n_watch_main_thread();
	if (getenv("H2N_SELFTEST")) return h2n_selftest() != 0;
	set_breaks(getenv("H2N_BREAK"));
	h2n_log("unused stack is %s\n", h2n_stack_mode_name());
	h2n_scrub_stack(0x100000);
	if (h2n_unlock_wanted()) h2n_pacing_patch(1);      /* (s10u: a function now - Unlock Performance can change while the game runs) */
	h2n_log("entering the game's entry function (retail 0x12190)\n");
	game_main();
	h2n_log("the game's entry function returned after %u logged Xbox calls\n", g_sequence);
	return 0;
}

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
void h2n_pad_rumble_stop(void);

extern char h2n_retail_range[], h2n_retail_range_end[];
struct h2n_section { const char *name; DWORD va, vsize, rsize, crc; };
extern const struct h2n_section h2n_sections[];
extern const DWORD h2n_delta[];
extern const DWORD h2n_fixups[];
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
   Rom folder next to the exe (relative to the current folder, like h2classic\ and the log: the launcher, the test launcher
   and Explorer all start the exe in its own folder). A first argument names another folder or image. */
char g_disc_root[400] = "Rom";
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
static void __cdecl early_init(void)
{
	DWORD old;
	h2n_console_attach();
	if ((DWORD)(ULONG_PTR)h2n_retail_range != RETAIL_FIRST || (DWORD)(ULONG_PTR)h2n_retail_range_end != RETAIL_END)
	{
		char text[200];
		snprintf(text, sizeof(text), "H2Classic: the exe is not laid out as built (retail range at %p)", (void *)h2n_retail_range);
		h2n_tell_user(text);
		ExitProcess(2);
	}
	VirtualProtect((void *)RETAIL_FIRST, RETAIL_END - RETAIL_FIRST, PAGE_READWRITE, &old);
}
__attribute__((section(".CRT$XCB"), used)) static void (__cdecl *const early_init_entry)(void) = early_init;

void h2n_net_setup(const unsigned char *xbe, size_t size);
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
	DWORD *list_table = (DWORD *)0x458a70, *screen_table = (DWORD *)0x453828;
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
			if (!g_disc_named)
				h2n_fatal("Halo 2's files were not found in the Rom folder (%s).\n    Put the v1.0 game's files there (default.xbe and the maps folder), or a disc image of them (.iso).\n"
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
				h2n_fatal("the %s section of %s is not the v1.0 game's.\n    This build takes its data from the unmodified v1.0 default.xbe "
					"(4,861,952 bytes); name that file as the second argument", want->name, xbe_path);
			memcpy((void *)want->va, xbe + h[3], h[4]);
			loaded++; bytes += h[4];
		}
		if (!found) h2n_fatal("%s has no %s section at %08lx: it is not the v1.0 default.xbe", xbe_path, want->name, want->va);
	}
	/* what retail's 407 start-up initialisers leave behind */
	for (const DWORD *d = h2n_delta; d[0]; d = (const DWORD *)((const char *)(d + 2) + ((d[1] + 3) & ~3u)))
	{
		memcpy((void *)d[0], d + 2, d[1]);
		runs++;
	}
	*(DWORD *)0x557c88 = GetCurrentThreadId();      /* retail initialiser 0x377ef0: the main thread's id */
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
	main_menu_six();                        /* (s10j) the main menu's sixth item, EXIT (after retail's code, which also writes table entries) */
	h2n_lods_retail();                      /* (s10l) Increased LODs in retail's code: the instances' fade, the objects' level of detail */
	h2n_title_keys(xbe, (size_t)size);      /* the keys saved files are signed with come from the certificate in the XBE */
	if (h2n_env_int("H2N_SKIPATTRACT", 0) && h2n_retail_code_loaded() && *(BYTE *)0x223a21 == 0x55)
	{	/* s9w: no attract movie when the menu stands idle: retail's attract_mode_play_movie (0x223a21, called by 0x1479c3 when
		   0x223976 says the menu has been idle long enough) returns at once */
		DWORD was;
		VirtualProtect((void *)0x223a21, 1, PAGE_EXECUTE_READWRITE, &was);
		*(BYTE *)0x223a21 = 0xc3;
		VirtualProtect((void *)0x223a21, 1, was, &was);
		h2n_log("H2N_SKIPATTRACT=1: the attract movie is not played when the menu stands idle\n");
	}
	h2n_net_setup(xbe, (size_t)size);       /* the game's socket and XNet calls: the PC's, or (H2N_XNET=retail) the XBE's library */
	free(xbe);
	VirtualProtect((void *)RETAIL_FIRST, RETAIL_CODE_END - RETAIL_FIRST, h2n_retail_code_loaded() ? PAGE_EXECUTE_READWRITE : PAGE_NOACCESS, &old);
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
	return a >= RETAIL_END && a < code_end;
}
/* a value on a stack that points just after a call instruction in our code: text and numbers that merely fall in
   the code's address range are left out */
static int is_return_address(DWORD a)
{
	const BYTE *p = (const BYTE *)a;
	if (h2n_retail_code_loaded() && a >= 0x12007 && a < 0x411000) ;         /* retail code in place counts too */
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

/* H2N_BREAK=<address>[,<address>...] (hex, addresses in this exe's code from its link map): the run stops the first
   time one of them is reached, with the usual report and stop file, which show who called it and with what. A way
   to ask "does the game get here, and from where?" of a build that is already out. */
static void set_breaks(const char *list)
{
	while (list && *list)
	{
		char *end;
		DWORD a = strtoul(list, &end, 16), old;
		if (end == list) break;
		if (is_our_code(a) && VirtualProtect((void *)(ULONG_PTR)a, 1, PAGE_EXECUTE_READWRITE, &old))
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
static void setting(const char *name, const char *value)
{
	if (!getenv(name)) { char text[300]; snprintf(text, sizeof(text), "%s=%s", name, value); _putenv(text); }
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
		if (!_stricmp(key, "render_scale")) setting("H2N_RENDERSCALE", value);
		else if (!_stricmp(key, "window_width")) snprintf(width, sizeof(width), "%s", value);
		else if (!_stricmp(key, "window_height")) snprintf(height, sizeof(height), "%s", value);
		else if (!_stricmp(key, "widescreen")) { setting("H2N_WIDE", !_stricmp(value, "4:3") || !strcmp(value, "0") ? "0" : "1"); setting("H2N_ASPECT", value); }
		else if (!_stricmp(key, "display_mode")) setting("H2N_DISPLAY", value);
		else if (!_stricmp(key, "unlock_performance")) setting("H2N_UNLOCK", value);
		else if (!_stricmp(key, "vsync")) setting("H2N_VSYNC", atoi(value) ? "on" : "off");
		else if (!_stricmp(key, "anti_aliasing")) { if (_stricmp(value, "off") && strcmp(value, "0")) setting("H2N_AA", value); }
		else if (!_stricmp(key, "smaa_quality")) setting("H2N_SMAA", value);          /* (s10g) low, medium, high, ultra */
		else if (!_stricmp(key, "dlss_preset")) setting("H2N_DLSS_PRESET", value);    /* (s10g) k, j (or default) */
		else if (!_stricmp(key, "fps_counter")) setting("H2N_FPS", value);
		else if (!_stricmp(key, "skip_intro")) setting("H2N_SKIPINTRO", value);
		else if (!_stricmp(key, "skip_attract")) setting("H2N_SKIPATTRACT", value);
		else if (!_stricmp(key, "network")) setting("H2N_XNET", value);
		else if (!_stricmp(key, "network_peer")) setting("H2N_XNET_PEER", value);
		else if (!_stricmp(key, "network_port")) setting("H2N_XNET_PORT", value);
	}
	fclose(f);
	if (width[0] && height[0]) { char size[40]; snprintf(size, sizeof(size), "%sx%s", width, height); setting("H2N_WINDOW_SIZE", size); }
	if (any) g_settings_read = 1;
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

int main(int argc, char **argv)
{
	char xbe_path[600];
	if (open_launcher_first()) return 0;
	InitializeCriticalSection(&g_log_lock); g_log_lock_ready = 1;
	g_log = h2n_real_fopen("h2classic_log.txt", "w");
	SetUnhandledExceptionFilter(crash_filter);
	h2n_settings_apply();                   /* (s9w: h2classic\settings.ini, before any H2N_ value is read) */
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
		else if (argc <= 2)
		{
			FILE *probe = h2n_real_fopen(xbe_path, "rb");
			if (probe) fclose(probe); else h2n_iso_search(g_disc_root);
		}
	}
	if (h2n_iso_active()) h2n_log("game files (the Xbox's D: drive) are read from inside the disc image: %s\n    (the image is only ever read)\n", h2n_iso_path());
	else
	{
		char full[600];
		if (!GetFullPathNameA(g_disc_root, sizeof(full), full, NULL)) snprintf(full, sizeof(full), "%s", g_disc_root);
		h2n_log("game files (the Xbox's D: drive) are read from: %s\n", full);
	}
	describe_system();
	if (h2n_run_seconds()) h2n_log("this test build stops by itself: once nothing new has happened for 20 seconds, or after %u seconds (H2N_SECONDS=0 lets it run)\n", h2n_run_seconds());
	load_retail_data(h2n_iso_active() && argc <= 2 ? NULL : xbe_path);
	h2n_watch_main_thread();
	if (getenv("H2N_SELFTEST")) return h2n_selftest() != 0;
	set_breaks(getenv("H2N_BREAK"));
	h2n_log("unused stack is %s\n", h2n_stack_mode_name());
	h2n_scrub_stack(0x100000);
	if (h2n_unlock_wanted())
	{
		/* the one instruction of the frame pacing that reads "a game is in progress" before the raise to two refreshes:
		   mov dl, [ecx+0x1120] ; test dl, dl ; je .. ; cmp dword [ecx+8], 3   (retail 0x12b172) -> mov dl, [h2n_pace_clamp] */
		static const unsigned char want[] = { 0x8a, 0x91, 0x20, 0x11, 0x00, 0x00, 0x84, 0xd2, 0x74 };
		static const unsigned char then[] = { 0x83, 0x79, 0x08, 0x03 };
		unsigned char *at = NULL; unsigned n, found = 0;
		for (n = 0; n < 0x180; n++)
			if (!memcmp(pacing_code + n, want, sizeof(want)) && !memcmp(pacing_code + n + sizeof(want) + 1, then, sizeof(then))) { at = pacing_code + n; found++; }
		/* s9o, as Spooky Ghost's v2.0.8 / v2.0.9 for a refresh rate above 60 (timing.c, h2n_display_rate): the 30 Hz hold
		   cmp ax, 2 ; jg +4 ; mov ax, 2  (retail 0x12b191) -> mov ax, [h2n_pace_min] ; 4 nops, and the read that turns
		   vblanks into seconds  movsx edx, word [0x485ac0]  (retail 0x12b1f6) -> movsx edx, word [h2n_refresh] */
		static const unsigned char hold[] = { 0x66, 0x3d, 0x02, 0x00, 0x7f, 0x04, 0x66, 0xb8, 0x02, 0x00 };
		static const unsigned char rate[] = { 0x0f, 0xbf, 0x15, 0xc0, 0x5a, 0x48, 0x00 };
		unsigned char *at_hold = NULL, *at_rate = NULL; unsigned found_hold = 0, found_rate = 0;
		for (n = 0; n + sizeof(hold) <= 0x1bd; n++)
		{
			if (!memcmp(pacing_code + n, hold, sizeof(hold))) { at_hold = pacing_code + n; found_hold++; }
			if (!memcmp(pacing_code + n, rate, sizeof(rate))) { at_rate = pacing_code + n; found_rate++; }
		}
		if (found == 1)
		{
			DWORD old, address = (DWORD)(size_t)&h2n_pace_clamp;
			VirtualProtect(at, 6, PAGE_EXECUTE_READWRITE, &old);
			at[0] = 0x8a; at[1] = 0x15; memcpy(at + 2, &address, 4);
			VirtualProtect(at, 6, old, &old);
			FlushInstructionCache(GetCurrentProcess(), at, 6);
			h2n_log("frame rate: H2N_UNLOCK - frames are shown as soon as they are ready (one a vertical blank); the game's ticks stay 30 a second\n");
			if (found_hold == 1 && found_rate == 1)
			{
				DWORD min_address = (DWORD)(size_t)&h2n_pace_min, rate_address = (DWORD)(size_t)&h2n_refresh;
				VirtualProtect(at_hold, sizeof(hold), PAGE_EXECUTE_READWRITE, &old);
				at_hold[0] = 0x66; at_hold[1] = 0xa1; memcpy(at_hold + 2, &min_address, 4); memset(at_hold + 6, 0x90, 4);
				VirtualProtect(at_hold, sizeof(hold), old, &old);
				VirtualProtect(at_rate, sizeof(rate), PAGE_EXECUTE_READWRITE, &old);
				memcpy(at_rate + 3, &rate_address, 4);
				VirtualProtect(at_rate, sizeof(rate), old, &old);
				FlushInstructionCache(GetCurrentProcess(), pacing_code, 0x1bd);
				h2n_follow_display = 1;
			}
			else h2n_log("*** frame rate: the frame pacing's 30 Hz hold or refresh read was found %u and %u times instead of once; the vertical blank stays the console's 59.94\n", found_hold, found_rate);
		}
		else h2n_log("*** frame rate: H2N_UNLOCK was asked for, but the frame pacing's instruction was found %u times instead of once; the game's own pacing is left alone\n", found);
	}
	h2n_log("entering the game's entry function (retail 0x12190)\n");
	game_main();
	h2n_log("the game's entry function returned after %u logged Xbox calls\n", g_sequence);
	return 0;
}

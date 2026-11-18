/* H2Classic: retail's own code, in place, for what the decompilation does not have yet.
 *
 * The exe keeps the console's image range free and already holds the game's data there. Here the XBE's code sections
 * are loaded at their own addresses as well, and each stub the decompilation has for a function without source
 * (object creation, parts of biped movement, the decompressor's core ...) is made to jump to retail's function. That
 * function runs exactly as on the console: it calls retail's other functions directly, reads and writes the same
 * data as our code, and comes back to our code through its return, or through the function pointers in the data,
 * which point at our functions. Only functions that retail compiled with the calling convention the stub declares
 * are routed this way (scripts/retailconv.py checks that from the code).
 *
 * What retail code cannot do here is reach the console itself: a kernel function it imports stops the run with a
 * report unless the layer has it below, and the console's Direct3D writes to the graphics chip's registers, which
 * faults at 0xFDxxxxxx. Nothing in the renderer is routed. */
#include "h2n.h"
#include <malloc.h>

struct code_section { const char *name; DWORD va, size, crc; };
struct kernel_import { DWORD ordinal; const char *name; void *trap; };
struct route { BYTE *stub; DWORD retail; const char *name; DWORD kind; };
struct fixup { DWORD address, retail; BYTE *ours; };
extern const struct fixup h2n_fixups[];
extern const struct code_section h2n_code_sections[];
extern const struct kernel_import h2n_kernel_imports[];
extern const DWORD h2n_kernel_table;
extern const struct route h2n_routes[];
struct library_patch { DWORD retail; BYTE *ours; const char *name; };
extern const struct library_patch h2n_library_patches[];
extern const struct library_patch h2n_game_patches[];
struct known_function { DWORD retail; BYTE *ours; };
extern const struct known_function h2n_all_functions[];
extern const DWORD h2n_pointer_retail[];
extern const struct fixup h2n_code_immediates[];
DWORD h2n_crc32(const unsigned char *p, size_t n);
static int g_loaded;
int h2n_retail_code_loaded(void) { return g_loaded; }
int h2n_xnet_retail(void);                              /* xnetx.c: H2N_XNET=retail */
int h2n_xnet_keeps_retail(const char *name);
void *h2n_kernel_by_name(const char *name);             /* xkernel.c */

/* a kernel function retail code asked for that the layer does not have */
void __cdecl h2n_kernel_trap(DWORD ordinal, DWORD return_address)
{
	const char *name = "?";
	for (const struct kernel_import *k = h2n_kernel_imports; k->ordinal; k++) if (k->ordinal == ordinal) name = k->name;
	h2n_log("\n*** stopped: retail code (near %08lx) called the console's kernel function %s (#%lu), which this build does not have\n",
		return_address, name, ordinal);
	h2n_write_stop_file(NULL, "kernel function");
	ExitProcess(4);
}

static void *kernel_function(DWORD ordinal)
{
	HMODULE ntdll = GetModuleHandleA("ntdll.dll");
	switch (ordinal)
	{
	case 277: return (void *)GetProcAddress(ntdll, "RtlEnterCriticalSection");
	case 294: return (void *)GetProcAddress(ntdll, "RtlLeaveCriticalSection");
	case 291: return (void *)GetProcAddress(ntdll, "RtlInitializeCriticalSection");
	case 306: return (void *)GetProcAddress(ntdll, "RtlTryEnterCriticalSection");
	/* for the WMA decoder (xdsound.cpp), which may yield, signal an event or translate a status: the same calls with
	   the same arguments on Windows */
	case 238: return (void *)GetProcAddress(ntdll, "NtYieldExecution");
	case 225: return (void *)GetProcAddress(ntdll, "NtSetEvent");
	case 301: return (void *)GetProcAddress(ntdll, "RtlNtStatusToDosError");
	}
	return NULL;
}

/* H2N_UNROUTE=<retail addresses>: these stay the source's functions (for finding which route changes something) */
static int unrouted(DWORD va)
{
	const char *list = getenv("H2N_UNROUTE"), *tick = getenv("H2N_TICK");
	/* the game's tick (0x137fe0) and the director's frame update (0x1554b0) are the source's functions unless
	 * H2N_TICK=retail asks for retail's (an experiment: see gen_code.py) */
	if ((va == 0x137fe0 || va == 0x1554b0) && !(tick && !strcmp(tick, "retail"))) return 1;
	while (list && *list)
	{
		char *end;
		if (strtoul(list, &end, 16) == va) return 1;
		if (end == list) break;
		list = *end ? end + 1 : end;
	}
	return 0;
}

/* H2N_NEWROUTES=off: the routes s9f added are left out (s9e's set), for comparing */
static int newroutes_off(void)
{
	const char *v = getenv("H2N_NEWROUTES");
	return v && !strcmp(v, "off");
}
/* H2N_MOVIES=off: the game's Bink calls stay the source's stand-ins (s9r's behaviour: no movie starts) */
static int movies_off(void)
{
	static int told;
	const char *v = getenv("H2N_MOVIES");
	int off = v && !strcmp(v, "off");
	if (off && !told++) h2n_log("H2N_MOVIES=off: no movie plays (the game's Bink calls stay the source's stand-ins)\n");
	return off;
}

void h2n_load_retail_code(const BYTE *xbe, size_t size)
{
	DWORD base = *(const DWORD *)(xbe + 0x104), count = *(const DWORD *)(xbe + 0x11c), headers = *(const DWORD *)(xbe + 0x120) - base;
	unsigned loaded = 0, bytes = 0, routed = 0, implemented = 0, imports = 0, patched = 0, handed_back = 0, tables = 0, entries = 0, immediates = 0;
	DWORD *slot = (DWORD *)(ULONG_PTR)h2n_kernel_table;
	for (const struct code_section *want = h2n_code_sections; want->name; want++)
	{
		int found = 0;
		for (DWORD i = 0; i < count && !found; i++)
		{
			const DWORD *h = (const DWORD *)(xbe + headers + i * 56);          /* flags, address, size, file offset, file size */
			if (h[1] != want->va || h[4] != want->size || (size_t)h[3] + h[4] > size) continue;
			if (h2n_crc32(xbe + h[3], h[4]) != want->crc)
				h2n_fatal("the %s section of the default.xbe is not the v1.0 game's", want->name);
			memcpy((void *)(ULONG_PTR)want->va, xbe + h[3], h[4]);
			loaded++; bytes += h[4]; found = 1;
		}
		if (!found) h2n_fatal("the default.xbe has no %s section at %08lx: it is not the v1.0 game's", want->name, want->va);
	}
	/* the kernel functions retail code imports: the layer's, or a trap that says which one was wanted */
	for (const struct kernel_import *k = h2n_kernel_imports; k->ordinal; k++, slot++, imports++)
	{
		void *have = kernel_function(k->ordinal);
		/* H2N_XNET=retail: the XBE's network library runs, on the kernel services in xkernel.c */
		if (!have && h2n_xnet_retail()) have = h2n_kernel_by_name(k->name);
		*slot = (DWORD)(ULONG_PTR)(have ? have : k->trap);
		implemented += have != NULL;
	}
	/* the stubs that become retail's functions */
	for (const struct route *r = h2n_routes; r->stub; r++, routed++)
	{
		DWORD old;
		BYTE *p = r->stub;
		if (unrouted(r->retail) || ((r->kind & 0x100) && newroutes_off()) || ((r->kind & 0x200) && movies_off())) { routed--; continue; }
		VirtualProtect(p, 32, PAGE_EXECUTE_READWRITE, &old);
		if ((r->kind & 0xff) == 6)
		{	/* s9x: our callers pass two arguments in ecx and edx (fastcall); retail's function takes the second in ebx and the
			   first on the stack (ret 4) and does not keep ebx, which our callers expect kept (network_observer_update_connection
			   0x76ff0) */
			*p++ = 0x53;                                 /* push ebx */
			*p++ = 0x89; *p++ = 0xd3;                    /* mov ebx, edx */
			*p++ = 0x51;                                 /* push ecx */
			p[0] = 0xe8; *(DWORD *)(p + 1) = r->retail - ((DWORD)(ULONG_PTR)p + 5); p += 5;    /* call retail's */
			*p++ = 0x5b;                                 /* pop ebx */
			*p++ = 0xc3;                                 /* ret */
			VirtualProtect(r->stub, 32, old, &old);
			continue;
		}
		if ((r->kind & 0xff) == 5)
		{
			/* our callers pass (this in ecx; address, type, message on the stack; callee pops 12); retail's function takes the
			   type in eax, the address in edx and only the message on the stack (s9u: the network message dispatcher 0x938e0) */
			static const BYTE adapter[] = { 0x8b, 0x54, 0x24, 0x08,        /* mov edx, [esp+8]: the type */
				0x8b, 0x04, 0x24, 0x89, 0x44, 0x24, 0x08,                 /* the return address moves up over it */
				0x89, 0xd0,                                               /* mov eax, edx */
				0x8b, 0x54, 0x24, 0x04,                                   /* mov edx, [esp+4]: the address */
				0x83, 0xc4, 0x08 };                                       /* add esp, 8: return address, message */
			memcpy(p, adapter, sizeof(adapter));
			p += sizeof(adapter);
		}
		else if ((r->kind & 0xff) == 4)
		{
			/* our callers pass the second argument in edx; retail's function wants it in eax (the first stays in ecx) */
			*p++ = 0x89; *p++ = 0xd0;                    /* mov eax, edx */
		}
		else if ((r->kind & 0xff) == 3)
		{
			/* our callers pass the one argument in ecx; retail's function wants it in eax */
			*p++ = 0x89; *p++ = 0xc8;                    /* mov eax, ecx */
		}
		else if (r->kind & 0xff)
		{
			/* our callers pass the arguments in ecx (and edx); retail's function wants them on the stack */
			*p++ = 0x58;                                 /* pop eax: the return address */
			if ((r->kind & 0xff) == 2) *p++ = 0x52;               /* push edx */
			*p++ = 0x51;                                 /* push ecx */
			*p++ = 0x50;                                 /* push eax */
			/* and whatever in the game's data points at this function is called retail's way: give it retail's */
			for (const struct fixup *f = h2n_fixups; f->address; f++)
				if (f->ours == r->stub) *(DWORD *)(ULONG_PTR)f->address = f->retail;
		}
		p[0] = 0xe9;
		*(DWORD *)(p + 1) = r->retail - ((DWORD)(ULONG_PTR)p + 5);
		VirtualProtect(r->stub, 32, old, &old);
	}
	/* retail's copies of the library functions the native build has: jump to ours */
	for (const struct library_patch *l = h2n_library_patches; l->retail; l++, patched++)
	{
		BYTE *at = (BYTE *)(ULONG_PTR)l->retail;
		if (h2n_xnet_keeps_retail(l->name)) { patched--; continue; }     /* H2N_XNET=retail: the library's own (xnetx.c) */
		at[0] = 0xe9;
		*(DWORD *)(at + 1) = (DWORD)(ULONG_PTR)l->ours - (l->retail + 5);
	}
	/* retail's C library allocation functions that the signatures did not name (its free already leads to ours, above):
	 * the C library's own, so that a block retail code takes is one the same library can free. The XBE's WMA decoder
	 * (xdsound.cpp) allocates with them; retail's own heap behind them is never set up here. */
	{
		static const struct { DWORD retail; void *ours; } allocation[] = {
			{ 0x00322e16, (void *)malloc }, { 0x00328283, (void *)calloc }, { 0x003241e9, (void *)realloc }, { 0x003243b8, (void *)_msize } };
		for (unsigned i = 0; i < sizeof(allocation) / sizeof(allocation[0]); i++, patched++)
		{
			BYTE *at = (BYTE *)(ULONG_PTR)allocation[i].retail;
			at[0] = 0xe9;
			*(DWORD *)(at + 1) = (DWORD)(ULONG_PTR)allocation[i].ours - (allocation[i].retail + 5);
		}
	}
	/* retail's copies of the game functions that call the console's Direct3D themselves: jump to our build's version,
	 * so that retail code which reaches one (from a routed function) draws through this build and not through retail's
	 * Direct3D, which is never started */
	for (const struct library_patch *l = h2n_game_patches; l->retail; l++, handed_back++)
	{
		BYTE *at = (BYTE *)(ULONG_PTR)l->retail;
		at[0] = 0xe9;
		*(DWORD *)(at + 1) = (DWORD)(ULONG_PTR)l->ours - (l->retail + 5);
	}
	/* the numbers in retail's code that name a game function (compared with, stored, handed on as a callback): this
	 * build's function, as in the data. Retail's 0x155760 compares a player's camera procedure with 0x23c110; the pointer
	 * it finds there is ours, so it never matched, and the zoom stayed off. (H2N_CODEPTRS=retail: not done) */
	{
		const char *mode = getenv("H2N_CODEPTRS");
		if (!mode || strcmp(mode, "retail"))
			for (const struct fixup *f = h2n_code_immediates; f->address; f++)
				if (*(DWORD *)(ULONG_PTR)f->address == f->retail) { *(DWORD *)(ULONG_PTR)f->address = (DWORD)(ULONG_PTR)f->ours; immediates++; }
	}
	/* unproven functions that retail's tables point to and that could not be routed whole: the table entries go back to
	 * retail's address. Whoever calls through a table passes the arguments as the table's type says, which is retail's
	 * way; direct calls from the source's functions still reach the source's draft. (H2N_TABLES=source: not done) */
	{
		const char *mode = getenv("H2N_TABLES");
		if (!mode || strcmp(mode, "source"))
			for (const DWORD *t = h2n_pointer_retail; *t; t++, tables++)
				for (const struct fixup *f = h2n_fixups; f->address; f++)
					if (f->retail == *t) { *(DWORD *)(ULONG_PTR)f->address = f->retail; entries++; }
	}
	/* switches for finding which function of the source misbehaves: comma-separated retail addresses.
	 *   H2N_ROUTE    our function jumps to retail's (only right where retail takes the arguments as the source declares)
	 *   H2N_BACK     retail's function jumps to ours (the same condition) */
	for (int which = 0; which < 2; which++)
	{
		const char *list = getenv(which ? "H2N_BACK" : "H2N_ROUTE");
		while (list && *list)
		{
			char *end;
			DWORD va = strtoul(list, &end, 16), old;
			if (end == list) break;
			const struct known_function *k = h2n_all_functions;
			while (k->retail && k->retail != va) k++;
			if (!k->retail) h2n_log("%s: no function of the source at %08lx\n", which ? "H2N_BACK" : "H2N_ROUTE", va);
			else
			{
				BYTE *from = which ? (BYTE *)(ULONG_PTR)va : k->ours;
				DWORD to = which ? (DWORD)(ULONG_PTR)k->ours : va;
				VirtualProtect(from, 8, PAGE_EXECUTE_READWRITE, &old);
				from[0] = 0xe9; *(DWORD *)(from + 1) = to - ((DWORD)(ULONG_PTR)from + 5);
				VirtualProtect(from, 8, old, &old);
				h2n_log("%s: %08lx %s\n", which ? "H2N_BACK: retail's code leads to the source's at" : "H2N_ROUTE: the source's function runs as retail's code at", va, "");
			}
			list = *end ? end + 1 : end;
		}
	}
	FlushInstructionCache(GetCurrentProcess(), NULL, 0);
	g_loaded = 1;
	h2n_log("retail code in place: %u sections (%u bytes); %u functions the source lacks or declares wrongly run from it; %u more are reached through %u table entries that stay retail's; %u library functions and %u game functions in it lead to ours; %u function addresses in it re-aimed; of the %u kernel functions it imports %u are provided\n",
		loaded, bytes, routed, tables, entries, patched, handed_back, immediates, imports, implemented);
}

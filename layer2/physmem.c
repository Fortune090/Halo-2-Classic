/* H2Classic: the Xbox's physical memory.
 *
 * On the console all RAM is visible at 0x80000000 + physical address, and again (write-combined, for filling textures
 * and frame buffers) at 0xF0000000 + physical address. The game relies on both: it takes one fixed block at physical
 * 0x61000-0x3145000 whose saved state holds raw addresses, and it ORs 0xF0000000 into pointers Direct3D hands it.
 * So one block of shared memory is mapped at both places. Physical 0-64 MB is the console's own; everything above
 * that is ours (64 MB), for what the Xbox's Direct3D would take from contiguous memory (frame buffers, created textures). */
#include "h2n.h"

#define PHYS_SIZE   0x08000000u            /* 128 MB, as on an upgraded console */
#define PHYS_VIEW   0x80000000u
#define PHYS_ALIAS  0xf0000000u
#define ARENA_FIRST 0x04000000u            /* above the console's 64 MB */

static int g_alias;
static CRITICAL_SECTION g_arena_lock;
struct block { DWORD offset, size; int used; struct block *next; };
static struct block *g_blocks;

/* ---- when there is no view at 0xF0000000: redirect each access through it to the view at 0x80000000 ---- */
static LONG g_alias_redirects;
static int alias_fix(DWORD *reg)
{
	if (*reg < PHYS_ALIAS || *reg >= PHYS_ALIAS + PHYS_SIZE) return 0;
	*reg -= PHYS_ALIAS - PHYS_VIEW;
	return 1;
}
static LONG CALLBACK alias_handler(EXCEPTION_POINTERS *e)
{
	const EXCEPTION_RECORD *r = e->ExceptionRecord;
	CONTEXT *c = e->ContextRecord;
	DWORD *regs[8] = { &c->Eax, &c->Ecx, &c->Edx, &c->Ebx, &c->Esp, &c->Ebp, &c->Esi, &c->Edi };
	const BYTE *p = (const BYTE *)c->Eip;
	int fixed = 0, address16 = 0;
	BYTE op;
	DWORD touched;
	if (r->ExceptionCode != EXCEPTION_ACCESS_VIOLATION || r->NumberParameters < 2) return EXCEPTION_CONTINUE_SEARCH;
	touched = (DWORD)r->ExceptionInformation[1];
	if (touched < PHYS_ALIAS || touched >= PHYS_ALIAS + PHYS_SIZE) return EXCEPTION_CONTINUE_SEARCH;
	/* which registers form the address: the string instructions use esi/edi, everything else has a ModRM byte */
	for (;; p++)
	{
		if (*p == 0x67) address16 = 1;
		else if (!(*p == 0x66 || *p == 0xf2 || *p == 0xf3 || *p == 0xf0 || *p == 0x2e || *p == 0x36 || *p == 0x3e || *p == 0x26 || *p == 0x64 || *p == 0x65)) break;
	}
	op = *p++;
	if ((op >= 0xa4 && op <= 0xa7) || (op >= 0xaa && op <= 0xaf)) fixed = alias_fix(&c->Esi) + alias_fix(&c->Edi);
	else if (op == 0xd7) fixed = alias_fix(&c->Ebx);
	else if (!(op >= 0xa0 && op <= 0xa3) && !address16)
	{
		BYTE modrm;
		if (op == 0x0f) { op = *p++; if (op == 0x38 || op == 0x3a) p++; }
		modrm = *p++;
		if ((modrm >> 6) != 3)
		{
			int rm = modrm & 7;
			if (rm == 4)
			{
				BYTE sib = *p;
				if (!((sib & 7) == 5 && (modrm >> 6) == 0)) fixed += alias_fix(regs[sib & 7]);
				if (((sib >> 3) & 7) != 4 && (sib >> 6) == 0) fixed += alias_fix(regs[(sib >> 3) & 7]);
			}
			else if (!(rm == 5 && (modrm >> 6) == 0)) fixed = alias_fix(regs[rm]);
		}
	}
	if (!fixed) return EXCEPTION_CONTINUE_SEARCH;
	if (InterlockedIncrement(&g_alias_redirects) == 1)
		h2n_log("       first access through the 0xF0000000 view (at %08lx, touching %08lx): redirected to the 0x80000000 view\n", (unsigned long)c->Eip, (unsigned long)touched);
	return EXCEPTION_CONTINUE_EXECUTION;
}

void h2n_tell_user(const char *text);
static void __cdecl physmem_init(void)
{
	HANDLE mapping = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE | SEC_COMMIT, 0, PHYS_SIZE, NULL);
	void *view = mapping ? MapViewOfFileEx(mapping, FILE_MAP_ALL_ACCESS, 0, 0, PHYS_SIZE, (void *)PHYS_VIEW) : NULL;
	if (!view)
	{
		char text[300];
		snprintf(text, sizeof(text), "H2Classic: cannot place the game's memory at 0x80000000 (error %lu).\n"
			"This needs a 64-bit Windows, where a 32-bit program may use addresses above 2 GB.", GetLastError());
		h2n_tell_user(text);
		ExitProcess(2);
	}
	g_alias = MapViewOfFileEx(mapping, FILE_MAP_ALL_ACCESS, 0, 0, PHYS_SIZE, (void *)PHYS_ALIAS) != NULL;
	/* where the second view cannot be placed (Wine keeps its own libraries that high), an access through it is
	   caught and its pointer moved to the first view: slower, but the same memory */
	if (!g_alias) AddVectoredExceptionHandler(1, alias_handler);
	InitializeCriticalSection(&g_arena_lock);
	g_blocks = calloc(1, sizeof(*g_blocks));
	g_blocks->offset = ARENA_FIRST; g_blocks->size = PHYS_SIZE - ARENA_FIRST;
}
__attribute__((section(".CRT$XCC"), used)) static void (__cdecl *const physmem_init_entry)(void) = physmem_init;
int h2n_physmem_alias(void) { return g_alias; }

/* memory the Xbox's Direct3D would allocate itself; 4 KB granular, zeroed */
void *__cdecl h2n_contig_alloc(unsigned size)
{
	void *result = NULL;
	size = (size + 0xfffu) & ~0xfffu;
	if (!size) size = 0x1000;
	EnterCriticalSection(&g_arena_lock);
	for (struct block *b = g_blocks; b; b = b->next)
	{
		if (b->used || b->size < size) continue;
		if (b->size > size)
		{
			struct block *rest = calloc(1, sizeof(*rest));
			rest->offset = b->offset + size; rest->size = b->size - size; rest->next = b->next;
			b->next = rest; b->size = size;
		}
		b->used = 1;
		result = (void *)(PHYS_VIEW + b->offset);
		break;
	}
	LeaveCriticalSection(&g_arena_lock);
	if (result) memset(result, 0, size);
	else h2n_log("       out of contiguous memory for %u bytes\n", size);
	return result;
}
void __cdecl h2n_contig_free(void *address)
{
	DWORD offset = (DWORD)(ULONG_PTR)address - PHYS_VIEW;
	if (!address) return;
	EnterCriticalSection(&g_arena_lock);
	for (struct block *b = g_blocks, *previous = NULL; b; previous = b, b = b->next)
	{
		if (b->offset != offset || !b->used) continue;
		b->used = 0;
		if (b->next && !b->next->used) { struct block *n = b->next; b->size += n->size; b->next = n->next; free(n); }
		if (previous && !previous->used) { previous->size += b->size; previous->next = b->next; free(b); }
		break;
	}
	LeaveCriticalSection(&g_arena_lock);
}

void *__stdcall h2n_XPhysicalAlloc(SIZE_T size, ULONG_PTR physical, ULONG_PTR alignment, DWORD protect) __asm__("_XPhysicalAlloc@16");
void *__stdcall h2n_XPhysicalAlloc(SIZE_T size, ULONG_PTR physical, ULONG_PTR alignment, DWORD protect)
{
	void *got;
	(void)alignment; (void)protect;
	if (physical == (ULONG_PTR)-1) got = h2n_contig_alloc((unsigned)size);
	else if (physical + size <= ARENA_FIRST) got = (void *)(PHYS_VIEW + physical);       /* the console's own RAM: always there */
	else got = NULL;
	h2n_log("[%4u] XPhysicalAlloc(%lu bytes, physical %08lx) -> %p%s\n", h2n_next_sequence(), (unsigned long)size, (unsigned long)physical, got,
		g_alias ? "" : "  (no second view at 0xF0000000: accesses through it are redirected)");
	return got;
}
DWORD __stdcall h2n_XPhysicalProtect(void *address, SIZE_T size, DWORD protect) __asm__("_XPhysicalProtect@12");
DWORD __stdcall h2n_XPhysicalProtect(void *address, SIZE_T size, DWORD protect) { (void)address; (void)size; (void)protect; return 0; }
void __stdcall h2n_MmFreeContiguousMemory(void *address) __asm__("_MmFreeContiguousMemory@4");
void __stdcall h2n_MmFreeContiguousMemory(void *address) { (void)address; }
void *__stdcall h2n_XMemAllocDefault(SIZE_T size, DWORD attributes) __asm__("_XMemAllocDefault@8");
void *__cdecl h2n_calloc(unsigned size);
void *__stdcall h2n_XMemAllocDefault(SIZE_T size, DWORD attributes) { (void)attributes; return h2n_calloc((unsigned)size); }
void __stdcall h2n_XPhysicalFree(void *address) __asm__("_XPhysicalFree@4");
void __stdcall h2n_XPhysicalFree(void *address) { h2n_contig_free(address); }     /* the console's own RAM is never handed back */

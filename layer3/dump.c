/* H2Classic: a snapshot of the process at the moment it stops, written next to the log as h2classic_stop.bin and read
 * afterwards with scripts/readdump.py. It holds what the short text report cannot: every thread's registers and its
 * whole stack, the game's data (the retail data in place, 0x411000-0x586000), and this exe's own writable data.
 * and the pages of game memory that the registers and the top of each stack point into. With H2N_DUMP=all it holds
 * the whole of the game's fixed memory block (51 MB) instead of those pages; H2N_DUMP=off writes nothing.
 *
 * The file is a list of records, all little-endian:
 *   "H2NSTOP1"
 *   1, address, size, <size bytes>                    memory
 *   2, thread id, flags (1 = the thread that stopped), 716, <CONTEXT>      a thread's registers
 *   3, length, <text>                                  a note (the build id, the reason)
 *   0                                                  end
 */
#include "h2n.h"
#include <tlhelp32.h>

static void put(FILE *f, const void *data, size_t size) { fwrite(data, 1, size, f); }
static void put32(FILE *f, DWORD value) { put(f, &value, 4); }
static void put_note(FILE *f, const char *text) { put32(f, 3); put32(f, (DWORD)strlen(text)); put(f, text, strlen(text)); }

/* whether every byte of [address, address + size) can be read: committed, not a guard or no-access page */
int __cdecl h2n_readable(const void *address, unsigned size)
{
	DWORD at = (DWORD)(ULONG_PTR)address, end = at + size;
	while (at < end)
	{
		MEMORY_BASIC_INFORMATION m;
		DWORD region_end;
		if (!VirtualQuery((void *)(ULONG_PTR)at, &m, sizeof(m))) return 0;
		if (m.State != MEM_COMMIT || (m.Protect & (PAGE_NOACCESS | PAGE_GUARD)) || !(m.Protect & 0xff)) return 0;
		region_end = (DWORD)(ULONG_PTR)m.BaseAddress + (DWORD)m.RegionSize;
		if (region_end <= at) return 0;
		at = region_end;
	}
	return 1;
}

/* the readable parts of [address, address + size) */
static void put_memory(FILE *f, DWORD address, DWORD size)
{
	DWORD at = address, end = address + size;
	while (at < end)
	{
		MEMORY_BASIC_INFORMATION m;
		DWORD region_end, take;
		if (!VirtualQuery((void *)(ULONG_PTR)at, &m, sizeof(m))) break;
		region_end = (DWORD)(ULONG_PTR)m.BaseAddress + (DWORD)m.RegionSize;
		if (region_end <= at) break;
		take = (region_end < end ? region_end : end) - at;
		if (m.State == MEM_COMMIT && !(m.Protect & (PAGE_NOACCESS | PAGE_GUARD)) && (m.Protect & 0xff))
		{
			put32(f, 1); put32(f, at); put32(f, take);
			put(f, (void *)(ULONG_PTR)at, take);
		}
		at += take;
	}
}

struct thread_copy { DWORD id, stopped, stack_at, stack_size; CONTEXT context; BYTE *stack; };

/* e: the exception that stopped the calling thread, or NULL when the stop is not a fault */
void h2n_write_stop_file(EXCEPTION_POINTERS *e, const char *reason)
{
	static LONG once;
	static struct thread_copy threads[64];
	static void *(WINAPI *allocate)(void *, SIZE_T, DWORD, DWORD);
	int count = 0;
	DWORD self = GetCurrentThreadId(), process = GetCurrentProcessId(), total = 0;
	HANDLE snapshot;
	FILE *f;
	const char *mode = getenv("H2N_DUMP");
	if (mode && !_stricmp(mode, "off")) return;
	if (InterlockedExchange(&once, 1)) return;
	if (!allocate) allocate = (void *)GetProcAddress(GetModuleHandleA("kernel32.dll"), "VirtualAlloc");

	/* the threads: each is held only while its registers and stack are copied, and nothing here takes a lock that a
	   held thread could own (no C library calls until all of them run again) */
	snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
	if (snapshot != INVALID_HANDLE_VALUE)
	{
		THREADENTRY32 t;
		t.dwSize = sizeof(t);
		if (Thread32First(snapshot, &t))
			do
			{
				struct thread_copy *c = &threads[count];
				MEMORY_BASIC_INFORMATION m;
				HANDLE h = NULL;
				DWORD sp;
				if (t.th32OwnerProcessID != process || count >= 64) continue;
				memset(c, 0, sizeof(*c));
				c->id = t.th32ThreadID;
				c->context.ContextFlags = CONTEXT_FULL | CONTEXT_FLOATING_POINT | CONTEXT_DEBUG_REGISTERS | CONTEXT_EXTENDED_REGISTERS;
				if (t.th32ThreadID == self)
				{
					c->stopped = 1;
					if (e) c->context = *e->ContextRecord;
					else { c->context.ContextFlags = CONTEXT_FULL; RtlCaptureContext(&c->context); }
				}
				else
				{
					h = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, t.th32ThreadID);
					if (!h || SuspendThread(h) == (DWORD)-1) { if (h) CloseHandle(h); continue; }
					if (!GetThreadContext(h, &c->context)) { ResumeThread(h); CloseHandle(h); continue; }
				}
				sp = c->context.Esp & ~3u;
				if (VirtualQuery((void *)(ULONG_PTR)sp, &m, sizeof(m)) && m.State == MEM_COMMIT && !(m.Protect & (PAGE_NOACCESS | PAGE_GUARD)))
				{
					DWORD top = (DWORD)(ULONG_PTR)m.BaseAddress + (DWORD)m.RegionSize;
					c->stack_at = sp; c->stack_size = top - sp;
					if (c->stack_size > 0x100000) c->stack_size = 0x100000;
					c->stack = allocate(NULL, c->stack_size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
					if (c->stack) memcpy(c->stack, (void *)(ULONG_PTR)sp, c->stack_size); else c->stack_size = 0;
				}
				if (h) { ResumeThread(h); CloseHandle(h); }
				count++;
			}
			while (Thread32Next(snapshot, &t));
		CloseHandle(snapshot);
	}

	f = h2n_real_fopen("h2classic_stop.bin", "wb");
	if (!f) { h2n_log("    (h2classic_stop.bin could not be written)\n"); return; }
	put(f, "H2NSTOP1", 8);
	put_note(f, "build " H2N_BUILD);
	put_note(f, reason ? reason : "");
	for (int i = 0; i < count; i++)
	{
		put32(f, 2); put32(f, threads[i].id); put32(f, threads[i].stopped); put32(f, sizeof(CONTEXT));
		put(f, &threads[i].context, sizeof(CONTEXT));
		if (threads[i].stack_size) { put32(f, 1); put32(f, threads[i].stack_at); put32(f, threads[i].stack_size); put(f, threads[i].stack, threads[i].stack_size); }
	}
	put_memory(f, RETAIL_CODE_END, RETAIL_END - RETAIL_CODE_END);              /* the game's data, in place */
	{
		/* this exe's own writable sections: the layer's state and the game data that is not retail's */
		const BYTE *base = (const BYTE *)GetModuleHandleA(NULL);
		const IMAGE_NT_HEADERS *nt = (const IMAGE_NT_HEADERS *)(base + ((const IMAGE_DOS_HEADER *)base)->e_lfanew);
		const IMAGE_SECTION_HEADER *s = IMAGE_FIRST_SECTION(nt);
		for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; i++, s++)
			if (s->Characteristics & IMAGE_SCN_MEM_WRITE) put_memory(f, (DWORD)(ULONG_PTR)base + s->VirtualAddress, s->Misc.VirtualSize);
	}
	if (mode && !_stricmp(mode, "all")) put_memory(f, 0x80061000u, 51265536u);   /* the game's fixed block */
	else
	{
		/* otherwise the pages of game memory and heap that the registers and the first words of each stack point
		   into: what the code was working on when it stopped */
		static DWORD pages[768];
		const BYTE *base = (const BYTE *)GetModuleHandleA(NULL);
		const IMAGE_NT_HEADERS *nt = (const IMAGE_NT_HEADERS *)(base + ((const IMAGE_DOS_HEADER *)base)->e_lfanew);
		DWORD image_first = (DWORD)(ULONG_PTR)base, image_end = image_first + nt->OptionalHeader.SizeOfImage;
		int page_count = 0;
		for (int i = 0; i < count; i++)
		{
			const CONTEXT *c = &threads[i].context;
			DWORD values[8 + 512];
			int n = 0;
			values[n++] = c->Eax; values[n++] = c->Ebx; values[n++] = c->Ecx; values[n++] = c->Edx;
			values[n++] = c->Esi; values[n++] = c->Edi; values[n++] = c->Ebp; values[n++] = c->Eip;
			for (DWORD w = 0; w < 512 && (w + 1) * 4 <= threads[i].stack_size; w++) values[n++] = ((const DWORD *)threads[i].stack)[w];
			for (int k = 0; k < n && page_count < 768; k++)
			{
				DWORD page = values[k] & ~0xfffu;
				int seen = 0;
				if (page < 0x10000 || (page >= image_first && page < image_end)) continue;      /* null, or this exe (in the file already, or its code) */
				if (page >= threads[i].stack_at && page < threads[i].stack_at + threads[i].stack_size) continue;
				for (int j = 0; j < page_count; j++) if (pages[j] == page) { seen = 1; break; }
				if (!seen) pages[page_count++] = page;
			}
		}
		for (int j = 0; j < page_count; j++) put_memory(f, pages[j], 0x1000);
	}
	{
		/* (s10r) H2N_DUMP_ALSO=<address>:<size> (hex): one more range, e.g. 84714000:20000, the text's glyph page */
		const char *also = getenv("H2N_DUMP_ALSO");
		char *colon = NULL;
		unsigned long at = also ? strtoul(also, &colon, 16) : 0, size = colon && *colon == ':' ? strtoul(colon + 1, NULL, 16) : 0;
		if (at >= 0x10000 && size && size <= 0x4000000) put_memory(f, (DWORD)at, (DWORD)size);
	}
	put32(f, 0);
	total = (DWORD)ftell(f);
	fclose(f);
	h2n_log("    h2classic_stop.bin written (%lu threads, %.1f MB): the registers and stack of every thread, and the game's data\n", (unsigned long)count, total / 1048576.0);
	if (!e && reason && !strncmp(reason, "asked", 5)) InterlockedExchange(&once, 0);      /* a file asked for from the key file: the run goes on, and may ask again or stop later */
}

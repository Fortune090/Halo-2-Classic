/* xkernel.c - the console kernel's services that the XBE's own network library calls (s9u, H2N_XNET=retail), given
 * to retail code through the kernel import table (retail.c asks h2n_kernel_by_name for each import it has no other
 * answer for). What each does here:
 *   - IRQL: "raised to DPC level" is holding one lock (the network library raises and lowers around everything it
 *     shares).
 *   - DPCs and timers: one thread runs queued DPCs and the DPCs of timers that are due, holding that lock, as the
 *     console runs them at DPC level. It also hands the network library its received frames (xnetx.c).
 *   - events: set and waited on by their signal state (the console's KEVENT layout).
 *   - pool, time, the PHY link, and the few files and objects the library may ask for (answered "not there").
 *   - the crypto calls: xcrypt.c.
 * The kernel's data exports (KeTickCount, XboxLANKey ...) are variables here. */
#include <windows.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

void h2n_log(const char *format, ...);
unsigned h2n_next_sequence(void);
void *__cdecl h2n_calloc(unsigned size);
void __cdecl h2n_free(void *address);
unsigned char *h2n_lan_key(void);
DWORD h2n_xnetx_link_state(void);
void h2n_xnetx_service(void);

/* the crypto calls (xcrypt.c) */
void __stdcall h2n_XcSHAInit(void *); void __stdcall h2n_XcSHAUpdate(void *, const void *, DWORD); void __stdcall h2n_XcSHAFinal(void *, void *);
void __stdcall h2n_XcHMAC(const void *, DWORD, const void *, DWORD, const void *, DWORD, void *);
void __stdcall h2n_XcKeyTable(DWORD, void *, const void *); void __stdcall h2n_XcBlockCrypt(DWORD, void *, const void *, const void *, DWORD);
void __stdcall h2n_XcBlockCryptCBC(DWORD, DWORD, void *, const void *, const void *, DWORD, void *);
void __stdcall h2n_XcDESKeyParity(void *, DWORD); DWORD __stdcall h2n_XcModExp(DWORD *, const DWORD *, const DWORD *, const DWORD *, DWORD);
void __stdcall h2n_XcRC4Key(void *, DWORD, const void *); void __stdcall h2n_XcRC4Crypt(void *, DWORD, void *);

/* ---- IRQL ---- */
/* A thread is at DPC level exactly while it holds g_dpc_lock (the lock's owner field says whether that is this thread;
 * no thread-local value, whose TlsGetValue would clear the thread's last error). */
static CRITICAL_SECTION g_dpc_lock;
static LONG g_made;
static void make(void);
static int at_dpc_level(void) { return (DWORD)(ULONG_PTR)g_dpc_lock.OwningThread == GetCurrentThreadId(); }
static UCHAR __stdcall k_KeRaiseIrqlToDpcLevel(void)
{
	make();
	if (at_dpc_level()) return 2;
	EnterCriticalSection(&g_dpc_lock);
	return 0;
}
static UCHAR __fastcall k_KfRaiseIrql(UCHAR irql)
{
	make();
	if (at_dpc_level()) return 2;
	if (irql >= 2) EnterCriticalSection(&g_dpc_lock);
	return 0;
}
static void __fastcall k_KfLowerIrql(UCHAR irql)
{
	if (irql < 2 && g_made == 2 && at_dpc_level()) LeaveCriticalSection(&g_dpc_lock);
}

/* ---- DPCs and timers ---- */
typedef struct kdpc { short type; unsigned char inserted, pad; LIST_ENTRY entry;
	void (__stdcall *routine)(struct kdpc *, void *, void *, void *); void *context, *arg1, *arg2; } kdpc;          /* 28 bytes */
typedef struct ktimer { unsigned char type, absolute, size, inserted; LONG signal; LIST_ENTRY waiters;
	ULONGLONG due; LIST_ENTRY entry; kdpc *dpc; LONG period; } ktimer;                                                 /* 40 bytes */
#define MAX_DPCS 512
#define MAX_TIMERS 256
static CRITICAL_SECTION g_queue_lock;
static kdpc *g_dpcs[MAX_DPCS];
static unsigned g_dpc_head, g_dpc_count;
static ktimer *g_timers[MAX_TIMERS];
static unsigned g_timer_count;
static HANDLE g_wake;
static LARGE_INTEGER g_qpf, g_qpc0;
static DWORD g_tick0;
static volatile DWORD k_KeTickCount;

static ULONGLONG now100(void)           /* 100 ns units since the layer started counting */
{
	LARGE_INTEGER q;
	QueryPerformanceCounter(&q);
	return (ULONGLONG)((q.QuadPart - g_qpc0.QuadPart) * 10000000.0 / (double)g_qpf.QuadPart);
}
static ULONGLONG system_time(void) { FILETIME f; GetSystemTimeAsFileTime(&f); return (ULONGLONG)f.dwHighDateTime << 32 | f.dwLowDateTime; }

static DWORD WINAPI dpc_thread(void *unused)
{
	(void)unused;
	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
	for (;;)
	{
		WaitForSingleObject(g_wake, 2);
		k_KeTickCount = GetTickCount() - g_tick0;
		k_KeRaiseIrqlToDpcLevel();
		{	/* timers that are due: signalled, their DPC queued, re-armed if periodic */
			ULONGLONG now = now100();
			ktimer *due[MAX_TIMERS];
			unsigned n = 0;
			EnterCriticalSection(&g_queue_lock);
			for (unsigned i = 0; i < g_timer_count;)
			{
				ktimer *t = g_timers[i];
				if (t->due <= now)
				{
					due[n++] = t;
					if (t->period > 0) { t->due = now + (ULONGLONG)t->period * 10000; i++; }
					else { t->inserted = 0; g_timers[i] = g_timers[--g_timer_count]; }
				}
				else i++;
			}
			LeaveCriticalSection(&g_queue_lock);
			for (unsigned i = 0; i < n; i++)
			{
				due[i]->signal = 1;
				if (due[i]->dpc)
				{
					ULONGLONG st = system_time();
					kdpc *d = due[i]->dpc;
					EnterCriticalSection(&g_queue_lock);
					if (!d->inserted && g_dpc_count < MAX_DPCS)
					{
						d->inserted = 1; d->arg1 = (void *)(ULONG_PTR)(DWORD)st; d->arg2 = (void *)(ULONG_PTR)(DWORD)(st >> 32);
						g_dpcs[(g_dpc_head + g_dpc_count++) % MAX_DPCS] = d;
					}
					LeaveCriticalSection(&g_queue_lock);
				}
			}
		}
		for (;;)
		{	/* the queued DPCs, in order */
			kdpc *d = NULL;
			EnterCriticalSection(&g_queue_lock);
			if (g_dpc_count) { d = g_dpcs[g_dpc_head]; g_dpc_head = (g_dpc_head + 1) % MAX_DPCS; g_dpc_count--; d->inserted = 0; }
			LeaveCriticalSection(&g_queue_lock);
			if (!d) break;
			d->routine(d, d->context, d->arg1, d->arg2);
		}
		h2n_xnetx_service();            /* frames received, frames sent */
		k_KfLowerIrql(0);
	}
	return 0;
}
static void make(void)
{
	if (g_made == 2) return;
	if (InterlockedCompareExchange(&g_made, 1, 0) == 0)
	{
		InitializeCriticalSection(&g_dpc_lock);
		InitializeCriticalSection(&g_queue_lock);
		QueryPerformanceFrequency(&g_qpf); QueryPerformanceCounter(&g_qpc0);
		g_tick0 = GetTickCount();
		g_wake = CreateEventA(NULL, FALSE, FALSE, NULL);
		InterlockedExchange(&g_made, 2);
		{
			HANDLE (WINAPI *create)(void *, SIZE_T, LPTHREAD_START_ROUTINE, void *, DWORD, DWORD *) =
				(void *)GetProcAddress(GetModuleHandleA("kernel32.dll"), "CreateThread");
			DWORD id;
			create(NULL, 0x40000, dpc_thread, NULL, 0, &id);
			h2n_log("[%4u] kernel: the DPC and timer thread started (id %lu)\n", h2n_next_sequence(), id);
		}
	}
	while (g_made != 2) Sleep(0);
}
void h2n_dpc_wake(void) { make(); SetEvent(g_wake); }

static void __stdcall k_KeInitializeDpc(kdpc *d, void *routine, void *context)
{
	make();
	memset(d, 0, sizeof(*d)); d->type = 0x13; d->routine = routine; d->context = context;
}
static BOOLEAN __stdcall k_KeInsertQueueDpc(kdpc *d, void *arg1, void *arg2)
{
	BOOLEAN done = FALSE;
	make();
	EnterCriticalSection(&g_queue_lock);
	if (!d->inserted && g_dpc_count < MAX_DPCS)
	{
		d->inserted = 1; d->arg1 = arg1; d->arg2 = arg2;
		g_dpcs[(g_dpc_head + g_dpc_count++) % MAX_DPCS] = d;
		done = TRUE;
	}
	LeaveCriticalSection(&g_queue_lock);
	if (done) SetEvent(g_wake);
	return done;
}
static BOOLEAN __stdcall k_KeRemoveQueueDpc(kdpc *d)
{
	BOOLEAN was = FALSE;
	make();
	EnterCriticalSection(&g_queue_lock);
	for (unsigned i = 0; i < g_dpc_count; i++)
		if (g_dpcs[(g_dpc_head + i) % MAX_DPCS] == d)
		{
			for (unsigned j = i; j + 1 < g_dpc_count; j++) g_dpcs[(g_dpc_head + j) % MAX_DPCS] = g_dpcs[(g_dpc_head + j + 1) % MAX_DPCS];
			g_dpc_count--; was = TRUE; break;
		}
	d->inserted = 0;
	LeaveCriticalSection(&g_queue_lock);
	return was;
}
static void __stdcall k_KeInitializeTimerEx(ktimer *t, int type)
{
	make();
	memset(t, 0, sizeof(*t)); t->type = (unsigned char)(8 + type); t->size = sizeof(*t) / 4;
}
static BOOLEAN timer_remove(ktimer *t)
{
	for (unsigned i = 0; i < g_timer_count; i++) if (g_timers[i] == t) { g_timers[i] = g_timers[--g_timer_count]; return TRUE; }
	return FALSE;
}
static BOOLEAN __stdcall k_KeSetTimerEx(ktimer *t, LARGE_INTEGER due, LONG period, kdpc *d)
{
	BOOLEAN was;
	ULONGLONG at;
	make();
	at = due.QuadPart < 0 ? now100() + (ULONGLONG)(-due.QuadPart)
		: now100() + ((ULONGLONG)due.QuadPart > system_time() ? (ULONGLONG)due.QuadPart - system_time() : 0);
	EnterCriticalSection(&g_queue_lock);
	was = timer_remove(t);
	t->due = at; t->period = period; t->dpc = d; t->signal = 0; t->inserted = 1;
	if (g_timer_count < MAX_TIMERS) g_timers[g_timer_count++] = t;
	LeaveCriticalSection(&g_queue_lock);
	SetEvent(g_wake);
	return was;
}
static BOOLEAN __stdcall k_KeSetTimer(ktimer *t, LARGE_INTEGER due, kdpc *d) { return k_KeSetTimerEx(t, due, 0, d); }
static BOOLEAN __stdcall k_KeCancelTimer(ktimer *t)
{
	BOOLEAN was;
	make();
	EnterCriticalSection(&g_queue_lock);
	was = timer_remove(t); t->inserted = 0;
	LeaveCriticalSection(&g_queue_lock);
	return was;
}

static DWORD k_ExEventObjectType[8];      /* (only its address is used: the type handed to ObReferenceObjectByHandle) */
/* ---- events and waits (the dispatcher header's signal state at +4; type 1 = a synchronization event) ----
 * An event handle the game hands the library (a Windows event here) is referenced as a console event object: a small
 * object that carries a duplicate of the handle (so it lives as long as the library holds it, as the console's
 * reference count keeps the object), which KeSetEvent and the waits act on. */
#define BRIDGE_MAGIC 0x45324e48u                /* 'H2NE' */
typedef struct bridge { unsigned char type, absolute, size, inserted; LONG signal; LIST_ENTRY waiters; DWORD magic; HANDLE handle; LONG refs; } bridge;
static int is_bridge(const unsigned char *o) { return ((const bridge *)o)->magic == BRIDGE_MAGIC; }
static LONG __stdcall k_KeSetEvent(unsigned char *e, LONG increment, BOOLEAN wait)
{
	(void)increment; (void)wait;
	if (is_bridge(e)) { SetEvent(((bridge *)e)->handle); return 0; }
	return InterlockedExchange((LONG *)(e + 4), 1);
}
static int signalled(unsigned char *o) { return is_bridge(o) ? WaitForSingleObject(((bridge *)o)->handle, 0) == WAIT_OBJECT_0 : *(LONG *)(o + 4) != 0; }
static NTSTATUS wait_poll(unsigned char **objects, ULONG count, int all, LARGE_INTEGER *timeout)
{
	ULONGLONG end;
	make();
	end = timeout ? (timeout->QuadPart < 0 ? now100() + (ULONGLONG)(-timeout->QuadPart) : now100()) : ~0ull;
	for (;;)
	{
		ULONG ready = 0, first = count;
		for (ULONG i = 0; i < count; i++) if (signalled(objects[i])) { ready++; if (first == count) first = i; }
		if (all ? ready == count : ready > 0)
		{
			for (ULONG i = 0; i < count; i++)
				if ((all || i == first) && !is_bridge(objects[i]) && (objects[i][0] & 7) == 1) *(LONG *)(objects[i] + 4) = 0;
			return all ? 0 : (NTSTATUS)first;
		}
		if (now100() >= end) return 0x102;          /* STATUS_TIMEOUT */
		Sleep(1);
	}
}
static NTSTATUS __stdcall k_KeWaitForSingleObject(unsigned char *o, int reason, int mode, BOOLEAN alertable, LARGE_INTEGER *timeout)
{ (void)reason; (void)mode; (void)alertable; return wait_poll(&o, 1, 0, timeout); }
static NTSTATUS __stdcall k_KeWaitForMultipleObjects(ULONG count, unsigned char **objects, int type, int reason, int mode, BOOLEAN alertable,
	LARGE_INTEGER *timeout, void *blocks)
{ (void)reason; (void)mode; (void)alertable; (void)blocks; return wait_poll(objects, count, type == 0, timeout); }

/* ---- objects, pool, time ---- */
static NTSTATUS __stdcall k_ObReferenceObjectByHandle(HANDLE h, void *type, void **object)
{
	static LONG told;
	HANDLE copy = NULL;
	if (object) *object = NULL;
	if (type == (void *)k_ExEventObjectType && h && DuplicateHandle(GetCurrentProcess(), h, GetCurrentProcess(), &copy, 0, FALSE, DUPLICATE_SAME_ACCESS))
	{
		bridge *b = h2n_calloc(sizeof(bridge));
		b->magic = BRIDGE_MAGIC; b->handle = copy; b->refs = 1; b->size = sizeof(bridge) / 4;
		if (object) *object = b;
		return 0;
	}
	if (InterlockedIncrement(&told) <= 4) h2n_log("[%4u] kernel: ObReferenceObjectByHandle(%p): not an event here\n", h2n_next_sequence(), h);
	return (NTSTATUS)0xC0000008;            /* STATUS_INVALID_HANDLE */
}
static void __fastcall k_ObfDereferenceObject(void *object)
{
	bridge *b = object;
	if (b && is_bridge(object) && InterlockedDecrement(&b->refs) == 0) { CloseHandle(b->handle); b->magic = 0; h2n_free(b); }
}
static void *__stdcall k_ExAllocatePoolWithTag(SIZE_T size, ULONG tag) { (void)tag; return h2n_calloc((unsigned)size); }
static void *__stdcall k_ExAllocatePool(SIZE_T size) { return h2n_calloc((unsigned)size); }
static void __stdcall k_ExFreePool(void *p) { h2n_free(p); }
static void __stdcall k_KeQuerySystemTime(LARGE_INTEGER *t) { t->QuadPart = (LONGLONG)system_time(); }
static ULONGLONG __stdcall k_KeQueryPerformanceCounter(void) { LARGE_INTEGER q; QueryPerformanceCounter(&q); return (ULONGLONG)q.QuadPart; }
static ULONGLONG __stdcall k_KeQueryPerformanceFrequency(void) { LARGE_INTEGER q; QueryPerformanceFrequency(&q); return (ULONGLONG)q.QuadPart; }
static ULONGLONG __stdcall k_KeQueryInterruptTime(void) { make(); return now100(); }
static void __stdcall k_KeStallExecutionProcessor(ULONG us) { make(); ULONGLONG end = now100() + (ULONGLONG)us * 10; while (now100() < end) YieldProcessor(); }
static NTSTATUS __stdcall k_KeDelayExecutionThread(int mode, BOOLEAN alertable, LARGE_INTEGER *interval)
{ (void)mode; (void)alertable; Sleep(interval && interval->QuadPart < 0 ? (DWORD)(-interval->QuadPart / 10000) : 0); return 0; }
static NTSTATUS __stdcall k_MmQueryStatistics(DWORD *s)
{
	if (!s || s[0] < 36) return (NTSTATUS)0xC000000D;
	s[1] = 16384; s[2] = 4096; s[3] = 32 << 20; s[4] = 64 << 20; s[5] = 1024; s[6] = 512; s[7] = 64; s[8] = 2048;
	return 0;
}
static struct { USHORT length, maximum; const char *buffer; } k_HalDiskModelNumber = { 9, 10, "H2Classic" }, k_HalDiskSerialNumber = { 8, 9, "00000000" };
static USHORT k_XboxKrnlVersion[4] = { 1, 0, 5838, 1 };
static unsigned char k_XboxHDKey[16], k_XePublicKeyData[284];
const unsigned char *h2n_hd_key(void) { return k_XboxHDKey; }       /* this console's hard disk key: zeros (xsign.c, content.c) */

/* ---- the PHY ---- */
static DWORD __stdcall k_PhyGetLinkState(BOOL update) { (void)update; return h2n_xnetx_link_state(); }
static NTSTATUS __stdcall k_PhyInitialize(BOOL force, void *parameter) { (void)force; (void)parameter; return 0; }
static void __stdcall k_HalRegisterShutdownNotification(void *registration, BOOLEAN do_register) { (void)registration; (void)do_register; }
static void __stdcall k_HalReturnToFirmware(int how) { h2n_log("[%4u] kernel: HalReturnToFirmware(%d) asked for and not done\n", h2n_next_sequence(), how); }
static NTSTATUS __stdcall k_NtSetSystemTime(void *a, void *b) { (void)a; (void)b; return 0; }

/* ---- files and devices: the library's stored settings (on the console's disk) are not there ---- */
static NTSTATUS __stdcall k_NtOpenFile(HANDLE *h, DWORD access, void *attributes, void *iosb, DWORD share, DWORD options)
{
	static LONG told;
	(void)access; (void)iosb; (void)share; (void)options;
	if (h) *h = NULL;
	if (InterlockedIncrement(&told) <= 6)
	{
		const struct { USHORT length, maximum; const char *buffer; } *name = attributes ? *(void **)((char *)attributes + 4) : NULL;
		h2n_log("[%4u] kernel: NtOpenFile(%.*s): not there\n", h2n_next_sequence(), name ? name->length : 1, name && name->buffer ? name->buffer : "?");
	}
	return (NTSTATUS)0xC0000034;            /* STATUS_OBJECT_NAME_NOT_FOUND */
}
/* (s10p) the kernel's NtReadFile, for XAPI's content signatures (1.5): synchronous, on a handle this program's CreateFileA
   gave (XAPI's CreateFileA is this program's, so its handles are the PC's): ByteOffset given = read there, else at the
   file's position, which moves past what was read either way (FILE_SYNCHRONOUS_IO, as XAPI opens files); nothing read
   at the end of the file = STATUS_END_OF_FILE. (The network library's stored settings never get this far: NtOpenFile
   above answers "not there".) */
BOOL __stdcall h2n_layer_ReadFile(HANDLE h, void *buffer, DWORD size, DWORD *got, OVERLAPPED *where) __asm__("_ReadFile@20");
static NTSTATUS __stdcall k_NtReadFile(HANDLE h, HANDLE event, void *apc, void *apc_context, void *iosb, void *buffer, ULONG length, LARGE_INTEGER *offset)
{
	OVERLAPPED at;
	DWORD got = 0, error;
	NTSTATUS status;
	BOOL ok;
	(void)event; (void)apc; (void)apc_context;
	memset(&at, 0, sizeof(at));
	if (offset) { at.Offset = offset->LowPart; at.OffsetHigh = (DWORD)offset->HighPart; }
	ok = h2n_layer_ReadFile(h, buffer, length, &got, offset ? &at : NULL);
	error = ok ? 0 : GetLastError();
	if (ok) status = got || !length ? 0 : (NTSTATUS)0xC0000011;                    /* STATUS_END_OF_FILE */
	else status = error == ERROR_HANDLE_EOF ? (NTSTATUS)0xC0000011 : error == ERROR_INVALID_HANDLE ? (NTSTATUS)0xC0000008 : (NTSTATUS)0xC0000185;
	if (iosb) { ((NTSTATUS *)iosb)[0] = status; ((ULONG_PTR *)iosb)[1] = got; }
	return status;
}
static NTSTATUS __stdcall k_NtWriteFile(HANDLE h, HANDLE e, void *r, void *c, void *iosb, void *b, ULONG l, void *o) { (void)h; (void)e; (void)r; (void)c; (void)iosb; (void)b; (void)l; (void)o; return (NTSTATUS)0xC0000008; }
static NTSTATUS __stdcall k_NtClose(HANDLE h) { (void)h; return 0; }
static NTSTATUS __stdcall k_ObReferenceObjectByName(void *n, ULONG a, void *t, void *p, void **o) { (void)n; (void)a; (void)t; (void)p; if (o) *o = NULL; return (NTSTATUS)0xC0000034; }
static void *__stdcall k_IoBuildSynchronousFsdRequest(ULONG f, void *d, void *b, ULONG l, void *o, void *e, void *s) { (void)f; (void)d; (void)b; (void)l; (void)o; (void)e; (void)s; return NULL; }
static NTSTATUS __fastcall k_IofCallDriver(void *d, void *i) { (void)d; (void)i; return (NTSTATUS)0xC0000001; }

static NTSTATUS __stdcall k_ExQueryNonVolatileSetting(DWORD index, DWORD *type, void *value, ULONG length, ULONG *result)
{	/* the console's EEPROM settings are not here (the network library asks for all of them as randomness only) */
	(void)index; (void)value; (void)length;
	if (type) *type = 0;
	if (result) *result = 0;
	return (NTSTATUS)0xC0000034;
}

/* ---- by name ---- */
void *h2n_kernel_by_name(const char *decorated)
{
	static const struct { const char *name; void *function; } table[] = {
		{ "KeRaiseIrqlToDpcLevel", k_KeRaiseIrqlToDpcLevel }, { "KfRaiseIrql", k_KfRaiseIrql }, { "KfLowerIrql", k_KfLowerIrql },
		{ "KeInitializeDpc", k_KeInitializeDpc }, { "KeInsertQueueDpc", k_KeInsertQueueDpc }, { "KeRemoveQueueDpc", k_KeRemoveQueueDpc },
		{ "KeInitializeTimerEx", k_KeInitializeTimerEx }, { "KeSetTimerEx", k_KeSetTimerEx }, { "KeSetTimer", k_KeSetTimer },
		{ "KeCancelTimer", k_KeCancelTimer }, { "KeSetEvent", k_KeSetEvent }, { "KeWaitForSingleObject", k_KeWaitForSingleObject },
		{ "KeWaitForMultipleObjects", k_KeWaitForMultipleObjects }, { "ExEventObjectType", k_ExEventObjectType },
		{ "ObReferenceObjectByHandle", k_ObReferenceObjectByHandle }, { "ObfDereferenceObject", k_ObfDereferenceObject },
		{ "ExAllocatePoolWithTag", k_ExAllocatePoolWithTag }, { "ExAllocatePool", k_ExAllocatePool }, { "ExFreePool", k_ExFreePool },
		{ "KeTickCount", (void *)&k_KeTickCount }, { "KeQuerySystemTime", k_KeQuerySystemTime },
		{ "KeQueryPerformanceCounter", k_KeQueryPerformanceCounter }, { "KeQueryPerformanceFrequency", k_KeQueryPerformanceFrequency },
		{ "KeQueryInterruptTime", k_KeQueryInterruptTime }, { "KeStallExecutionProcessor", k_KeStallExecutionProcessor },
		{ "KeDelayExecutionThread", k_KeDelayExecutionThread }, { "MmQueryStatistics", k_MmQueryStatistics },
		{ "HalDiskModelNumber", &k_HalDiskModelNumber }, { "HalDiskSerialNumber", &k_HalDiskSerialNumber },
		{ "XboxKrnlVersion", k_XboxKrnlVersion }, { "XboxHDKey", k_XboxHDKey }, { "XePublicKeyData", k_XePublicKeyData },
		{ "PhyGetLinkState", k_PhyGetLinkState }, { "PhyInitialize", k_PhyInitialize },
		{ "HalRegisterShutdownNotification", k_HalRegisterShutdownNotification }, { "HalReturnToFirmware", k_HalReturnToFirmware },
		{ "NtSetSystemTime", k_NtSetSystemTime }, { "NtOpenFile", k_NtOpenFile }, { "NtReadFile", k_NtReadFile },
		{ "NtWriteFile", k_NtWriteFile }, { "NtClose", k_NtClose }, { "ObReferenceObjectByName", k_ObReferenceObjectByName },
		{ "IoBuildSynchronousFsdRequest", k_IoBuildSynchronousFsdRequest }, { "IofCallDriver", k_IofCallDriver },
		{ "XcSHAInit", h2n_XcSHAInit }, { "XcSHAUpdate", h2n_XcSHAUpdate }, { "XcSHAFinal", h2n_XcSHAFinal }, { "XcHMAC", h2n_XcHMAC },
		{ "XcKeyTable", h2n_XcKeyTable }, { "XcBlockCrypt", h2n_XcBlockCrypt }, { "XcBlockCryptCBC", h2n_XcBlockCryptCBC },
		{ "XcDESKeyParity", h2n_XcDESKeyParity }, { "XcModExp", h2n_XcModExp }, { "XcRC4Key", h2n_XcRC4Key }, { "XcRC4Crypt", h2n_XcRC4Crypt },
	};
	static const char *const from_ntdll[] = { "RtlInitAnsiString", "RtlTimeToTimeFields", "RtlTimeFieldsToTime" };
	char name[64];
	const char *p = decorated + (decorated[0] == '_' || decorated[0] == '@');
	size_t n = strcspn(p, "@");
	if (n >= sizeof(name)) return NULL;
	memcpy(name, p, n); name[n] = 0;
	if (!strcmp(name, "XboxLANKey")) return h2n_lan_key();
	if (!strcmp(name, "ExQueryNonVolatileSetting")) return k_ExQueryNonVolatileSetting;
	for (unsigned i = 0; i < sizeof(from_ntdll) / sizeof(from_ntdll[0]); i++)            /* the same calls with the same arguments */
		if (!strcmp(from_ntdll[i], name)) return (void *)GetProcAddress(GetModuleHandleA("ntdll.dll"), name);
	for (unsigned i = 0; i < sizeof(table) / sizeof(table[0]); i++) if (!strcmp(table[i].name, name)) return table[i].function;
	return NULL;
}

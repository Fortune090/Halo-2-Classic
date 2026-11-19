/* H2Classic DLSS helper: what NVIDIA's NGX static library (nvsdk_ngx_d.lib, compiled with Microsoft's compiler for the
   dynamic C runtime) expects from the static part of Microsoft's C runtime, written here so that the helper needs no
   Microsoft library: the C++ allocation operators, thread-safe local statics, the /GS cookie and its exception-time
   checks, and _fltused. The C++ exception machinery itself (__CxxFrameHandler3, _CxxThrowException) and the two
   std::_X... throws come from the Visual C++ runtime DLLs on the PC (vcruntime140.dll, msvcp140.dll). */
#include <windows.h>
#include <stdlib.h>
#include <limits.h>

int _fltused = 0x9875;

/* operator new(size_t) and operator delete(void *) as Microsoft's compiler names them */
void *msvc_new(size_t n) __asm__("??2@YAPEAX_K@Z");
void *msvc_new(size_t n)
{
	void *p = malloc(n ? n : 1);
	if (!p) { OutputDebugStringA("h2classic_dlss: out of memory\n"); ExitProcess(3); }
	return p;
}
void msvc_delete(void *p) __asm__("??3@YAXPEAX@Z");
void msvc_delete(void *p) { free(p); }

/* the /GS cookie (checked on return from functions with stack buffers) */
unsigned long long __security_cookie = 0x00002B992DDFA232ull;
void __security_check_cookie(unsigned long long cookie)
{
	if (cookie != __security_cookie) { OutputDebugStringA("h2classic_dlss: stack cookie overwritten\n"); ExitProcess(4); }
}

/* the language handlers of functions with a /GS cookie: __GSHandlerCheck (no C++ handling: let the search go on) and
   __GSHandlerCheck_EH (the function also has C++ handling: its handler data starts with the FuncInfo that
   __CxxFrameHandler3 reads, so it is handed on as it is) */
EXCEPTION_DISPOSITION __CxxFrameHandler3(EXCEPTION_RECORD *, void *, CONTEXT *, void *);
EXCEPTION_DISPOSITION __GSHandlerCheck(EXCEPTION_RECORD *record, void *frame, CONTEXT *context, void *dispatcher)
{
	(void)record; (void)frame; (void)context; (void)dispatcher;
	return ExceptionContinueSearch;
}
EXCEPTION_DISPOSITION __GSHandlerCheck_EH(EXCEPTION_RECORD *record, void *frame, CONTEXT *context, void *dispatcher)
{
	return __CxxFrameHandler3(record, frame, context, dispatcher);
}

/* thread-safe initialisation of function-local statics (Microsoft's protocol: the guard is 0 before, -1 while one thread
   runs the initialiser, then the epoch at which it finished; each thread keeps the last epoch it has seen) */
__thread int _Init_thread_epoch = INT_MIN;
static int g_init_epoch = INT_MIN;
static SRWLOCK g_init_lock = SRWLOCK_INIT;
static CONDITION_VARIABLE g_init_done = CONDITION_VARIABLE_INIT;
void _Init_thread_header(int *guard)
{
	AcquireSRWLockExclusive(&g_init_lock);
	if (*guard == 0) *guard = -1;                       /* this thread runs the initialiser */
	else
	{
		while (*guard == -1) SleepConditionVariableSRW(&g_init_done, &g_init_lock, INFINITE, 0);
		_Init_thread_epoch = g_init_epoch;
	}
	ReleaseSRWLockExclusive(&g_init_lock);
}
void _Init_thread_footer(int *guard)
{
	AcquireSRWLockExclusive(&g_init_lock);
	*guard = ++g_init_epoch;
	_Init_thread_epoch = g_init_epoch;
	ReleaseSRWLockExclusive(&g_init_lock);
	WakeAllConditionVariable(&g_init_done);
}
void _Init_thread_abort(int *guard)
{
	AcquireSRWLockExclusive(&g_init_lock);
	*guard = 0;
	ReleaseSRWLockExclusive(&g_init_lock);
	WakeAllConditionVariable(&g_init_done);
}

/* std::_Xbad_alloc and std::_Xlength_error (msvcp140.dll) are reached only when memory runs out or a vector would grow
   past its limit: here they end the helper with a message instead of needing msvcp140.dll */
static void x_bad_alloc(void) { OutputDebugStringA("h2classic_dlss: std::bad_alloc\n"); ExitProcess(5); }
static void x_length_error(const char *what) { (void)what; OutputDebugStringA("h2classic_dlss: std::length_error\n"); ExitProcess(6); }
void (*imp_x_bad_alloc)(void) __asm__("__imp_?_Xbad_alloc@std@@YAXXZ") = x_bad_alloc;
void (*imp_x_length_error)(const char *) __asm__("__imp_?_Xlength_error@std@@YAXPEBD@Z") = x_length_error;

/* the default target of Control Flow Guard's dispatch pointer that the mingw runtime refers to: call what rax holds */
__asm__(".globl __guard_dispatch_icall_dummy\n__guard_dispatch_icall_dummy:\n\tjmp *%rax\n");

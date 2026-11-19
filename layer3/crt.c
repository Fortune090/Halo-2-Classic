/* H2Classic: C++ runtime helpers the SDK compiler's code expects by name */
#include "h2n.h"
#include <malloc.h>
/* every block the layer hands the game is 16-byte aligned and zeroed: Havok carves its own blocks out of what it is
   given and reads them with aligned SSE instructions, as it can on the console */
void *__cdecl h2n_calloc(unsigned size)
{
	void *p = _aligned_malloc(size ? size : 1, 16);
	if (p) memset(p, 0, size ? size : 1);
	return p;
}
void __cdecl h2n_free(void *address) { if (address) _aligned_free(address); }
void *__cdecl h2n_new(unsigned size) __asm__("??2@YAPAXI@Z");
void *__cdecl h2n_new(unsigned size) { return h2n_calloc(size); }
void __cdecl h2n_delete(void *p) __asm__("??3@YAXPAX@Z");
void __cdecl h2n_delete(void *p) { h2n_free(p); }
typedef void (__thiscall *member_function)(void *);
void __stdcall h2n_vector_constructor(void *p, unsigned size, int count, member_function construct, member_function destruct)
	__asm__("??_L@YGXPAXIHP6EX0@Z1@Z");
void __stdcall h2n_vector_constructor(void *p, unsigned size, int count, member_function construct, member_function destruct)
{
	(void)destruct;
	for (int i = 0; i < count; i++) construct((char *)p + i * size);
}
void __stdcall h2n_vector_destructor(void *p, unsigned size, int count, member_function destruct) __asm__("??_M@YGXPAXIHP6EX0@Z@Z");
void __stdcall h2n_vector_destructor(void *p, unsigned size, int count, member_function destruct)
{
	for (int i = count - 1; i >= 0; i--) destruct((char *)p + i * size);
}

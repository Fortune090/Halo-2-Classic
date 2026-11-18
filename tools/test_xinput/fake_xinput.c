/* TEST ONLY (never shipped): an XInput library with one controller, at index 0, that never moves; every XInputSetState
 * it is given is written to fake_xinput_log.txt in the current folder, with the time and whether the caller's window has
 * the focus. */
#include <windows.h>
#include <stdio.h>
typedef struct { DWORD packet; WORD buttons; BYTE lt, rt; SHORT lx, ly, rx, ry; } st;
typedef struct { WORD l, r; } vib;
static DWORD g_sets, g_gets;
DWORD WINAPI XInputGetState(DWORD i, st *s)
{
	if (i) return ERROR_DEVICE_NOT_CONNECTED;
	memset(s, 0, sizeof(*s)); s->packet = 1; g_gets++;
	return ERROR_SUCCESS;
}
DWORD WINAPI XInputSetState(DWORD i, vib *v)
{
	FILE *f;
	if (i) return ERROR_DEVICE_NOT_CONNECTED;
	g_sets++;
	f = fopen("fake_xinput_log.txt", "a");
	if (f) { fprintf(f, "%lu ms: set %lu (after %lu reads): left %u right %u\n", (unsigned long)GetTickCount(), (unsigned long)g_sets, (unsigned long)g_gets, v->l, v->r); fclose(f); }
	return ERROR_SUCCESS;
}

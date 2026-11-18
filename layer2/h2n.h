/* H2Classic: the Windows side of a native Halo 2 build (see main.c) */
#ifndef H2N_H
#define H2N_H
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

/* the retail image's address range, kept free inside our exe (reserve.S); the game's data lives there, at retail's
   own addresses */
#define RETAIL_BASE      0x00010000u
#define RETAIL_FIRST     0x00011000u
#define RETAIL_CODE_END  0x00411000u    /* .rdata starts at 0x411520 */
#define RETAIL_END       0x00586000u

void h2n_log(const char *format, ...);
void h2n_fatal(const char *format, ...);
void h2n_note_activity(void);              /* timing.c: something new happened */
HANDLE h2n_start_thread(LPTHREAD_START_ROUTINE start, void *parameter);   /* main.c: a thread of the layer's own */
void h2n_scrub_stack(unsigned bytes);      /* fill the dead part of the calling thread's stack (see main.c) */
unsigned h2n_next_sequence(void);
extern char g_disc_root[400];
int h2n_is_retail_address(DWORD address);

/* the system's and the C library's own functions, for the layer itself (the game's calls of the same names are
   wrapped in xapi.c) */
FILE *h2n_real_fopen(const char *name, const char *mode);
void *h2n_real_VirtualAlloc(void *address, SIZE_T size, DWORD type, DWORD protect);

/* xapi.c */
int h2n_xbox_path(const char *in, char *out, size_t size);

/* dump.c: a snapshot of every thread and of the game's data, written when the run stops on a fault or hangs */
void h2n_write_stop_file(EXCEPTION_POINTERS *e, const char *reason);

/* a stand-in in stubs.S was called (index into h2n_stub_names) */
void __cdecl h2n_stub_hit(unsigned index);
#endif

/* xiso.c: the disc as an Xbox disc image (.iso) instead of a folder */
int h2n_iso_use(const char *path);
int h2n_iso_search(const char *folder);
int h2n_iso_active(void);
const char *h2n_iso_path(void);
int h2n_iso_find(const char *rel, ULONGLONG *offset, DWORD *size, DWORD *attributes);
unsigned char *h2n_iso_read(const char *rel, size_t *size);
HANDLE h2n_iso_open(const char *rel, DWORD flags);
int h2n_iso_handle(HANDLE h, ULONGLONG *base, DWORD *size);
void h2n_iso_forget(HANDLE h);

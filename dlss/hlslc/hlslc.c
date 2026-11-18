/* hlslc: compile one HLSL entry point with the d3dcompiler DLL named on the command line, and write the byte code as a
 * C array (with the compiler's disassembly as a comment, so that what the shader does can be read without it).
 *
 *   hlslc <d3dcompiler dll> <source.hlsl> <entry> <target> <array name> <out.h> [NAME=VALUE | NAME] ...
 *
 * Used for H2Classic only with Wine's own built-in d3dcompiler_47.dll (vkd3d-shader), run under Wine: the DLSS helper's
 * compute shader (dlss/shaders/dlss_inputs.cs.hlsl, cs_5_0). #include "file" is looked up next to the source. A build
 * tool only: nothing of it goes into the game's exe. */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { const char *Name, *Definition; } MACRO;
typedef struct HBLOB HBLOB;
typedef struct { HRESULT (WINAPI *QueryInterface)(HBLOB *, const void *, void **); ULONG (WINAPI *AddRef)(HBLOB *); ULONG (WINAPI *Release)(HBLOB *);
	void *(WINAPI *GetBufferPointer)(HBLOB *); SIZE_T (WINAPI *GetBufferSize)(HBLOB *); } HBLOB_VTBL;
struct HBLOB { const HBLOB_VTBL *v; };
typedef struct INCLUDE INCLUDE;
typedef struct { HRESULT (WINAPI *Open)(INCLUDE *, int, const char *, const void *, const void **, UINT *); HRESULT (WINAPI *Close)(INCLUDE *, const void *); } INCLUDE_VTBL;
struct INCLUDE { const INCLUDE_VTBL *v; };
typedef HRESULT (WINAPI *COMPILE)(const void *, SIZE_T, const char *, const MACRO *, INCLUDE *, const char *, const char *, UINT, UINT, HBLOB **, HBLOB **);
typedef HRESULT (WINAPI *DISASSEMBLE)(const void *, SIZE_T, UINT, const char *, HBLOB **);

static char g_dir[MAX_PATH];

static char *read_file(const char *name, long *size)
{
	FILE *f = fopen(name, "rb");
	char *p;
	if (!f) return NULL;
	fseek(f, 0, SEEK_END); *size = ftell(f); fseek(f, 0, SEEK_SET);
	p = malloc(*size + 1);
	if (fread(p, 1, *size, f) != (size_t)*size) { fclose(f); free(p); return NULL; }
	p[*size] = 0; fclose(f);
	return p;
}
static HRESULT WINAPI inc_open(INCLUDE *self, int type, const char *name, const void *parent, const void **data, UINT *bytes)
{
	char path[MAX_PATH * 2];
	long size;
	(void)self; (void)type; (void)parent;
	snprintf(path, sizeof(path), "%s%s", g_dir, name);
	*data = read_file(path, &size);
	if (!*data) { fprintf(stderr, "hlslc: cannot open include %s\n", path); return E_FAIL; }
	*bytes = (UINT)size;
	return S_OK;
}
static HRESULT WINAPI inc_close(INCLUDE *self, const void *data) { (void)self; free((void *)data); return S_OK; }
static const INCLUDE_VTBL g_inc_vtbl = { inc_open, inc_close };

int main(int argc, char **argv)
{
	HMODULE dll;
	COMPILE compile;
	DISASSEMBLE disassemble;
	MACRO macros[64];
	INCLUDE inc = { &g_inc_vtbl };
	HBLOB *code = NULL, *errors = NULL, *text = NULL;
	char *source, *slash;
	long size;
	int i, n = 0;
	HRESULT hr;
	FILE *out;
	const unsigned *words;
	unsigned count;

	if (argc < 7) { fprintf(stderr, "usage: hlslc <D3DCompiler_43.dll> <source.hlsl> <entry> <target> <array name> <out.h> [NAME=VALUE]...\n"); return 2; }
	dll = LoadLibraryA(argv[1]);
	argv++; argc--;
	compile = dll ? (COMPILE)GetProcAddress(dll, "D3DCompile") : NULL;
	disassemble = dll ? (DISASSEMBLE)GetProcAddress(dll, "D3DDisassemble") : NULL;
	if (!compile) { fprintf(stderr, "hlslc: D3DCompiler_43.dll did not load\n"); return 1; }
	source = read_file(argv[1], &size);
	if (!source) { fprintf(stderr, "hlslc: cannot read %s\n", argv[1]); return 1; }
	strncpy(g_dir, argv[1], sizeof(g_dir) - 1);
	slash = strrchr(g_dir, '\\'); if (!slash) slash = strrchr(g_dir, '/');
	if (slash) slash[1] = 0; else g_dir[0] = 0;
	static char defines_text[4096];
	for (i = 6; i < argc; i++) { strncat(defines_text, " ", sizeof(defines_text) - strlen(defines_text) - 1); strncat(defines_text, argv[i], sizeof(defines_text) - strlen(defines_text) - 1); }
	for (i = 6; i < argc && n < 62; i++)
	{
		char *eq = strchr(argv[i], '=');
		if (eq) { *eq = 0; macros[n].Name = argv[i]; macros[n].Definition = eq + 1; }
		else { macros[n].Name = argv[i]; macros[n].Definition = "1"; }
		n++;
	}
	macros[n].Name = NULL; macros[n].Definition = NULL;
	hr = compile(source, (SIZE_T)size, argv[1], macros, &inc, argv[2], argv[3], 0, 0, &code, &errors);
	if (errors) fprintf(stderr, "%s", (const char *)errors->v->GetBufferPointer(errors));
	if (FAILED(hr) || !code) { fprintf(stderr, "hlslc: %s %s failed (%08lx)\n", argv[2], argv[3], (unsigned long)hr); return 1; }
	words = code->v->GetBufferPointer(code);
	count = (unsigned)(code->v->GetBufferSize(code) / 4);
	out = fopen(argv[5], "wb");
	if (!out) { fprintf(stderr, "hlslc: cannot write %s\n", argv[5]); return 1; }
	fprintf(out, "/* %s: %s, %s, compiled by D3DCompiler_43 (hlslc)", argv[4], argv[2], argv[3]);
	fprintf(out, "%s */\n", defines_text);
	if (disassemble && SUCCEEDED(disassemble(words, count * 4, 0, NULL, &text)) && text)
	{
		const char *t = text->v->GetBufferPointer(text);
		fprintf(out, "#if 0\n");
		for (; *t; t++) if (*t != '\r') fputc(*t, out);
		fprintf(out, "#endif\n");
	}
	fprintf(out, "static const DWORD %s[%u] = {", argv[4], count);
	for (i = 0; i < (int)count; i++) fprintf(out, "%s0x%08x,", i % 8 ? " " : "\n\t", words[i]);
	fprintf(out, "\n};\n");
	fclose(out);
	printf("%s: %u words\n", argv[4], count);
	return 0;
}

/* H2Classic: the first map's wait drawn as the game's level loading screen (build 53).
 *
 * At start the game copies mainmenu.map to its cache and waits in function_163890 (loading.cpp). On the console the intro
 * movie plays over that wait; with the intro left out the game draws its plain progress frame instead - function_12b6f0:
 * the text "LOADING n%" (function_2233a0) on a dark blue clear. The level loading screen (the logo, the rings and the glow)
 * is function_289e0 (unknown_0350e0.cpp), which the frame function calls while the main menu waits for a level; its five
 * pictures, its vertex shaders and its settings are tags of the loaded map (the globals' rasterizer block, g_485a80), so
 * the game cannot draw it before the first map is in memory.
 *
 * Here the game's own function_289e0 draws the first wait too. What it needs is brought in early, the way the game's
 * loader (cache_files_load_map, unknown_122870.cpp) and its texture cache (function_12d5a0 / function_12d6d0,
 * unknown_12c0d0.cpp) bring it in:
 *  - the tag block of the map being waited for is read from the user's own file on D: (read only) into the layer's
 *    own memory. It cannot go where the loader will put it a moment later: during the wait that memory is the pool of
 *    the wait's own texture cache (function_18f100, unknown_18f100.cpp), which the map copy's buffer and the render
 *    targets are taken from. So the few pointers the screen follows are moved to where the bytes are: the tag table's
 *    entries of the tags it uses, each bitmap tag's list of pictures, the settings' list of vertex shaders and each
 *    vertex shader's entries, inputs and program (s_shader_tag, unknown_01cf50.cpp). Nothing of the game's memory is
 *    written;
 *  - the pixels of the screen's pictures are read from the same file into the layer's contiguous memory and the game's
 *    function_12d6d0 builds their texture headers; each bitmap's texture pointer and frame stamp are set as the texture
 *    cache sets them for a resident texture, so the game's lookup (cached_bitmap_texture) returns them and the texture
 *    cache is not asked;
 *  - while function_289e0 runs - and only then - the tag table (g_4e3b44), the rasterizer settings (g_485a80) and the game
 *    clock (g_510c54) point at these; the clock counts 30 ticks a second, as the main menu's does during a level's wait.
 * The screen is started and ended as the menu's loading screen does it: function_28980 with the address of the progress
 * value, function_28950 when the wait is over. The progress is the copy's (the value function_12b6f0 is given).
 * Only the first wait (the one at start) is drawn this way: after it the game's waits are as they were.
 * Anything missing or unexpected (no room, another file's pixels, a header that does not check) leaves the game's own
 * progress frame. H2N_BOOTSCREEN=0 leaves it too. */
#include <xtl.h>

extern "C" {
void __cdecl h2n_log(const char *format, ...);
int __cdecl h2n_env_int(const char *name, int otherwise);
void *__cdecl h2n_contig_alloc(unsigned size);
void __cdecl h2n_contig_free(void *address);
void __cdecl gx_memory_gone(const unsigned char *memory);
void *__cdecl h2n_calloc(unsigned size);
void __cdecl h2n_free(void *memory);
int __cdecl h2n_boot_screen(float progress);
void __cdecl h2n_boot_screen_done(void);
}

/* ---- the game's (declared as its sources declare them) ---- */
typedef unsigned char byte;
typedef float real;
struct s_tag_instance { DWORD group; long index; byte *bytes; long size; };
extern s_tag_instance *g_4e3b44;                 /* the loaded map's tag table */
extern byte *g_485a80;                           /* the globals' rasterizer block */
struct s_game_time_globals { bool active; byte unknown01; short field_2_3; real rate; long game_time; real scale; byte rest[0x30]; };
extern s_game_time_globals *g_510c54;
struct s_physical_memory_globals { long field_0; long base_address; long field_8_3; long field_c_6[5]; long field_20[5]; };
extern s_physical_memory_globals g_global_f9ae07;
extern long g_4ed294;                            /* 5 while function_18f100 waits for a map in its own memory stage */
extern char const *g_4e9bb8;                     /* the map function_163890 is waiting for */
union point3f { struct { real x, y, z; }; real n[3]; };
typedef void (__stdcall *t_aux_view_callback)(void *context);
void __fastcall function_2ca00(point3f const *position, t_aux_view_callback callback, void *context);
bool __fastcall function_289e0();
void __fastcall function_28980(long index);
void __fastcall function_28950(void);
struct s_bitmap_texture_view;
void __stdcall function_12d6d0(s_bitmap_texture_view const *bitmap, long level, D3DTexture *texture);
struct s_map_name { void map_file_path_get(char *path) const; };

#define BOOT_TEXTURES 16
static struct
{
	int state;                                   /* 0 not tried, 1 drawing, -1 the game's own frame, 2 over */
	s_tag_instance *instances;                   /* the tag table: only the tags the screen uses have their bytes */
	long count;
	byte *meta;                                  /* the map's tag data, and the addresses it was built for */
	DWORD meta_low, meta_high;
	byte *settings;
	s_game_time_globals time;
	real progress;
	DWORD started;
	D3DTexture texture[BOOT_TEXTURES];
	void *pixels[BOOT_TEXTURES];
	unsigned textures;
	unsigned frames;
} g_boot;

static bool read_at(HANDLE file, DWORD offset, void *buffer, DWORD size)
{
	DWORD got = 0;
	if (SetFilePointer(file, (LONG)offset, NULL, FILE_BEGIN) == 0xFFFFFFFF) return false;
	return ReadFile(file, buffer, size, &got, NULL) && got == size;
}
static DWORD sector_align(DWORD size) { return (size + 0x1ff) & ~0x1ffu; }

static void boot_free(void)
{
	for (unsigned i = 0; i < g_boot.textures; i++)
		if (g_boot.pixels[i]) { gx_memory_gone((const unsigned char *)g_boot.pixels[i]); h2n_contig_free(g_boot.pixels[i]); g_boot.pixels[i] = NULL; }
	g_boot.textures = 0;
	if (g_boot.instances) h2n_free(g_boot.instances);
	if (g_boot.meta) h2n_free(g_boot.meta);
	g_boot.instances = NULL; g_boot.meta = NULL; g_boot.settings = NULL;
}

/* where the bytes built for a tag data address are */
static byte *moved(DWORD address, DWORD size)
{
	if (address < g_boot.meta_low || address > g_boot.meta_high || size > g_boot.meta_high - address) return NULL;
	return g_boot.meta + (address - g_boot.meta_low);
}
/* a pointer field of the tag data, re-aimed at where its bytes are (a null pointer of an empty list stays) */
static bool move_field(byte *field, DWORD size)
{
	DWORD address = *(DWORD *)field;
	if (!address) return size == 0;
	byte *to = moved(address, size);
	if (!to) return false;
	*(DWORD *)field = (DWORD)to;
	return true;
}
/* a tag the screen uses: its entry of the tag table gets its bytes (once) */
static byte *boot_tag(const s_tag_instance *all, DWORD index, DWORD least, bool *first)
{
	*first = false;
	if (index == 0xffffffff || (long)(index & 0xffff) >= g_boot.count) return NULL;
	s_tag_instance *entry = &g_boot.instances[index & 0xffff];
	if (entry->bytes) return entry->bytes;
	const s_tag_instance *from = &all[index & 0xffff];
	if ((DWORD)from->size < least) return NULL;
	entry->bytes = moved((DWORD)from->bytes, (DWORD)from->size);
	*first = entry->bytes != NULL;
	return entry->bytes;
}

/* one picture's pixels and texture header (the texture cache's function_12d5a0, with the pixels read from D:) */
static bool boot_bitmap(HANDLE file, byte *bitmap)
{
	DWORD offset = *(DWORD *)(bitmap + 0x1c), size = *(DWORD *)(bitmap + 0x34);
	if (g_boot.textures >= BOOT_TEXTURES || !size || (offset & 0xc0000000) || *(short *)(bitmap + 0xa) != 0) return false;     /* pixels of another map file; not a flat picture */
	void *pixels = h2n_contig_alloc(sector_align(size));
	if (!pixels) return false;
	unsigned n = g_boot.textures++;
	g_boot.pixels[n] = pixels;
	if (!read_at(file, offset, pixels, sector_align(size)) && !read_at(file, offset, pixels, size)) return false;
	*(DWORD *)(bitmap + 0x40) = (DWORD)pixels;
	memset(&g_boot.texture[n], 0, sizeof(D3DTexture));
	function_12d6d0((s_bitmap_texture_view const *)bitmap, 0, &g_boot.texture[n]);
	*(D3DTexture **)(bitmap + 0x50) = &g_boot.texture[n];
	*(long *)(bitmap + 0x70) = 0x7fffffff;
	return true;
}

static bool boot_load(void)
{
	char path[256];
	byte header[0x800];
	path[0] = 0;
	if (!g_4e9bb8) return false;
	((s_map_name const *)g_4e9bb8)->map_file_path_get(path);
	HANDLE file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (file == INVALID_HANDLE_VALUE) { h2n_log("loading screen: %s does not open; the game's own progress frame is shown\n", path); return false; }
	bool good = false;
	const char *why = "the map's header";
	byte *index = NULL;
	do
	{
		if (!read_at(file, 0, header, sizeof(header))) break;
		if (*(DWORD *)header != 'head' || *(DWORD *)(header + 0x7fc) != 'foot' || *(DWORD *)(header + 4) != 8) break;
		DWORD tag_offset = *(DWORD *)(header + 0x10), tag_size = *(DWORD *)(header + 0x14);
		DWORD data_size = *(DWORD *)(header + 0x18), whole = *(DWORD *)(header + 0x1c);
		if (whole < data_size || whole - data_size < tag_size || tag_size < 0x20 || whole > 0x4000000) break;
		/* the address the tags were built for: where the loader (cache_files_load_map) will put them - the bottom of the
		   memory stage under the wait's own (function_18f100 drops the wait's stage before the map is loaded) */
		why = "the place of the map's tags";
		long stage = g_global_f9ae07.field_0 - 1;
		if (g_4ed294 != 5 || stage < 0 || stage >= 4) break;
		long address = g_global_f9ae07.field_c_6[stage];
		if (!address || (long)(address + ((whole + 0xfff) & 0xfffff000)) > g_global_f9ae07.field_20[stage]) break;
		DWORD base = (DWORD)address | 0x80000000;
		why = "the map's tags";
		index = (byte *)h2n_calloc(sector_align(tag_size));
		g_boot.meta = (byte *)h2n_calloc(sector_align(data_size));
		if (!index || !g_boot.meta) break;
		g_boot.meta_low = base + whole - data_size;
		g_boot.meta_high = base + whole;
		if (!read_at(file, tag_offset, index, tag_size)) break;
		if (!read_at(file, tag_offset + tag_size, g_boot.meta, data_size)) break;
		DWORD table = *(DWORD *)(index + 8), globals_index = *(DWORD *)(index + 0x10);
		long count = *(long *)(index + 0x18);
		if (*(DWORD *)(index + 0x1c) != 'tags' || count <= 0 || count > 0x10000 || table < base || table - base > tag_size ||
			(DWORD)count * sizeof(s_tag_instance) > tag_size - (table - base)) break;
		const s_tag_instance *all = (const s_tag_instance *)(index + (table - base));
		g_boot.count = count;
		g_boot.instances = (s_tag_instance *)h2n_calloc((unsigned)count * sizeof(s_tag_instance));
		if (!g_boot.instances) break;
		for (long i = 0; i < count; i++) { g_boot.instances[i].group = all[i].group; g_boot.instances[i].index = all[i].index; g_boot.instances[i].size = all[i].size; }
		bool first;
		why = "the screen's settings";
		byte *globals = boot_tag(all, globals_index, 0x110, &first);
		if (!globals || !*(long *)(globals + 0x108)) break;
		byte *settings = moved(*(DWORD *)(globals + 0x10c), 0x110);
		if (!settings) break;
		/* the vertex shaders (the settings' list at +0x58; the screen's passes take the one at +0x64 of it) */
		why = "the screen's vertex shaders";
		long shaders = *(long *)(settings + 0x58);
		if (shaders < 13 || shaders > 256 || !move_field(settings + 0x5c, (DWORD)shaders * 8)) break;
		byte *list = *(byte **)(settings + 0x5c);
		bool programs = true;
		for (long i = 0; i < shaders && programs; i++)
		{
			DWORD tag = *(DWORD *)(list + i * 8 + 4);
			if (tag == 0xffffffff) { if (i == 12) programs = false; continue; }
			byte *shader = boot_tag(all, tag, 12, &first);
			if (!shader) { programs = false; break; }
			if (!first) continue;
			DWORD entries = *(DWORD *)(shader + 4);
			if (entries > 4096 || !move_field(shader + 8, entries * 0x1c)) { programs = false; break; }
			byte *entry = *(byte **)(shader + 8);
			for (DWORD e = 0; e < entries; e++, entry += 0x1c)
				if (!move_field(entry + 8, *(DWORD *)(entry + 4) * 2) || !move_field(entry + 0x10, *(DWORD *)(entry + 0xc))) { programs = false; break; }
		}
		if (!programs) break;
		static const int tags[5] = { 0x1c, 0x24, 0x2c, 0x3c, 0x44 };
		why = "the screen's pictures";
		bool pictures = true;
		for (int t = 0; t < 5 && pictures; t++)
		{
			byte *definition = boot_tag(all, *(DWORD *)(settings + tags[t]), 0x4c, &first);     /* (a missing one would send the game to a fallback picture) */
			if (!definition) { pictures = false; break; }
			if (!first) continue;
			long bitmaps = *(long *)(definition + 0x44);
			if (bitmaps <= 0 || bitmaps > BOOT_TEXTURES || !move_field(definition + 0x48, (DWORD)bitmaps * 0x74)) { pictures = false; break; }
			byte *bitmap = *(byte **)(definition + 0x48);
			for (long b = 0; b < bitmaps; b++)
				if (!boot_bitmap(file, bitmap + b * 0x74)) { pictures = false; break; }
		}
		if (!pictures) break;
		g_boot.settings = settings;
		good = true;
	} while (0);
	if (index) h2n_free(index);
	CloseHandle(file);
	if (!good) { boot_free(); h2n_log("loading screen: %s of %s did not check; the game's own progress frame is shown\n", why, path); }
	else h2n_log("loading screen: the level loading screen draws the first wait (%u pictures read from %s)\n", g_boot.textures, path);
	return good;
}

static void __stdcall boot_draw(void *context)
{
	s_tag_instance *instances = g_4e3b44;
	byte *settings = g_485a80;
	s_game_time_globals *time = g_510c54;
	g_4e3b44 = g_boot.instances;
	g_485a80 = g_boot.settings;
	g_510c54 = &g_boot.time;
	function_289e0();
	g_4e3b44 = instances;
	g_485a80 = settings;
	g_510c54 = time;
}

/* from function_12b6f0 (unknown_12b6f0.cpp), in place of its progress frame: 1 when the frame was drawn here */
int __cdecl h2n_boot_screen(float progress)
{
	if (g_boot.state < 0 || g_boot.state == 2 || g_4e3b44) return 0;      /* only before the first map is in memory */
	if (!g_boot.state)
	{
		if (!h2n_env_int("H2N_BOOTSCREEN", 1) || !boot_load()) { g_boot.state = -1; return 0; }
		g_boot.state = 1;
		g_boot.started = GetTickCount();
		g_boot.time.active = true;
		g_boot.time.rate = 1.0f / 30.0f;
		g_boot.time.scale = 1.0f;
		g_boot.progress = 0.0f;
		s_game_time_globals *time = g_510c54;
		g_510c54 = &g_boot.time;                 /* the screen's start time is read from the clock it will be drawn with */
		function_28980((long)&g_boot.progress);
		g_510c54 = time;
	}
	g_boot.progress = progress;
	g_boot.time.game_time = (long)((GetTickCount() - g_boot.started) * 30 / 1000);
	point3f black;
	black.x = black.y = black.z = 0.0f;
	function_2ca00(&black, boot_draw, NULL);
	g_boot.frames++;
	return 1;
}

/* from function_163890 (loading.cpp) when its wait is over: the screen ends as the menu's loading screen ends it */
void __cdecl h2n_boot_screen_done(void)
{
	if (!g_boot.state) g_boot.state = -1;        /* the first wait went by without the progress frame (the intro played over it): later waits keep the game's own */
	if (g_boot.state != 1) return;
	g_boot.state = 2;
	function_28950();
	for (DWORD stage = 0; stage < 4; stage++) D3DDevice_SetTexture(stage, NULL);
	boot_free();
	h2n_log("loading screen: the first wait is over after %u frames\n", g_boot.frames);
}

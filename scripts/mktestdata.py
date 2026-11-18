"""Make stand-in game files for testing the start-up without the real ones: eight "fonts" (no glyphs) and three
"maps" with valid headers and checksums but no tags. With them the game's start-up follows the real course: fonts are
copied, the main menu is asked for, mainmenu.map is copied into the cache behind the loading screen, opened and read;
the load then fails on the missing tags and the game idles. Without fonts the game sets its error state at once and
never asks for a map, which is NOT what a real game folder does.

usage: mktestdata.py <game folder>     (writes <game folder>/maps/..., next to the default.xbe you put there)"""
import os, struct, random, sys

root = sys.argv[1]
fonts = os.path.join(root, 'maps', 'fonts'); os.makedirs(fonts, exist_ok=True)
names = ['fixedsys-9', 'conduit-12', 'handel_gothic-11', 'handel_gothic-24', 'conduit-13', 'mslcd-14', 'conduit-16', 'handel_gothic-13']
open(os.path.join(fonts, 'font_table.txt'), 'wb').write(('\r\n'.join(names) + '\r\n').encode())
for n in names:
    d = bytearray(0x10000)
    struct.pack_into('<IhhH', d, 0x200, 0xf0000001, 10, 2, 1)     # the header sits at 0x200: version, ascending, descending, leading
    open(os.path.join(fonts, n), 'wb').write(d)

def make_map(name, kind, size, seed):
    """kind: 2 main menu, 3 shared, 4 single player shared (0 and 1 are campaign and multiplayer levels)"""
    random.seed(seed)
    h = bytearray(size)
    for i in range(0x800, size, 4096): struct.pack_into('<I', h, i, random.getrandbits(32))
    struct.pack_into('<3I', h, 0, 0x68656164, 8, size)                 # 'head', version 8, file size
    struct.pack_into('<4I', h, 0x10, 0x100000, 0x80000, 0x40000, 0x1000)   # tag data offset and sizes (nothing valid there)
    h[0x120:0x120 + 15] = b'02.09.27.09809\0'                          # the v1.0 build
    struct.pack_into('<H', h, 0x140, kind)
    text = name.encode() + b'\0'; h[0x198:0x198 + len(text)] = text
    struct.pack_into('<I', h, 0x7fc, 0x666f6f74)                       # 'foot'
    x = 0
    for i in range(0x800, size, 4): x ^= struct.unpack_from('<I', h, i)[0]
    struct.pack_into('<I', h, 0x2d0, x)                                # xor of every word after the header
    open(os.path.join(root, 'maps', name + '.map'), 'wb').write(h)

make_map('mainmenu', 2, 3 << 20, 1)
make_map('shared', 3, 5 << 20, 2)
make_map('single_player_shared', 4, 6 << 20, 3)
print('stand-in fonts and maps written under', os.path.join(root, 'maps'))

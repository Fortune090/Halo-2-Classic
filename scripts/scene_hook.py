#!/usr/bin/env python3
"""h2native, anti-aliasing: point function_2ba10's call of function_13680 at the layer's h2n_scene_13680 (gfx9.c), which
runs the same call between gx_scene_begin and gx_scene_end (the view's 3D, before its HUD).

function_2ba10 (unknown_023540.cpp) is the only code in unknown_023540.obj that calls ?function_13680@@YIX_N@Z (one
REL32 relocation; the script checks). Renaming that external symbol in this one object makes the call land on
@h2n_scene_13680@4 (void __fastcall h2n_scene_13680(int)); function_13680 itself (unknown_01cf50.obj) is untouched and is
what the layer then calls. The same as this source change to function_2ba10, without recompiling the object:
        extern "C" void __fastcall h2n_scene_13680(int enabled);     (defined in layer2/gfx9.c)
        h2n_scene_13680(enabled);                                     (in place of function_13680(enabled))
Usage: scene_hook.py <obj in> <obj out>      (in place when both are the same file)"""
import struct, sys

OLD = b'?function_13680@@YIX_N@Z'
NEW = b'@h2n_scene_13680@4'

def main(src, dst):
    d = bytearray(open(src, 'rb').read())
    machine, nsec, stamp, symptr, nsym, optsize, chars = struct.unpack_from('<HHIIIHH', d, 0)
    if machine != 0x14c: raise SystemExit('not an i386 COFF object')
    strtab = symptr + nsym * 18
    strsize = struct.unpack_from('<I', d, strtab)[0]
    if strtab + strsize != len(d): raise SystemExit('the string table is not at the end of the object')
    found = None
    i = 0
    while i < nsym:
        o = symptr + i * 18
        if struct.unpack_from('<I', d, o)[0] == 0:
            off = struct.unpack_from('<I', d, o + 4)[0]
            name = bytes(d[strtab + off:d.index(b'\0', strtab + off)])
        else:
            name = bytes(d[o:o + 8]).rstrip(b'\0')
        if name == OLD:
            if found is not None: raise SystemExit('the symbol is there twice')
            found = i
        if name == NEW: raise SystemExit('already done')
        i += 1 + d[o + 17]
    if found is None: raise SystemExit('no symbol %s' % OLD.decode())
    o = symptr + found * 18
    value, section = struct.unpack_from('<Ih', d, o + 8)
    if section != 0: raise SystemExit('the symbol is defined here, not called')
    # every relocation against it: exactly one, a REL32 (a call)
    uses = 0
    for k in range(nsec):
        h = 20 + optsize + k * 40
        relptr, nrel = struct.unpack_from('<I', d, h + 24)[0], struct.unpack_from('<H', d, h + 32)[0]
        for r in range(nrel):
            va, sym, typ = struct.unpack_from('<IIH', d, relptr + r * 10)
            if sym == found:
                if typ != 0x14: raise SystemExit('a use that is not a call (type %#x)' % typ)
                uses += 1
    if uses != 1: raise SystemExit('%d uses, one expected' % uses)
    # the new name at the end of the string table
    new_off = strsize
    d += NEW + b'\0'
    struct.pack_into('<I', d, strtab, strsize + len(NEW) + 1)
    struct.pack_into('<II', d, o, 0, new_off)
    open(dst, 'wb').write(d)
    print('%s: the call of %s now goes to %s' % (dst, OLD.decode(), NEW.decode()))

if __name__ == '__main__':
    if len(sys.argv) != 3: raise SystemExit(__doc__)
    main(sys.argv[1], sys.argv[2])

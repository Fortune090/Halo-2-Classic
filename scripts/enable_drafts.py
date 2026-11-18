#!/usr/bin/env python3
"""enable_drafts.py <repo>: for the native build, switch on the drafts of functions that the source keeps under '#if 0'
only because writing them changes how the whole-program compiler lays out other, already matching functions (that does
not matter to the native build, which compiles every file on its own), and switch off the empty stubs that stood in for
them. Edits src/*.cpp and src/stubs/*.cpp in place; running it twice changes nothing more."""
import sys, os, re
root = sys.argv[1]
MARK = ' // h2native: on in the native build'
EDITS = {
 'src/unknown_023540.cpp': [('#if 0 // Activation must preserve all matched callers and callees.', '#if 1' + MARK + ' (0x2b5d0, the frame renderer)')],
 'src/unknown_03bcb0.cpp': [('#if 0 // Activation changes matched rendering helpers.', '#if 1' + MARK + ' (0x3bd00)'),
   # a fault in the draft of 0x4d0b0: retail hands 0x4daa0 these two flags the other way round (AL = the mask test, CL = bit 9)
   ('''        bool filtered = TEST_FIELD_BIT((entry->unknown00 >> 9) & 1) && !(part[2] & 8);
        bool strip = (entry->unknown00 & 0x40) && *(long *)(entry->unknown10 + 4) && *(long *)(geometry + 8);
        function_4daa0(filtered, strip, (s_4daa0_geometry *)geometry,''',
    '''        bool strip = TEST_FIELD_BIT((entry->unknown00 >> 9) & 1) && !(part[2] & 8);       // h2native: the two were named the other way round
        bool filtered = (entry->unknown00 & 0x40) && *(long *)(entry->unknown10 + 4) && *(long *)(geometry + 8);
        function_4daa0(filtered, strip, (s_4daa0_geometry *)geometry,''')],
 'src/unknown_01cf50.cpp': [('#if 0\n// Retail 0x14b60. The additional clear call', '#if 1' + MARK + ' (0x14b60, clear)\n// Retail 0x14b60. The additional clear call')],
 'src/unknown_013450.cpp': [('#if 0 // Draft (also defines g_4b893c', '#if 1' + MARK + ' (0x13450) // Draft (also defines g_4b893c')],
 'src/stubs/lane_k.cpp': [('// @stub 0x2b5d0\nvoid function_2b5d0(long arg_0, long arg_1, long arg_2, long arg_3, s_223240 const *arg_4) { }', '// @stub 0x2b5d0' + MARK + ': the draft in unknown_023540.cpp')],
 'src/stubs/lane_w.cpp': [
   ('// @stub 0x14b60\nvoid __stdcall function_14b60(dword, dword, real, byte) {}', '// @stub 0x14b60' + MARK + ': the draft in unknown_01cf50.cpp'),
   ('// @stub 0x3bd00\nbool __stdcall function_3bd00(real, dword) { return true; }', '// @stub 0x3bd00' + MARK + ': the draft in unknown_03bcb0.cpp'),
   ('// @stub 0x13450\nbool function_13450(s_depth_parameters_13450 const *parameters) { return false; }', '// @stub 0x13450' + MARK + ': the draft in unknown_013450.cpp'),
 ],
}
for name, edits in EDITS.items():
    path = os.path.join(root, name)
    text = open(path, encoding='utf-8', newline='').read()
    crlf = '\r\n' in text
    if crlf: text = text.replace('\r\n', '\n')
    changed = 0
    for old, new in edits:
        if new in text: continue
        if old not in text: print('NOT FOUND in', name, ':', old[:60].replace('\n', ' | ')); continue
        text = text.replace(old, new, 1); changed += 1
    if changed:
        if crlf: text = text.replace('\n', '\r\n')
        open(path, 'w', encoding='utf-8', newline='').write(text)
    print(name, 'changed' if changed else 'as it was', changed)

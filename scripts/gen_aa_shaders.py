#!/usr/bin/env python3
"""h2native: assemble the anti-aliasing shaders (layer2/shaders/*.asm) into layer2/gfx_aa_shaders.h.

Each shader is checked first by the rules gfx9.c's check_shader() applies to the layer's shaders (what Windows' Direct3D 9
enforces when it accepts a shader): an instruction reads at most one constant register, one input register and three
temporaries; a temporary is written before it is read; rcp/rsq/exp/log take one component; inputs and samplers are
declared; every declared output is written in full (vertex shaders); the colour is written in full (pixel shaders).
SMAA is assembled once per preset, with the preset's values given as #define lines (SMAA.hlsl's SMAA_PRESET_HIGH and
SMAA_PRESET_ULTRA). Usage: gen_aa_shaders.py   (paths are fixed: run from anywhere)"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sm3asm

LAYER = '/home/claude/native/layer2'
SH = LAYER + '/shaders'

PRESETS = {  # SMAA.hlsl: SMAA_THRESHOLD, 2 * SMAA_MAX_SEARCH_STEPS, rep counts (steps + 2), SMAA_MAX_SEARCH_STEPS_DIAG - 1,
             # 1 - SMAA_CORNER_ROUNDING / 100. LOW and MEDIUM leave the diagonal and corner detection out (see smaa_blend.ps.asm).
    'low':    dict(THRESHOLD='0.15', STEPS2='8', REP_ORTHO='6', REP_DIAG='1', DIAGMAX='-1', CORNERW='0'),
    'medium': dict(THRESHOLD='0.1', STEPS2='16', REP_ORTHO='10', REP_DIAG='1', DIAGMAX='-1', CORNERW='0'),
    'high':   dict(THRESHOLD='0.1', STEPS2='32', REP_ORTHO='18', REP_DIAG='10', DIAGMAX='7', CORNERW='0.75'),
    'ultra':  dict(THRESHOLD='0.05', STEPS2='64', REP_ORTHO='34', REP_DIAG='18', DIAGMAX='15', CORNERW='0.75'),
}

SHADERS = [  # (C name, file, preset or None)
    ('g_aa_quad_vs', 'aa_quad.vs.asm', None),
    ('g_aa_edge_ps', 'aa_edge.ps.asm', None),
    ('g_aa_copy_ps', 'aa_copy.ps.asm', None),
    ('g_smaa_edge_vs', 'smaa_edge.vs.asm', None),
    ('g_smaa_neighbor_vs', 'smaa_neighbor.vs.asm', None),
    ('g_smaa_neighbor_ps', 'smaa_neighbor.ps.asm', None),
    ('g_smaa_edge_ps_low', 'smaa_edge.ps.asm', 'low'),
    ('g_smaa_blend_vs_low', 'smaa_blend.vs.asm', 'low'),
    ('g_smaa_blend_ps_low', 'smaa_blend.ps.asm', 'low'),
    ('g_smaa_edge_ps_medium', 'smaa_edge.ps.asm', 'medium'),
    ('g_smaa_blend_vs_medium', 'smaa_blend.vs.asm', 'medium'),
    ('g_smaa_blend_ps_medium', 'smaa_blend.ps.asm', 'medium'),
    ('g_smaa_edge_ps_high', 'smaa_edge.ps.asm', 'high'),
    ('g_smaa_blend_vs_high', 'smaa_blend.vs.asm', 'high'),
    ('g_smaa_blend_ps_high', 'smaa_blend.ps.asm', 'high'),
    ('g_smaa_edge_ps_ultra', 'smaa_edge.ps.asm', 'ultra'),
    ('g_smaa_blend_vs_ultra', 'smaa_blend.vs.asm', 'ultra'),
    ('g_smaa_blend_ps_ultra', 'smaa_blend.ps.asm', 'ultra'),
    ('g_taa_depth_ps', 'taa_depth.ps.asm', None),
    ('g_taa_velocity_ps', 'taa_velocity.ps.asm', None),
    ('g_taa_resolve_ps', 'taa_resolve.ps.asm', None),
    ('g_taa_debug_ps', 'taa_debug.ps.asm', None),
]

RT_TEMP, RT_INPUT, RT_CONST, RT_OUTPUT, RT_COLOROUT, RT_DEPTHOUT, RT_SAMPLER = 0, 1, 2, 6, 8, 9, 10

def check(words, name):
    """gfx9.c check_shader(), the same rules (plus: flow control and texkill as Direct3D 9 defines them), and the rules of
    Microsoft's Direct3D 9 reference that Wine does not enforce but Windows does (s9p-aa2's taa_depth was refused for the
    first): the Output Color Register page - oC# is written only by mov, with no source swizzle (except the default .xyzw)
    and no source modifier, at most once, never inside dynamic flow control, and oC0 always; sincos - ps (ps_3_0): the
    destination a temporary with the mask .x, .y or .xy, the source a replicate swizzle; pow and the other one-component
    operations: replicate swizzles; texld: the sampler without a modifier"""
    problems = []
    depth = 0; oc_writes = [0] * 4
    pixel = (words[0] >> 16) == 0xffff
    written = [0] * 32
    out_declared = [0] * 16; out_written = [0] * 16
    inputs = 0; samplers = 0
    i = 1; index = 0
    def rtype(t): return ((t >> 28) & 7) | ((t >> 8) & 0x18)
    while i < len(words) and (words[i] & 0xffff) != 0xffff:
        op = words[i] & 0xffff; length = (words[i] >> 24) & 15; first = i + 1
        if op == 31:  # dcl
            reg = words[first + 1]; t = rtype(reg)
            if t == RT_INPUT: inputs |= 1 << (reg & 31)
            elif t == RT_SAMPLER: samplers |= 1 << (reg & 31)
            elif t == RT_OUTPUT and (reg & 0x7ff) < 16: out_declared[reg & 0x7ff] |= (reg >> 16) & 15
            i = first + length; continue
        if op in (81, 48):  # def, defi
            i = first + length; continue
        flow = op in (38, 39, 41, 42, 43, 44, 45)
        if op in (38, 41): depth += 1
        elif op in (39, 43): depth -= 1
        has_dst = not flow and op != 65
        dstw = words[first] if (has_dst or op == 65) else 0
        mask = (dstw >> 16) & 15
        consts = set(); ins = set(); temps = set()
        for n in range(first + (1 if has_dst else 0), first + length):
            s = words[n]; t = rtype(s); num = s & 0x7ff; swz = (s >> 16) & 255
            if op == 65: read = mask; swz = 0xe4
            elif op == 8: read = 7
            elif op == 9: read = 15
            elif op in (6, 7, 14, 15, 32, 37):
                read = 1
                if swz != (swz & 3) * 0x55: problems.append('%d: a one-component operation is given several components' % index)
            elif op in (41, 45): read = 1
            elif op == 38: read = 0
            elif op in (66, 95): read = 15 if n == first + 1 else 0
            else: read = mask
            if op in (66, 95) and n == first + 2 and (s >> 24) & 15: problems.append('%d: the sampler has a modifier' % index)
            if pixel and has_dst and rtype(dstw) in (RT_COLOROUT, RT_DEPTHOUT) and (swz != 0xe4 or (s >> 24) & 15):
                problems.append('%d: an output colour (oC#) is written from a swizzled or modified source' % index)
            if t == RT_CONST: consts.add(num)
            elif t == RT_INPUT:
                ins.add(num)
                if not inputs & (1 << num): problems.append('%d: reads input v%d, which is not declared' % (index, num))
            elif t == RT_SAMPLER:
                if not samplers & (1 << num): problems.append('%d: uses sampler s%d, which is not declared' % (index, num))
            elif t == RT_TEMP:
                temps.add(num)
                for c in range(4):
                    if read & (1 << c) and not written[num] & (1 << ((swz >> (2 * c)) & 3)):
                        problems.append('%d: reads r%d.%s before it is written' % (index, num, 'xyzw'[(swz >> (2 * c)) & 3])); break
        if len(consts) > 1: problems.append('%d: reads %d constant registers %s' % (index, len(consts), sorted(consts)))
        if len(ins) > 1: problems.append('%d: reads %d input registers' % (index, len(ins)))
        if len(temps) > 3: problems.append('%d: reads %d temporaries' % (index, len(temps)))
        if has_dst:
            t = rtype(dstw)
            if not mask: problems.append('%d: writes nothing' % index)
            if op == 37 and (t != RT_TEMP or mask not in (1, 2, 3)): problems.append('%d: sincos writes other than r#.x, .y or .xy' % index)
            if pixel and t in (RT_COLOROUT, RT_DEPTHOUT):
                if op != 1: problems.append('%d: an output (oC#, oDepth) is written by an operation other than mov' % index)
                if depth: problems.append('%d: an output (oC#, oDepth) is written inside flow control' % index)
                if t == RT_COLOROUT and (dstw & 0x7ff) < 4:
                    oc_writes[dstw & 0x7ff] += 1
                    if oc_writes[dstw & 0x7ff] > 1: problems.append('%d: oC%d is written more than once' % (index, dstw & 0x7ff))
            if t == RT_TEMP: written[dstw & 0x7ff] |= mask
            elif (t == RT_OUTPUT and not pixel) or (t == RT_COLOROUT and pixel): out_written[dstw & 0x7ff] |= mask
            elif t == RT_DEPTHOUT and pixel: pass
            else: problems.append('%d: writes a register of type %d' % (index, t))
        index += 1
        i = first + length
    if index > 512: problems.append('%d instructions; 512 are sure to be allowed' % index)
    if pixel:
        if out_written[0] != 15: problems.append('does not write the whole colour')
    else:
        for k in range(16):
            if out_declared[k] != out_written[k]: problems.append('output %d declared %x written %x' % (k, out_declared[k], out_written[k]))
    return index, problems

def main():
    out = ['/* h2native: the anti-aliasing shaders (gfx_aa.c), assembled by scripts/gen_aa_shaders.py from the .asm files in layer2/shaders.',
           ' * Generated: edit the .asm files and run the script again. */']
    bad = 0
    for cname, fname, preset in SHADERS:
        text = open(os.path.join(SH, fname)).read()
        if preset:
            lines = text.split('\n')
            text = '\n'.join(lines[:1] + ['#define %s %s' % kv for kv in PRESETS[preset].items()] + lines[1:])
        words = sm3asm.assemble(text)
        count, problems = check(words, cname)
        print('%-24s %4d instructions %5d words%s' % (cname, count, len(words), '' if not problems else '  PROBLEMS: ' + '; '.join(problems[:8])))
        bad += len(problems)
        out.append('/* %s%s: %d instructions */' % (fname, ' (SMAA_PRESET_%s)' % preset.upper() if preset else '', count))
        out.append(sm3asm.c_array(cname + '_code', words))
    open(os.path.join(LAYER, 'gfx_aa_shaders.h'), 'w').write('\n'.join(out) + '\n')
    print('problems:', bad)
    return 1 if bad else 0

if __name__ == '__main__':
    sys.exit(main())

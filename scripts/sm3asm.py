#!/usr/bin/env python3
"""h2native: a small assembler for Direct3D 9 shader model 3 byte code (vs_3_0 / ps_3_0).

Writes the token words of each shader as a C array, so that the layer needs no shader compiler on the PC (as gfx9.c,
which writes its byte code directly). What the layer's own shaders use is understood:

  vs_3_0 | ps_3_0                       first line
  dcl_<usage><n> reg[.mask]             position, texcoord, color, ... (inputs v#, outputs o#)
  dcl_2d sN | dcl_cube sN | dcl_volume sN
  def cN, a, b, c, d                    defi iN, a, b, c, d
  <op>[_sat] dst[.mask], src, ...       mov add sub mul mad dp3 dp4 min max rcp rsq exp log frc abs lrp cmp dp2add
                                        pow nrm sincos dsx dsy
  texld / texldl dst, src, sN           texkill reg[.mask]   (the pixel is dropped if a named component is below 0)
  if_gt|if_ge|if_lt|if_le|if_eq|if_ne src0, src1 ; else ; endif
  rep iN ; endrep ; break ; break_gt|break_ge|break_lt|break_le|break_eq|break_ne src0, src1
  registers: rN cN vN sN oN iN oC0 oDepth; a source may be negated (-r0) and/or take the absolute value (r0_abs or
  r0.x_abs); swizzles .x .xy .xyzw ... (a short swizzle repeats its last letter)
  comments start with ; or //
  #define NAME text                     a plain word replacement in the lines that follow (register names, numbers)

Usage: sm3asm.py <file.asm> <c name>   (prints the C array)
"""
import re, struct, sys

OPS = {  # name: (opcode, number of source operands)
    'mov': (1, 1), 'add': (2, 2), 'sub': (3, 2), 'mad': (4, 3), 'mul': (5, 2), 'rcp': (6, 1), 'rsq': (7, 1),
    'dp3': (8, 2), 'dp4': (9, 2), 'min': (10, 2), 'max': (11, 2), 'exp': (14, 1), 'log': (15, 1), 'lrp': (18, 3),
    'frc': (19, 1), 'pow': (32, 2), 'abs': (35, 1), 'nrm': (36, 1), 'sincos': (37, 1), 'dsx': (86, 1), 'dsy': (87, 1),
    'cmp': (88, 3), 'dp2add': (90, 3),
}
TEX = {'texld': 66, 'texldl': 95}
COMPARE = {'gt': 1, 'eq': 2, 'ge': 3, 'lt': 4, 'ne': 5, 'le': 6}
USAGE = {'position': 0, 'blendweight': 1, 'blendindices': 2, 'normal': 3, 'psize': 4, 'texcoord': 5, 'tangent': 6,
         'binormal': 7, 'tessfactor': 8, 'positiont': 9, 'color': 10, 'fog': 11, 'depth': 12, 'sample': 13}
TEXTYPE = {'2d': 2, 'cube': 3, 'volume': 4}
REGTYPE = {'r': 0, 'v': 1, 'c': 2, 'o': 6, 'i': 7, 'oC': 8, 'oDepth': 9, 's': 10}
COMP = {'x': 0, 'y': 1, 'z': 2, 'w': 3, 'r': 0, 'g': 1, 'b': 2, 'a': 3}

def regbits(kind):
    return ((kind & 7) << 28) | ((kind & 0x18) << 8)

def parse_reg(text):
    m = re.fullmatch(r'(oC|oDepth|[rvcosi])(\d*)', text)
    if not m: raise ValueError('register? ' + text)
    kind = REGTYPE[m.group(1)]
    return kind, int(m.group(2) or 0)

def dst(text, sat=False):
    text = text.strip()
    reg, _, mask = text.partition('.')
    kind, num = parse_reg(reg)
    bits = 0
    for ch in (mask or 'xyzw'): bits |= 1 << COMP[ch]
    return 0x80000000 | regbits(kind) | (bits << 16) | ((1 << 20) if sat else 0) | num

def src(text):
    text = text.strip()
    neg = text.startswith('-')
    if neg: text = text[1:]
    absolute = '_abs' in text
    text = text.replace('_abs', '')
    reg, _, swz = text.partition('.')
    kind, num = parse_reg(reg)
    swz = swz or 'xyzw'
    swz = (swz + swz[-1] * 4)[:4]
    s = 0
    for i, ch in enumerate(swz): s |= COMP[ch] << (2 * i)
    mod = (12 if neg else 11) if absolute else (1 if neg else 0)
    return 0x80000000 | regbits(kind) | (s << 16) | (mod << 24) | num

def assemble(source):
    words = []
    defines = {}
    lines = []
    for l in source.splitlines():
        l = re.split(r';|//', l)[0].strip()
        if not l: continue
        if l.startswith('#define'):
            _, name, value = l.split(None, 2)
            defines[name] = value
            continue
        for name in sorted(defines, key=len, reverse=True):
            l = re.sub(r'(?<![\w.])' + re.escape(name) + r'(?!\w)', defines[name], l)
        lines.append(l)
    version = lines[0]
    words.append({'ps_3_0': 0xFFFF0300, 'vs_3_0': 0xFFFE0300}[version])
    depth = 0
    for line in lines[1:]:
        op, _, rest = line.partition(' ')
        args = [a.strip() for a in rest.split(',')] if rest.strip() else []
        def emit(opcode, params, control=0):
            words.append(opcode | (control << 16) | (len(params) << 24))
            words.extend(params)
        if op.startswith('dcl_'):
            what = op[4:]
            if what in TEXTYPE:
                emit(31, [0x80000000 | (TEXTYPE[what] << 27), dst(args[0])])
            else:
                m = re.fullmatch(r'([a-z]+?)(\d*)', what)
                emit(31, [0x80000000 | USAGE[m.group(1)] | (int(m.group(2) or 0) << 16), dst(args[0])])
        elif op == 'def':
            vals = [struct.unpack('<I', struct.pack('<f', float(eval(a, {}, {}))))[0] for a in args[1:5]]
            emit(81, [dst(args[0])] + vals)
        elif op == 'defi':
            vals = [int(a) & 0xffffffff for a in args[1:5]]
            emit(48, [dst(args[0])] + vals)
        elif op.startswith('if_') and op[3:] in COMPARE:
            emit(41, [src(args[0]), src(args[1])], COMPARE[op[3:]]); depth += 1
        elif op == 'else':
            emit(42, [])
        elif op == 'endif':
            emit(43, []); depth -= 1
        elif op == 'rep':
            emit(38, [src(args[0])]); depth += 1
        elif op == 'endrep':
            emit(39, []); depth -= 1
        elif op == 'break':
            emit(44, [])
        elif op.startswith('break_') and op[6:] in COMPARE:
            emit(45, [src(args[0]), src(args[1])], COMPARE[op[6:]])
        elif op == 'texkill':
            emit(65, [dst(args[0])])
        elif op in TEX:
            emit(TEX[op], [dst(args[0]), src(args[1]), src(args[2])])
        else:
            sat = op.endswith('_sat')
            name = op[:-4] if sat else op
            if name not in OPS: raise ValueError('unknown operation: ' + line)
            opcode, n = OPS[name]
            if len(args) != n + 1: raise ValueError('operands: ' + line)
            emit(opcode, [dst(args[0], sat)] + [src(a) for a in args[1:]])
    if depth: raise ValueError('if/rep not closed')
    words.append(0x0000FFFF)
    return words

def c_array(name, words):
    out = ['static const DWORD %s[%d] = {' % (name, len(words))]
    for i in range(0, len(words), 8):
        out.append('\t' + ', '.join('0x%08x' % w for w in words[i:i + 8]) + ',')
    out.append('};')
    return '\n'.join(out)

if __name__ == '__main__':
    text = open(sys.argv[1]).read()
    print(c_array(sys.argv[2], assemble(text)))

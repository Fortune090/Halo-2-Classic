#!/usr/bin/env python3
"""tracesum.py <log> [first frame only]: one line per draw of a described frame: where it went, what with, what it changed"""
import sys, re
lines = open(sys.argv[1], errors='replace').read().split('\n')
out = []; cur = None
for l in lines:
    m = re.match(r'\s+draw (\d+): primitive (\d+), (\d+) vertices( indexed)?, program (\w+) \(start (\d+)\), combiners (\w+) \((\d+) stages, modes (\w+)\), target (\w+) (\d+)x(\d+) depth (\w+), viewport (\d+) (\d+) (\d+) (\d+)', l)
    if m:
        cur = dict(n=int(m.group(1)), prim=m.group(2), verts=m.group(3), vp=m.group(5), ps=m.group(7), st=m.group(8), modes=m.group(9), tgt=m.group(10)[-6:], size=m.group(11)+'x'+m.group(12), tex=[], chg='?', vpt=' '.join(m.group(i) for i in (14,15,16,17)))
        out.append(cur); continue
    if l.startswith('       clear:'): out.append(dict(clear=l.strip())); cur = None; continue
    if 'visibility test' in l: out.append(dict(clear=l.strip())); continue
    if 'F11: frame' in l or 'the back buffer is' in l: out.append(dict(clear='==== ' + l.strip())); cur = None; continue
    if cur is None: continue
    m = re.match(r'\s+blend (\d+) (\w+) (\w+) op (\w+), alpha test (\d+) (\w+) (\d+), z (\d+) write (\d+) func (\w+), cull (\w+), mask (\w+), stencil (\d+)', l)
    if m: cur['blend'] = (m.group(2)+'/'+m.group(3)) if m.group(1) == '1' else '-'; cur['at'] = m.group(5); cur['z'] = m.group(8)+('w' if m.group(9)=='1' else '-')+m.group(10)[-1]; cur['mask'] = m.group(12); cur['sten'] = m.group(13); continue
    m = re.match(r'\s+depth: (.*?), range ([\d.e+-]+)\.\.([\d.e+-]+) of \S+, clip control (\w+), offset (\w+) slope (\S+) units (\S+)', l)
    if m: cur['rng'] = m.group(2); cur['cc'] = m.group(4); cur['off'] = (m.group(6)+'/'+m.group(7)) if m.group(5) == 'on' else ''; continue
    m = re.match(r'\s+changed (\d+) pixels in \((\d+),(\d+)\)-\((\d+),(\d+)\)', l)
    if m: cur['chg'] = '%s px (%s,%s)-(%s,%s)' % m.groups(); continue
    if 'changed nothing' in l: cur['chg'] = '0'; continue
    m = re.match(r'\s+texture (\d): (\w+) format (\w+) (\d+)x(\d+)x(\d+) levels (\d+)(.*?) -> (.*)', l)
    if m: cur['tex'].append('%s:%s/%s/%sx%s%s' % (m.group(1), m.group(2)[-6:], m.group(3), m.group(4), m.group(5), '*' if 'game drew' in m.group(9) else '' if 'unpacked' in m.group(9) else '!')); continue
for o in out:
    if 'clear' in o: print(o['clear'][:150]); continue
    print('%3d %s %-7s p%s v%-4s vs %s ps %s/%s/%s  z %s cc %s rng %s %s blend %s at %s mask %s st %s | %s | %s' % (o['n'], o['tgt'], o['size'], o['prim'], o['verts'], o['vp'], o['ps'], o['st'], o['modes'], o.get('z','?'), o.get('cc','?'), o.get('rng','?'), o.get('off',''), o.get('blend','?'), o.get('at','?'), o.get('mask','?'), o.get('sten','?'), ' '.join(o['tex']), o['chg']))

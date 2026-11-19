#!/usr/bin/env python3
"""H2Classic icon concepts: two families (A: hexagonal badge, B: cut-corner panel), three icons each.
Original geometry only: the H2 monogram, the cog and the upscale mark are drawn here as polygons."""
import math, subprocess, os, sys

H = "M0,0 L18,0 L18,41 L52,41 L52,0 L70,0 L70,100 L52,100 L52,59 L18,59 L18,100 L0,100 Z"
TWO = "M84,0 L128,0 L140,12 L140,50 L106,82 L140,82 L140,100 L84,100 L84,84 L122,48 L122,18 L84,18 Z"
C = "M166,0 L204,0 L204,18 L176,18 L172,22 L172,78 L176,82 L204,82 L204,100 L166,100 L154,88 L154,12 Z"

def cog(cx, cy, r_out, r_in, teeth, hole):
    pts = []
    for i in range(teeth):
        a0 = 2 * math.pi * i / teeth
        step = 2 * math.pi / teeth
        for frac, r in ((0.00, r_in), (0.12, r_out), (0.38, r_out), (0.50, r_in)):
            a = a0 + frac * step
            pts.append((cx + r * math.cos(a), cy + r * math.sin(a)))
        a = a0 + 0.75 * step
        pts.append((cx + r_in * math.cos(a), cy + r_in * math.sin(a)))
    d = "M" + " L".join("%.1f,%.1f" % p for p in pts) + " Z"
    # the hole: an octagon, drawn the other way round
    hp = [(cx + hole * math.cos(-2 * math.pi * i / 8 + math.pi / 8), cy + hole * math.sin(-2 * math.pi * i / 8 + math.pi / 8)) for i in range(8)]
    d += " M" + " L".join("%.1f,%.1f" % p for p in hp) + " Z"
    return d

def upscale_mark(x, y, s):
    """a stair-stepped (aliased) edge, a chevron, and a smooth diamond: in a box of size s at (x, y)"""
    q = s / 10.0
    stairs = []
    for i in range(4):
        stairs.append('<rect x="%.1f" y="%.1f" width="%.1f" height="%.1f"/>' % (x + i * q, y + s * 0.62 - i * q, q * 0.9, q * 0.9))
    chev = "M%.1f,%.1f L%.1f,%.1f L%.1f,%.1f L%.1f,%.1f L%.1f,%.1f L%.1f,%.1f Z" % (
        x + 4.4 * q, y + 3.0 * q, x + 5.4 * q, y + 3.0 * q, x + 6.4 * q, y + 4.5 * q, x + 5.4 * q, y + 6.0 * q, x + 4.4 * q, y + 6.0 * q, x + 5.4 * q, y + 4.5 * q)
    cx, cy, r = x + 8.3 * q, y + 4.5 * q, 1.7 * q
    diamond = "M%.1f,%.1f L%.1f,%.1f L%.1f,%.1f L%.1f,%.1f Z" % (cx, cy - r, cx + r, cy, cx, cy + r, cx - r, cy)
    return "".join(stairs), chev, diamond

DEFS = '''<defs>
 <linearGradient id="metal" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#2b3a4f"/><stop offset="0.55" stop-color="#121b28"/><stop offset="1" stop-color="#070b12"/></linearGradient>
 <linearGradient id="rim" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="#c9d6e3"/><stop offset="0.5" stop-color="#6f8296"/><stop offset="1" stop-color="#2c3a49"/></linearGradient>
 <linearGradient id="glyph" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#e9f8ff"/><stop offset="1" stop-color="#7fd4ff"/></linearGradient>
 <linearGradient id="navy" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="#14304f"/><stop offset="1" stop-color="#06111f"/></linearGradient>
 <filter id="glow" x="-30%" y="-30%" width="160%" height="160%"><feGaussianBlur stdDeviation="6"/></filter>
</defs>'''

HEX = "M128,8 L236,68 L236,188 L128,248 L20,188 L20,68 Z"
HEX_IN = "M128,22 L224,75 L224,181 L128,234 L32,181 L32,75 Z"
PANEL = "M58,14 L234,14 L242,22 L242,190 L190,242 L22,242 L14,234 L14,58 Z"
PANEL_IN = "M64,26 L230,26 L230,186 L186,230 L26,230 L26,64 Z"

def content(kind, family):
    """the symbol in the middle, glowing"""
    if kind == "game":
        # H2C, solid, square-cut, 13 units thick (the user: thinner letters; no serifs on H and C; the 2's neck a curve running
        # into its flat base, its top end cut straight; even gaps) - each letter in a box 0..100 high, placed by GAME_X
        W = 13
        H_T = "M6,0 L19,0 L19,43.5 L59,43.5 L59,0 L72,0 L72,100 L59,100 L59,56.5 L19,56.5 L19,100 L6,100 Z"
        # the 2 as one filled outline (the user: no step where the neck meets the base - its left side runs straight up from the
        # base's left end into the neck's curve)
        TWO_T = ("M1.5,30 C1.5,11 15,0 33,0 C51,0 64,11 64,30 C64,48 51,59 38.5,68.5 C28,76.5 21,82 19.5,87 L51,87 L51,74 L64,74 "
                 "L64,100 L3,100 L3,88 C3,80 14,72 27.5,63.5 C40,55 51,47 51,30 C51,18 43,13 33,13 C22,13 14.5,19 14.5,30 Z")
        C_T = "M57,20 C51,11 43,6.5 34,6.5 C18,6.5 7.5,24 7.5,50 C7.5,76 18,93.5 34,93.5 C43,93.5 51,89 57,80"
        def letters(color):
            return ('<path d="%s" fill="%s"/>' % (H_T, color) +
                    '<g transform="translate(%.2f,0)"><path d="%s" fill="%s"/></g>' % (GAME_X[0], TWO_T, color) +
                    '<g transform="translate(%.2f,0)"><path d="%s" fill="none" stroke="%s" stroke-width="%d" stroke-linecap="butt"/></g>' % (GAME_X[1], C_T, color, W))
        g = '<g transform="translate(%.2f,90) scale(0.82)">%s</g>'
        return g % (GAME_X[2], '<g filter="url(#glow)" opacity="0.9">%s</g>%s' % (letters('#2fb0ff'), letters('#cdeeff')))
    if kind == "settings":
        d = cog(128, 126, 70, 54, 8, 24)
        return ('<path d="%s" fill="#2fb0ff" fill-rule="evenodd" filter="url(#glow)" opacity="0.9"/>' % d +
                '<path d="%s" fill="url(#glyph)" fill-rule="evenodd"/>' % d)
    if kind == "dlss":
        stairs, chev, diamond = upscale_mark(40, 82, 176)
        body = '<g>%s</g><path d="%s"/><path d="%s"/>' % (stairs, chev, diamond)
        return ('<g fill="#2fb0ff" filter="url(#glow)" opacity="0.9">%s</g><g fill="url(#glyph)">%s</g>' % (body, body))

GAME_X = (84.40, 161.30, 33.86)   # the 2's and the C's offsets, the group's x (set by space_letters below)

def svg(kind, family):
    if family == "A":
        frame = ('<path d="%s" fill="url(#rim)"/><path d="%s" fill="url(#metal)"/>' % (HEX, HEX_IN) +
                 '<path d="%s" fill="none" stroke="#3fb8ff" stroke-width="2.5" opacity="0.85"/>' % HEX_IN)
    else:
        frame = ('<path d="%s" fill="url(#navy)" stroke="#7cc4ff" stroke-width="5"/>' % PANEL +
                 '<path d="%s" fill="none" stroke="#7cc4ff" stroke-width="2" opacity="0.55"/>' % PANEL_IN +
                 ''.join('<rect x="40" y="%d" width="176" height="1.6" fill="#7cc4ff" opacity="0.10"/>' % y for y in range(40, 230, 8)))
    return '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 256 256" width="256" height="256">%s%s%s</svg>' % (DEFS, frame, content(kind, family))

if __name__ == "__main__":
    out = sys.argv[1] if len(sys.argv) > 1 else "out"
    os.makedirs(out, exist_ok=True)
    for family in "AB":
        for kind in ("game", "settings", "dlss"):
            path = os.path.join(out, "%s_%s.svg" % (family, kind))
            open(path, "w").write(svg(kind, family))
            for size in (256, 48, 32, 16):
                subprocess.run(["rsvg-convert", "-w", str(size), "-h", str(size), "-o", os.path.join(out, "%s_%s_%d.png" % (family, kind, size)), path], check=True)
    print("ok")

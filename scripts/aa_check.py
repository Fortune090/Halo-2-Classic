#!/usr/bin/env python3
"""h2native: check the hand-written SMAA shaders (layer2/shaders/smaa_*.asm) against SMAA.hlsl itself, ported to Python here
line by line from the HLSL (not from the assembly), on a picture the game drew.

Run the game with H2N_AA=smaa H2N_AADUMP=<n>: the n-th view's input, edges, blending weights and output are written as
aa_input.raw, aa_edges.raw, aa_blend.raw, aa_output.raw. Then:  aa_check.py <folder with the .raw files> [high|ultra]
Texture reads are done as Direct3D 9 does them: point reads at texel centres, bilinear with clamped edges elsewhere;
the area texture is AreaTex.h's bytes read as D3DFMT_A8L8 (.ra), the search texture SearchTex.h's as D3DFMT_L8."""
import math, re, struct, sys
import numpy as np

SMAA_DIR = '/home/claude/native/layer2/smaa'

def load_raw(path):
    data = open(path, 'rb').read()
    w, h, fmt = struct.unpack_from('<III', data, 0)
    a = np.frombuffer(data, np.uint8, offset=12).reshape(h, w, 4)
    rgba = a[..., [2, 1, 0, 3]].astype(np.float64) / 255.0       # A8R8G8B8 in memory: B G R A
    return rgba

def load_header_bytes(path, name):
    text = open(path).read()
    body = text[text.index(name):]
    body = body[body.index('{') + 1:body.index('}')]
    return np.array([int(x, 0) for x in re.findall(r'0x[0-9a-fA-F]+|\d+', body)], np.uint8)

PRESETS = {'high': dict(threshold=0.1, steps=16, diag=8, corner=25), 'ultra': dict(threshold=0.05, steps=32, diag=16, corner=25)}

class Tex:
    """a texture with Direct3D 9 sampling: coordinates in 0..1, texel centres at (i + 0.5) / size, clamped"""
    def __init__(self, a):
        self.a = a; self.h, self.w = a.shape[:2]
    def point(self, u, v):
        x = min(max(int(math.floor(u * self.w)), 0), self.w - 1); y = min(max(int(math.floor(v * self.h)), 0), self.h - 1)
        return self.a[y, x]
    def linear(self, u, v):
        # the position in texels with 8 bits below the texel, as graphics hardware takes it (a read at a texel's centre is that texel)
        x = round((u * self.w - 0.5) * 256.0) / 256.0; y = round((v * self.h - 0.5) * 256.0) / 256.0
        x0 = math.floor(x); y0 = math.floor(y); fx = x - x0; fy = y - y0
        def t(xx, yy): return self.a[min(max(yy, 0), self.h - 1), min(max(xx, 0), self.w - 1)]
        return (t(x0, y0) * (1 - fx) * (1 - fy) + t(x0 + 1, y0) * fx * (1 - fy) + t(x0, y0 + 1) * (1 - fx) * fy + t(x0 + 1, y0 + 1) * fx * fy)

def rnd(x): return math.floor(x + 0.5)

def edges_ref(color, p):
    """SMAALumaEdgeDetectionPS for every pixel (texel-centre reads: point = linear), as float4(edges, 0, 0); discarded -> 0"""
    h, w = color.shape[:2]
    L = color[..., 0] * 0.2126 + color[..., 1] * 0.7152 + color[..., 2] * 0.0722
    def sh(a, dx, dy):     # a read at (x + dx, y + dy), clamped
        ys = np.clip(np.arange(h) + dy, 0, h - 1); xs = np.clip(np.arange(w) + dx, 0, w - 1)
        return a[ys][:, xs]
    Lleft, Ltop = sh(L, -1, 0), sh(L, 0, -1)
    dx_, dy_ = np.abs(L - Lleft), np.abs(L - Ltop)
    ex = (dx_ >= p['threshold']).astype(float); ey = (dy_ >= p['threshold']).astype(float)
    keep = (ex + ey) != 0
    Lright, Lbottom = sh(L, 1, 0), sh(L, 0, 1)
    mx = np.maximum(dx_, np.abs(L - Lright)); my = np.maximum(dy_, np.abs(L - Lbottom))
    Lll, Ltt = sh(L, -2, 0), sh(L, 0, -2)
    mx = np.maximum(mx, np.abs(Lleft - Lll)); my = np.maximum(my, np.abs(Ltop - Ltt))
    final = np.maximum(mx, my)
    ex *= (2.0 * dx_ >= final); ey *= (2.0 * dy_ >= final)
    out = np.zeros((h, w, 4)); out[..., 0] = ex * keep; out[..., 1] = ey * keep
    return out

def blend_ref(edges, area, search, p, only=None):
    """SMAABlendingWeightCalculationPS (subsampleIndices = 0) for every pixel with an edge"""
    h, w = edges.shape[:2]
    rt = (1.0 / w, 1.0 / h, float(w), float(h))
    E, A, S = Tex(edges), Tex(area), Tex(search)
    steps, diag = p['steps'], p['diag']
    corner_norm = p['corner'] / 100.0
    AMAX, AMAXD = 16.0, 20.0
    APX = (1.0 / 160.0, 1.0 / 560.0)
    def S0(u, v): return E.linear(u, v)          # SMAASampleLevelZero(edgesTex, ...)
    def area_sel(s): return np.array([s[0], s[3]])  # .ra of A8L8: L in r (and g, b), A in a
    def decode2(e): e = e.copy(); e[0] = e[0] * abs(5.0 * e[0] - 5.0 * 0.75); return np.floor(e + 0.5)
    def search_diag1(tc, d):
        coord = [tc[0], tc[1], -1.0, 1.0]; e = np.zeros(2)
        while coord[2] < float(diag - 1) and coord[3] > 0.9:
            coord[0] += rt[0] * d[0]; coord[1] += rt[1] * d[1]; coord[2] += 1.0
            e = S0(coord[0], coord[1])[:2]
            coord[3] = e[0] * 0.5 + e[1] * 0.5
        return np.array(coord[2:]), e
    def search_diag2(tc, d):
        coord = [tc[0] + 0.25 * rt[0], tc[1], -1.0, 1.0]; e = np.zeros(2)
        while coord[2] < float(diag - 1) and coord[3] > 0.9:
            coord[0] += rt[0] * d[0]; coord[1] += rt[1] * d[1]; coord[2] += 1.0
            e = decode2(S0(coord[0], coord[1])[:2])
            coord[3] = e[0] * 0.5 + e[1] * 0.5
        return np.array(coord[2:]), e
    def area_diag(dist, e):
        tc = np.array([AMAXD * e[0] + dist[0], AMAXD * e[1] + dist[1]])
        tc = APX * tc + 0.5 * np.array(APX); tc[0] += 0.5
        return area_sel(A.linear(tc[0], tc[1]))
    def diag_weights(tc, e):
        weights = np.zeros(2); d = np.zeros(4)
        if e[0] > 0.0:
            r, end = search_diag1(tc, (-1.0, 1.0)); d[0], d[2] = r; d[0] += float(end[1] > 0.9)
        r, end = search_diag1(tc, (1.0, -1.0)); d[1], d[3] = r
        if d[0] + d[1] > 2.0:
            coords = np.array([-d[0] + 0.25, d[0], d[1], -d[1] - 0.25]) * np.array([rt[0], rt[1], rt[0], rt[1]]) + np.array([tc[0], tc[1], tc[0], tc[1]])
            c = np.zeros(4)
            c[0:2] = S0(coords[0] - rt[0], coords[1])[:2]
            c[2:4] = S0(coords[2] + rt[0], coords[3])[:2]
            dec = c.copy(); dec[0] = dec[0] * abs(5.0 * dec[0] - 3.75); dec[2] = dec[2] * abs(5.0 * dec[2] - 3.75); dec = np.floor(dec + 0.5)
            c = np.array([dec[1], dec[0], dec[3], dec[2]])          # c.yxwz = decode(c.xyzw)
            cc = np.array([2.0 * c[0] + c[1], 2.0 * c[2] + c[3]])
            if d[2] >= 0.9: cc[0] = 0.0
            if d[3] >= 0.9: cc[1] = 0.0
            weights += area_diag(d[:2], cc)
        r, end = search_diag2(tc, (-1.0, -1.0)); d[0], d[2] = r
        if S0(tc[0] + rt[0], tc[1])[0] > 0.0:
            r, end = search_diag2(tc, (1.0, 1.0)); d[1], d[3] = r; d[1] += float(end[1] > 0.9)
        else:
            d[1] = d[3] = 0.0
        if d[0] + d[1] > 2.0:
            coords = np.array([-d[0], -d[0], d[1], d[1]]) * np.array([rt[0], rt[1], rt[0], rt[1]]) + np.array([tc[0], tc[1], tc[0], tc[1]])
            c = np.zeros(4)
            c[0] = S0(coords[0] - rt[0], coords[1])[1]
            c[1] = S0(coords[0], coords[1] - rt[1])[0]
            s_ = S0(coords[2] + rt[0], coords[3]); c[2] = s_[1]; c[3] = s_[0]
            cc = np.array([2.0 * c[0] + c[1], 2.0 * c[2] + c[3]])
            if d[2] >= 0.9: cc[0] = 0.0
            if d[3] >= 0.9: cc[1] = 0.0
            weights += area_diag(d[:2], cc)[::-1]
        return weights
    def search_length(e, offset):
        scale = np.array([66.0, 33.0]) * np.array([0.5, -1.0]); bias = np.array([66.0, 33.0]) * np.array([offset, 1.0])
        scale += np.array([-1.0, 1.0]); bias += np.array([0.5, -0.5])
        scale /= np.array([64.0, 16.0]); bias /= np.array([64.0, 16.0])
        c = scale * e + bias
        return S.point(c[0], c[1])[0]
    def x_left(tc, end):
        tc = list(tc); e = np.array([0.0, 1.0])
        while tc[0] > end and e[1] > 0.8281 and e[0] == 0.0:
            e = S0(tc[0], tc[1])[:2]; tc[0] -= 2.0 * rt[0]
        off = -(255.0 / 127.0) * search_length(e, 0.0) + 3.25
        return rt[0] * off + tc[0]
    def x_right(tc, end):
        tc = list(tc); e = np.array([0.0, 1.0])
        while tc[0] < end and e[1] > 0.8281 and e[0] == 0.0:
            e = S0(tc[0], tc[1])[:2]; tc[0] += 2.0 * rt[0]
        off = -(255.0 / 127.0) * search_length(e, 0.5) + 3.25
        return -rt[0] * off + tc[0]
    def y_up(tc, end):
        tc = list(tc); e = np.array([1.0, 0.0])
        while tc[1] > end and e[0] > 0.8281 and e[1] == 0.0:
            e = S0(tc[0], tc[1])[:2]; tc[1] -= 2.0 * rt[1]
        off = -(255.0 / 127.0) * search_length(e[::-1], 0.0) + 3.25
        return rt[1] * off + tc[1]
    def y_down(tc, end):
        tc = list(tc); e = np.array([1.0, 0.0])
        while tc[1] < end and e[0] > 0.8281 and e[1] == 0.0:
            e = S0(tc[0], tc[1])[:2]; tc[1] += 2.0 * rt[1]
        off = -(255.0 / 127.0) * search_length(e[::-1], 0.5) + 3.25
        return -rt[1] * off + tc[1]
    def area_fn(dist, e1, e2):
        tc = AMAX * np.floor(4.0 * np.array([e1, e2]) + 0.5) + dist
        tc = APX * tc + 0.5 * np.array(APX)
        return area_sel(A.linear(tc[0], tc[1]))
    out = np.zeros((h, w, 4))
    pixels = only if only is not None else [(x, y) for y in range(h) for x in range(w) if edges[y, x, 0] > 0 or edges[y, x, 1] > 0]
    for (x, y) in pixels:
        tc = ((x + 0.5) / w, (y + 0.5) / h); pix = (tc[0] * w, tc[1] * h)
        off0 = (tc[0] - 0.25 * rt[0], tc[1] - 0.125 * rt[1], tc[0] + 1.25 * rt[0], tc[1] - 0.125 * rt[1])
        off1 = (tc[0] - 0.125 * rt[0], tc[1] - 0.25 * rt[1], tc[0] - 0.125 * rt[0], tc[1] + 1.25 * rt[1])
        off2 = (off0[0] - 2.0 * steps * rt[0], off0[2] + 2.0 * steps * rt[0], off1[1] - 2.0 * steps * rt[1], off1[3] + 2.0 * steps * rt[1])
        weights = np.zeros(4)
        e = E.linear(tc[0], tc[1])[:2].copy()
        if e[1] > 0.0:
            weights[0:2] = diag_weights(tc, e)
            if weights[0] == -weights[1]:
                cx = x_left(off0[0:2], off2[0]); cy = off1[1]
                dd = np.array([cx, 0.0]); e1 = S0(cx, cy)[0]
                cz = x_right(off0[2:4], off2[1]); dd[1] = cz
                dd = np.abs(np.floor(rt[2] * dd - pix[0] + 0.5))
                e2 = S0(cz + rt[0], cy)[0]
                weights[0:2] = area_fn(np.sqrt(dd), e1, e2)
                cy = tc[1]
                lr = np.array([float(dd[1] >= dd[0]), float(dd[0] >= dd[1])]); rounding = (1.0 - corner_norm) * lr / (lr[0] + lr[1])
                f = np.array([1.0, 1.0])
                f[0] -= rounding[0] * S0(cx, cy + rt[1])[0]; f[0] -= rounding[1] * S0(cz + rt[0], cy + rt[1])[0]
                f[1] -= rounding[0] * S0(cx, cy - 2 * rt[1])[0]; f[1] -= rounding[1] * S0(cz + rt[0], cy - 2 * rt[1])[0]
                weights[0:2] *= np.clip(f, 0, 1)
            else:
                e[0] = 0.0
        if e[0] > 0.0:
            cy = y_up(off1[0:2], off2[2]); cx = off0[0]
            dd = np.array([cy, 0.0]); e1 = S0(cx, cy)[1]
            cz = y_down(off1[2:4], off2[3]); dd[1] = cz
            dd = np.abs(np.floor(rt[3] * dd - pix[1] + 0.5))
            e2 = S0(cx, cz + rt[1])[1]
            weights[2:4] = area_fn(np.sqrt(dd), e1, e2)
            cx = tc[0]
            lr = np.array([float(dd[1] >= dd[0]), float(dd[0] >= dd[1])]); rounding = (1.0 - corner_norm) * lr / (lr[0] + lr[1])
            f = np.array([1.0, 1.0])
            f[0] -= rounding[0] * S0(cx + rt[0], cy)[1]; f[0] -= rounding[1] * S0(cx + rt[0], cz + rt[1])[1]
            f[1] -= rounding[0] * S0(cx - 2 * rt[0], cy)[1]; f[1] -= rounding[1] * S0(cx - 2 * rt[0], cz + rt[1])[1]
            weights[2:4] *= np.clip(f, 0, 1)
        out[y, x] = weights
    return out, pixels

def srgb_to_linear(c): return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)
def linear_to_srgb(c): return np.where(c <= 0.0031308, c * 12.92, 1.055 * np.power(np.maximum(c, 0), 1 / 2.4) - 0.055)

def neighbor_ref(color, blend, srgb=True):
    h, w = color.shape[:2]
    lin = srgb_to_linear(color) if srgb else color
    C, B = Tex(lin), Tex(blend)
    out = np.zeros_like(color)
    rt = (1.0 / w, 1.0 / h)
    for y in range(h):
        for x in range(w):
            tc = ((x + 0.5) / w, (y + 0.5) / h)
            a = np.array([B.linear(tc[0] + rt[0], tc[1])[3], B.linear(tc[0], tc[1] + rt[1])[1], 0.0, 0.0])
            b = B.linear(tc[0], tc[1]); a[3] = b[0]; a[2] = b[2]
            if a.sum() < 1e-5:
                out[y, x] = C.linear(*tc)
            else:
                hz = max(a[0], a[2]) > max(a[1], a[3])
                off = np.array([a[0], 0.0, a[2], 0.0]) if hz else np.array([0.0, a[1], 0.0, a[3]])
                wgt = np.array([a[0], a[2]]) if hz else np.array([a[1], a[3]])
                wgt = wgt / wgt.sum()
                bc = off * np.array([rt[0], rt[1], -rt[0], -rt[1]]) + np.array([tc[0], tc[1], tc[0], tc[1]])
                out[y, x] = wgt[0] * C.linear(bc[0], bc[1]) + wgt[1] * C.linear(bc[2], bc[3])
    return linear_to_srgb(out) if srgb else out

def main():
    folder = sys.argv[1]; p = PRESETS[sys.argv[2] if len(sys.argv) > 2 else 'high']
    color = load_raw(folder + '/aa_input.raw'); edges_gpu = load_raw(folder + '/aa_edges.raw')
    blend_gpu = load_raw(folder + '/aa_blend.raw'); out_gpu = load_raw(folder + '/aa_output.raw')
    h, w = color.shape[:2]
    # the area and search textures as the layer uploads them: AreaTex.h bytes (R8G8 pairs -> A8L8: L = R, A = G)
    ab = load_header_bytes(SMAA_DIR + '/AreaTex.h', 'areaTexBytes').reshape(560, 160, 2).astype(np.float64) / 255.0
    area = np.zeros((560, 160, 4)); area[..., 0] = area[..., 1] = area[..., 2] = ab[..., 0]; area[..., 3] = ab[..., 1]
    sb = load_header_bytes(SMAA_DIR + '/SearchTex.h', 'searchTexBytes').reshape(16, 64).astype(np.float64) / 255.0
    search = np.zeros((16, 64, 4)); search[..., 0] = search[..., 1] = search[..., 2] = sb; search[..., 3] = 1.0
    # pass 1
    edges_ref_ = edges_ref(color, p)
    diff = np.abs(edges_ref_[..., :2] - edges_gpu[..., :2]) > 0.5
    print('edges: %d pixels with an edge here, %d on the GPU, %d differ' % ((edges_ref_[..., :2].sum(2) > 0).sum(), (edges_gpu[..., :2].sum(2) > 0).sum(), diff.any(2).sum()))
    # pass 2, from the GPU's own edges (so that only this pass is compared)
    blend_ref_, pixels = blend_ref(edges_gpu, area, search, p)
    bd = np.abs(blend_ref_ - blend_gpu)
    print('weights: %d pixels computed; largest difference %.4f; differ by more than 2/255: %d; nonzero here %d, on the GPU %d' % (
        len(pixels), bd.max(), (bd > 2.0 / 255).any(2).sum(), (blend_ref_.sum(2) > 0).sum(), (blend_gpu.sum(2) > 0).sum()))
    worst = np.argwhere((bd > 2.0 / 255).any(2))[:8]
    for (y, x) in worst: print('   at %d,%d: here %s  GPU %s  edges %s' % (x, y, np.round(blend_ref_[y, x], 3), np.round(blend_gpu[y, x], 3), edges_gpu[y, x, :2]))
    # pass 3, from the GPU's weights
    out_ref = neighbor_ref(color, blend_gpu, True)
    od = np.abs(out_ref[..., :3] - out_gpu[..., :3])
    print('output: largest difference %.4f; differ by more than 2/255: %d of %d; pixels the GPU changed from the input: %d' % (
        od.max(), (od > 2.0 / 255).any(2).sum(), h * w, (np.abs(out_gpu[..., :3] - color[..., :3]) > 1.0 / 255).any(2).sum()))

if __name__ == '__main__':
    main()

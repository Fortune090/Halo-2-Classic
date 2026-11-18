#!/usr/bin/env python3
"""h2native: check the hand-written TAA resolve (layer2/shaders/taa_resolve.ps.asm) against Playdead's
TemporalReprojection.shader, ported to Python here from the Cg (its defaults: MINMAX_3X3_ROUNDED, UNJITTER_COLORSAMPLES,
USE_CLIPPING with USE_OPTIMIZATIONS, USE_DILATION 3x3 nearest, no YCoCg, no motion blur), on a frame the game drew.

Run the game with H2N_AA=taa H2N_AADUMP=<frame>: aa_input.raw (this frame's view), aa_history.raw (the history before),
aa_velocity.raw, aa_depth.raw and aa_resolved.raw (the new history) are written, and the log gives the jitter.
Usage: taa_check.py <folder> <jitter x> <jitter y>   (pixels, from the log line "jitter ...")
The noise the shader adds (PDsrand4 / 510, at most 1/510) is not reproduced: a difference up to that much is expected."""
import struct, sys
import numpy as np

def load(path):
    d = open(path, 'rb').read()
    w, h, fmt = struct.unpack_from('<III', d, 0)
    if fmt == 21: return np.frombuffer(d, np.uint8, offset=12).reshape(h, w, 4)[..., [2, 1, 0, 3]].astype(np.float64) / 255.0
    if fmt == 112: return np.frombuffer(d, np.float16, offset=12).reshape(h, w, 2).astype(np.float64)    # G16R16F: r, g
    if fmt == 114: return np.frombuffer(d, np.float32, offset=12).reshape(h, w).astype(np.float64)        # R32F
    raise SystemExit('format %d?' % fmt)

def bilinear(img, u, v):
    """Direct3D 9 linear filtering, clamped, 8 bits below the texel; u, v arrays"""
    h, w = img.shape[:2]
    x = np.round((u * w - 0.5) * 256) / 256; y = np.round((v * h - 0.5) * 256) / 256
    x0 = np.floor(x).astype(int); y0 = np.floor(y).astype(int); fx = (x - x0)[..., None]; fy = (y - y0)[..., None]
    def t(xx, yy): return img[np.clip(yy, 0, h - 1), np.clip(xx, 0, w - 1)]
    return t(x0, y0) * (1 - fx) * (1 - fy) + t(x0 + 1, y0) * fx * (1 - fy) + t(x0, y0 + 1) * (1 - fx) * fy + t(x0 + 1, y0 + 1) * fx * fy

def point(img, u, v):
    h, w = img.shape[:2]
    return img[np.clip(np.floor(v * h).astype(int), 0, h - 1), np.clip(np.floor(u * w).astype(int), 0, w - 1)]

def main():
    folder = sys.argv[1]; jx, jy = float(sys.argv[2]), float(sys.argv[3])
    cur = load(folder + '/aa_input.raw'); prev = load(folder + '/aa_history.raw'); vel = load(folder + '/aa_velocity.raw')
    depth = load(folder + '/aa_depth.raw'); gpu = load(folder + '/aa_resolved.raw')
    h, w = cur.shape[:2]
    du, dv = 1.0 / w, 1.0 / h
    jitter_uv = np.array([jx / w, jy / h])
    yy, xx = np.mgrid[0:h, 0:w]
    u = (xx + 0.5) / w; v = (yy + 0.5) / h
    # USE_DILATION: find_closest_fragment_3x3 (ZCMP_GT(a, b) = a > b: the nearest), in the order the shader checks them
    best = np.stack([np.full((h, w), -1.0), np.full((h, w), -1.0), point(depth, u - du, v - dv)], -1)
    for ox, oy in [(0, -1), (1, -1), (-1, 0), (0, 0), (1, 0), (-1, 1), (0, 1), (1, 1)]:
        d = point(depth, u + ox * du, v + oy * dv)
        take = best[..., 2] > d
        best[take] = np.stack([np.full(take.sum(), float(ox)), np.full(take.sum(), float(oy)), d[take]], -1)
    ss_vel = point(vel, u + best[..., 0] * du, v + best[..., 1] * dv)
    # temporal_reprojection(ss_txc, ss_vel)
    texel0 = bilinear(cur, u - jitter_uv[0], v - jitter_uv[1])
    texel1 = bilinear(prev, u - ss_vel[..., 0], v - ss_vel[..., 1])
    s = {}
    for name, ox, oy in [('tl', -1, -1), ('tc', 0, -1), ('tr', 1, -1), ('ml', -1, 0), ('mc', 0, 0), ('mr', 1, 0), ('bl', -1, 1), ('bc', 0, 1), ('br', 1, 1)]:
        s[name] = bilinear(cur, u + ox * du, v + oy * dv)
    nine = [s[k] for k in ['tl', 'tc', 'tr', 'ml', 'mc', 'mr', 'bl', 'bc', 'br']]
    five = [s[k] for k in ['tc', 'ml', 'mc', 'mr', 'bc']]
    cmin = np.minimum.reduce(nine); cmax = np.maximum.reduce(nine); cavg = sum(nine) / 9.0
    cmin5 = np.minimum.reduce(five); cmax5 = np.maximum.reduce(five); cavg5 = sum(five) / 5.0
    cmin = 0.5 * (cmin + cmin5); cmax = 0.5 * (cmax + cmax5); cavg = 0.5 * (cavg + cavg5)
    # clip_aabb(cmin.xyz, cmax.xyz, clamp(cavg, cmin, cmax), texel1), USE_OPTIMIZATIONS
    p = np.minimum(np.maximum(cavg, cmin), cmax)
    p_clip = 0.5 * (cmax[..., :3] + cmin[..., :3]); e_clip = 0.5 * (cmax[..., :3] - cmin[..., :3]) + 1e-8
    v_clip = texel1 - np.concatenate([p_clip, p[..., 3:4]], -1)
    ma = np.abs(v_clip[..., :3] / e_clip).max(-1)
    clipped = np.concatenate([p_clip, p[..., 3:4]], -1) + v_clip / ma[..., None]
    texel1 = np.where((ma > 1.0)[..., None], clipped, texel1)
    lum = np.array([0.22, 0.707, 0.071])
    lum0 = texel0[..., :3] @ lum; lum1 = texel1[..., :3] @ lum
    diff = np.abs(lum0 - lum1) / np.maximum(lum0, np.maximum(lum1, 0.2))
    weight = (1.0 - diff) ** 2
    k = 0.88 + (0.97 - 0.88) * weight
    out = texel0 + (texel1 - texel0) * k[..., None]
    d = np.abs(np.clip(out[..., :3], 0, 1) - gpu[..., :3])
    print('resolve: largest difference %.4f (%.1f/255); pixels beyond 2/255: %d of %d; mean %.5f' % (d.max(), d.max() * 255, (d > 2 / 255).any(-1).sum(), h * w, d.mean()))
    print('this frame against the result: mean difference %.4f; the history against the result: %.4f' % (
        np.abs(cur[..., :3] - gpu[..., :3]).mean(), np.abs(prev[..., :3] - gpu[..., :3]).mean()))
    print('motion: largest |velocity| %.5f (uv), pixels with motion %d' % (np.abs(vel).max(), (np.abs(vel).sum(-1) > 0).sum()))
    worst = np.argwhere((d > 2 / 255).any(-1))[:6]
    for (y, x) in worst: print('   at %d,%d: here %s GPU %s' % (x, y, np.round(out[y, x, :3] * 255), np.round(gpu[y, x, :3] * 255)))

if __name__ == '__main__':
    main()

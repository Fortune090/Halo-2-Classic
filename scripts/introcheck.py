"""introcheck.py <recorded wav> <movie audio wav>: where the movie's sound sits in the recording, and what else is heard
during the movie (the residual after the movie's own sound is taken out) and after it."""
import sys, wave
import numpy as np
def load(p):
    w = wave.open(p); n = w.getnframes(); ch = w.getnchannels(); sr = w.getframerate()
    a = np.frombuffer(w.readframes(n), dtype='<i2').astype(np.float32).reshape(-1, ch)
    return a.mean(axis=1), sr
rec, sr = load(sys.argv[1]); mov, sr2 = load(sys.argv[2])
assert sr == sr2
print('recording %.1f s, movie sound %.1f s' % (len(rec) / sr, len(mov) / sr))
def env(x, step=0.1):
    k = int(sr * step); m = len(x) // k
    return np.sqrt((x[:m * k].reshape(m, k) ** 2).mean(axis=1))
er = env(rec); em = env(mov)
a = np.log1p(er); b = np.log1p(em)
best = max((np.corrcoef(a[l:l + len(b)], b)[0, 1], l) for l in range(0, len(a) - len(b)))
lag = best[1]
print('movie sound starts in the recording at about %.1f s (envelope r=%.3f)' % (lag * 0.1, best[0]))
# fine alignment by cross-correlation of the signal around that point (+-0.2 s)
k0 = lag * int(sr * 0.1); seg = mov[sr * 2: sr * 6]
bestf = None
for d in range(-int(0.2 * sr), int(0.2 * sr), 8):
    s = k0 + sr * 2 + d
    if s < 0: continue
    r = rec[s: s + len(seg)]
    if len(r) < len(seg): break
    c = float(np.dot(r, seg) / (np.linalg.norm(r) * np.linalg.norm(seg) + 1e-9))
    if bestf is None or c > bestf[0]: bestf = (c, s - sr * 2)
start = bestf[1]
print('fine alignment: %.3f s, correlation %.3f' % (start / sr, bestf[0]))
r = rec[start: start + len(mov)]; m = mov[:len(r)]
g = float(np.dot(r, m) / (np.dot(m, m) + 1e-9))
res = r - g * m
print('gain %.3f' % g)
print('per second during the movie: recording level, movie level, what is left (dB below the recording)')
for s in range(0, len(r) // sr):
    a1 = np.sqrt(np.mean(r[s * sr:(s + 1) * sr] ** 2)) + 1e-9
    a2 = np.sqrt(np.mean(res[s * sr:(s + 1) * sr] ** 2)) + 1e-9
    print('  %5.1f s  rec %7.1f  left %7.1f  (%5.1f dB)' % ((start / sr) + s, a1, a2, 20 * np.log10(a2 / a1)))
after = rec[start + len(mov):]
print('after the movie, level per second:', ' '.join('%.0f' % np.sqrt(np.mean(after[s * sr:(s + 1) * sr] ** 2)) for s in range(0, min(30, len(after) // sr))))

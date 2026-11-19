"""introcheck2.py <recorded wav> <movie audio wav> [start seconds]: per half second, the movie's own sound is matched
in the recording (best local shift and gain), and what is left over is measured: sound that is not the movie's."""
import sys, wave
import numpy as np
def load(p):
    w = wave.open(p); n = w.getnframes(); ch = w.getnchannels(); sr = w.getframerate()
    a = np.frombuffer(w.readframes(n), dtype='<i2').astype(np.float64).reshape(-1, ch)
    return a.mean(axis=1), sr
rec, sr = load(sys.argv[1]); mov, _ = load(sys.argv[2])
start = int(float(sys.argv[3]) * sr) if len(sys.argv) > 3 else 0
win = sr // 2; search = int(0.06 * sr)
shift = 0
rows = []
for k in range(0, len(mov) // win - 1):
    m = mov[k * win:(k + 1) * win]
    best = None
    for d in range(shift - search, shift + search + 1, 4):
        s = start + k * win + d
        if s < 0 or s + win > len(rec): continue
        r = rec[s:s + win]
        g = np.dot(r, m) / (np.dot(m, m) + 1e-9)
        left = np.sqrt(np.mean((r - g * m) ** 2))
        if best is None or left < best[0]: best = (left, d, g, np.sqrt(np.mean(r ** 2)))
    if best is None: break
    shift = best[1]
    rows.append((k * 0.5, best[3], best[0], best[1] / sr * 1000, best[2]))
for t, lv, left, d, g in rows:
    print('  %5.1f s  recording %7.1f  not the movie %7.1f (%6.1f dB)  shift %6.1f ms  gain %.2f' % (t, lv, left, 20 * np.log10((left + 1e-9) / (lv + 1e-9)), d, g))

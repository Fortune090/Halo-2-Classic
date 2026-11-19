#!/usr/bin/env python3
"""nav.py [options] <step> <step> ...: one scripted run of the game that waits for what it needs instead of sleeping
for fixed times, and ends as soon as its last step is done (run15nav.sh slept through every screen and then waited for
the game's own time limit: five minutes and more a run).

steps:  <button>        a press (a, b, x, y, start, up, down, left, right, ...); the next step waits until the log has it
        <button>/N      ... and N seconds more (default 1.5: a menu's transition)
        ?text           wait until the log (from here on) has text (default 120 s; ?text/N for N seconds)
        shot            save the frame (waits for the file)
        sleep/N         wait N seconds
        loaded/N        a level's start seen in the picture: dark (loading), then not dark; at most N s (default 120)
        ready           wait for the title screen (the saved games are listed, the menu's music has started)
options: --v 10|15 (default 15)   --dir <run folder>   --name <label for the sheet>   --intro (play the intro movie)
         --keep (keep z:\\preferences.dat: the menus then remember their last places)   --tries N (Direct3D 9 retries)
         NAME=value ... (environment for the game)
prints how the run ended, the presses the log has, and writes /tmp/nav_<name>.png (the shots side by side)."""
import os, sys, time, subprocess, glob

V = '15'; DIR = None; NAME = 'run'; INTRO = False; KEEP = False; TRIES = 3; ENV = {}; steps = []
a = sys.argv[1:]
while a:
    x = a.pop(0)
    if x == '--v': V = a.pop(0)
    elif x == '--dir': DIR = a.pop(0)
    elif x == '--name': NAME = a.pop(0)
    elif x == '--intro': INTRO = True
    elif x == '--keep': KEEP = True
    elif x == '--tries': TRIES = int(a.pop(0))
    elif '=' in x and x.split('=')[0].isupper(): ENV[x.split('=')[0]] = x.split('=', 1)[1]
    else: steps.append(x)
# (s10r) one program for both versions: the version is asked for through H2N_VERSION
EXE = os.environ.get('NAV_EXE') or '/home/claude/native/out3/halo2_native.exe'
PAT = 'halo2_native.ex[e]'
ENV.setdefault('H2N_VERSION', '1.5' if V == '15' else '1.0')
DIR = DIR or ('/home/claude/native/run/work_s10o' if V == '15' else '/home/claude/native/run/work2')
os.chdir(DIR)
if subprocess.run(['pgrep', '-f', 'Xvfb :77'], capture_output=True).returncode:      # (the display the tests draw on)
    subprocess.Popen('setsid nohup Xvfb :77 -screen 0 1920x1200x24 +extension GLX +render -noreset > /dev/null 2>&1 &', shell=True)
    time.sleep(2)
LOG = 'h2classic_log.txt'

def log_text():
    try: return open(LOG, 'r', errors='replace').read()
    except OSError: return ''
def stopped(t):
    for l in t.splitlines():
        if l.startswith('*** stopped') or l.startswith('*** cannot continue') or ('no frame presented for 5 s' in l and not os.environ.get('NAV_SLOW')): return l[:230]   # NAV_SLOW=1: a slow picture (a large render scale drawn in software) is not a stop
    return None
class Ended(Exception): pass
def wait_for(text, pos, seconds, proc):
    end = time.time() + seconds
    while time.time() < end:
        t = log_text(); i = t.find(text, pos)
        if i >= 0: return i + len(text)
        s = stopped(t)
        if s: raise Ended(s)
        if proc.poll() is not None: raise Ended('the game ended (exit %s)' % proc.returncode)
        time.sleep(0.25)
    raise Ended('waited %d s for "%s" in the log' % (seconds, text))

def run_once():
    for f in glob.glob('h2classic_frame_*.bmp') + glob.glob('h2classic_surface_*.bmp') + ['h2classic_stop.bin', 'keys.txt', LOG]:   # (a run starts clean)
        if os.path.exists(f): os.remove(f)
    if not KEEP and os.path.exists('h2classic/Z/preferences.dat'): os.remove('h2classic/Z/preferences.dat')
    open('keys.txt', 'w').close()
    env = dict(os.environ, H2N_NOLAUNCHER='1', WINEDEBUG='-all', WINEPREFIX='/home/claude/.wine32', DISPLAY=':77',
               H2N_KEYFILE='keys.txt', H2N_SECONDS='900')
    if not INTRO: env['H2N_SKIPINTRO'] = '1'
    env.update(ENV)
    t0 = time.time()
    proc = subprocess.Popen(['wine', EXE], env=env, stdout=open('/tmp/nav_out.txt', 'w'), stderr=subprocess.STDOUT)
    pos = 0; result = 'the steps are done'; shots = 0
    try:
        # Direct3D 9 either starts in the first seconds or not at all: no point in running on without a picture
        end = time.time() + 60
        while time.time() < end:
            t = log_text()
            if 'Direct3D 9 did not start' in t: return None, time.time() - t0
            if 'controller: presses are also read' in t or stopped(t): break
            if proc.poll() is not None: break
            time.sleep(0.25)
        for s in steps:
            name, _, n = s.partition('/')
            if name == 'ready':
                pos = wait_for('saved games on u:', pos, 120, proc)
                pos = wait_for('WmaCreateInMemoryDecoder', pos, 60, proc); time.sleep(float(n or 3))
            elif name.startswith('?'): pos = wait_for(name[1:], pos, float(n or 120), proc)
            elif name == 'sleep':
                end = time.time() + float(n or 1)
                while time.time() < end:
                    if stopped(log_text()): raise Ended(stopped(log_text()))
                    time.sleep(0.25)
            elif name == 'loaded':
                # a level's start seen in the picture: shots every 2 s until a dark one (the loading screen), then until
                # one that is not dark (the level has faded in); the dark ones are not kept. At most N seconds.
                from PIL import Image, ImageStat
                end = time.time() + float(n or 120); dark_seen = False; done = False
                while time.time() < end and not done:
                    have = set(glob.glob('h2classic_frame_*.bmp'))
                    open('keys.txt', 'a').write('shot\n'); e2 = time.time() + 15; new = []
                    while time.time() < e2 and not new: time.sleep(0.2); new = sorted(set(glob.glob('h2classic_frame_*.bmp')) - have)
                    if stopped(log_text()): raise Ended(stopped(log_text()))
                    if new:
                        time.sleep(0.3)
                        try: bright = sum(ImageStat.Stat(Image.open(new[0]).convert('RGB')).mean) / 3
                        except Exception: bright = 0
                        if bright < 6: dark_seen = True
                        if dark_seen and bright > 14: done = True
                        else: os.remove(new[0])
                    if not done: time.sleep(2)
                if not done: raise Ended('no level seen starting in %s s (%s)' % (n or 120, 'it stayed dark' if dark_seen else 'the picture never went dark'))
            elif name == 'memory':                              # h2classic_stop.bin written, the run goes on (H2N_DUMP=all: all of it)
                open('keys.txt', 'a').write('memory\n')
                pos = wait_for('h2classic_stop.bin written on request', pos, 60, proc)
            elif name == 'shot':
                have = len(glob.glob('h2classic_frame_*.bmp'))
                open('keys.txt', 'a').write('shot\n'); end = time.time() + 20
                while time.time() < end and len(glob.glob('h2classic_frame_*.bmp')) == have: time.sleep(0.2)
                shots += 1; time.sleep(0.3)
            else:
                open('keys.txt', 'a').write(name + '\n')
                shown = name.split('*')[0].split('+')[0]             # ("forward*40": held for 40 readings; "2:a": port 2)
                if len(shown) > 2 and shown[1] == ':': shown = shown[2:]
                pos = wait_for('"%s" from the key file' % shown, pos, 30, proc); time.sleep(float(n or 1.5))
            s2 = stopped(log_text())
            if s2: raise Ended(s2)
    except Ended as e:
        result = str(e)
    time.sleep(0.5)
    subprocess.run(['pkill', '-f', PAT]); time.sleep(1.0)
    if proc.poll() is None: proc.kill()
    return result, time.time() - t0

for attempt in range(TRIES):
    result, took = run_once()
    if result is not None: break
    subprocess.run(['pkill', '-f', PAT]); time.sleep(2)
    print('Direct3D 9 did not start (try %d): again' % (attempt + 1))
else:
    result = 'Direct3D 9 did not start in %d tries' % TRIES
t = log_text()
print('%s  [%.0f s]' % (result, took))
print('presses: ' + ' '.join(l.split('"')[1] for l in t.splitlines() if 'from the key file at frame' in l))
maps = [l.split('->')[0].split('file:')[1].strip() for l in t.splitlines() if '.map ->' in l and 'opened' in l and ('$c' in l.lower() or 'maps\\' in l)]
print('maps opened: ' + ', '.join(dict.fromkeys(m.split('\\')[-1] for m in maps)))
fs = sorted(glob.glob('h2classic_frame_*.bmp'))
if fs:
    from PIL import Image
    fs = fs[-12:]; cols = min(4, len(fs)); rows = (len(fs) + cols - 1) // cols
    W = Image.new('RGB', (640 * cols, 480 * rows))
    for i, f in enumerate(fs): W.paste(Image.open(f).convert('RGB').resize((640, 480)), ((i % cols) * 640, (i // cols) * 480))
    out = '/tmp/nav_%s.png' % NAME
    if cols * 640 > 1920: W = W.resize((1920, int(480 * rows * 1920 / (640 * cols))))
    W.save(out); print('shots: %d -> %s' % (len(fs), out))

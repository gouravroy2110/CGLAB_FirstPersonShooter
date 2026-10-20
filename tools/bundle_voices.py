# Copies the radio voices the game generated (Windows speech, cached in
# %LOCALAPPDATA%/Raster FPS/voice) into the repo's voices/ folder and rewrites
# voices.qrc, so the lines are compiled into the game and everyone gets them.
#
# Run after adding or changing lines in radiolines.h / missions.h:
#   1. start the game once on Windows (it speaks any new line into the cache)
#   2. python tools/bundle_voices.py
#   3. rebuild (qmake picks up voices.qrc)
#
# The files are trimmed of the engine's leading / trailing silence and stored at
# 6 kHz: the in-game radio filter keeps only 320-3000 Hz, so nothing audible is
# lost and the bundle stays small.
import os, glob, sys
import numpy as np
from scipy.io import wavfile
from scipy.signal import resample_poly

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CACHE = os.path.join(os.environ.get('LOCALAPPDATA', ''), 'Raster FPS', 'voice')
OUT = os.path.join(ROOT, 'voices')
RATE = 6000

if not os.path.isdir(CACHE):
    sys.exit('no voice cache at %s - run the game once first' % CACHE)
os.makedirs(OUT, exist_ok=True)
names = []
for f in sorted(glob.glob(os.path.join(CACHE, '*.wav'))):
    sr, x = wavfile.read(f)
    if x.ndim > 1: x = x[:, 0]
    loud = np.nonzero(np.abs(x.astype(np.int32)) > 400)[0]          # trim silence, keep 60 ms either side
    if len(loud):
        pad = int(0.06 * sr)
        x = x[max(0, loud[0] - pad):min(len(x), loud[-1] + pad)]
    y = resample_poly(x.astype(np.float64), RATE, sr)          # band-limits, then resamples
    y = np.clip(y, -32768, 32767).astype(np.int16)
    name = os.path.basename(f)
    wavfile.write(os.path.join(OUT, name), RATE, y)
    names.append(name)
# drop bundled files that no longer exist in the cache (lines that were removed)
for f in glob.glob(os.path.join(OUT, '*.wav')):
    if os.path.basename(f) not in names: os.remove(f)
with open(os.path.join(ROOT, 'voices.qrc'), 'w', newline='\n') as q:
    q.write('<!DOCTYPE RCC>\n<RCC version="1.0">\n<qresource prefix="/">\n')
    for n in names: q.write('    <file>voices/%s</file>\n' % n)
    q.write('</qresource>\n</RCC>\n')
total = sum(os.path.getsize(os.path.join(OUT, n)) for n in names)
print('%d voice lines bundled, %.1f MB' % (len(names), total / 1e6))

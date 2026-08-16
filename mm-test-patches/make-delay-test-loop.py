"""Generate delay-test-loop.wav for RB-Retours-1 / RB-Retours-2.

44.1 kHz, 16-bit, stereo (dual mono), 8 s, loops cleanly.

Delays need silence around events — an echo that lands on top of fresh
source material can't be heard as an echo. The loop is built in four
sections, each serving a different Retours test:

  0.0-0.15  isolated blip      single-tap delay time, repeat counting
  0.15-3.0  silence            room for ~7 repeats at the loaded ~0.4 s
  3.0-3.9   three plucks       rhythmic figure for slice / beat-repeat
  3.9-5.0   silence
  5.0-7.0   held tone          doppler bend, shimmer ladder, quality warble
  7.0-8.0   silence

The held tone carries harmonics so decimation (Cold/Scorched) and pitch
waver (Sunny/Scorched) are audible on it.
"""

import numpy as np
import wave

SR = 44100
DUR = 8.0

def midi_hz(m):
    return 440.0 * 2 ** ((m - 69) / 12)

def pluck(freq, dur, decay=0.9965):
    """Karplus-Strong with a darkened excitation, so the fundamental reads."""
    n = int(SR * dur)
    period = int(round(SR / freq))
    rng = np.random.default_rng(int(freq * 100) % 2**32)
    buf = rng.uniform(-1, 1, period)
    for _ in range(2):
        buf = (np.roll(buf, 1) + buf + np.roll(buf, -1)) / 3
    out = np.empty(n)
    for i in range(n):
        out[i] = buf[i % period]
        buf[i % period] = decay * 0.5 * (buf[i % period] + buf[(i + 1) % period])
    f = int(SR * 0.005)
    out[-f:] *= np.linspace(1, 0, f)
    return out

def held(freq, dur, fade=0.02):
    t = np.arange(int(SR * dur)) / SR
    sig = (np.sin(2 * np.pi * freq * t)
           + 0.45 * np.sin(2 * np.pi * 2 * freq * t)
           + 0.25 * np.sin(2 * np.pi * 3 * freq * t)
           + 0.12 * np.sin(2 * np.pi * 5 * freq * t))
    env = np.ones_like(sig)
    f = int(SR * fade)
    env[:f] = np.linspace(0, 1, f)
    env[-f:] = np.linspace(1, 0, f)
    return sig * env / 1.82

sig = np.zeros(int(SR * DUR))

def place(at, x):
    i = int(SR * at)
    sig[i:i + len(x)] += x

# Isolated blip: short, bright, unmistakable transient to track through repeats.
place(0.0, pluck(midi_hz(81), 0.15) * 1.0)          # A5

# Rhythmic figure for the slice / beat-repeat block.
for at, m in ((3.0, 76), (3.3, 69), (3.6, 72)):     # E5 A4 C5
    place(at, pluck(midi_hz(m), 0.28) * 0.9)

# Held tone: steady pitch for doppler bend, shimmer, and quality waver.
place(5.0, held(midi_hz(57), 2.0) * 0.9)            # A3

sig = sig / np.max(np.abs(sig)) * 0.8
pcm = (sig * 32767).astype(np.int16)
stereo = np.column_stack([pcm, pcm]).ravel()

out = "/Users/gabrielroth/Dev/RobotBoy/mm-test-patches/delay-test-loop.wav"
with wave.open(out, "wb") as w:
    w.setnchannels(2)
    w.setsampwidth(2)
    w.setframerate(SR)
    w.writeframes(stereo.tobytes())

print(f"wrote {out}: {len(sig)/SR:.2f} s, peak {np.max(np.abs(sig)):.2f}")

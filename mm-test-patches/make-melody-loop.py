"""Generate melody-loop.wav for RB-Particules-1: plucky A-minor arpeggio + held tone.

44.1 kHz, 16-bit, stereo (dual mono), ~4 s, loops cleanly.
Plucks via Karplus-Strong (sharp attack for Shape/Size tests, distinct
pitches at distinct buffer positions for Time/Freeze tests); held tone is
steady additive A3 (for Quality waver/warble and pitch-interval listening).
"""

import numpy as np
import wave

SR = 44100

def midi_hz(m):
    return 440.0 * 2 ** ((m - 69) / 12)

def karplus_strong(freq, dur, decay=0.996):
    n = int(SR * dur)
    period = int(round(SR / freq))
    rng = np.random.default_rng(hash(freq) % 2**32)
    buf = rng.uniform(-1, 1, period)
    # darken the excitation (3-tap smooth, twice) so the fundamental dominates
    for _ in range(2):
        buf = (np.roll(buf, 1) + buf + np.roll(buf, -1)) / 3
    out = np.empty(n)
    for i in range(n):
        out[i] = buf[i % period]
        buf[i % period] = decay * 0.5 * (buf[i % period] + buf[(i + 1) % period])
    return out

def held_tone(freq, dur, attack=0.03):
    t = np.arange(int(SR * dur)) / SR
    sig = (np.sin(2 * np.pi * freq * t)
           + 0.4 * np.sin(2 * np.pi * 2 * freq * t)
           + 0.2 * np.sin(2 * np.pi * 3 * freq * t))
    env = np.ones_like(sig)
    a = int(SR * attack)
    env[:a] = np.linspace(0, 1, a)
    return sig * env / 1.6

# A minor arpeggio ascending: A2 C3 E3 A3 C4 E4 A4 E4
midi_notes = [45, 48, 52, 57, 60, 64, 69, 64]
NOTE_DUR = 0.3
HELD_DUR = 1.6  # A3, steady

parts = []
for m in midi_notes:
    note = karplus_strong(midi_hz(m), NOTE_DUR)
    # let each pluck ring into a fixed slot; hard cut with 5 ms fade to avoid clicks
    f = int(SR * 0.005)
    note[-f:] *= np.linspace(1, 0, f)
    parts.append(note)
parts.append(held_tone(midi_hz(57), HELD_DUR))

sig = np.concatenate(parts)
# fade the loop end so wrap to the first pluck is clean
f = int(SR * 0.05)
sig[-f:] *= np.linspace(1, 0, f)

sig = sig / np.max(np.abs(sig)) * 0.8
pcm = (sig * 32767).astype(np.int16)
stereo = np.column_stack([pcm, pcm]).ravel()

out = "/Users/gabrielroth/Dev/RobotBoy/mm-test-patches/melody-loop.wav"
with wave.open(out, "wb") as w:
    w.setnchannels(2)
    w.setsampwidth(2)
    w.setframerate(SR)
    w.writeframes(stereo.tobytes())

print(f"wrote {out}: {len(sig)/SR:.2f} s, peak {np.max(np.abs(sig)):.2f}")

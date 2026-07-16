#!/usr/bin/env python3
"""Generate input WAVs for the three Retours demo renders (Task 13 sanity
renders, also referenced by the user checklist's audible checks).

All files are stereo float32 @48 kHz in the simulator's minimal WAV format
(see gen_burst.py). MM sim amplitude convention is 1:1 volts.

Channel plan (the headless sim exposes exactly 2 input channels, mapped to
panel jacks 0/1 by audio_wrapper.hh; knobs cannot be automated, so gate-type
control rides channel 1):

  demo_karplus_in.wav      ch0 = 8 ms noise burst (+/-1 V), ch1 = silence
  demo_clocked_in.wav      ch0 = 15 ms 300 Hz burst at t=0.6 s,
                           ch1 = 5 V clock pulses every 0.5 s
  demo_freeze_in.wav       ch0 = 8 x 0.5 s tone segments 200..900 Hz,
                           then 1.5 s silence,
                           ch1 = 0 V until t=4.0 s, then 5 V (freeze gate)

Usage: python3 gen_demo_inputs.py [outdir]
"""
import numpy as np
import struct
import sys
import os

SR = 48000


def write_wav(path, left, right):
    n = len(left)
    assert len(right) == n
    stereo = np.empty((n, 2), dtype=np.float32)
    stereo[:, 0] = left
    stereo[:, 1] = right
    channels = 2
    bytes_per_sample = 4
    block_align = channels * bytes_per_sample
    data_bytes = stereo.astype("<f4").tobytes()
    datasize = len(data_bytes)
    with open(path, "wb") as f:
        f.write(b"RIFF")
        f.write(struct.pack("<I", 4 + (8 + 18) + (8 + datasize)))
        f.write(b"WAVEfmt ")
        f.write(struct.pack("<I", 0x12))
        f.write(struct.pack("<H", 0x3))          # float
        f.write(struct.pack("<H", channels))
        f.write(struct.pack("<I", SR))
        f.write(struct.pack("<I", SR * block_align))
        f.write(struct.pack("<H", block_align))
        f.write(struct.pack("<H", 8 * bytes_per_sample))
        f.write(struct.pack("<H", 0))            # ext_size
        f.write(b"data")
        f.write(struct.pack("<I", datasize))
        f.write(data_bytes)
    print(f"wrote {path}: {n} samples ({n / SR:.3f} s)")


def karplus():
    # 2.0 s. 8 ms Hann-windowed noise burst at t=0, +/-1 V. DENSITY=0 puts the
    # base time at the kMinDelaySeconds clamp (2 ms -> 500 Hz); FEEDBACK=0.95
    # rings it out Karplus-Strong style.
    n = int(SR * 2.0)
    n_burst = int(SR * 0.008)
    rng = np.random.default_rng(42)
    sig = np.zeros(n, dtype=np.float32)
    sig[:n_burst] = (np.hanning(n_burst) * rng.uniform(-1, 1, n_burst)).astype(np.float32)
    return sig, np.zeros(n, dtype=np.float32)


def clocked():
    # 3.2 s. ch1: 5 V, 5 ms clock pulses at t=0, 0.5, 1.0, ... (2 Hz).
    # ch0: one 15 ms 300 Hz burst (+/-1.5 V) at t=0.6 s -- after the second
    # tick so the clock interval is already locked when the burst arrives.
    n = int(SR * 3.2)
    ch0 = np.zeros(n, dtype=np.float32)
    n_burst = int(SR * 0.015)
    t = np.arange(n_burst) / SR
    win = 0.5 - 0.5 * np.cos(2 * np.pi * np.arange(n_burst) / n_burst)
    start = int(0.6 * SR)
    ch0[start:start + n_burst] = (1.5 * win * np.sin(2 * np.pi * 300.0 * t)).astype(np.float32)

    ch1 = np.zeros(n, dtype=np.float32)
    pulse = int(SR * 0.005)
    f = 0
    while f < n:
        ch1[f:min(f + pulse, n)] = 5.0
        f += int(0.5 * SR)
    return ch0, ch1


def freeze():
    # 5.5 s total. ch0: 4.0 s of 8 back-to-back 0.5 s tone segments
    # (200,300,...,900 Hz, +/-1.5 V, 5 ms edge fades), then 1.5 s silence.
    # ch1: freeze gate, 0 V -> 5 V at t=4.0 s, held high to the end.
    # With DENSITY = 4/11 (0.5 s base time on the 4 s HiFi buffer -> 8
    # slices), frozen slice k should loop the tone at (900 - 100*k) Hz.
    seg_s, freqs = 0.5, [200, 300, 400, 500, 600, 700, 800, 900]
    n_seg = int(SR * seg_s)
    fade = int(SR * 0.005)
    chunks = []
    for fr in freqs:
        t = np.arange(n_seg) / SR
        tone = 1.5 * np.sin(2 * np.pi * fr * t)
        env = np.ones(n_seg)
        env[:fade] = np.linspace(0, 1, fade)
        env[-fade:] = np.linspace(1, 0, fade)
        chunks.append((tone * env).astype(np.float32))
    n_tail = int(SR * 1.5)
    ch0 = np.concatenate(chunks + [np.zeros(n_tail, dtype=np.float32)])
    n = len(ch0)
    ch1 = np.zeros(n, dtype=np.float32)
    ch1[n_seg * len(freqs):] = 5.0
    return ch0, ch1


if __name__ == "__main__":
    outdir = sys.argv[1] if len(sys.argv) > 1 else "."
    for name, fn in (("demo_karplus_in.wav", karplus),
                     ("demo_clocked_in.wav", clocked),
                     ("demo_freeze_in.wav", freeze)):
        l, r = fn()
        write_wav(os.path.join(outdir, name), l, r)

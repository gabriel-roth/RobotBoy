#!/usr/bin/env python3
"""Generate a stereo impulse-burst test WAV at 48kHz for MetaModule headless
sim testing of Echos (beads-style delay).

A short burst near t=0 lets us look for delayed/attenuated repeats of the
same waveform later in the output -- much easier to eyeball than a
continuous tone. Format matches the simulator's minimal WAV
reader/writer (simulator/src/headless/wav_file.hh): float32, 2-channel,
extended fmt chunk (ext_size=0), 'data' chunk immediately after 'fmt '.

Amplitude is 1:1 "volts" (MetaModule convention), unlike VCV headless which
scales volts/5.
"""
import numpy as np
import struct
import sys

SR = 48000
DUR = 4.0          # total length; long enough to see 3 echo repeats at 1s spacing
BURST_MS = 20.0    # burst duration
FREQ = 440.0       # Hz, easy to eyeball/window
AMPLITUDE = 2.0    # +/-2 "volts" 1:1 (leave headroom for feedback build-up)

n_total = int(SR * DUR)
n_burst = int(SR * BURST_MS / 1000.0)

t_burst = np.arange(n_burst) / SR
# Hann-windowed tone burst -- avoids a click at the burst edges that would
# otherwise smear across the whole spectrum and complicate echo detection.
window = 0.5 - 0.5 * np.cos(2 * np.pi * np.arange(n_burst) / n_burst)
burst = AMPLITUDE * window * np.sin(2 * np.pi * FREQ * t_burst)

sig = np.zeros(n_total, dtype=np.float32)
sig[:n_burst] = burst.astype(np.float32)

stereo = np.empty((n_total, 2), dtype=np.float32)
stereo[:, 0] = sig
stereo[:, 1] = sig

out = sys.argv[1] if len(sys.argv) > 1 else "audio_in.wav"

channels = 2
sample_rate = SR
bytes_per_sample = 4  # float32
block_align = channels * bytes_per_sample
bytes_per_sec = sample_rate * block_align
bits_per_sample = 8 * bytes_per_sample
data_bytes = stereo.astype("<f4").tobytes()
datasize = len(data_bytes)

with open(out, "wb") as f:
    f.write(b"RIFF")
    riff_len = 4 + (8 + 18) + (8 + datasize)
    f.write(struct.pack("<I", riff_len))
    f.write(b"WAVEfmt ")
    f.write(struct.pack("<I", 0x12))
    f.write(struct.pack("<H", 0x3))
    f.write(struct.pack("<H", channels))
    f.write(struct.pack("<I", sample_rate))
    f.write(struct.pack("<I", bytes_per_sec))
    f.write(struct.pack("<H", block_align))
    f.write(struct.pack("<H", bits_per_sample))
    f.write(struct.pack("<H", 0))
    f.write(b"data")
    f.write(struct.pack("<I", datasize))
    f.write(data_bytes)

print(f"wrote {out}: {n_total} samples @ {SR} Hz, {DUR}s total, "
      f"{BURST_MS}ms {FREQ}Hz burst at t=0, amplitude {AMPLITUDE}")

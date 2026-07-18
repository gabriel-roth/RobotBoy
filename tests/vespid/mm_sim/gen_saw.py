#!/usr/bin/env python3
"""Generate a stereo saw-wave test WAV at 48kHz for MetaModule headless sim testing.

The headless simulator's WavReader (simulator/src/headless/wav_file.hh) is a
minimal, strict parser: RIFF/WAVEfmt / fmt-chunk (size 0x10 or 0x12, format
1=PCM or 3=float32) / optional 2-byte extension-size field (only if the fmt
chunk size is 0x12) / 'data' chunk immediately after -- no LIST/INFO/fact
chunks tolerated. scipy.io.wavfile inserts a 'fact' chunk for float32 data,
which this reader chokes on ("bad WAV format (data)"). So we hand-roll the
exact byte layout the reader (and its sibling WavWriter) expect, matching a
32-bit float, 2-channel WAV with an 18-byte extended fmt chunk (ext_size=0).

Amplitude is 1:1 "volts" (MetaModule convention), unlike VCV headless which
scales volts/5 -- see the known gotcha in the task brief.
"""
import numpy as np
import struct
import sys

SR = 48000
DUR = 3.0
FREQ = 110.0  # Hz -- strong harmonics well above the audio band from a naive saw
AMPLITUDE = 5.0  # +/-5 "volts" 1:1

t = np.arange(int(SR * DUR)) / SR
# Naive (non-bandlimited) sawtooth: -1..1 ramp. Aliasing/bandlimiting is
# exactly what the Wasp lowpass under test should be clamping down on.
saw = 2.0 * (t * FREQ - np.floor(0.5 + t * FREQ))
sig = (AMPLITUDE * saw).astype(np.float32)
stereo = np.empty((sig.shape[0], 2), dtype=np.float32)
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
    riff_len = 4 + (8 + 18) + (8 + datasize)  # "WAVE" + fmt chunk (hdr+18) + data chunk (hdr+data)
    f.write(struct.pack("<I", riff_len))
    f.write(b"WAVEfmt ")
    f.write(struct.pack("<I", 0x12))       # fmt chunk size (extended, matches WavWriter/WavReader)
    f.write(struct.pack("<H", 0x3))        # format: 3 = IEEE float
    f.write(struct.pack("<H", channels))
    f.write(struct.pack("<I", sample_rate))
    f.write(struct.pack("<I", bytes_per_sec))
    f.write(struct.pack("<H", block_align))
    f.write(struct.pack("<H", bits_per_sample))
    f.write(struct.pack("<H", 0))          # extension size = 0
    f.write(b"data")
    f.write(struct.pack("<I", datasize))
    f.write(data_bytes)

print(f"wrote {out}: {stereo.shape[0]} samples @ {SR} Hz, {DUR}s saw @ {FREQ}Hz, amplitude {AMPLITUDE}")

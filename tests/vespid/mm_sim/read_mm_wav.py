#!/usr/bin/env python3
"""Read the minimal WAV format written by simulator/src/headless/wav_file.hh's
WavWriter (float32, extended fmt chunk, no fact/LIST chunks)."""
import struct
import numpy as np
import sys

def read_mm_wav(path):
    with open(path, "rb") as f:
        data = f.read()
    assert data[0:4] == b"RIFF"
    assert data[8:16] == b"WAVEfmt "
    bs = struct.unpack_from("<I", data, 16)[0]
    fmt = struct.unpack_from("<H", data, 20)[0]
    channels = struct.unpack_from("<H", data, 22)[0]
    rate = struct.unpack_from("<I", data, 24)[0]
    off = 20 + bs  # start of chunk after fmt
    assert data[off:off+4] == b"data", f"expected data chunk at {off}, got {data[off:off+4]}"
    datasize = struct.unpack_from("<I", data, off+4)[0]
    raw = data[off+8:off+8+datasize]
    assert fmt == 3, f"expected float32 format, got {fmt}"
    arr = np.frombuffer(raw, dtype="<f4").reshape(-1, channels)
    return arr, rate

if __name__ == "__main__":
    arr, rate = read_mm_wav(sys.argv[1])
    print(f"{sys.argv[1]}: {arr.shape[0]} frames x {arr.shape[1]} ch @ {rate} Hz")
    print("min/max/mean/abs-mean per channel:")
    for c in range(arr.shape[1]):
        col = arr[:, c]
        print(f"  ch{c}: min={col.min():.4f} max={col.max():.4f} mean={col.mean():.6f} absmean={np.abs(col).mean():.4f}")
    nan_count = np.isnan(arr).sum()
    inf_count = np.isinf(arr).sum()
    print(f"NaN count: {nan_count}, Inf count: {inf_count}")

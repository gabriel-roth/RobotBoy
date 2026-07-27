#!/usr/bin/env python3
"""Design the short inner halfband pair for Vespid's 4x oversampling stages.

The inner stages (host*2 <-> host*4, i.e. 96k <-> 192k at a 48 kHz host) only
need stopband above 0.375*fs: after the outer 47-tap stage, inner-stage
aliases/images can reach the audio band only from the top quarter of the
spectrum (fold to < 24 kHz  <=>  f > 72 kHz at fs=192k). That enormous
transition band (24k..72k) lets 15 taps match the outer filter's 70 dB
stopband. Prints the taps for wasp_dsp_utils.hpp's kHalfbandTapsInner.
"""
import numpy as np
from scipy.signal import remez, freqz

fs = 192000.0
N = 15
h = remez(N, [0, 24000, fs/2 - 24000, fs/2], [1, 0], fs=fs)
c = N // 2
hb = h.copy()
for i in range(N):
    d = i - c
    if d != 0 and d % 2 == 0:
        hb[i] = 0.0          # exact halfband zeros
hb[c] = 0.5                  # exact center tap

w, H = freqz(hb, worN=1 << 15, fs=fs)
pb = np.abs(H)[w <= 24000]
sb = np.abs(H)[w >= 72000]
print(f"passband ripple {20*np.log10(pb.max()):+.4f}/{20*np.log10(pb.min()):+.4f} dB")
print(f"stopband max    {20*np.log10(sb.max()):.2f} dB (want <= -70)")
print()
for i in range(0, N, 4):
    print(", ".join(f"{v:.8e}f" for v in hb[i:i+4]) + ",")

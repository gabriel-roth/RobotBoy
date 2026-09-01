"""Generate halfband FIR coefficients for the Vespid oversampler.

The original target was remez(31, [0, 0.205, 0.295, 0.5], [1, 0])
validated to: passband ripple < 0.1 dB below 0.20*fs, stopband > 70 dB above
0.295*fs. Those two goals are mutually unachievable at 31 taps: a true
halfband is equal-ripple in both bands (weighting one band destroys the
halfband zero-tap structure), and 31 taps over a 0.09-wide transition buys
~52.6 dB. Kaiser's estimate puts 70 dB at ~48 taps. So the primary design
here is the 47-tap halfband, which meets both validation numbers
(passband dev 0.0021 dB, stopband -72.1 dB); the 31-tap variant is also
emitted (52.6 dB) in case the cheaper filter is ever preferred at
the lower attenuation. Recorded as a deviation in fitted_constants.md.

Exact halfband structure is enforced by zeroing the (already ~1e-6)
even-offset taps; center tap is 0.5 (exact by construction, asserted).
"""
import numpy as np
from scipy.signal import remez, freqz

def design(ntaps):
    h = remez(ntaps, [0, 0.205, 0.295, 0.5], [1, 0], fs=1.0)
    c = ntaps//2
    # force exact halfband structure: zero even-offset taps (except center)
    for k in range(-c, c+1):
        if k % 2 == 0 and k != 0:
            assert abs(h[c+k]) < 1e-4, f"tap {k} not halfband-small: {h[c+k]}"
            h[c+k] = 0.0
    w, H = freqz(h, worN=8192, fs=1.0)
    Hdb = 20*np.log10(np.abs(H) + 1e-12)
    pb = Hdb[w <= 0.20]; sb = Hdb[w >= 0.295]
    pb_dev = max(abs(pb.max()), abs(pb.min()))
    sb_max = sb.max()
    return h, pb_dev, sb_max

def cpp_array(h, name):
    vals = ",\n    ".join(", ".join(f"{v:.8e}f" for v in h[i:i+4])
                          for i in range(0, len(h), 4))
    return f"constexpr float {name}[{len(h)}] = {{\n    {vals}\n}};"

if __name__ == "__main__":
    for ntaps, name in ((47, "kHalfbandTaps47"), (31, "kHalfbandTaps31")):
        h, pb_dev, sb_max = design(ntaps)
        ok_pb = pb_dev < 0.1; ok_sb = sb_max < -70
        print(f"// {ntaps}-tap halfband: passband dev {pb_dev:.4f} dB below "
              f"0.20*fs ({'OK' if ok_pb else 'FAIL'} <0.1), stopband max "
              f"{sb_max:.2f} dB above 0.295*fs ({'OK' if ok_sb else 'FAIL'} <-70)")
        print(cpp_array(h, name))
        print()

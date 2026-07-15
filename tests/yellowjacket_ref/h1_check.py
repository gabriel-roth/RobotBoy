import numpy as np

# Component values (DAFx paper Table 1)
R3 = 27e3; R4 = 27e3; R14 = 1e3; R15 = 100e3
Rres = 50e3
C7 = 0.22e-6
R2 = 27e3; C2 = 100e-12

def Z1(rho, w, R13):
    """Feedback impedance Z1(jw) via Delta-Y-Delta (paper eqs 21-24).
    Triangle sides: (rho*Rres + R14), (1-rho)*Rres, (R13 + R15)."""
    s = 1j * w
    s1 = rho * Rres + R14
    s2 = (1 - rho) * Rres
    s3 = R13 + R15
    tot = Rres + R13 + R14 + R15
    Za = s1 * s3 / tot
    Zb = s2 * s3 / tot + 1 / (s * C7)
    Zc = s1 * s2 / tot + R4
    return (Zb * Zc + Za * Zb + Za * Zc) / Zb

def H1(rho, w, R13):
    return R3 / Z1(rho, w, R13)

def Q(rho, fc, R13):
    wc = 2 * np.pi * fc
    return 1.0 / (np.real(H1(rho, wc, R13)) + R2 * C2 * wc)

print("Q vs resonance pot rho, at various cutoffs")
print(f"{'rho':>5} | " + " | ".join(f"{'Q@%dHz (1M)'%f:>12}" for f in [100, 400, 1000, 5000, 15000]))
for rho in [0.0, 0.3, 0.5, 0.7, 0.8, 0.9, 0.95, 0.98, 1.0]:
    row = [Q(rho, f, 1e6) for f in [100, 400, 1000, 5000, 15000]]
    print(f"{rho:>5} | " + " | ".join(f"{q:>12.3f}" for q in row))

print("\nSame with R13 = 100k (original EDP)")
for rho in [0.0, 0.5, 0.8, 0.95, 1.0]:
    row = [Q(rho, f, 100e3) for f in [100, 400, 1000, 5000, 15000]]
    print(f"{rho:>5} | " + " | ".join(f"{q:>12.3f}" for q in row))

print("\n|H1| magnitude at 1kHz vs rho (should match paper Fig 3: ~-6..-9dB flat at low rho, steep drop at rho->1)")
for rho in [0.1, 0.5, 0.9, 0.95, 0.98, 1.0]:
    for f in [100, 1000, 10000]:
        h = H1(rho, 2*np.pi*f, 1e6)
        print(f"rho={rho:>4} f={f:>6} |H1|={20*np.log10(abs(h)):>8.2f} dB  Re(H1)={np.real(h):>8.5f}")

# H1 as first-order (b1 s + b0)/(a1 s + a0): extract numerically for code use
def coeffs(rho, R13):
    # Z1 = (ZbZc + ZaZb + ZaZc)/Zb; with Zb = Rb + 1/(sC7):
    # numerator N(s) = Zc*(Rb + 1/(sC7)) + Za*(Rb + 1/(sC7)) + Za*Zc
    # H1 = R3*Zb/ (ZbZc+ZaZb+ZaZc)  -- first order in s. Fit at two freqs.
    w1, w2 = 2*np.pi*10, 2*np.pi*10000
    h1, h2 = H1(rho, w1, R13), H1(rho, w2, R13)
    # H1(s)=(b1 s + b0)/(a1 s + a0), normalize a0=1: 3 unknowns, complex eqs
    # h*(a1 s + 1) = b1 s + b0  ->  b1 s + b0 - h a1 s = h
    A = []
    y = []
    for w, h in [(w1, h1), (w2, h2)]:
        s = 1j*w
        A.append([s, 1, -h*s]); y.append(h)
    A = np.array([[c for c in row] for row in A + [[np.conj(v) for v in row] for row in A]])
    y = np.array(y + [np.conj(v) for v in y])
    sol, *_ = np.linalg.lstsq(A, y, rcond=None)
    b1, b0, a1 = (np.real(v) for v in sol)
    return b1, b0, a1

for rho in [0.0, 0.5, 0.95, 1.0]:
    b1, b0, a1 = coeffs(rho, 1e6)
    print(f"rho={rho}: H1(s)=({b1:.6g} s + {b0:.6g})/({a1:.6g} s + 1)   pole at {1/(2*np.pi*a1):.1f} Hz, zero at {b0/(2*np.pi*b1) if b1 else float('inf'):.1f} Hz, H1(0)={b0:.4f}, H1(inf)={b1/a1:.4f}")

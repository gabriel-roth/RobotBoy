"""Vespid reference simulation (design validator + golden data).

Implements the spec model (docs/superpowers/specs/2026-07-15-vespid-dsp-design.md,
including the Revision 1 section):
TPT SVF, scalar implicit solve in hp via scipy brentq (robust, offline-only),
H1 pole-zero damping network, diode boost, inverter saturator, rail clamps.

Rev 1 physics (see spec "Revision 1"):
- Loop-gain factor lambda = A0/(nInv + A0) multiplies every loop term in the
  small-signal theory: D(s) = s^2 + lam*(H1 + kC2eff)*wc*s + lam*kR2*wc^2.
  LP passband gain = 1/kR2 (lambda cancels at DC) -- Screaming's LP passband
  is genuinely about -11.4 dB (the 10k mod does that in hardware too).
- wcComp = 1/sqrt(lambda*kR2) for BOTH modes, so the knob frequency lands on
  the actual resonant peak w0 = wc_int*sqrt(lambda*kR2).
- Self-oscillation mechanism: the inverter's finite bandwidth (one-pole lag
  at fPole) folds into the kC2 coefficient with no extra state:
      kC2eff = R3*C2*wc_int - kR2*wc_int/(2*pi*fPole)
  kC2eff may be negative -- that is the point (negative damping at rho->1).
"""
import numpy as np, json
from scipy.optimize import brentq

R3=27e3; R2=27e3; C2=100e-12; R4=27e3; R13=1e6; R14=1e3; R15=100e3
Rres=50e3; C7=0.22e-6; VT=0.025852; Is=2.52e-9; eta=1.752
kD = R3*2*Is; vD = eta*VT

# Inverter bandwidth (Rev 1): one-pole lag; designated tuning constant for
# the self-oscillation acceptance criteria (try 50-120 kHz if 80k misses).
FPOLE = 80e3

# A0/vHi/vLo (== invHi/invLo) per mode: fitted by fit_inverter.py from the
# DAFx-2022 CD4069 MOSFET model (Table 2) at the mode's supply voltage
# (12V screaming, 5V tame). See fitted_constants.md for the fit run and
# the investigation note about the inverter's operating-point discrepancy
# (the model's DC crossing sits near 8.7V at 12V supply vs paper Fig 5's
# ~5.5-6V; coefficients verified against the paper verbatim -- the *shape*
# of the fit, which is what these constants describe, is unaffected).
def _mode(c, kR2, nInv, A0, vHi, vLo, makeup):
    lam = A0/(nInv + A0)
    return dict(c=c, kR2=kR2, nInv=nInv, wcComp=1/np.sqrt(lam*kR2), lam=lam,
                vHi=vHi, vLo=vLo, A0=A0, invHi=vHi, invLo=vLo, makeup=makeup)

MODES = {
 "screaming": _mode(c=0.296, kR2=3.70, nInv=6.70,
                    A0=17.88, vHi=3.031, vLo=8.500, makeup=2.0),
 "tame":      _mode(c=0.155, kR2=1.0,  nInv=4.0,
                    A0=23.44, vHi=1.708, vLo=3.105, makeup=1.0),
}

def h1_coeffs(rho):
    s1 = rho*Rres + R14; s2 = (1-rho)*Rres; s3 = R13 + R15
    tot = s1 + s2 + s3
    Ra = s1*s3/tot; Rb = s2*s3/tot; Rc = s1*s2/tot
    Za = Ra; Zc = Rc + R4
    b1 = R3*Rb*C7; b0 = R3
    a1 = C7*(Rb*(Za+Zc) + Za*Zc); a0 = Za + Zc
    return b1, b0, a1, a0

def bilinear_1p1z(b1, b0, a1, a0, fs):
    K = 2*fs
    d = a0 + a1*K
    return (b0 + b1*K)/d, (b0 - b1*K)/d, (a0 - a1*K)/d   # beta0, beta1, alpha1

def S(v, c):    return np.tanh(c*v)/c
def finv(vg, m):
    A0, hi, lo = m["A0"], m["invHi"], m["invLo"]
    return hi*np.tanh(-A0*vg/hi) if vg <= 0 else -lo*np.tanh(A0*vg/lo)
def fdiode(yd): return yd + kD*np.sinh(np.clip(yd/vD, -30, 30))
def rail(v, m):
    w = 0.3
    hi = m["vHi"] - w; lo = -m["vLo"] + w
    if v > hi: return hi + w*np.tanh((v-hi)/w)
    if v < lo: return lo - w*np.tanh((lo-v)/w)
    return v

class WaspRef:
    def __init__(self, mode, fs, os=8):
        self.m = MODES[mode]; self.fsi = fs*os; self.os = os
        self.sBP = self.sLP = 0.0; self.z1 = 0.0
        self.hin_s = 0.0; self.hp_prev = 0.0
        self.dc = [0.0]*3
    def set_controls(self, fc, rho):
        m = self.m
        fci = np.clip(fc*m["wcComp"], 0.25, 0.45*self.fsi)
        self.g = np.tan(np.pi*fci/self.fsi)
        wci = 2*np.pi*fci
        # Rev 1: kC2eff (may be negative -- self-oscillation mechanism)
        self.kC2 = R3*C2*wci - m["kR2"]*wci/(2*np.pi*FPOLE)
        b1,b0,a1,a0 = h1_coeffs(np.clip(rho,0,1))
        self.b0d, self.b1d, self.a1d = bilinear_1p1z(b1,b0,a1,a0,self.fsi)
    def _residual(self, hp, hin):
        m = self.m; c = m["c"]
        bp = self.sBP + self.g*S(hp, c)
        lp = self.sLP + self.g*S(bp, c)
        yd = self.b0d*bp + self.z1
        s = hin + fdiode(yd) + m["kR2"]*lp + self.kC2*S(bp, c)
        return hp - finv((s + hp)/m["nInv"], m), bp, lp, yd
    def tick(self, x):
        m = self.m; c = m["c"]
        # input HPF 22 Hz
        a = 2*np.pi*22/self.fsi
        hin = x - self.hin_s; self.hin_s += a*hin
        f = lambda h: self._residual(h, hin)[0]
        span = max(m["vHi"], m["vLo"]) + 1.0
        try:  hp = brentq(f, -span-abs(hin), span+abs(hin), xtol=1e-9)
        except ValueError: hp = self.hp_prev
        _, bp, lp, yd = self._residual(hp, hin)
        self.hp_prev = hp
        self.sBP = rail(2*bp - self.sBP, m)
        self.sLP = rail(2*lp - self.sLP, m)
        self.z1 = self.b1d*bp - self.a1d*yd
        return lp, bp, hp
    def run(self, x_host):
        # naive zero-order-hold oversampling + decimation by averaging is fine
        # for analysis purposes (offline reference only)
        out = np.zeros((len(x_host), 3))
        for i, x in enumerate(x_host):
            acc = np.zeros(3)
            for _ in range(self.os):
                acc += self.tick(x)
            out[i] = acc/self.os
        return out

def response_db(mode, fc, rho, fprobe, fs=48000, amp=0.05, dur=0.5):
    """Small-signal gain (dB) of LP at fprobe. amp small => linear regime.

    The probe is synthesized and the response measured at the INTERNAL
    (oversampled) rate: the naive ZOH-hold + boxcar-average resampling in
    run() is fine for the self-osc probes but costs ~-2.9 dB of pure
    measurement artifact at 15 kHz (input ZOH sinc + boxcar decimator
    sinc), which would poison golden.json for the C++ tests. Measuring at
    the internal rate gives the true filter response (verified: the
    fc=5000/fp=15000 deviation vs theory drops from -2.58 dB to -0.09 dB).

    amp note: 0.05 V is small-signal for the rho=0.3 grid, but NOT at the
    rho=0.9 resonance peak (+19 dB -> ~0.45 V internal swing compresses
    the peak by ~2.5 dB); peak measurements pass amp=0.02 (verified
    within 0.03 dB of linear theory)."""
    w = WaspRef(mode, fs); w.set_controls(fc, rho)
    fsi = w.fsi
    n = int(fsi*dur); t = np.arange(n)/fsi
    x = amp*np.sin(2*np.pi*fprobe*t)
    y = np.empty(n)
    for i in range(n):
        y[i] = w.tick(x[i])[0]
    seg = y[n//2:]; ref = x[n//2:]
    return 20*np.log10((np.sqrt(2)*np.std(seg) + 1e-12)/(np.sqrt(2)*np.std(ref)))

def H1_of_jw(rho, w):
    """Exact H1(jw) via the Delta-Y-Delta closed form (h1_check.py math)."""
    s = 1j*w
    s1 = rho*Rres + R14; s2 = (1-rho)*Rres; s3 = R13 + R15
    tot = Rres + R13 + R14 + R15
    Za = s1*s3/tot
    Zb = s2*s3/tot + 1/(s*C7)
    Zc = s1*s2/tot + R4
    Z1 = (Zb*Zc + Za*Zb + Za*Zc)/Zb
    return R3/Z1

def theory_lp_db(mode, fc, rho, fprobe):
    """Rev 1 linear theory: |H_LP(j2*pi*fprobe)| including the loop-gain
    factor lambda, wcComp knob compensation, kC2eff (inverter lag), and the
    22 Hz input HPF. D(s) = s^2 + lam*(H1(s)+kC2eff)*wc*s + lam*kR2*wc^2;
    H_LP = -lam*wc^2*H_in/D."""
    m = MODES[mode]
    lam = m["lam"]; kR2 = m["kR2"]
    fci = fc*m["wcComp"]
    wc = 2*np.pi*fci
    kC2eff = R3*C2*wc - kR2*wc/(2*np.pi*FPOLE)
    w = 2*np.pi*fprobe
    s = 1j*w
    H1 = H1_of_jw(rho, w)
    D = s**2 + lam*(H1 + kC2eff)*wc*s + lam*kR2*wc**2
    Hin = s/(s + 2*np.pi*22)
    HLP = -lam*wc**2*Hin/D
    return 20*np.log10(abs(HLP) + 1e-15)

def selfosc_probe(mode, fc=1000, rho=1.0, fs=48000, dur=2.0):
    """Kick the filter, remove input, report ring amplitude at end + freq.
    Amplitude is measured on the raw BP state (pre-makeup)."""
    w = WaspRef(mode, fs); w.set_controls(fc, rho)
    n = int(fs*dur); x = np.zeros(n); x[:fs//10] = 3.0*np.sin(2*np.pi*fc*np.arange(fs//10)/fs)
    y = w.run(x)[:,1]  # BP
    tail = y[-fs//4:]
    tail = tail - np.mean(tail)   # asymmetric osc has DC; remove for amp/freq
    ampl = np.sqrt(2)*np.std(tail)
    # zero-crossing freq estimate
    zc = np.where(np.diff(np.sign(tail)) > 0)[0]
    freq = fs*(len(zc)-1)/(zc[-1]-zc[0]) if len(zc) > 3 else 0.0
    return float(ampl), float(freq)

if __name__ == "__main__":
    golden = {}
    print(f"lambda: screaming={MODES['screaming']['lam']:.4f} "
          f"tame={MODES['tame']['lam']:.4f}   fPole={FPOLE:.0f} Hz")
    print(f"wcComp: screaming={MODES['screaming']['wcComp']:.4f} "
          f"tame={MODES['tame']['wcComp']:.4f}")

    print("\n=== Small-signal LP response vs Rev-1 linear theory (rho=0.3) ===")
    max_dev = 0.0
    for fc in (200, 1000, 5000):
        for fp in (fc/4, fc, fc*3):
            sim = response_db("screaming", fc, 0.3, fp)
            th = theory_lp_db("screaming", fc, 0.3, fp)
            dev = sim - th
            max_dev = max(max_dev, abs(dev))
            key = f"lp_db_screaming_fc{fc}_f{int(fp)}"
            golden[key] = sim
            print(f"  fc={fc:5d} fp={fp:8.1f}  sim={sim:7.2f} dB  theory={th:7.2f} dB  dev={dev:+6.2f} dB")
    print(f"  max |deviation| = {max_dev:.2f} dB (criterion: <= 1.5 dB)")

    print("\n=== Resonance peak grows with rho (screaming, fc=1000) ===")
    for rho in (0.0, 0.5, 0.9):
        sim = response_db("screaming", 1000, rho, 1000, amp=0.02)
        th = theory_lp_db("screaming", 1000, rho, 1000)
        golden[f"lp_db_screaming_peak_rho{rho}"] = sim
        print(f"  rho={rho:4.2f}  sim={sim:7.2f} dB  theory={th:7.2f} dB  dev={sim-th:+6.2f} dB")

    print("\n=== Self-oscillation ===")
    a, f = selfosc_probe("screaming")
    golden["selfosc_screaming_amp"] = a; golden["selfosc_screaming_freq"] = f
    print(f"  screaming: amp={a:.3f} V (want 1.0-8.0), freq={f:.1f} Hz (want within 15% of 1000 -> 850-1150)")
    a2, f2 = selfosc_probe("tame")
    golden["selfosc_tame_amp"] = a2
    print(f"  tame:      amp={a2:.4f} V (want < 0.05, i.e. rings out)")

    golden["lambda_screaming"] = MODES["screaming"]["lam"]
    golden["lambda_tame"] = MODES["tame"]["lam"]
    golden["fpole"] = FPOLE

    print("\n" + json.dumps(golden, indent=1))
    json.dump(golden, open("golden.json", "w"), indent=1)

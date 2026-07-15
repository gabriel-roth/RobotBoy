# Yellowjacket Wasp Filter DSP Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement the stereo, polyphonic EDP Wasp VCF emulation (nonlinear CMOS/OTA state-variable filter) behind the existing Yellowjacket panel shell.

**Architecture:** Header-only DSP core (`WaspFilter`) using TPT/trapezoidal discretization with a scalar zero-delay-feedback solve (mystran fixed-pivot base + optional Newton refinement), wrapped in a per-channel oversampling adapter, driven by an MF-20-style module with modulate-rate control math. A Python reference simulation validates the design and generates fitted constants + golden test data first.

**Tech Stack:** C++20 (VCV Rack plugin + MetaModule cmake build), Python/numpy+scipy for the offline reference, plain-g++ test lane (`tests/run.sh`).

**Spec:** `docs/superpowers/specs/2026-07-15-yellowjacket-dsp-design.md` (design) and `docs/superpowers/specs/2026-07-15-yellowjacket-research-notes.md` (all constants, sources, derivations). Implementers should read both.

## Global Constraints

- Work ONLY on the `worktree-wasp` worktree (`/Users/gabrielroth/Dev/RobotBoy/.worktrees/worktree-wasp`); never touch main or other worktrees.
- **Do not modify the panel SVGs, widget positions, or param/port enums** in `src/Yellowjacket.cpp` — DSP and menu code only.
- No new external dependencies. No heap allocation on the audio thread. MetaModule compatibility: no `std::thread`, no exceptions in the audio path, keep per-sample cost lean.
- Commit after each passing task (short messages, ≤15 words, no AI attribution).
- All physical constants come from the research notes; do not invent values. VT = 0.025852 V, diode Is = 2.52e-9 A, η = 1.752, R3 = 27e3.
- Signal domain: real circuit volts, VCV volts map 1:1 at the module boundary (±5 V audio).

## Component constants reference (used across tasks)

```
Shared:      R3=27e3  R2=27e3  C2=100e-12  R4=27e3  R13=1e6  R14=1e3
             R15=100e3  Rres=50e3  C7=0.22e-6  VT=0.025852  Is=2.52e-9  eta=1.752
Derived:     kD = R3*2*Is = 1.3608e-4      vD = eta*VT = 0.045293
Screaming:   c=0.296  kR2=3.70 (R2eff=27k||10k=7297)  nInv=6.70  wcComp=1/sqrt(3.70)=0.5199
             rails: vHi=5.1 vLo=5.3 (12V supply, op point ~5.7V)  makeup=1.0
Tame:        c=0.155  kR2=1.0  nInv=4.0  wcComp=1.0
             rails: vHi=2.15 vLo=2.25 (5V supply, op point ~2.4V)  makeup=2.4
Inverter:    finv(vg) = vg<=0 ? vHi*tanh(-A0*vg/vHi) : -vLo*tanh(A0*vg/vLo)
             A0 (open-loop gain magnitude): default 25 (Screaming), 15 (Tame)
             — Task 1 refines A0/vHi/vLo per mode by fitting the Table-2 MOSFET model.
```

---

### Task 1: Python reference simulation + constant fitting

**Files:**
- Create: `tests/yellowjacket_ref/fit_inverter.py`
- Create: `tests/yellowjacket_ref/wasp_ref.py`
- Create: `tests/yellowjacket_ref/golden.json` (generated)
- Create: `tests/yellowjacket_ref/fitted_constants.md` (generated summary)
- Reference: `tests/yellowjacket_ref/h1_check.py` (already present — reuse its H1 functions)

**Interfaces:**
- Produces: `golden.json` with keys documented in Step 4 (consumed by Task 3's tests) and `fitted_constants.md` with final values for `A0/vHi/vLo` per mode plus 31-tap halfband FIR coefficients (consumed by Task 2).

Environment: `source ~/Dev/python-scripts/.venv/bin/activate` (numpy/scipy available).

- [ ] **Step 1: Write `fit_inverter.py`** — solve the DAFx Table-2 CD4069 MOSFET model for the DC transfer curve and fit the asymmetric-tanh closed form.

```python
"""Fit the Yellowjacket inverter saturator to the DAFx-2022 CD4069 model.

MOSFET model (paper eqs 33-34), NMOS params (Table 2):
  alpha_n(v) = 3.1252e-6*v^2 - 1.0471e-4*v + 0.0022
  vt_n(v)    = 0.0149*v^2 + 0.2212*v + 0.8655        lambda_n = 1e-3
PMOS (cubic alpha):
  alpha_p(v) = -2.7258e-6*v^3 - 6.7622e-5*v^2 - 5.1393e-4*v - 4.9942e-4
  vt_p(v)    = 0.0135*v^2 - 0.1941*v + 0.6239        lambda_p = 0.06
iD piecewise: 0 if vgs<=vt; linear if vds <= vgs-vt; else saturation:
  i_lin = a*(vgs - vt - vds/2)*vds*(1+lam*vds)
  i_sat = a/2*(vgs - vt)^2*(1+lam*vds)
Inverter at supply VDD: NMOS vgs=vin, vds=vout; PMOS vgs=VDD-vin, vds=VDD-vout.
Solve i_n(vout) == i_p(vout) for vout by bisection over [0, VDD].
NOTE the PMOS alpha polynomial is negative-valued in the paper's table;
use its absolute value (current magnitude) — check the resulting curve
against paper Fig 5 (12 V): output ~11.9V at vin=0, ~0.05V at vin=12,
transition centered near vin ~5.5-6V. If the curve is upside down or
degenerate, revisit signs before fitting.
"""
import numpy as np, json
from scipy.optimize import curve_fit

def alpha_n(v): return 3.1252e-6*v**2 - 1.0471e-4*v + 0.0022
def vt_n(v):    return 0.0149*v**2 + 0.2212*v + 0.8655
def alpha_p(v): return abs(-2.7258e-6*v**3 - 6.7622e-5*v**2 - 5.1393e-4*v - 4.9942e-4)
def vt_p(v):    return 0.0135*v**2 - 0.1941*v + 0.6239

def i_fet(alpha, vt, lam, vgs, vds):
    if vgs <= vt: return 0.0
    if vds <= vgs - vt:
        return alpha*(vgs - vt - vds/2)*vds*(1 + lam*vds)
    return alpha/2*(vgs - vt)**2*(1 + lam*vds)

def inverter_out(vin, VDD):
    lo, hi = 0.0, VDD
    for _ in range(60):
        vo = 0.5*(lo+hi)
        i_n = i_fet(alpha_n(vin), vt_n(vin), 1e-3, vin, vo)
        i_p = i_fet(alpha_p(VDD-vin), vt_p(VDD-vin), 0.06, VDD-vin, VDD-vo)
        # NMOS pulls vout down, PMOS pulls up: if pull-up wins, raise vo
        if i_p > i_n: lo = vo
        else: hi = vo
    return 0.5*(lo+hi)

def fit_supply(VDD):
    vin = np.linspace(0, VDD, 2400)
    vout = np.array([inverter_out(v, VDD) for v in vin])
    # operating point: vin == vout crossing
    op = vin[np.argmin(np.abs(vout - vin))]
    vg = vin - op            # centered input
    vo = vout - op           # centered output
    def model(x, A0, vHi, vLo):
        return np.where(x <= 0, vHi*np.tanh(-A0*x/vHi), -vLo*np.tanh(A0*x/vLo))
    p, _ = curve_fit(model, vg, vo, p0=[20, VDD/2, VDD/2], maxfev=20000)
    resid = np.sqrt(np.mean((model(vg, *p) - vo)**2))
    return dict(VDD=VDD, op=float(op), A0=float(p[0]), vHi=float(p[1]),
                vLo=float(p[2]), rms_fit_err=float(resid))

if __name__ == "__main__":
    for vdd in (12.0, 5.0):
        r = fit_supply(vdd)
        print(json.dumps(r, indent=1))
```

- [ ] **Step 2: Run it and sanity-check.** `python fit_inverter.py`. Expected: for 12 V, op point 5–6.5 V, A0 roughly 10–40, vHi+vLo ≈ 10–12 (most of the rail span), rms_fit_err < 0.4 V. For 5 V: op 2–3 V, vHi+vLo ≈ 4–5. If the tanh fit is poor (rms > 0.4), restrict the fit to |vg| within ±80% of the rails (the extreme tails matter less) and note it. Record results in `fitted_constants.md`.

- [ ] **Step 3: Write `wasp_ref.py`** — the full nonlinear reference sim, mirroring the spec equations exactly, brute-force solver, 8× oversampling.

```python
"""Yellowjacket reference simulation (design validator + golden data).

Implements the spec model (docs/superpowers/specs/2026-07-15-yellowjacket-dsp-design.md):
TPT SVF, scalar implicit solve in hp via scipy brentq (robust, offline-only),
H1 pole-zero damping network, diode boost, inverter saturator, rail clamps.
"""
import numpy as np, json
from scipy.optimize import brentq

R3=27e3; R2=27e3; C2=100e-12; R4=27e3; R13=1e6; R14=1e3; R15=100e3
Rres=50e3; C7=0.22e-6; VT=0.025852; Is=2.52e-9; eta=1.752
kD = R3*2*Is; vD = eta*VT

MODES = {
 "screaming": dict(c=0.296, kR2=3.70, nInv=6.70, wcComp=1/np.sqrt(3.70),
                   vHi=5.1, vLo=5.3, A0=25.0, invHi=5.1, invLo=5.3, makeup=1.0),
 "tame":      dict(c=0.155, kR2=1.0,  nInv=4.0,  wcComp=1.0,
                   vHi=2.15, vLo=2.25, A0=15.0, invHi=2.15, invLo=2.25, makeup=2.4),
}
# Step 3a: overwrite A0/invHi/invLo (and rail vHi/vLo = same values) with
# fit_inverter.py output for 12V (screaming) and 5V (tame).

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
        self.kC2 = R3*C2*2*np.pi*fci
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

def response_db(mode, fc, rho, fprobe, fs=48000, amp=0.05):
    """Small-signal gain (dB) of LP at fprobe. amp small => linear regime."""
    w = WaspRef(mode, fs); w.set_controls(fc, rho)
    n = int(fs*0.5); t = np.arange(n)/fs
    x = amp*np.sin(2*np.pi*fprobe*t)
    y = w.run(x)[:,0]
    seg = y[n//2:]; ref = x[n//2:]
    return 20*np.log10((np.sqrt(2)*np.std(seg) + 1e-12)/(np.sqrt(2)*np.std(ref)))

def selfosc_probe(mode, fc=1000, rho=1.0, fs=48000, dur=2.0):
    """Kick the filter, remove input, report ring amplitude at end + freq."""
    w = WaspRef(mode, fs); w.set_controls(fc, rho)
    n = int(fs*dur); x = np.zeros(n); x[:fs//10] = 3.0*np.sin(2*np.pi*fc*np.arange(fs//10)/fs)
    y = w.run(x)[:,1]  # BP
    tail = y[-fs//4:]
    ampl = np.sqrt(2)*np.std(tail)
    # zero-crossing freq estimate
    zc = np.where(np.diff(np.sign(tail)) > 0)[0]
    freq = fs*(len(zc)-1)/(zc[-1]-zc[0]) if len(zc) > 3 else 0.0
    return float(ampl), float(freq)

if __name__ == "__main__":
    golden = {}
    # 1) small-signal LP response vs linear theory, rho=0.3
    for fc in (200, 1000, 5000):
        for fp in (fc/4, fc, fc*3):
            golden[f"lp_db_screaming_fc{fc}_f{int(fp)}"] = response_db("screaming", fc, 0.3, fp)
    # 2) resonance peak grows with rho
    for rho in (0.0, 0.5, 0.9):
        golden[f"lp_db_screaming_peak_rho{rho}"] = response_db("screaming", 1000, rho, 1000)
    # 3) self-oscillation
    a, f = selfosc_probe("screaming"); golden["selfosc_screaming_amp"] = a; golden["selfosc_screaming_freq"] = f
    a, f = selfosc_probe("tame");      golden["selfosc_tame_amp"] = a
    # 4) drive: THD-ish proxy (rms of residual after removing fundamental) at drive 8x
    print(json.dumps(golden, indent=1))
    json.dump(golden, open("golden.json", "w"), indent=1)
```

- [ ] **Step 4: Run `wasp_ref.py` and validate the design.** Acceptance criteria (iterate on constants if violated, recording every change in `fitted_constants.md`):
  - `lp_db_*` small-signal numbers within ±1.5 dB of the linear SVF prediction `|H_LP(j2πf)|` with `Q(ρ, ωc)` from `h1_check.py` math (add a quick comparison print in the script — compute the theory value with numpy directly).
  - `selfosc_screaming_amp` between 1.0 and 8.0 V and `selfosc_screaming_freq` within 15% of 1000 Hz (compensation working).
  - `selfosc_tame_amp` < 0.05 V (rings out and dies — no free-run in Tame).
  - If Tame free-runs: reduce Tame `A0` (12, 10, ...) until it decays; if Screaming won't sustain: check `kR2`, raise `A0`, and verify the ω0 compensation; note final values.
- [ ] **Step 5: Generate halfband FIR coefficients** (append to `fit_inverter.py` or a small `gen_halfband.py`): `scipy.signal.remez(31, [0, 0.205, 0.295, 0.5], [1, 0], fs=1.0)`; verify with `freqz`: passband ripple < 0.1 dB below 0.20·fs, stopband > 70 dB above 0.295·fs; force exact halfband structure by zeroing the near-zero even taps (all taps at even offsets from center except the center must be ~0) and print the array as a C++ `constexpr float` list into `fitted_constants.md`.
- [ ] **Step 6: Write `fitted_constants.md`** summarizing: per-mode `A0/vHi/vLo` (fitted), any acceptance-criteria retunes, halfband taps, and the exact golden.json semantics.
- [ ] **Step 7: Commit** — `git add tests/yellowjacket_ref && git commit -m "Yellowjacket: Python reference sim, inverter fit, golden data"`.

---

### Task 2: DSP utility header (`wasp_dsp_utils.hpp`) + tests

**Files:**
- Create: `src/yellowjacket/wasp_dsp_utils.hpp`
- Test: `tests/yellowjacket/test_wasp_utils.cpp`
- Modify: `tests/run.sh` (add `yellowjacket` to the dir loop: `for d in mf20 loooop particules yellowjacket; do`)

**Interfaces:**
- Produces (namespace `wasp`): `float tanhApprox(float)`, `float tanhXdX(float)`, `struct H1Coeffs { float beta0, beta1, alpha1; }`, `H1Coeffs computeH1(float rho, float fsInt)`, `struct DcBlocker { float process(float x); void reset(); }` (~8 Hz, set rate via `void setSampleRate(float)`), `struct HalfbandUp { void process(float in, float* out2); }` / `struct HalfbandDown { float process(float in0, float in1); }` (31-tap FIR halfband from Task 1 constants), `float railClamp(float v, float vHi, float vLo)`.
- Consumes: halfband taps + nothing else from Task 1 (pure math otherwise).

- [ ] **Step 1: Write the failing tests** in `tests/yellowjacket/test_wasp_utils.cpp`, plain-assert style copied from `tests/mf20/test_module_dsp.cpp` (check/report helpers). Cover:

```cpp
// tanhApprox: |tanhApprox(x) - std::tanh(x)| < 0.01 for x in {-6,-3,-1,-0.1,0,0.1,1,3,6}
// tanhXdX: tanhXdX(0) == 1 (no NaN); tanhXdX(3) ≈ tanh(3)/3 within 1e-3; continuity at |x|=3
// computeH1: H1 DC gain beta-sum check — for rho=0.5, fs=192000:
//   dc = (beta0+beta1)/(1+alpha1) ≈ 0.515 within 2% (value from h1_check.py, H1(0)=0.5151)
//   for rho=0.0: dc ≈ 0.964 within 2%; for rho=1.0: dc ≈ 0.356 within 2%
// DcBlocker: DC input 1.0 for 1 s at 48k -> |out| < 0.05; passes 100 Hz sine within 0.5 dB
// Halfband: upsample+downsample a 1 kHz sine at 48k -> RMS within 0.3 dB, no NaN
// railClamp: identity inside +/- (limit-0.3); output never exceeds vHi/vLo + 1e-3; monotone
```

Write each as real code with the check() pattern; ~25 assertions total.

- [ ] **Step 2: Run to verify failure**: `cd tests && ./run.sh` → compile error (header missing). 
- [ ] **Step 3: Implement `src/yellowjacket/wasp_dsp_utils.hpp`:**

```cpp
#pragma once
// wasp_dsp_utils.hpp — shared math for the Yellowjacket Wasp VCF core.
// Constants and derivations: docs/superpowers/specs/2026-07-15-yellowjacket-*.md
#include <cmath>
#include <algorithm>

namespace wasp {

// tanh approximant x*(27+x^2)/(27+9x^2), exact-valued and slope-continuous
// at the |x|=3 handoff to +/-1 (tanh(3)=0.995).
inline float tanhApprox(float x) {
    if (x >  3.f) return 1.f;
    if (x < -3.f) return -1.f;
    float x2 = x*x;
    return x*(27.f + x2)/(27.f + 9.f*x2);
}
// tanh(x)/x with the same regions; limit 1 at 0.
inline float tanhXdX(float x) {
    float ax = std::fabs(x);
    if (ax > 3.f) return 1.f/ax;
    float x2 = x*x;
    return (27.f + x2)/(27.f + 9.f*x2);
}

// H1(s) resonance-network pole-zero, Δ-Y closed form (research notes),
// bilinear-transformed at the internal rate. rho = resonance pot 0..1.
struct H1Coeffs { float beta0, beta1, alpha1; };
inline H1Coeffs computeH1(float rho, float fsInt) {
    constexpr float R3=27e3f, R4=27e3f, R13=1e6f, R14=1e3f, R15=100e3f,
                    Rres=50e3f, C7=0.22e-6f;
    float s1 = rho*Rres + R14, s2 = (1.f-rho)*Rres, s3 = R13 + R15;
    float tot = s1 + s2 + s3;
    float Ra = s1*s3/tot, Rb = s2*s3/tot, Rc = s1*s2/tot;
    float Za = Ra, Zc = Rc + R4;
    float b1 = R3*Rb*C7, b0 = R3;
    float a1 = C7*(Rb*(Za+Zc) + Za*Zc), a0 = Za + Zc;
    float K = 2.f*fsInt, d = a0 + a1*K;
    return { (b0 + b1*K)/d, (b0 - b1*K)/d, (a0 - a1*K)/d };
}

// One-pole DC blocker (~8 Hz default).
struct DcBlocker {
    float state = 0.f, a = 0.001f;
    void setSampleRate(float fs) { a = 1.f - std::exp(-2.f*float(M_PI)*8.f/fs); }
    void reset() { state = 0.f; }
    float process(float x) { float y = x - state; state += a*y; return y; }
};

// 31-tap FIR halfband pair (taps generated by tests/yellowjacket_ref, Task 1).
// kHalfbandTaps: replace with the fitted_constants.md array.
inline constexpr int kHbN = 31;
inline constexpr float kHalfbandTaps[kHbN] = { /* Task 1 values */ };
struct HalfbandUp {
    float hist[kHbN] = {};
    void process(float in, float* out2);   // zero-stuff + filter, gain 2 on taps
};
struct HalfbandDown {
    float hist[kHbN] = {};
    float process(float in0, float in1);   // filter + keep every 2nd
};

// Soft rail clamp: identity until within 0.3 V of a rail, tanh knee beyond.
inline float railClamp(float v, float vHi, float vLo) {
    constexpr float w = 0.3f;
    float hi = vHi - w, lo = -vLo + w;
    if (v > hi) return hi + w*tanhApprox((v - hi)/w);
    if (v < lo) return lo - w*tanhApprox((lo - v)/w);
    return v;
}

} // namespace wasp
```

Implement `HalfbandUp/HalfbandDown` fully (simple shift-register FIR; exploit zero taps if convenient but correctness first). Insert the Task 1 tap values.

- [ ] **Step 4: Run tests to green**: `cd tests && ./run.sh` — all yellowjacket assertions pass, existing suites still pass.
- [ ] **Step 5: Commit** — `git add src/yellowjacket tests && git commit -m "Yellowjacket: DSP utility header with tests"`.

---

### Task 3: `WaspFilter` core + behavioral tests

**Files:**
- Create: `src/yellowjacket/WaspFilter.hpp`
- Test: `tests/yellowjacket/test_wasp_filter.cpp`

**Interfaces:**
- Consumes: everything in `wasp_dsp_utils.hpp` (Task 2), constants from `fitted_constants.md` (Task 1).
- Produces:

```cpp
namespace wasp {
struct ModeConfig {
    float c;        // OTA tanh scale (1/V)
    float kR2;      // R3/R2eff LP-feedback ratio
    float nInv;     // summing-node divider factor
    float wcComp;   // knob-true cutoff compensation (fc_int = fc * wcComp)
    float vHi, vLo; // rail headroom (V, positive numbers)
    float invA0;    // inverter open-loop gain magnitude
    float makeup;   // output makeup gain
};
constexpr ModeConfig kScreaming = { /* Task 1 fitted */ };
constexpr ModeConfig kTame      = { /* Task 1 fitted */ };

class WaspFilter {
public:
    struct Out { float lp, bp, hp; };
    void setSampleRate(float fsInternal);       // oversampled rate
    void setMode(const ModeConfig& m);          // switch resets nothing; states carry
    void reset();
    bool stateFinite() const;
    // g = tan(pi*fcInt/fsInt) (caller prewarps+slews), h1 = computeH1(rho, fsInt)
    // kC2 = R3*C2*2*pi*fcInt, highAcc = add 2 Newton iterations
    Out process(float inVolts, float g, const H1Coeffs& h1, float kC2, bool highAcc);
};
}
```

- [ ] **Step 1: Write failing behavioral tests** (`test_wasp_filter.cpp`), driving `WaspFilter` directly at fsInt = 192000 (standing in for 48k×4), helpers for sine runs. Assertions (tolerances deliberately loose — cross-implementation):
  - Small-signal LP passband gain at fc=1 kHz, ρ=0.3, probe 250 Hz, amp 0.05 V: within ±1.5 dB of golden.json `lp_db_screaming_fc1000_f250`.
  - LP rolloff: gain at 3·fc at least 9 dB below passband (12 dB/oct minus resonance shelf slack).
  - Resonance monotone: peak gain at fc for ρ ∈ {0, 0.5, 0.9} strictly increasing (compare to golden values ±2 dB).
  - Screaming self-osc: kick with 0.1 s burst at ρ=1, run 1 s at fc=1 kHz; tail RMS·√2 within [max(0.5·golden, 0.5), 1.5·golden] volts; zero-crossing freq within 15% of golden freq; all outputs finite.
  - Tame ring-out: same probe, tail amplitude < 0.05 V.
  - Boundedness: 10 V square at 30 Hz, fc=20 kHz knob (g clamped), ρ=1, drive-equivalent input ×8 → all states finite, |outputs| < 15 V for 2 s.
  - HP/BP sanity: BP peaks near fc; HP passband (probe 8·fc) within ±2.5 dB of unity·makeup.
  - DC: LP output mean over last 0.5 s of a 2 V DC input < 0.05 V (blocker works).
  - `highAcc=true` vs `false`: small-signal results differ by < 0.5 dB (solver consistency).
  - Load golden.json with a tiny hand-rolled parser or hardcode the numbers into the test after reading the file (hardcoding the current golden values as constants with a comment pointing at golden.json is acceptable and dependency-free — do that).
- [ ] **Step 2: Run to verify failure** (`cd tests && ./run.sh`).
- [ ] **Step 3: Implement `WaspFilter.hpp`.** Full algorithm per `process()` call:

```cpp
// Members: float fsInt, sBP, sLP, z1, hinState, hpPrev, bpPrev, ydPrev, vgPrev;
// DcBlocker dcLp, dcBp, dcHp; const ModeConfig* mode; float hinA (22 Hz coeff).
// Constants: kD=1.3608e-4f, vD=0.045293f.

// finv + derivative (asymmetric tanh, see spec):
float finv(float vg) const {
    const auto& m = *mode;
    if (vg <= 0.f) return  m.vHi*tanhApprox(-m.invA0*vg/m.vHi);
    return               -m.vLo*tanhApprox( m.invA0*vg/m.vLo);
}
float finvSlope(float vg) const {   // d finv / d vg  (negative)
    const auto& m = *mode;
    float s = (vg <= 0.f) ? tanhXdX(-m.invA0*vg/m.vHi) : tanhXdX(m.invA0*vg/m.vLo);
    // derivative of a*tanh(b*x/a) is b*sech^2; approximate sech^2 via
    // (tanh'/x-form): use finite slope = invA0 * (1 - t*t) with t = tanh value.
    float t = (vg <= 0.f) ? tanhApprox(-m.invA0*vg/m.vHi) / 1.f : tanhApprox(m.invA0*vg/m.vLo);
    (void)s;
    return -m.invA0*(1.f - t*t);
}

Out process(float inVolts, float g, const H1Coeffs& h1, float kC2, bool highAcc) {
    const auto& m = *mode;
    // input HPF (22 Hz at fsInt)
    float hin = inVolts - hinState; hinState += hinA*hin;

    // ---- fixed-pivot linear pass ----
    float th = tanhXdX(m.c*hpPrev);          // S(hp) ≈ th*hp
    float tb = tanhXdX(m.c*bpPrev);
    float sd = 1.f + ((std::fabs(ydPrev) > 0.2f)
              ? kD*std::sinh(std::clamp(ydPrev/vD, -30.f, 30.f))/ydPrev - 0.f : 0.f);
    // fd(yd) ≈ sd*yd (secant through prev point; sd=1 when diodes off)
    float A  = finvSlope(vgPrev);            // negative
    float F0 = finv(vgPrev);
    float gth = g*th, gtb = g*tb;
    float Q = gth*(sd*h1.beta0 + gtb*m.kR2 + kC2*tb);
    float P = sd*(h1.beta0*sBP + z1) + m.kR2*(sLP + gtb*sBP) + kC2*tb*sBP + hin;
    // hp = F0 + A*((P + Q*hp + hp)/nInv - vgPrev)
    float denom = 1.f - A*(Q + 1.f)/m.nInv;
    float hp = (F0 - A*vgPrev + A*P/m.nInv)/std::max(denom, 0.05f);

    // ---- optional Newton refinement (2 iterations) ----
    if (highAcc) {
        for (int it = 0; it < 2; ++it) {
            float Sh = tanhApprox(m.c*hp)/m.c;
            float bp = sBP + g*Sh;
            float Sb = tanhApprox(m.c*bp)/m.c;
            float lp = sLP + g*Sb;
            float yd = h1.beta0*bp + z1;
            float dArg = std::clamp(yd/vD, -30.f, 30.f);
            float fd = yd + kD*std::sinh(dArg);
            float sum = hin + fd + m.kR2*lp + kC2*Sb;
            float vg  = (sum + hp)/m.nInv;
            float r   = hp - finv(vg);
            // dr/dhp: chain rule
            float dSh = tanhXdX(m.c*hp);           // dSh/dhp (approx of sech^2)
            float dbp = g*dSh;
            float dSb = tanhXdX(m.c*bp)*dbp;
            float dlp = g*dSb;
            float dyd = h1.beta0*dbp;
            float dfd = dyd*(1.f + kD*std::cosh(dArg)/vD);
            float dsum = dfd + m.kR2*dlp + kC2*dSb;
            float drdhp = 1.f - finvSlope(vg)*(dsum + 1.f)/m.nInv;
            float step = r/std::max(drdhp, 0.25f);
            step = std::clamp(step, -2.f, 2.f);
            hp -= step;
        }
    }

    // ---- commit: recompute chain at solved hp, update states ----
    float Sh = tanhApprox(m.c*hp)/m.c;
    float bp = sBP + g*Sh;
    float Sb = tanhApprox(m.c*bp)/m.c;
    float lp = sLP + g*Sb;
    float yd = h1.beta0*bp + z1;
    float sum = hin + (yd + kD*std::sinh(std::clamp(yd/vD,-30.f,30.f))) + m.kR2*lp + kC2*Sb;
    float vg  = (sum + hp)/m.nInv;

    sBP = railClamp(2.f*bp - sBP, m.vHi, m.vLo);
    sLP = railClamp(2.f*lp - sLP, m.vHi, m.vLo);
    z1  = h1.beta1*bp - h1.alpha1*yd;
    hpPrev = hp; bpPrev = bp; ydPrev = yd; vgPrev = vg;

    return { dcLp.process(lp)*m.makeup, dcBp.process(bp)*m.makeup,
             dcHp.process(hp)*m.makeup };
}
```

Notes for the implementer: the `sd` secant needs the 0/0 guard exactly as the pivot spirit demands (`sd = 1 + kD*sinh(x)/yd` with the whole second term skipped when `|yd| ≤ 0.2` — write it as a clean if/else, the ternary above is schematic). `tanhXdX` is used as a stand-in for `sech²` in slopes — it is not equal to sech², but for pivot/Newton slope purposes it is close enough and keeps everything reusing one primitive; if Newton shows poor convergence in tests, switch slopes to `1 − t²` with `t = tanhApprox(...)` (exact derivative) — that form is already used in `finvSlope`. Use `1 − t²` everywhere if in doubt; document the choice.

- [ ] **Step 4: Iterate until tests pass.** Expect tuning: if Screaming self-osc amplitude/freq misses golden, first verify `wcComp` application (fc_int = fc·wcComp both in `g` and `kC2`), then compare a 20-sample `hp` trace against `wasp_ref.py` (add a debug print mode to both). If Tame free-runs, lower `kTame.invA0` to the Task-1-retuned value.
- [ ] **Step 5: Run full lane** (`cd tests && ./run.sh`) — everything green.
- [ ] **Step 6: Commit** — `git commit -m "Yellowjacket: WaspFilter nonlinear core with behavioral tests"`.

---

### Task 4: Engine + module wiring + VCV build

**Files:**
- Create: `src/yellowjacket/engine.hpp`
- Modify: `src/Yellowjacket.cpp` (process/modulate/menu only — no widget-position or enum changes; ADD new members/methods to the module struct freely)

**Interfaces:**
- Consumes: `WaspFilter`, `wasp_dsp_utils.hpp`, `OnePoleSmoother` via `#include "mf20/dsp_utils.hpp"` (compiles because both builds add `src/` to the include path — verify; if not, use a relative `../mf20/dsp_utils.hpp` from `src/yellowjacket/`).
- Produces: `wasp::VoiceEngine` (stereo pair of oversampled channels), `wasp::EnginePool` mirroring `src/mf20/engine.hpp`.

- [ ] **Step 1: Write `engine.hpp`:**

```cpp
#pragma once
// engine.hpp — per-voice state for Yellowjacket polyphony (MF-20 pattern).
#include "WaspFilter.hpp"
#include "wasp_dsp_utils.hpp"
#include "../mf20/dsp_utils.hpp"

namespace wasp {

// One audio channel: oversampling wrapper around WaspFilter.
struct Channel {
    WaspFilter filt;
    HalfbandUp up;       // used when os >= 2
    HalfbandDown down;
    HalfbandUp up4a; HalfbandDown down4a;  // second stage for 4x
    // os: 1, 2 or 4. Returns host-rate Out.
    WaspFilter::Out process(float in, int os, float g, const H1Coeffs& h1,
                            float kC2, bool highAcc);
    void reset();
};

struct VoiceEngine {
    Channel l, r;
    OnePoleSmoother gSlew { 0.049f };
    OnePoleSmoother kC2Slew { 0.f };
    OnePoleSmoother rhoSlew { 0.f };
    OnePoleSmoother driveSlew { 1.f };
    float gTarget = 0.049f, kC2Target = 0.f, rhoTarget = 0.f, driveTarget = 1.f;
    H1Coeffs h1 {};   // recomputed each modulate() from slewed rho
    void setSampleRate(float fsInt);
    void reset();
    void sanitize();  // NaN recovery, MF-20 style
};

struct EnginePool {
    VoiceEngine engines[16];
    int activeVoices = 1;
    void setVoices(int n);       // reset voices entering active range
    void setSampleRate(float fsInt);
    void resetAll();
};

} // namespace wasp
```

Implementation detail for `Channel::process`: os==1 → single `filt.process`; os==2 → up.process(in, buf2); two filt calls; down. os==4 → cascade two halfband stages (in → 2x → 4x), 4 filt calls, then decimate twice. H1 coeffs and g are per-host-sample constants (slewed at host rate, used for all sub-samples).
NOTE: `WaspFilter::setSampleRate`/`computeH1` must receive the INTERNAL rate (host×os); when the os factor changes (menu or host rate change), call `setSampleRate` again and reset the resamplers only (filter states can carry).

- [ ] **Step 2: Wire `src/Yellowjacket.cpp`.** Replace the passthrough `process()` with the MF-20 pattern:
  - Members: `wasp::EnginePool _pool; int _modulationSteps=100,_steps=100; float _sampleRate=44100; int _osMenu=0 /*0=auto,1,2,4*/; int _osActual=4; bool _highAcc=true; float _inputTrimDb=0; float _dither=1e-9f;` plus existing `screaming`, `panelTheme`.
  - `onSampleRateChange`: recompute `_osActual` (auto: fs<=48k→4, <=96k→2, else 1; else menu value), set pool internal rate = fs·os, smoother alphas (5 ms), `_modulationSteps = fs*0.0025`.
  - `modulate()` (every ~2.5 ms): read params; per active voice: fc = exp2(knobLog2 + freqCvAtten·freqCv) clamped [1, 0.45·fsInt]; fcInt = fc·mode.wcComp; `gTarget = tan(π·fcInt/fsInt)` (clamp fcInt again vs fsInt); `kC2Target = 27e3·100e-12·2π·fcInt`; `rhoTarget = clamp(res + resCvAtten·resCv/10, 0, 1)`; `driveTarget = exp2(3·clamp(drive + driveCvAtten·driveCv/10, 0, 1)) · exp2(_inputTrimDb/6.0206·0.5)`... (implement trim as `std::pow(10, _inputTrimDb/20)` — compute at modulate rate, fold into driveTarget); blend handled per-sample below (cheap). Also `eng.sanitize()`.
  - Per-sample `processChannel(c)`: slew g/kC2/rho/drive; `h1 = wasp::computeH1(rho, fsInt)` — NO: computeH1 at modulate rate only (division-heavy); instead slew beta0/beta1/alpha1 individually (three more smoothers per voice, targets set in modulate()). Blend m = clamp(blendKnob + blendCv/10, 0, 1) read per modulate, slewed. L: `auto o = eng.l.process(inL·drive + dither, os, g, h1, kC2, _highAcc)`; outputs: LP/BP/HP = o.lp/o.bp/o.hp volts; MIX = (1−m)·o.lp + m·o.hp. R: if connected, own channel; else mirror L outputs (skip compute).
  - Character menu already exists (`screaming`); apply via `setMode` pointers in modulate(): `eng.l.filt.setMode(screaming ? wasp::kScreaming : wasp::kTame)` etc.
  - Menu additions in `appendContextMenu` after Character: Accuracy (Standard/High), Oversampling (Auto/1x/2x/4x — changing it re-runs the `onSampleRateChange` logic; call a shared `updateOversampling()`), Input trim (`createMenuItem` slider — use `rack::ui::Slider` with a small `Quantity` subclass, range ±12 dB, default 0, label "Input trim"). Persist `_highAcc`, `_osMenu`, `_inputTrimDb` in dataTo/FromJson alongside existing fields.
  - Keep the deterministic `_dither` sign-flip from MF-20 (`_dither = -_dither` per process call).
- [ ] **Step 3: Build**: `make -C vcv -j8` → clean compile.
- [ ] **Step 4: Run test lane again** (`cd tests && ./run.sh`) — still green (module file isn't in the lane, but engine.hpp gets pulled if tested; primarily a regression check).
- [ ] **Step 5: Install + smoke** per `robotboy-vcv-install` memory: `make -C vcv -j8 && cp vcv/plugin.dylib ~/Library/Application\ Support/Rack2/plugins-mac-arm64/RobotBoy/ 2>/dev/null || true` (exact dir per memory/skill `build-vcv-plugin`; if the skill script exists use it). Launching the Rack GUI is user-side; the smoke here is just build+install.
- [ ] **Step 6: Commit** — `git commit -m "Yellowjacket: wire Wasp DSP engine into module with menu options"`.

---

### Task 5: MetaModule build + headless simulator check

**Files:**
- Modify: none expected (`metamodule/CMakeLists.txt` already lists `Yellowjacket.cpp`; add `src/yellowjacket/*.hpp` include dirs only if the build fails to find them)
- Create: `tests/yellowjacket/mm-sim-notes.md` (results log)

**Interfaces:** consumes the finished module from Task 4.

- [ ] **Step 1:** Use the `vcv-to-metamodule` / `build-simulator` skills to build the `.mmplugin` and the simulator build of this plugin. Follow the skill instructions exactly (they know the SDK paths).
- [ ] **Step 2:** Headless simulator run: build a minimal patch with Yellowjacket (use the skill's patch workflow / an existing RobotBoy test patch as a template) — sine or saw into Audio L, sweep Freq via CV if the harness allows; capture LP output to WAV.
- [ ] **Step 3:** Verify: output WAV is non-silent, band-limited as expected (spot-check spectrum with Python), no NaN/silence dropouts; note simulator CPU% at 1x/2x/4x oversampling. If 4x is too hot on MM (>~60% single module), set the MM-side default via a compile-time guard (`#if defined(METAMODULE)` → auto policy caps at 2x) — MetaModule builds define METAMODULE (verify the exact macro in the SDK; `metamodule/CMakeLists.txt` or existing sources show the convention — search `grep -r METAMODULE src/`).
- [ ] **Step 4:** Log results in `tests/yellowjacket/mm-sim-notes.md`; commit — `git commit -m "Yellowjacket: MetaModule build and simulator verification notes"`.

---

### Task 6: Docs, changelog, final verification, user checklist

**Files:**
- Modify: `CHANGELOG.md` (add Yellowjacket DSP entry under Unreleased/current dev heading, matching file's existing style)
- Modify: `README.md` (Yellowjacket section: one-paragraph module description — filter behavior, Character modes, menu options)
- Create: `docs/superpowers/plans/2026-07-15-yellowjacket-user-checklist.md`

- [ ] **Step 1:** Update CHANGELOG.md and README.md (read them first; follow existing tone/format; escape literal tildes as `\~`).
- [ ] **Step 2:** Full verification pass: `cd tests && ./run.sh` (all green), `make -C vcv -j8` (clean), MetaModule build (clean). Record outputs.
- [ ] **Step 3:** Write the user checklist (GUI things only a human can do, per repo convention): VCV Rack listening tests (Tame vs Screaming character, resonance sweep at several cutoffs, drive behavior, blend notch at noon, stereo/poly, CPU meter at each oversampling setting), MetaModule on-hardware checks, and the context-menu slider feel. Include suggested patches.
- [ ] **Step 4:** Commit — `git commit -m "Yellowjacket: changelog, README, and user checklist"`.

---

## Self-review notes

- Spec coverage: model equations (T1/T3), solver two-tier (T3), oversampling adaptive+menu (T2 taps, T4 wiring), parameter mappings (T4), outputs+DC blockers (T3), character modes (T1 constants, T3/T4), menu extras incl. input trim slider (T4), Python-first verification (T1), C++ tests (T2/T3), builds+simulator (T4/T5), docs/checklist (T6). Panel untouched throughout.
- Known intentional simplifications (documented in spec): state-clamp stands in for the OTA rail-current surfaces; diode network loading ignored beyond the sinh boost; Tame reuses the A-124 H1 network values.
- Type consistency: `H1Coeffs{beta0,beta1,alpha1}`, `ModeConfig` fields, `WaspFilter::process(in, g, h1, kC2, highAcc)`, `Channel::process(in, os, g, h1, kC2, highAcc)` used consistently across Tasks 2–4.

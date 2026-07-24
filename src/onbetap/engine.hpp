#pragma once
/**
 * engine.hpp — per-voice DSP state for Onbetap polyphony (MF-20 pattern).
 *
 * OnbetapVoice: L+R filter cores plus per-sample smoothers. Cutoff smoothing
 * happens in the g (prewarp-gain) domain: modulate() computes tan/exp2/pow at
 * ~2.5 ms intervals; the audio path only slews. xPrevL/R hold the previous
 * host-rate input sample for oversampling interpolation.
 *
 * DriftWalker: deterministic OU (Ornstein–Uhlenbeck) random walk for the
 * Vintage cutoff drift, xorshift32-seeded — bit-reproducible across runs and
 * across the VCV/MetaModule builds (headless comparison relies on this).
 */

#include "OnbetapFilter.hpp"
#include "../mf20/dsp_utils.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>

struct DCBlock {
    float x1 = 0.f, y1 = 0.f;
    float process(float x, float R) {
        float y = x - x1 + R * y1;
        x1 = x;
        y1 = y;
        return y;
    }
    void reset() { x1 = y1 = 0.f; }
};

// 13-tap decimation lowpass FIR, run at the oversampled rate ahead of
// downsampling. Replaces a 2-tap boxcar average decimator, which only
// attenuates the alias band by ~3 dB at the new Nyquist (cos(pi/4)) —
// audible top-octave droop plus aliased spurs folding back into the audio
// band (measured, Task 5: -29.5 dB worst spur at 5 kHz/max drive, -2.6 dB
// droop @ 18 kHz pre-fix). Kaiser-windowed FIR (scipy firwin, 13 taps,
// cutoff 23.5 kHz, beta 1.0, at fsOs = 96 kHz for 2x oversampling), tuned
// for passband flatness rather than max stopband: the linear-interp
// upsampler ahead of it (untouched, in scope for a later task) already
// contributes ~1.4 dB of its own droop near 18-20 kHz, so the decimator's
// budget has to stay under ~0.5 dB there for the combined path to clear
// the 2 dB target (measured combined droop 0.7 dB @ 18 kHz after this
// swap — see docs/research/onbetap-worklog.md). A textbook 7-tap
// half-band was tried first and rejected: it is pinned to -6 dB at exactly
// fsOs/4 by construction, which pushed the measured 18 kHz droop to
// -3.4 dB, worse than the boxcar it replaced. push() is called once per
// oversampled substep; only the value returned on the substep that lands
// on a decimation instant is kept.
struct DecimFir13 {
    static constexpr float h[13] = {
        0.0078065f,  0.0510650f, -0.0089609f, -0.0952920f,  0.0096953f,
        0.3019243f,  0.4675237f,  0.3019243f,  0.0096953f, -0.0952920f,
       -0.0089609f,  0.0510650f,  0.0078065f
    };
    // Ring buffer instead of a shift register: push() previously moved all
    // 12 history samples every call (cpu-optimization-2026-07-24.md §4.5 —
    // the same rework Vespid's halfbands already got). head indexes the
    // most recent sample; the MAC walks the taps in the same
    // newest-to-oldest order as the shift-register version. Scalar op order
    // is identical; compiler vectorization may still reorder the reduction,
    // so outputs match to float-noise (measured ≤ 1e-6 on ±5 noise), same
    // as the Vespid halfband rework's documented ~1e-7.
    float z[13] = {0.f};
    int head = 0;
    void reset() { for (auto& v : z) v = 0.f; head = 0; }
    // Advance history without computing the output — for substeps whose
    // output the caller discards (§4.4).
    void pushHistory(float x) { head = (head == 0) ? 12 : head - 1; z[head] = x; }
    float push(float x) {
        pushHistory(x);
        float y = 0.f;
        int i = 0;
        for (int idx = head; idx < 13; ++idx, ++i) y += h[i] * z[idx];
        for (int idx = 0; i < 13; ++idx, ++i)      y += h[i] * z[idx];
        return y;
    }
};

// 9-tap stage-A decimation FIR for the 4x oversampling path, run at
// fsOs = 192 kHz (48 kHz host), decimating 192k -> 96k ahead of the same
// DecimFir13 stage the 2x path uses (96k -> 48k) — so the 4x passband
// matches the 2x path by construction, and the 2x/4x comparison isolates
// the oversampling factor (spec: 2026-07-18-onbetap-4x-decimator-design).
// Folding-band math: after both decimations the audible 0-24k band is
// polluted only by content at 72-96 kHz (folds straight in) — 48-72 kHz
// folds to 24-48 kHz, which stage B's stopband already covers. So stage A
// needs strong attenuation only above 72 kHz, and with a transition band
// a quarter of the sample rate wide, 9 taps suffice.
// scipy.signal.firwin(9, 45500, window=("kaiser", 3.25), fs=192000):
// passband dev <= 0.096 dB to 20 kHz, stopband <= -41 dB over 72-96 kHz.
// Group delay 4 samples at 192k = 1 host sample (stage B adds ~3 more).
struct DecimFir9 {
    static constexpr float h[9] = {
        -0.0042816f, -0.0436724f, 0.0182510f, 0.2921273f, 0.4751513f,
         0.2921273f,  0.0182510f, -0.0436724f, -0.0042816f
    };
    // Ring buffer, same rework as DecimFir13 above. pushHistory() matters
    // here: on the 4x path this FIR is fed every substep but its output is
    // consumed only on even substeps, so the odd-substep MACs were pure
    // waste (§4.4) — the caller now advances history only.
    float z[9] = {0.f};
    int head = 0;
    void reset() { for (auto& v : z) v = 0.f; head = 0; }
    void pushHistory(float x) { head = (head == 0) ? 8 : head - 1; z[head] = x; }
    float push(float x) {
        pushHistory(x);
        float y = 0.f;
        int i = 0;
        for (int idx = head; idx < 9; ++idx, ++i) y += h[i] * z[idx];
        for (int idx = 0; i < 9; ++idx, ++i)      y += h[i] * z[idx];
        return y;
    }
};

struct OnbetapVoice {
    // Defaults ≈ 750 Hz at 96 kHz (2× OS of 48 kHz); corrected by the first
    // modulate() within 2.5 ms.
    static constexpr float kDefaultG = 0.0245f;

    OnbetapFilter fL, fR;
    OnePoleSmoother gSlew      { kDefaultG };
    OnePoleSmoother kSlew      { 1.02f };
    OnePoleSmoother driveSlew  { 0.25f };
    OnePoleSmoother makeupSlew { 1.f };
    OnePoleSmoother pushSlew   { 1.f };
    float gTarget = kDefaultG, kTarget = 1.02f;
    float driveTarget = 0.25f, makeupTarget = 1.f;
    float pushTarget = 1.f;
    float xPrevL = 0.f, xPrevR = 0.f;
    float fRgRatio = 1.f;
    DCBlock dcL, dcR;
    // Decimation FIR state, one per tap per side (2x oversampling path only).
    DecimFir13 firLpL, firBpL, firHpL, firLpR, firBpR, firHpR;
    // Stage-A decimation FIRs (4x path only): 192k -> 96k ahead of the
    // DecimFir13s above, which serve as stage B at 96k on the 4x path.
    DecimFir9 fir4LpL, fir4BpL, fir4HpL, fir4LpR, fir4BpR, fir4HpR;

    void setAlpha(float a) {
        gSlew.setAlpha(a); kSlew.setAlpha(a);
        driveSlew.setAlpha(a); makeupSlew.setAlpha(a);
        pushSlew.setAlpha(a);
    }
    void reset() {
        fL.reset(); fR.reset();
        gSlew.reset(kDefaultG); kSlew.reset(1.02f);
        driveSlew.reset(0.25f); makeupSlew.reset(1.f);
        pushSlew.reset(1.f);
        gTarget = kDefaultG; kTarget = 1.02f;
        driveTarget = 0.25f; makeupTarget = 1.f;
        pushTarget = 1.f;
        xPrevL = xPrevR = 0.f;
        fRgRatio = 1.f;
        dcL.reset();
        dcR.reset();
        firLpL.reset(); firBpL.reset(); firHpL.reset();
        firLpR.reset(); firBpR.reset(); firHpR.reset();
        fir4LpL.reset(); fir4BpL.reset(); fir4HpL.reset();
        fir4LpR.reset(); fir4BpR.reset(); fir4HpR.reset();
    }
    // NaN recovery per modulate block: filters only (smoother inputs are
    // clamped params, always finite) — avoids a parameter sweep on recovery.
    // Also clears the DC blockers and decimation FIR state: a NaN filter
    // state is a necessary precondition for the DC blocker's own recursion
    // to go NaN (it only ever sees the filter's output), so this trigger is
    // sufficient to catch it too. Without this, a single NaN input would
    // wedge dcL/dcR's y1 at NaN forever even after the filter recovers.
    void sanitize() {
        if (!fL.stateFinite() || !fR.stateFinite()) {
            fL.reset(); fR.reset();
            dcL.reset(); dcR.reset();
            firLpL.reset(); firBpL.reset(); firHpL.reset();
            firLpR.reset(); firBpR.reset(); firHpR.reset();
            fir4LpL.reset(); fir4BpL.reset(); fir4HpL.reset();
            fir4LpR.reset(); fir4BpR.reset(); fir4HpR.reset();
        }
    }
};

struct OnbetapPool {
    OnbetapVoice voices[16];
    int activeVoices = 1;

    void setVoices(int n) {
        n = std::clamp(n, 1, 16);
        for (int i = activeVoices; i < n; i++) voices[i].reset();
        activeVoices = n;
    }
    void resetAll() { for (auto& v : voices) v.reset(); }
};

namespace onbetap {

// Fixed per-side integrator mismatch (Vintage). Deterministic by design.
constexpr float kMismatchL1 = 0.06f,  kMismatchL2 = -0.045f;
constexpr float kMismatchR1 = -0.05f, kMismatchR2 = 0.055f;

// Phase-lag damping-correction factor, a pure function of cutoff.
// Evaluated at kCLagRefFsOs — the oversampled rate the module's kCLag
// constant was calibrated at (Task 5: 2x OS, 48 kHz host) — so the
// correction, and with it kEff, self-osc onset, and top-octave damping,
// no longer depend on the oversample setting or the host rate. cutoffToG's
// own fc clamp (0.245·fsOs = 23.52 kHz here) saturates the correction
// above the calibrated range, reachable only above the 2x/48k core clamp
// (4x, or high host rates).
inline constexpr float kCLagRefFsOs = 96000.f;
inline float cutoffLagCorr(float fcHz) {
    float gr = OnbetapFilter::cutoffToG(fcHz, kCLagRefFsOs);
    return gr * gr / (1.f + gr * gr);
}

// Deterministic OU random walk. step() advances one modulate block and
// returns the current value in octaves (log2 cutoff offset). depthOct sets
// the stationary standard deviation; tau fixes the wander timescale.
struct DriftWalker {
    uint32_t rng;
    float value = 0.f;
    explicit DriftWalker(uint32_t seed) : rng(seed ? seed : 1u) {}

    float step(float dtSec, float depthOct) {
        // xorshift32 → uniform in [-1, 1]
        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
        float u = (int32_t)rng * (1.f / 2147483648.f);
        constexpr float tau = 45.f;              // seconds
        float a = dtSec / tau;
        // OU: sigma = depthOct·sqrt(2a); with uniform [-1,1] noise (var 1/3)
        // this gives stationary std ≈ depthOct/sqrt(3), not depthOct — the
        // constant was calibrated empirically against that actual std, so
        // this is a naming note only, not a bug (see worklog Task 5, 2e).
        float sigma = depthOct * std::sqrt(2.f * a);
        value += a * (0.f - value) + sigma * u;
        value = std::clamp(value, -3.f * depthOct, 3.f * depthOct);
        return value;
    }
    void reset() { value = 0.f; }
};

} // namespace onbetap

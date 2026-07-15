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

struct OnbetapVoice {
    // Defaults ≈ 750 Hz at 96 kHz (2× OS of 48 kHz); corrected by the first
    // modulate() within 2.5 ms.
    static constexpr float kDefaultG = 0.0245f;

    OnbetapFilter fL, fR;
    OnePoleSmoother gSlew      { kDefaultG };
    OnePoleSmoother kSlew      { 1.02f };
    OnePoleSmoother driveSlew  { 0.25f };
    OnePoleSmoother makeupSlew { 1.f };
    float gTarget = kDefaultG, kTarget = 1.02f;
    float driveTarget = 0.25f, makeupTarget = 1.f;
    float xPrevL = 0.f, xPrevR = 0.f;
    float fRgRatio = 1.f;
    DCBlock dcL, dcR;

    void setAlpha(float a) {
        gSlew.setAlpha(a); kSlew.setAlpha(a);
        driveSlew.setAlpha(a); makeupSlew.setAlpha(a);
    }
    void reset() {
        fL.reset(); fR.reset();
        gSlew.reset(kDefaultG); kSlew.reset(1.02f);
        driveSlew.reset(0.25f); makeupSlew.reset(1.f);
        gTarget = kDefaultG; kTarget = 1.02f;
        driveTarget = 0.25f; makeupTarget = 1.f;
        xPrevL = xPrevR = 0.f;
        fRgRatio = 1.f;
        dcL.reset();
        dcR.reset();
    }
    // NaN recovery per modulate block: filters only (smoother inputs are
    // clamped params, always finite) — avoids a parameter sweep on recovery.
    void sanitize() {
        if (!fL.stateFinite() || !fR.stateFinite()) { fL.reset(); fR.reset(); }
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
        // OU: sigma chosen so stationary std ≈ depthOct
        float sigma = depthOct * std::sqrt(2.f * a);
        value += a * (0.f - value) + sigma * u;
        value = std::clamp(value, -3.f * depthOct, 3.f * depthOct);
        return value;
    }
    void reset() { value = 0.f; }
};

} // namespace onbetap

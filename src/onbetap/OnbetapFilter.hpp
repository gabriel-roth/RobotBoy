#pragma once
/**
 * OnbetapFilter.hpp — Polivoks VCF core (Onbetap)
 *
 * Model (circuit references: Filters.md, Onbetap: Sources):
 *
 * The Polivoks filter core is two К140УД12 programmable op-amps run open-loop
 * as current-controlled integrators (internal 30 pF comp caps; GBW ∝ Iset),
 * closed into a two-integrator state-variable loop by six resistors:
 *
 *   node = Gin·x + y₂ + k·y₁        current sum into the 1 kΩ node
 *   ẏ₁  = −ωc · N(node)            stage 1, inverting
 *   ẏ₂  = +ωc · N(y₁)              stage 2, non-inverting
 *
 * Everything is referred to stage-output scale and normalised so the diff-pair
 * linear window (±2·V_T at the pins × the 47k:1k divider ≈ ±2.4 V at outputs)
 * is ±1. N is the diff-pair saturator: slew limiting IS its rate ceiling
 * (SR = ωc·window), so one tanh-shaped N reproduces both regimes. The negative
 * window is 7% smaller (class-B output asymmetry → even harmonics).
 *
 * Linearised: s² + k·ωc·s + ωc², i.e. Q = 1/k. Resonance on the hardware
 * REMOVES damping (BP fed back through the pot): k spans ≈1.02 (pot at
 * minimum — the filter is never flatter than Q≈1) down past 0 (self-osc).
 *
 * States are clamped at the output-swing rails (Wasp DAFx-2022 result: clamp
 * the STATES, not the output, or high-resonance behavior is wrong):
 *   Hard (factory/Erica): asymmetric hard clamp at +4.4/−4.1.
 *   Soft (diode-clamp mod, Elta "soft"): saturating knee 3.4→4.0.
 *   Measured: both self-oscillate square-ish with
 *     nearly identical crest (hard 1.044, soft 1.052); Soft sits slightly
 *     higher in pitch. The audible difference is mainly pitch and behavior
 *     near the onset of oscillation, not waveshape.
 *
 * Discretization: TPT trapezoidal integrators; the two embedded saturators are
 * handled by secant-gain linearisation (N(v) ≈ n·v, n = N(v*)/v*), two fixed
 * passes (predict from states, refine at solved mids). With per-stage gains
 * g₁ = g·(1+m₁), g₂ = g·(1+m₂) (integrator mismatch, vintage mode):
 *
 *   y₁ = (s₁ − g₁n₁(Gin·x + off + s₂)) / (1 + g₁n₁(k + g₂n₂))
 *   y₂ = s₂ + g₂n₂·y₁
 *   HP = −N(node) (the saturated node signal = ẏ₁/ωc — physical, gritty)
 *   state update s = 2·mid − s, then rail clamp.
 *
 * Denominator guard: cutoffToG clamps fc to 0.245·fsOs (0.49·Nyquist), so
 * g ≤ tan(0.245π) ≈ 0.97; with kEff ≥ −0.31 (module clamps) and n ∈ (0,1],
 * D stays ≥ ~0.47; the 0.05 floor below is cheap insurance regardless.
 *
 * off is a DC current injected at the node (per-unit offsets, ×48 noise gain;
 * vintage mode scales it with cutoff → sweep thump).
 *
 * Deep-overdrive guards (2026-07-18, see the overdrive-stability spec):
 * at extreme input drive the huge node swing swamps n1, collapsing the
 * loop's damping — the resonant mode comes unhooked and rings at ~cutoff
 * (a subsonic burst that swallows the note when cutoff is low), and the
 * asymmetric rectification DC can drag both states to the negative rail
 * where a hard-flat saturator would pass nothing (absolute silence). Two
 * guards, both inert in normal operation:
 *   - drive-gated state leak: pole kLeakPoleHz at full gate, gate =
 *     min(|xin|/8, 1) — zero-input self-oscillation is untouched at every
 *     cutoff because the gate keys on input depth, not frequency;
 *   - sat() keeps a kSatLeak residual slope beyond the former hard clamp,
 *     so small-signal gain never reaches exactly zero (a real diff pair
 *     chokes asymptotically, never completely). tanhish itself stays exact
 *     (the output VCA's 9 V bound depends on it).
 *
 * Signal convention: normalised core units, window = ±1 ≈ 2.4 V physical.
 * The module wrapper owns volts↔core scaling, drive, oversampling, taps.
 */

#include <cmath>
#include <algorithm>

class OnbetapFilter {
public:
    enum class Limit { Hard, Soft };
    struct Out { float lp, bp, hp; };

    static constexpr float kRailPos  = 4.4f;   // +10.5 V / 2.4 V window
    static constexpr float kRailNeg  = 4.1f;   // asymmetric per-unit clip
    static constexpr float kSoftKnee = 3.4f;   // soft-limit knee start
    static constexpr float kSoftMax  = 4.0f;   // soft-limit asymptote
    static constexpr float kAsymNeg  = 0.93f;  // negative window ratio
    static constexpr float kGin      = 1.2f;   // Erica 47k/39k input ratio
    static constexpr float kLeakPoleHz = 15.f; // drive-gated state-leak pole
    // Below kLeakCornerHz the wrapper boosts the leak pole by corner/fc
    // (capped at kLeakBoostMax) — stronger damping at the lowest cutoffs,
    // where residual deep-drive dropouts were still audible (user report,
    // 2026-07-18). No effect at or above the corner.
    static constexpr float kLeakCornerHz  = 80.f;
    static constexpr float kLeakBoostMax  = 4.f;
    static constexpr float kSatLeak    = 0.05f;// sat() slope beyond the clamp
    // Reciprocals: the MetaModule SDK compiles without -ffast-math, so every
    // division by a constant is a real Cortex-A7 VDIV.F32 unless precomputed
    // (cpu-optimization-2026-07-24.md §4.3).
    static constexpr float kInvAsymNeg  = 1.f / kAsymNeg;
    static constexpr float kInvSoftSpan = 1.f / (kSoftMax - kSoftKnee);

    static float cutoffToG(float fcHz, float fsOs) {
        float fc = std::clamp(fcHz, 0.5f, fsOs * 0.245f);
        return std::tan(kPi * fc / fsOs);
    }

    // Rational tanh approximation, exact ±1 at |x| = 3, hard-limited beyond.
    static float tanhish(float x) {
        x = std::clamp(x, -3.f, 3.f);
        float x2 = x * x;
        return x * (27.f + x2) / (27.f + 9.f * x2);
    }

    // Diff-pair saturator, asymmetric window (+1 / −kAsymNeg), with a
    // kSatLeak residual slope beyond the tanhish clamp (|v| > 3·window) so
    // small-signal gain never reaches exactly zero (rail-pin guard).
    static float sat(float v) {
        float lim  = (v >= 0.f) ? 3.f : -3.f * kAsymNeg;
        float core = (v >= 0.f) ? tanhish(v) : kAsymNeg * tanhish(v * kInvAsymNeg);
        float over = (v >= 0.f) ? std::max(v - lim, 0.f) : std::min(v - lim, 0.f);
        return core + kSatLeak * over;
    }

    // Secant gain sat(v)/v — the linearised per-sample gain. →1 as v→0.
    // Closed per-region forms (cpu-optimization-2026-07-24.md §4.1): the
    // division by v cancels inside the tanhish window, so each call is one
    // divide instead of two. The rational regions are well-behaved at 0
    // (→ 1 exactly), so the old 1e-4 guard is unnecessary. In the leak
    // regions `core` is a constant: exactly 1.0 at the positive clamp
    // (3·36/108) and −kAsymNeg at the negative one.
    static float satGain(float v) {
        if (v >= 0.f) {
            if (v <= 3.f) { float v2 = v * v; return (27.f + v2) / (27.f + 9.f * v2); }
            return (1.f + kSatLeak * (v - 3.f)) / v;
        }
        float w = v * kInvAsymNeg;
        if (w >= -3.f) { float w2 = w * w; return (27.f + w2) / (27.f + 9.f * w2); }
        return (-kAsymNeg + kSatLeak * (v + 3.f * kAsymNeg)) / v;
    }

    void setLimit(Limit m) { limit = m; }
    void setMismatch(float m1, float m2) { gs1 = 1.f + m1; gs2 = 1.f + m2; }
    void setOffset(float o) { off = o; }
    // Per-substep state-leak coefficient at full gate: 2π·kLeakPoleHz/fsOs.
    void setLeak(float l) { leak = l; }
    void reset() { s1 = s2 = 0.f; }
    bool stateFinite() const { return std::isfinite(s1) && std::isfinite(s2); }

    // needHp = false skips the HP tap's sat() evaluation and returns hp = 0.
    // hp is not part of the state update, so this is exactly equivalent when
    // the caller discards it (LP/BP modes at 1x — see §4.6/§7.2; the module
    // only passes false when no crossfade endpoint reads hp).
    Out processG(float in, float g, float kEff, bool needHp = true) {
        float g1 = g * gs1, g2 = g * gs2;
        float xin = kGin * in + off;

        // Pass 1: secant gains predicted from current states.
        float n1 = satGain(xin + s2 + kEff * s1);
        float n2 = satGain(s1);
        float y1 = solveY1(xin, g1, g2, kEff, n1, n2);
        float y2 = s2 + g2 * n2 * y1;
        // Pass 2: refine at the solved mids.
        n1 = satGain(xin + y2 + kEff * y1);
        n2 = satGain(y1);
        y1 = solveY1(xin, g1, g2, kEff, n1, n2);
        y2 = s2 + g2 * n2 * y1;

        float hp = needHp ? -sat(xin + y2 + kEff * y1) : 0.f;

        s1 = 2.f * y1 - s1;
        s2 = 2.f * y2 - s2;
        if (leak > 0.f) {
            // Drive-gated: full leak only when the input swing is far beyond
            // the diff-pair window; zero input (self-osc) → zero leak.
            float gate = std::min(std::fabs(xin) * 0.125f, 1.f);
            float l = 1.f - leak * gate;
            s1 *= l; s2 *= l;
        }
        clampStates();

        return { y2, y1, hp };
    }

private:
    static constexpr float kPi = 3.14159265358979f;

    float s1 = 0.f, s2 = 0.f;      // BP, LP states
    float gs1 = 1.f, gs2 = 1.f;    // per-stage g scale (mismatch)
    float off = 0.f;               // node DC offset
    float leak = 0.f;              // gated state leak per substep (0 = off)
    Limit limit = Limit::Hard;

    float solveY1(float xin, float g1, float g2, float kEff,
                  float n1, float n2) const {
        float D = 1.f + g1 * n1 * (kEff + g2 * n2);
        D = std::max(D, 0.05f);
        return (s1 - g1 * n1 * (xin + s2)) / D;
    }

    static float softLimitOne(float v) {
        float av = std::fabs(v);
        if (av <= kSoftKnee) return v;
        float t = (av - kSoftKnee) * kInvSoftSpan;
        float lim = kSoftKnee + (kSoftMax - kSoftKnee) * t / (1.f + t);
        return v > 0.f ? lim : -lim;
    }

    void clampStates() {
        if (limit == Limit::Hard) {
            s1 = std::clamp(s1, -kRailNeg, kRailPos);
            s2 = std::clamp(s2, -kRailNeg, kRailPos);
        } else {
            s1 = softLimitOne(s1);
            s2 = softLimitOne(s2);
        }
    }
};

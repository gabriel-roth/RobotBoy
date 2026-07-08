#pragma once
/**
 * MF20Filter.hpp — Korg MS-20 filter emulation
 *
 * Implements two modes:
 *   OTA (Mode::OTA) — later LM13600-based revision of the MS-20.
 *   K35 (Mode::K35) — original Korg35-based MS-20.
 *
 * ── OTA mode ──────────────────────────────────────────────────────────────────
 *
 * OTA analog topology (Stinchcombe Fig 4):
 *   Transfer function: H(s) = −k₁ / (s²/ωc² + (2 − k₁k₂)·s/ωc + 1)
 *   Self-oscillation threshold: k₁k₂ = 2  (damping term → 0)
 *
 * State equations (reproduce the Stinchcombe TF exactly):
 *   ẋ₁ = ωc·(in − 2x₁ + diodeClip(k·x₁) − x₂)   x₁ = BP node
 *   ẋ₂ = ωc·x₁                                     x₂ = LP output
 *
 * TPT discretization:
 *   g    = tan(π·fc/fs)          bilinear prewarping gain
 *   rhs  = s₁ + g·(in − s₂)    state-only contribution (known before solve)
 *
 *   Solve: x₁_mid·(1+g)² = rhs + g·diodeClip(k·x₁_mid)
 *
 *   Piecewise-linear clip gives closed-form per region (no iteration):
 *     Region 1  |k·x₁| ≤ T  → x₁_mid = rhs / ((1+g)² − g·k)
 *     Region 2  k·x₁ > T   → x₁_mid = (rhs + (1−satSlope)·g·T) / ((1+g)² − satSlope·g·k)
 *     Region 3  k·x₁ < −T  → x₁_mid = (rhs − (1−satSlope)·g·T) / ((1+g)² − satSlope·g·k)
 *
 *   HP = in − 2·x₁_mid + clip(k·x₁_mid) − x₂_mid
 *   Self-oscillation at k = 2.
 *
 * ── K35 mode ──────────────────────────────────────────────────────────────────
 *
 * K35 analog topology (Korg35 chip, original MS-20):
 *   Component values: C1 = 3.3 nF, C2 = 1 nF  (3:1 ratio → damping constant 8/3)
 *   Self-oscillation threshold: damping → 0 at k = 8/3 ≈ 2.667
 *
 * Key structural difference from OTA:
 *   Nonlinearity location:  BOTH the forward path (clips the INPUT, drive-scaled)
 *                           AND the resonance loop (clips the feedback k·x₁, fixed
 *                           threshold/slope) — the loop clip is what keeps the
 *                           filter bounded once resTaper() pushes k past 8/3.
 *   Damping coefficient:    8/3 − k in the linear region (vs 2 − k in OTA)
 *
 * State equations:
 *   ẋ₁ = ωc·(clip(in) − (8/3)·x₁ + fbClip(k·x₁) − x₂)    x₁ = BP node
 *   ẋ₂ = ωc·x₁                                              x₂ = LP output
 *
 * Forward-path clip: input pre-gained by √drive, clipped at normalised thresholds
 *   T_pos=1.0, T_neg=(1−kK35Asymmetry)=0.85. In the linear region clip_in = in×√drive
 *   (amplified); above threshold clip_in approaches the normalised clip level. More drive
 *   → louder and more saturated → wilder. Asymmetric thresholds create even-order
 *   harmonics (Stinchcombe §4).
 *
 * Resonance-loop clip: the Korg35 loop transistor clips k·x₁ at a FIXED normalised
 *   threshold (1.0) and slope (0.25) — unaffected by drive, which only shapes the
 *   forward-path clip. Without this, resTaper() (max 1.025) drives k up to ≈2.733,
 *   past the k=8/3 linear self-oscillation threshold, and a purely linear loop
 *   diverges to NaN. In the linear region (|k·x₁| ≤ 1) the solve is algebraically
 *   identical to the previous formulation: base − g·k = (1+g)² − g·(k − 2/3).
 *
 * TPT discretization — closed-form, region-wise (no iteration):
 *   rhs    = s₁ + g·(clip_in − s₂)
 *   base   = (1+g)² + (2/3)·g
 *   D1     = base − g·k                                [linear region, |k·x₁| ≤ 1]
 *   x₁_mid = rhs / D1,  fb = k·x₁_mid                   [if D1 > 0 and |fb| ≤ 1]
 *   D2     = base − 0.25·g·k                            [saturated region, always > 0]
 *   x₁_mid = (rhs ± (1−0.25)·g·1) / D2                  [sign from rhs]
 *   x₂_mid = s₂ + g·x₁_mid
 *   HP     = clip_in − (8/3)·x₁_mid + fb − x₂_mid
 *
 *   For k > 8/3, D1 ≤ 0 over a band of cutoffs (the linear region has no valid
 *   solution there — a bistable analog regime); the code falls back directly to
 *   the saturated-region solve, with branch sign taken from rhs.
 *
 *   Resonance scaling: k = res × (8/3)  so res = 1.0 is always the oscillation onset.
 *
 * ── Common ────────────────────────────────────────────────────────────────────
 *
 * Clipping: piecewise-linear approximation of a diode/transistor nonlinearity.
 *   Default: T = 1.0, satSlope = 0.25; scale with 1/sqrt(drive).
 *
 * State update (both modes): s = 2·mid − s
 *
 * Outputs:
 *   lp = x₂_mid                 12 dB/oct lowpass
 *   bp = x₁_mid                 6 dB/oct bandpass node
 *   hp = (mode-specific)        12 dB/oct highpass
 *
 * Signal level convention:
 *   Internally normalised to ±1 V.  For VCV Rack (±5 V audio) use processVCV().
 *
 * References:
 *   [H] Huovilainen (2010) MSc thesis §4.2.3, §5.3.1
 *   [S] Stinchcombe (2006) "A Study of the Korg MS10 & MS20 Filters"
 */

#include <cmath>
#include <algorithm>

class MF20Filter {
public:
    enum class Mode { OTA, K35 };
    struct Out { float lp, bp, hp; };

    MF20Filter() = default;

    void setSampleRate(float fs) { sampleRate = fs; }
    void setMode(Mode m) { mode = m; }

    void reset() { s1 = s2 = 0.f; }

    /** False once any non-finite value has entered the TPT state (the
        s = 2·mid − s update propagates NaN/inf forever). */
    bool stateFinite() const { return std::isfinite(s1) && std::isfinite(s2); }

    /** Scale diode character with drive: higher drive = lower threshold + steeper saturation. */
    void setDriveCharacter(float drive) {
        float s       = 1.f / std::sqrt(drive);
        clipThreshold = s;
        satSlope      = 0.25f * s;
    }

    /**
     * Process one sample.
     * @param in        Normalised input (±1 V)
     * @param cutoffHz  Cutoff frequency [1, Nyquist)
     * @param res       Resonance [0, 1]; 1.0 = onset of self-oscillation
     */
    Out process(float in, float cutoffHz, float res) {
        if (mode == Mode::K35)
            return processK35(in, cutoffHz, res);
        return processOTA(in, cutoffHz, res);
    }

    Out processVCV(float inVolts, float cutoffHz, float res) {
        auto [lp, bp, hp] = process(inVolts * kVCVScale, cutoffHz, res);
        return { lp / kVCVScale, bp / kVCVScale, hp / kVCVScale };
    }

private:
    static constexpr float kPi           = 3.14159265358979f;
    static constexpr float kVCVScale     = 0.2f;
    static constexpr float kK35Asymmetry = 0.15f;  // negative clips 15% harder than positive

    float sampleRate    = 44100.f;
    float s1            = 0.f;    // BP integrator state
    float s2            = 0.f;    // LP integrator state
    float clipThreshold = 1.f;    // diode clip threshold (normalised ±1 V)
    float satSlope      = 0.25f;  // gain in saturation region
    Mode  mode          = Mode::OTA;

    Out processOTA(float in, float cutoffHz, float res) {
        float fc = std::clamp(cutoffHz, 1.f, sampleRate * 0.498f);
        float g  = std::tan(kPi * fc / sampleRate);
        float k  = res * 2.f;

        // Right-hand side: contribution from current states only.
        float rhs = s1 + g * (in - s2);

        // Solve x1_mid from: x1_mid·(1+g)² = rhs + g·diodeClip(k·x1_mid)
        // Try region 1 (no clipping) first.
        float D1    = (1.f + g) * (1.f + g) - g * k;
        float x1_r1 = rhs / D1;

        float x1_mid;
        float clip_val;
        if (std::fabs(k * x1_r1) <= clipThreshold) {
            x1_mid   = x1_r1;
            clip_val = k * x1_mid;
        } else {
            // Regions 2 / 3: clip active, effective gain → satSlope·k
            float D2     = (1.f + g) * (1.f + g) - satSlope * g * k;
            float knee   = (1.f - satSlope) * g * clipThreshold;
            float offset = (x1_r1 > 0.f) ? knee : -knee;
            x1_mid = (rhs + offset) / D2;
            float sign = (x1_r1 > 0.f) ? 1.f : -1.f;
            clip_val = satSlope * k * x1_mid + sign * (1.f - satSlope) * clipThreshold;
        }

        float x2_mid = s2 + g * x1_mid;
        float hp_out = in - 2.f * x1_mid + clip_val - x2_mid;

        // TPT state update: s_new = 2·mid − s
        s1 = 2.f * x1_mid - s1;
        s2 = 2.f * x2_mid - s2;

        return { x2_mid, x1_mid, hp_out };
    }

    Out processK35(float in, float cutoffHz, float res) {
        float fc = std::clamp(cutoffHz, 1.f, sampleRate * 0.498f);
        float g  = std::tan(kPi * fc / sampleRate);
        float k  = res * (8.f / 3.f);

        // Forward-path nonlinearity: pre-gain input by 1/clipThreshold (= √drive),
        // then clip at normalised thresholds (T_pos=1.0, T_neg=0.85).
        // Linear region: clip_in = in_n = in * √drive  (amplified).
        // Saturation: clip_in ≈ normalised clip level  (limited, independent of drive).
        // Net effect: more drive → louder and more saturated → wilder.
        float clip_in;
        {
            constexpr float kSatSlope = 0.25f;
            float in_n = in / clipThreshold;      // pre-gain: = in * √drive

            if (in_n >= 0.f) {
                clip_in = (in_n <= 1.f) ? in_n
                        : kSatSlope * in_n + (1.f - kSatSlope) * 1.f;
            } else {
                constexpr float T_neg_n = 1.f - kK35Asymmetry;  // 0.85
                clip_in = (in_n >= -T_neg_n) ? in_n
                        : kSatSlope * in_n - (1.f - kSatSlope) * T_neg_n;
            }
        }

        // Resonance loop with saturating feedback:
        //   ẋ₁ = ωc·(clip_in − (8/3)·x₁ + fbClip(k·x₁) − x₂)
        // The Korg35 loop transistor clips, which bounds the resonance even
        // when the module's resTaper() pushes k past the linear-oscillation
        // threshold 8/3 (max k = 1.025 × 8/3 ≈ 2.733). Fixed normalised
        // threshold/slope: drive shapes the input clip only.
        // Linear region (|k·x₁| ≤ 1) is algebraically identical to the
        // previous formulation: base − g·k = (1+g)² − g·(k − 2/3).
        constexpr float kFbThreshold = 1.f;
        constexpr float kFbSlope     = 0.25f;

        float rhs  = s1 + g * (clip_in - s2);
        float base = (1.f + g) * (1.f + g) + (2.f / 3.f) * g;
        float D1   = base - g * k;

        // For k > 8/3, D1 ≤ 0 over a band of g (fc ≈ 11.6–16.6 kHz at 48 kHz
        // when k = 2.733): the linear region has no valid solution there
        // (bistable analog regime), so solve the saturated region, taking the
        // branch sign from the drive term rhs. When D1 > 0 the region-1 trial
        // classifies exactly (implicit LHS is monotone), as in processOTA.
        bool linear = false;
        float x1_mid = 0.f, fb_val = 0.f;
        if (D1 > 0.f) {
            float x1_try = rhs / D1;
            if (std::fabs(k * x1_try) <= kFbThreshold) {
                x1_mid = x1_try;
                fb_val = k * x1_mid;
                linear = true;
            }
        }
        if (!linear) {
            float D2   = base - kFbSlope * g * k;
            float sign = (rhs > 0.f) ? 1.f : -1.f;
            float knee = (1.f - kFbSlope) * g * kFbThreshold;
            x1_mid = (rhs + sign * knee) / D2;
            fb_val = kFbSlope * k * x1_mid + sign * (1.f - kFbSlope) * kFbThreshold;
        }

        float x2_mid = s2 + g * x1_mid;
        // HP from state equation: clip(in) − (8/3)·x₁ + fbClip(k·x₁) − x₂
        float hp_out = clip_in - (8.f / 3.f) * x1_mid + fb_val - x2_mid;

        s1 = 2.f * x1_mid - s1;
        s2 = 2.f * x2_mid - s2;

        return { x2_mid, x1_mid, hp_out };
    }
};

#pragma once
#include <cmath>
#include <algorithm>
#include "../../include/retours_delay_dsp/types.h"
#include "util/dsp_utils.h"   // particules_dsp::Crossfade, cosine table

namespace retours_delay_dsp {

// Tempo-synced amplitude envelope on the wet path. Phase period = base
// delay time; phase advances per sample, resyncs to 0 on clock ticks.
//
// Gain(ph) morph across shape_ in [0,1] (ph in [0,1) is phase within the
// period):
//   s in (0, 1/3]: t = s*3; a trapezoid gate (rising edge, plateau, falling
//     edge, then closed for the remainder of the period) with plateau width
//     `duty = 0.9 - 0.4*t` (shrinking from 0.9 to 0.5), 5 ms linear edges,
//     crossfaded from flat (1.0, matching shape=0) to the gate as t: 0->1.
//   s in (1/3, 2/3]: t = (s-1/3)*3; crossfades the duty=0.5 gate to a Hann
//     window (via the cosine table, no per-sample std::cos).
//   s in (2/3, 1]: t = (s-2/3)*3; crossfades the Hann window to ramp^2
//     (slow attack across the period, hard reset at phase wrap).
// The three segments are continuous at s=1/3 and s=2/3 (both endpoints
// evaluate to the same duty=0.5 gate / Hann window respectively); segment 3
// is intentionally discontinuous at the phase wrap (ramp resets to 0).
class RepeatEnvelope {
public:
    void Init(float sample_rate) { sample_rate_ = sample_rate; phase_ = 0.f; }
    // block rate:
    void SetPeriodSamples(float period) { inc_ = period > 1.f ? 1.f / period : 1.f; }
    void SetShape(float shape) { shape_ = shape; }   // 0..1
    void SyncPhase() { phase_ = 0.f; }
    // per sample: returns gain 0..1
    float Next() {
        phase_ += inc_; if (phase_ >= 1.f) phase_ -= 1.f;
        if (shape_ <= 0.001f) return 1.f;
        return Gain(phase_);
    }

private:
    static constexpr float kThird = 1.f / 3.f;
    static constexpr float kTwoThird = 2.f / 3.f;

    // Trapezoid gate: rising ramp over [0,e), plateau (=1) over
    // [e, e+duty), falling ramp over [e+duty, 2e+duty), 0 for the rest of
    // the period. `e` is clamped so the active window (duty + 2*e) never
    // exceeds the period, degrading gracefully at very short periods
    // (down to a plain on/off gate when e collapses to 0).
    static float Gate(float ph, float duty, float edge) {
        float e = std::max(0.f, edge);
        float max_e = std::max(0.f, (1.f - duty) * 0.5f);
        e = std::min(e, max_e);
        if (e <= 1e-6f) return ph < duty ? 1.f : 0.f;
        if (ph < e) return ph / e;
        if (ph < e + duty) return 1.f;
        if (ph < 2.f * e + duty) return (2.f * e + duty - ph) / e;
        return 0.f;
    }

    float Gain(float ph) const {
        // 5 ms edge, expressed as a fraction of the period.
        float edge = 0.005f * sample_rate_ * inc_;
        if (shape_ <= kThird) {
            float t = shape_ * 3.f;
            float duty = 0.9f - 0.4f * t;
            float gate = Gate(ph, duty, edge);
            return particules_dsp::Crossfade(1.f, gate, t);
        } else if (shape_ <= kTwoThird) {
            float t = (shape_ - kThird) * 3.f;
            float gate = Gate(ph, 0.5f, edge);
            float hann = 0.5f - 0.5f * particules_dsp::CosLookup(ph);
            return particules_dsp::Crossfade(gate, hann, t);
        } else {
            float t = (shape_ - kTwoThird) * 3.f;
            float hann = 0.5f - 0.5f * particules_dsp::CosLookup(ph);
            float ramp2 = ph * ph;
            return particules_dsp::Crossfade(hann, ramp2, t);
        }
    }

    float sample_rate_ = 48000.f, phase_ = 0.f, inc_ = 0.f, shape_ = 0.f;
};

} // namespace retours_delay_dsp

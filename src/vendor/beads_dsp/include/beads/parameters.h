#pragma once

#include "types.h"

namespace beads {

struct BeadsParameters {
    // Primary grain controls (0-1 normalized, except size which is -1..1)
    float time = 0.5f;
    float size = -0.2f;  // -1..1; boundary at -0.2 (11 o'clock): abs_size=0 → 30ms, forward.
                         // CW (> -0.2) = forward, CCW (< -0.2) = reverse; each zone: 30ms..max.
    float shape = 0.5f;
    float pitch = 0.0f;       // Semitones, -24 to +24
    float density = 0.5f;
    float density_cv = 0.0f;
    bool  density_cv_connected = false;

    // Mix controls (0-1)
    float feedback = 0.0f;
    float dry_wet = 0.5f;
    float reverb = 0.0f;

    // Attenurandomizer knobs (-1 to +1, 0=noon)
    float time_ar = 0.0f;
    float size_ar = 0.0f;
    float shape_ar = 0.0f;
    float pitch_ar = 0.0f;

    // CV inputs (volts, 0 if unpatched)
    float time_cv = 0.0f;
    float size_cv = 0.0f;
    float shape_cv = 0.0f;
    float pitch_cv = 0.0f;
    bool time_cv_connected = false;
    bool size_cv_connected = false;
    bool shape_cv_connected = false;
    bool pitch_cv_connected = false;

    // Slow random LFOs for delay/wavetable mode — replaces per-grain random noise.
    // Set by host; range [-1, 1]. The peaked vs. uniform distribution character is
    // baked in at target-generation time; ApplyLfoAr applies the formula.
    float time_lfo  = 0.0f;
    float size_lfo  = 0.0f;
    float shape_lfo = 0.0f;
    float pitch_lfo = 0.0f;  // Normalized [-1,1]; same scale as grain-mode random source

    // Gate/clock/freeze
    bool gate = false;
    bool freeze = false;
    bool seed_connected = false;         // true when SEED jack is patched
    float clock_interval_samples = 0.0f; // measured clock period; 0 = unclocked
    TriggerMode trigger_mode = TriggerMode::kLatched;
    QualityMode quality_mode = QualityMode::kHiFi;

    // MIDI note control (set by host when trigger_mode == kMidi)
    float midi_pitch_offset = 0.0f;   // Semitones offset from MIDI note (C4=0)
    float midi_velocity_gain = 1.0f;  // 0-1 from velocity/127

    // Input config
    float manual_gain_db = NAN;  // NaN = auto-gain
    bool auto_gain = true;       // true = calibrate-and-lock auto-gain
    bool stereo_input = true;

    // Mode overrides
    bool delay_mode = false;     // true = use delay engine regardless of SIZE

    // Pitch lock quantization (applied as the final step after AR and CV)
    // 0 = off, 1 = octaves only, 2 = octaves + perfect 5ths
    int pitch_lock = 0;
};

// Quantize semitones to the nearest allowed value for pitch_lock mode.
// Applied after all modulation so it is always the last word on pitch.
inline float QuantizePitchLock(float semitones, int mode) {
    if (mode == 1) {
        return std::round(semitones / 12.f) * 12.f;
    } else if (mode == 2) {
        float base = std::floor(semitones / 12.f) * 12.f;
        float candidates[] = { base, base + 7.f, base + 12.f, base + 19.f };
        float nearest = candidates[0];
        float minDist = std::fabs(semitones - candidates[0]);
        for (float c : candidates) {
            float d = std::fabs(semitones - c);
            if (d < minDist) { minDist = d; nearest = c; }
        }
        return nearest;
    }
    return semitones;
}

// Apply AR + slow LFO modulation for delay/wavetable mode.
// Encodes the four-way Beads attenurandomizer logic:
//   CW  + CV:    base + ar * cv                  (deterministic CV attenuation)
//   CCW + CV:    base + lfo * (-ar) * |cv|        (LFO gated by CV level)
//   CW  + no CV: base + lfo * ar                  (LFO, depth scaled by ar)
//   CCW + no CV: base + lfo * (-ar)               (peaked LFO, depth scaled by ar)
inline float ApplyLfoAr(float base, float ar, float cv, bool cv_connected, float lfo) {
    if (ar == 0.0f) return base;
    if (cv_connected) {
        return ar > 0.0f
            ? base + ar * cv
            : base + lfo * (-ar) * std::fabs(cv);
    } else {
        return ar > 0.0f
            ? base + lfo * ar
            : base + lfo * (-ar);
    }
}

} // namespace beads

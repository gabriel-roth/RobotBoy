#pragma once

#include "../../include/particules_dsp/types.h"
#include "../buffer/recording_buffer.h"
#include "../util/cosine_table.h"
#include "../util/dsp_utils.h"

#include <cmath>

namespace particules_dsp {

class Grain {
public:
    struct GrainParameters {
        float position;         // Buffer position in frames (where to read)
        float size;             // Duration in samples
        float pitch_ratio;      // Playback rate (1.0 = normal, 2.0 = octave up)
        float shape;            // 0-1 envelope shape
        float pan;              // -1 to +1 stereo pan
        int pre_delay;          // Sub-block start offset in samples
        float gain = 1.0f;      // Amplitude gain (e.g. from MIDI velocity)
    };

    void Init();

    // Trigger a new grain
    void Start(const GrainParameters& params);

    // Mark grain for zero-crossing kill (replaces instant fade-out)
    void StartPendingKill();

    // Process one sample, reading from the recording buffer.
    // Returns true if grain is still active.
    // INLINED for the grain-major hot loop — must stay in the header.
    inline bool Process(const RecordingBuffer& buffer,
                        float buf_size, float* out_l, float* out_r) {
        if (!active_) {
            *out_l = 0.0f;
            *out_r = 0.0f;
            return false;
        }

        // Pre-delay: output silence until the grain's sub-block offset is reached.
        if (pre_delay_ > 0) {
            --pre_delay_;
            *out_l = 0.0f;
            *out_r = 0.0f;
            return true;
        }

        // Check if envelope has completed before computing this sample.
        // (Checking *before* the increment avoids discarding the last
        // computed sample, which would cause a small discontinuity.)
        // Note: NaN fails all ordered comparisons, so we must also
        // explicitly check for it to avoid passing NaN into ComputeEnvelope.
        if (envelope_phase_ >= 1.0f || !std::isfinite(envelope_phase_)) {
            active_ = false;
            *out_l = 0.0f;
            *out_r = 0.0f;
            return false;
        }

        // Envelope (smoothness/slope precomputed in Start())
        float env = ComputeEnvelope(envelope_phase_);
        envelope_phase_ += envelope_increment_;

        // Read from recording buffer with Hermite interpolation.
        float sample_l, sample_r;
        buffer.ReadHermiteStereoFast(read_position_, &sample_l, &sample_r);

        // Advance read position, wrapping around buffer size.
        read_position_ += phase_increment_;
        if (buf_size > 0.0f) {
            while (read_position_ >= buf_size) read_position_ -= buf_size;
            while (read_position_ < 0.0f) read_position_ += buf_size;
        }

        // Apply envelope, gain, and panning.
        float out_l_val = sample_l * env * gain_ * pan_l_;
        float out_r_val = sample_r * env * gain_ * pan_r_;

        // Zero-crossing kill: detect sign change on mono sum
        // (handles hard-panned content better than L-only).
        if (pending_kill_) {
            if (ApplyPendingKill(out_l_val, out_r_val)) {
                *out_l = 0.0f;
                *out_r = 0.0f;
                return false;
            }
        } else {
            prev_mono_ = out_l_val + out_r_val;
        }
        *out_l = out_l_val;
        *out_r = out_r_val;

        return true;
    }

    // Process a full block. Checks each grain's own contribution for NaN
    // before accumulating, so a NaN from another grain can't kill this one.
    // buf_size_f should be static_cast<float>(buffer.size()).
    inline void ProcessBlock(const RecordingBuffer& buffer, float buf_size_f,
                             StereoFrame* output, size_t num_frames) {
        for (size_t i = 0; i < num_frames; ++i) {
            float gl = 0.0f, gr = 0.0f;
            Process(buffer, buf_size_f, &gl, &gr);
            if (!std::isfinite(gl) || !std::isfinite(gr)) {
                active_ = false;
                return;
            }
            output[i].l += gl;
            output[i].r += gr;
        }
    }

    bool active() const { return active_; }
    bool pending_kill() const { return pending_kill_; }
    float read_position() const { return read_position_; }
    float phase_increment() const { return phase_increment_; }
    int pre_delay_remaining() const { return pre_delay_; }

    // Spawn order, stamped by GrainEngine at activation. Used by the
    // kill-fallback to pick the true oldest active grain (see
    // GrainEngine::AllocateGrain) rather than an arbitrary array slot.
    uint32_t spawn_serial() const { return spawn_serial_; }
    void SetSpawnSerial(uint32_t serial) { spawn_serial_ = serial; }

private:
    bool active_ = false;
    bool pending_kill_ = false;
    uint32_t spawn_serial_ = 0;

    // Zero-crossing kill with fallback fade
    static constexpr int kZeroCrossDeadline = 32;
    static constexpr int kFallbackFadeSamples = 4;
    static constexpr float kFallbackFadeInv = 1.0f / static_cast<float>(kFallbackFadeSamples);
    int kill_deadline_ = 0;
    bool fallback_fade_ = false;
    int fallback_counter_ = 0;
    float prev_mono_ = 0.0f;  // mono sum for zero-crossing detection

    // Read position (fractional for sub-sample accuracy)
    float read_position_ = 0.0f;
    float phase_increment_ = 0.0f;

    // Envelope
    float envelope_phase_ = 0.0f;
    float envelope_increment_ = 0.0f;

    // Precomputed envelope parameters (derived from shape in Start())
    // slope_: peak position (0.05=early/decay, 0.5=center/bell, 0.9=late/reversed)
    // smoothness_: trapezoid ↔ raised-cosine blend
    // steepness_: rectangularity (high values clip triangle to rectangle)
    float smoothness_ = 1.0f;
    float slope_ = 0.5f;
    float steepness_ = 1.0f;
    float inv_slope_ = 2.0f;         // 1.0f / slope_
    float inv_one_minus_slope_ = 2.0f; // 1.0f / (1.0f - slope_)

    // Amplitude gain (MIDI velocity etc.)
    float gain_ = 1.0f;

    // Stereo
    float pan_l_ = 1.0f;
    float pan_r_ = 1.0f;

    // Pre-delay counter
    int pre_delay_ = 0;

    // Apply one sample of pending-kill logic. Modifies out_l/out_r in-place
    // (applies fade if in fallback mode). Returns true if the grain was killed
    // this sample (caller should output 0 and stop processing the grain).
    // INLINED — called per-sample in both hot loop paths.
    inline bool ApplyPendingKill(float& out_l, float& out_r) {
        float mono = out_l + out_r;
        if (fallback_fade_) {
            // Decrement first, so `fade` is computed as
            // (counter - 1) / kFallbackFadeSamples: the emitted sequence
            // is 0.75, 0.5, 0.25, 0.0 (kFallbackFadeSamples == 4) — the
            // last emitted sample reaches exactly 0 instead of hard-
            // cutting from 0.25 straight to silence.
            if (--fallback_counter_ < 0) {
                active_ = false;
                pending_kill_ = false;
                fallback_fade_ = false;
                prev_mono_ = 0.0f;
                return true;
            }
            float fade = static_cast<float>(fallback_counter_) * kFallbackFadeInv;
            out_l *= fade;
            out_r *= fade;
        } else {
            bool crossed = (prev_mono_ > 1e-5f && mono <= 0.0f) ||
                           (prev_mono_ < -1e-5f && mono >= 0.0f);
            if (crossed) {
                active_ = false;
                pending_kill_ = false;
                prev_mono_ = 0.0f;
                return true;
            }
            if (--kill_deadline_ <= 0) {
                fallback_fade_ = true;
                fallback_counter_ = kFallbackFadeSamples;
            }
        }
        prev_mono_ = mono;
        return false;
    }

    // Compute envelope value from phase using precomputed smoothness/slope.
    // INLINED — called per-sample per-grain in the hot loop.
    //
    // Morphs through 4 shapes as the SHAPE knob sweeps 0→1:
    //   Rectangle → Snappy Decay → Smooth Bell → Reversed
    //
    // Two components blended by smoothness_:
    //   1. Asymmetric trapezoid: triangle with peak at slope_, steepness
    //      controls rectangularity (high steepness clips triangle to rect).
    //   2. Asymmetric raised cosine: Hann window remapped so peak aligns
    //      with slope_ (not always at center).
    inline float ComputeEnvelope(float phase) {
        // Asymmetric triangle with peak at slope_, scaled by steepness.
        float trap;
        if (phase < slope_) {
            trap = phase * inv_slope_;
        } else {
            trap = (1.0f - phase) * inv_one_minus_slope_;
        }
        trap = Clamp(trap * steepness_, 0.0f, 1.0f);

        // Asymmetric raised cosine with peak at slope_.
        // Remap phase so [0,slope_]→[0,0.5] and [slope_,1]→[0.5,1].
        float hann_phase;
        if (phase < slope_) {
            hann_phase = phase * inv_slope_ * 0.5f;
        } else {
            hann_phase = 0.5f + (phase - slope_) * inv_one_minus_slope_ * 0.5f;
        }
        float hann = 0.5f - 0.5f * CosLookup(hann_phase);

        // Blend between trapezoid and raised cosine.
        return Crossfade(trap, hann, smoothness_);
    }
};

} // namespace particules_dsp

#pragma once

#include "../../include/beads/types.h"
#include "../buffer/recording_buffer.h"
#include "../util/cosine_table.h"
#include "../util/dsp_utils.h"

#include <cmath>

namespace beads {

// DTC pre-fetch cache for grain buffer reads.
// Before each grain's block, a contiguous region of the recording buffer
// is copied into this small DTC-resident struct. Grain processing then
// reads from the linear cache (single-cycle DTC) instead of random DRAM.
struct GrainDTCCache {
    // 64 samples * max pitch 4.5x + Hermite margin + rounding = ~300 frames
    static constexpr int kMaxCacheFrames = 300;
    float data[kMaxCacheFrames * 2];  // stereo interleaved
    float base_position;              // buffer position of cache frame 0
    float buf_size;                   // recording buffer size in frames
    int num_frames;                   // frames actually cached (0 = cache miss)

    // Fill cache with the buffer region the grain will read this block.
    // Returns false if the required range exceeds kMaxCacheFrames (fallback).
    inline bool Prefetch(const RecordingBuffer& buffer, float read_pos,
                         float phase_inc, int num_samples,
                         int pre_delay_remaining) {
        buf_size = static_cast<float>(buffer.size());
        int buf_size_int = static_cast<int>(buffer.size());

        int active_samples = num_samples - pre_delay_remaining;
        if (active_samples <= 0) {
            num_frames = 0;
            return true;  // nothing to read, no fallback needed
        }

        float end_pos = read_pos + phase_inc * static_cast<float>(active_samples - 1);
        float min_pos, max_pos;
        if (phase_inc >= 0.0f) {
            min_pos = read_pos;
            max_pos = end_pos;
        } else {
            min_pos = end_pos;
            max_pos = read_pos;
        }

        // Hermite needs frames at -1 before min and +2 after max.
        // Floor for negative: static_cast truncates toward zero; adjust if fractional.
        int min_int = static_cast<int>(min_pos);
        if (min_pos < static_cast<float>(min_int)) min_int--;  // proper floor
        int start_frame = min_int - 1;
        int end_frame = static_cast<int>(max_pos) + 4;  // ceil + 2 for Hermite + margin

        num_frames = end_frame - start_frame;
        if (num_frames > kMaxCacheFrames || num_frames < 4) {
            num_frames = 0;
            return false;  // signal fallback to direct DRAM reads
        }

        // Normalize base to [0, buf_size)
        base_position = static_cast<float>(
            ((start_frame % buf_size_int) + buf_size_int) % buf_size_int);

        buffer.CopyRegionTo(start_frame, num_frames, data);
        return true;
    }

    // Read from cache using Hermite interpolation.
    // buf_pos is in original buffer coordinates.
    inline void ReadHermiteStereo(float buf_pos, float* out_l, float* out_r) const {
        float local = buf_pos - base_position;
        if (local < 0.0f) local += buf_size;
        int pos_int = static_cast<int>(local);
        float frac = local - static_cast<float>(pos_int);
        const float* p_1 = &data[(pos_int - 1) * 2];
        const float* p0  = &data[pos_int * 2];
        const float* p1  = &data[(pos_int + 1) * 2];
        const float* p2  = &data[(pos_int + 2) * 2];
        *out_l = InterpolateHermite(p_1[0], p0[0], p1[0], p2[0], frac);
        *out_r = InterpolateHermite(p_1[1], p0[1], p1[1], p2[1], frac);
    }

    // Read from cache using linear interpolation (~3x cheaper).
    inline void ReadLinearStereo(float buf_pos, float* out_l, float* out_r) const {
        float local = buf_pos - base_position;
        if (local < 0.0f) local += buf_size;
        int pos_int = static_cast<int>(local);
        float frac = local - static_cast<float>(pos_int);
        const float* p0 = &data[pos_int * 2];
        const float* p1 = &data[(pos_int + 1) * 2];
        *out_l = p0[0] + frac * (p1[0] - p0[0]);
        *out_r = p0[1] + frac * (p1[1] - p0[1]);
    }
};

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

    // Process a full block reading from a DTC pre-fetch cache.
    // The cache must have been filled via Prefetch() before calling.
    // use_linear: true for linear interpolation (cheaper, used under load).
    inline void ProcessBlockCached(const GrainDTCCache& cache, float buf_size_f,
                                   StereoFrame* output, size_t num_frames) {
        if (!active_) return;

        for (size_t i = 0; i < num_frames; ++i) {
            if (!active_) break;

            if (pre_delay_ > 0) {
                --pre_delay_;
                continue;  // output already zeroed by caller
            }

            if (envelope_phase_ >= 1.0f || !std::isfinite(envelope_phase_)) {
                active_ = false;
                break;
            }

            float env = ComputeEnvelope(envelope_phase_);
            envelope_phase_ += envelope_increment_;

            float sample_l, sample_r;
            if (use_linear_) {
                cache.ReadLinearStereo(read_position_, &sample_l, &sample_r);
            } else {
                cache.ReadHermiteStereo(read_position_, &sample_l, &sample_r);
            }

            read_position_ += phase_increment_;
            if (buf_size_f > 0.0f) {
                while (read_position_ >= buf_size_f) read_position_ -= buf_size_f;
                while (read_position_ < 0.0f) read_position_ += buf_size_f;
            }

            float out_l_val = sample_l * env * gain_ * pan_l_;
            float out_r_val = sample_r * env * gain_ * pan_r_;

            if (!std::isfinite(out_l_val) || !std::isfinite(out_r_val)) {
                active_ = false;
                return;
            }

            if (pending_kill_) {
                if (ApplyPendingKill(out_l_val, out_r_val)) break;
            } else {
                prev_mono_ = out_l_val + out_r_val;
            }

            output[i].l += out_l_val;
            output[i].r += out_r_val;
        }
    }

    bool active() const { return active_; }
    bool pending_kill() const { return pending_kill_; }
    float read_position() const { return read_position_; }
    float phase_increment() const { return phase_increment_; }
    int pre_delay_remaining() const { return pre_delay_; }
    void set_use_linear(bool v) { use_linear_ = v; }

private:
    bool active_ = false;
    bool pending_kill_ = false;
    bool use_linear_ = false;

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
            float fade = static_cast<float>(fallback_counter_) * kFallbackFadeInv;
            out_l *= fade;
            out_r *= fade;
            if (--fallback_counter_ < 0) {
                active_ = false;
                pending_kill_ = false;
                fallback_fade_ = false;
                prev_mono_ = 0.0f;
                return true;
            }
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

} // namespace beads

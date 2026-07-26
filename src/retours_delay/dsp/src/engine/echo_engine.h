#pragma once
#include "../../include/retours_delay_dsp/types.h"
#include "buffer/recording_buffer.h"

namespace retours_delay_dsp {

// Owns read-head positioning over the shared RecordingBuffer.
// All distances INTERNALLY in buffer frames (host samples ÷ decimation).
class EchoEngine {
public:
    void Init(particules_dsp::RecordingBuffer* buffer, float sample_rate);

    // Block-rate: set targets. delay_samples/tap2 in HOST samples.
    void SetTargets(float delay_samples, bool multi_tap,
                    TimeChangeMode mode, float slew_seconds);
    void NotifyFreeze(bool frozen, float slice_len_samples, int slice_index);

    // Block-rate: tape wow/flutter scales the per-sample read-position
    // advance (1.0 = no modulation). Applies to both the normal tape/
    // crossfade read and the frozen-slice read. Non-finite or non-positive
    // values are ignored (treated as 1.0) — playback must never stall or
    // reverse.
    void SetReadRateScale(float scale);

    // Per-sample: advance read position by 1/decimation host-sample and
    // read the wet tap(s). Returns wet (tap1 + kTap2Gain*tap2).
    StereoFrame ReadWet();

    float CurrentDelaySamples() const;   // slewed actual (host samples)

private:
    particules_dsp::RecordingBuffer* buf_ = nullptr;
    float sample_rate_ = 48000.f;
    float delay_frames_ = 4800.f;        // slewed, buffer frames
    float target_frames_ = 4800.f;
    float slew_coeff_ = 0.001f;          // per-sample one-pole
    // Cache for slew_coeff_'s recompute (M6): 1-exp(-1/(slew_s*sr)) is a
    // transcendental call, and slew_seconds is usually unchanged block to
    // block (a fixed knob/constant -- see SetTargets' caller). -1.f is not a
    // legal (clamped-positive) slew_s/sample_rate, so the first call always
    // misses and recomputes.
    float cached_slew_s_ = -1.f;
    float cached_slew_sr_ = -1.f;
    bool  multi_tap_ = false;
    TimeChangeMode mode_ = TimeChangeMode::kTape;
    // crossfade-jump state (kCrossfade mode)
    float fade_from_frames_ = 0.f, fade_pos_ = 1.f, fade_step_ = 0.f;
    float queued_target_ = -1.f;
    // Last raw (pre-alignment) delay request seen in kCrossfade mode. The
    // fade trigger compares against this rather than target_frames_, because
    // splice alignment leaves target_frames_ deliberately a few frames off
    // the request -- see SetTargets in echo_engine.cpp.
    float requested_frames_ = -1.f;
    // freeze state
    bool  frozen_ = false;
    float slice_start_ = 0.f, slice_len_frames_ = 1.f, slice_phase_ = 0.f;
    float frozen_anchor_ = 0.f;          // write head at freeze
    float read_subsample_ = 0.f;         // accumulates 1/decimation steps
    float read_rate_scale_ = 1.f;        // tape wow/flutter, block-rate
    float inv_decimation_ = 1.f;         // 1/decimation, refreshed in SetTargets

    // Frozen-seam hoists: NotifyFreeze refreshes these unconditionally every
    // block for as long as freeze holds (not just when slice_start_ actually
    // moves), so caching the Hermite read at slice_start_pos_ bounds its
    // staleness to at most one block (<=64 samples) rather than requiring
    // buffer content there to be provably static -- see NotifyFreeze's
    // refresh_seam_cache comment for why that weaker guarantee is enough.
    float slice_start_pos_ = 0.f;        // WrapPosition(slice_start_, size_f)
    float slice_fade_len_ = 1.f;         // min(kSeamCrossfadeFrames, len*0.5)
    float inv_slice_fade_len_ = 1.f;     // 1/slice_fade_len_
    float seam_l_ = 0.f, seam_r_ = 0.f;  // Hermite read at slice_start_pos_
};

} // namespace retours_delay_dsp

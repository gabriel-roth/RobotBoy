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
    bool  multi_tap_ = false;
    TimeChangeMode mode_ = TimeChangeMode::kTape;
    // crossfade-jump state (kCrossfade mode)
    float fade_from_frames_ = 0.f, fade_pos_ = 1.f, fade_step_ = 0.f;
    float queued_target_ = -1.f;
    // freeze state
    bool  frozen_ = false;
    float slice_start_ = 0.f, slice_len_frames_ = 1.f, slice_phase_ = 0.f;
    float frozen_anchor_ = 0.f;          // write head at freeze
    float read_subsample_ = 0.f;         // accumulates 1/decimation steps
    float read_rate_scale_ = 1.f;        // tape wow/flutter, block-rate
    float inv_decimation_ = 1.f;         // 1/decimation, refreshed in SetTargets

    // Frozen-seam hoists (NotifyFreeze recomputes these whenever slice_start_
    // changes; ReadWet's frozen branch just reads them). Safe to cache the
    // buffer content at slice_start_pos_ across samples because writes into
    // the RecordingBuffer are fully suppressed for the entire freeze duration
    // (retours_processor.cpp gates Write() on `!params.freeze`, not just at
    // the write-head seam) -- see NotifyFreeze for the full citation.
    float slice_start_pos_ = 0.f;        // WrapPosition(slice_start_, size_f)
    float slice_fade_len_ = 1.f;         // min(kSeamCrossfadeFrames, len*0.5)
    float inv_slice_fade_len_ = 1.f;     // 1/slice_fade_len_
    float seam_l_ = 0.f, seam_r_ = 0.f;  // Hermite read at slice_start_pos_
};

} // namespace retours_delay_dsp

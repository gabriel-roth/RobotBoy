#pragma once

// Internal header — defines ParticulesProcessor::Impl
// Not part of the public API.

#include "../include/particules_dsp/particules_dsp.h"
#include "buffer/recording_buffer.h"
#include "grain/grain_engine.h"
#include "fx/reverb.h"
#include "fx/saturation.h"
#include "quality/quality_processor.h"
#include "input/auto_gain.h"
#include "util/svf.h"

namespace particules_dsp {

// The Impl struct holds all sub-processors. It is placement-new'd into
// the front of the user-provided memory block by Init().
struct ParticulesProcessor::Impl {
    // Sub-processors
    RecordingBuffer recording_buffer;
    GrainEngine grain_engine;
    Reverb reverb;
    Saturation saturation;
    QualityProcessor quality_processor;
    AutoGain auto_gain;

    // Feedback HP filter (removes DC from feedback path)
    StateVariableFilter feedback_hp_l;
    StateVariableFilter feedback_hp_r;

    // Current parameters
    ParticulesParameters params;
    float sample_rate = 48000.0f;

    // Previous block's wet output (post-quality-processing, pre-reverb,
    // NaN-guarded). The next block's input stage mixes prev_wet_buf[i] in
    // per-sample — a one-block feedback delay (1 frame in VCV, 64 on MM),
    // matching the hardware Beads behavior instead of a block-rate hold.
    StereoFrame prev_wet_buf[kMaxBlockSize] = {};
    size_t prev_wet_len = 0;

    // Previous freeze state for crossfade detection
    bool prev_freeze = false;

    // Config transition (quality mode and/or input channel count): fade the
    // wet path out, reconfigure + clear the buffer (a layout change makes old
    // pool bytes garbage), hold muted until the deferred clear drains, then
    // fade back in. See docs/superpowers/plans/2026-07-20-quality-buffer-decoupling.md.
    enum class QualityTransition : uint8_t { kIdle, kFadeOut, kClearing, kFadeIn };
    QualityTransition qt_state = QualityTransition::kIdle;
    QualityMode active_quality = QualityMode::kBrightDigital;
    QualityMode pending_quality = QualityMode::kBrightDigital;
    bool active_mono = false;
    bool pending_mono = false;
    int qt_fade_counter = 0;
    static constexpr int kQualityFadeSamples = 2048;   // ~43 ms at 48 kHz
    // Exact reciprocal (power of two): multiply instead of dividing per
    // sample in the fade-out/fade-in gain computation below.
    static constexpr float kQualityFadeSamplesRecip = 1.0f / kQualityFadeSamples;

    // Smoothed mix parameters (zipper noise prevention)
    float smoothed_dry_wet = 0.5f;
    float smoothed_feedback = 0.0f;

    // Cached closed-form one-pole coefficient for smoothed_dry_wet; only
    // recomputed when the host's block size changes (never within a run).
    size_t dry_wet_coeff_frames = 0;
    float  dry_wet_coeff = 0.0f;

    // Work buffers for Process() — moved here from the stack to avoid
    // overflowing the audio-thread stack on constrained targets (e.g. NT).
    StereoFrame wet_buf[kMaxBlockSize];
    // Input captured before auto-gain, for dry/wet mix output.
    // Indexed by intra-block offset only; Process() chunks caller frames
    // into blocks of <= kMaxBlockSize.
    StereoFrame dry_input_buf[kMaxBlockSize];

    static constexpr size_t kAlignment = 16;
};

} // namespace particules_dsp

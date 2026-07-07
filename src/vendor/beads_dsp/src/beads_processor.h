#pragma once

// Internal header — defines BeadsProcessor::Impl
// Not part of the public API.

#include "../include/beads/beads.h"
#include "buffer/recording_buffer.h"
#include "grain/grain_engine.h"
#include "fx/reverb.h"
#include "fx/saturation.h"
#include "quality/quality_processor.h"
#include "input/auto_gain.h"
#include "util/svf.h"

namespace beads {

// The Impl struct holds all sub-processors. It is placement-new'd into
// the front of the user-provided memory block by Init().
struct BeadsProcessor::Impl {
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
    BeadsParameters params;
    float sample_rate = 48000.0f;

    // Feedback sample (captured after dry/wet, before reverb)
    StereoFrame feedback_sample = {0.0f, 0.0f};

    // Previous freeze state for crossfade detection
    bool prev_freeze = false;

    // Quality mode transition
    QualityMode prev_quality_mode = QualityMode::kHiFi;
    // Duck duration must cover the buffer clear time. At 64 samples/block
    // (MetaModule), clearing 128 chunks takes 8192 samples (~170ms at 48kHz).
    static constexpr int kQualityXfadeSamples = 8192;
    int quality_xfade_counter = 0;

    // Smoothed mix parameters (zipper noise prevention)
    float smoothed_dry_wet = 0.5f;
    float smoothed_feedback = 0.0f;

    // Work buffers for Process() — moved here from the stack to avoid
    // overflowing the audio-thread stack on constrained targets (e.g. NT).
    StereoFrame wet_buf[kMaxBlockSize];
    // Input captured before auto-gain, for dry/wet mix output.
    // Sized to kMaxBlockSize; callers must not pass num_frames > kMaxBlockSize.
    StereoFrame dry_input_buf[kMaxBlockSize];

    static constexpr size_t kAlignment = 16;
};

} // namespace beads

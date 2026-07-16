#pragma once
// Internal header — defines RetoursProcessor::Impl. Not public API.
#include "../include/retours_delay_dsp/retours_dsp.h"
#include "buffer/recording_buffer.h"   // particules_dsp
#include "fx/saturation.h"
#include "quality/quality_processor.h"
#include "util/svf.h"
#include "engine/echo_engine.h"
#include "time/base_time.h"
#include "pitch/rotary_shifter.h"
#include "env/repeat_envelope.h"
#include "mod/ar_modulator.h"
#include "random/random.h"

namespace retours_delay_dsp {

struct RetoursProcessor::Impl {
    particules_dsp::RecordingBuffer recording_buffer;
    particules_dsp::QualityProcessor quality_processor;
    particules_dsp::Saturation saturation;
    particules_dsp::StateVariableFilter feedback_hp_l, feedback_hp_r;
    BaseTimeControl base_time;
    EchoEngine engine;
    RotaryShifter shifter;
    RepeatEnvelope envelope;

    // Slow-random attenurandomizer modulation (TIME/PITCH/SHAPE), sharing
    // one PRNG across three independently-salted LFOs.
    particules_dsp::Random mod_rng;
    ArModulator ar_time, ar_pitch, ar_shape;

    RetoursParameters params;
    float sample_rate = 48000.f;

    // Quality-mode change tracking (block-rate edge detect). Change is
    // ignored while frozen (see self-review in task-8 report) — deferred
    // change is picked up on the first block after unfreeze.
    QualityMode prev_quality = QualityMode::kHiFi;
    // Previous block's freeze state, so the quality-change branch can tell
    // "steady-state unfrozen" apart from "this is the unfreeze block" (the
    // falling edge). Applying a pending quality change on the falling-edge
    // block itself clears/resyncs the buffer before EchoEngine::NotifyFreeze
    // has finished its unfreeze continuity math against the pre-clear write
    // head, corrupting delay_frames_ (see Fix round in task-8 report). The
    // pending change is deferred one more block, to the first block where
    // both this block and the previous one are unfrozen.
    bool prev_freeze = false;
    // Duck duration must cover the buffer clear time: with the chunk size
    // below, draining the full buffer takes 128 blocks × 64 frames = 8192
    // samples — same derivation as Particules' identical constant.
    static constexpr int kQualityXfadeSamples = 8192;
    int quality_xfade_counter = 0;

    // Smoothed mix params (zipper prevention)
    float smoothed_dry_wet = 0.5f;
    float smoothed_feedback = 0.f;

    static constexpr size_t kAlignment = 16;
};

} // namespace retours_delay_dsp

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

    // Previous block's freeze state, so the quality-change branch can tell
    // "steady-state unfrozen" apart from "this is the unfreeze block" (the
    // falling edge). Applying a pending quality change on the falling-edge
    // block itself clears/resyncs the buffer before EchoEngine::NotifyFreeze
    // has finished its unfreeze continuity math against the pre-clear write
    // head, corrupting delay_frames_ (see Fix round in task-8 report). The
    // pending change is deferred one more block, to the first block where
    // both this block and the previous one are unfrozen.
    bool prev_freeze = false;

    // Config transition (quality mode and/or input channel count): fade the
    // wet path out, reconfigure + clear the buffer (a layout change makes old
    // pool bytes garbage), hold muted until the deferred clear drains, then
    // fade back in. Mirrors Particules' identical state machine (Task 7).
    enum class QualityTransition : uint8_t { kIdle, kFadeOut, kClearing, kFadeIn };
    QualityTransition qt_state = QualityTransition::kIdle;
    QualityMode active_quality = QualityMode::kBrightDigital;
    QualityMode pending_quality = QualityMode::kBrightDigital;
    bool active_mono = false;
    bool pending_mono = false;
    int qt_fade_counter = 0;
    static constexpr int kQualityFadeSamples = 2048;   // ~43 ms at 48 kHz

    // True once the very first block has resolved the real channel-count/
    // quality config. Init() always configures the buffer for stereo
    // (cable state isn't known yet at construction time), so a mono-cabled
    // patch's first block sees params.mono_input (true) mismatch
    // active_mono (false) and would otherwise fall into the crossfaded
    // transition below meant for a LIVE quality/cabling change mid-patch --
    // muting the wet path for ~250 ms and eating the first pluck of every
    // mono Karplus-Strong patch (see ProcessBlock's first-block resolution).
    bool config_resolved = false;

    // Smoothed mix params (zipper prevention)
    float smoothed_dry_wet = 0.5f;
    float smoothed_feedback = 0.f;

    // Block-rate cache: input trim -> linear gain (M5). DbToGain is a powf()
    // call; input_trim_db is a knob/CV-derived value that's usually constant
    // block-to-block (and baked to 0 dB with no UI on the wrapper), so skip
    // the recompute unless it actually changed. 1e9f is not a legal dB value
    // reachable from any real param, so the very first block always misses
    // and recomputes.
    float cached_trim_db_ = 1e9f;
    float cached_input_gain_ = 1.f;

    // Block-rate cache: slow-random LFO rate (L2). SetRate is cheap on its
    // own, but three calls a block add up; params.random_lfo_hz is a fixed
    // constant in current callers (see retours_processor.cpp), so gate on
    // actual change.
    float cached_random_lfo_hz_ = -1e9f;
    float cached_random_lfo_sr_ = -1e9f;

    static constexpr size_t kAlignment = 16;
};

} // namespace retours_delay_dsp

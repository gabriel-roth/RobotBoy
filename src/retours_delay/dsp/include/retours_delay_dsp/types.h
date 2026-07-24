#pragma once
#include <cstddef>
#include <cstdint>
#include "particules_dsp/types.h"   // StereoFrame, QualityMode, DecimationFactorForQuality

namespace retours_delay_dsp {

using particules_dsp::StereoFrame;
using particules_dsp::QualityMode;

static constexpr size_t kBufferFrames = 192000;   // 4 s stereo @48k HiFi
static constexpr size_t kMaxBlockSize = 64;
static constexpr float  kManualOctaves = 11.0f;
static constexpr size_t kShifterSize = 4096;      // frames, power of two
static constexpr float  kMinDelaySeconds = 0.002f;
static constexpr float  kSlewSecondsDefault = 0.285f;  // baked Doppler slew (no UI)
static constexpr float  kRandomLfoHz = 0.1f;
static constexpr int    kJumpCrossfadeFrames = 1024;
static constexpr float  kTap2Ratio = 0.61803f;
static constexpr float  kTap2Gain = 0.7f;
static constexpr float  kShifterBypassSemitones = 0.25f;

enum class TimeChangeMode : uint8_t { kTape = 0, kCrossfade = 1 };

struct RetoursParameters {
    // Knobs (normalized)
    float density = 0.5f;          // 0..1, noon = 0.5
    float time = 0.0f;             // 0..1 → multiplier / slice select
    float pitch_semitones = 0.0f;  // -24..+24 (adapter applies notch map)
    float shape = 0.0f;            // 0..1, 0 = no envelope
    float feedback = 0.0f;         // 0..1
    float dry_wet = 0.5f;          // 0..1

    // CV (volts) + patched flags
    float density_cv = 0.0f;       // exponential, −1 V/oct on time
    float time_cv = 0.0f, pitch_cv = 0.0f, shape_cv = 0.0f;
    float feedback_cv = 0.0f, dry_wet_cv = 0.0f;   // added directly, /5 V
    bool  time_cv_connected = false, pitch_cv_connected = false,
          shape_cv_connected = false;

    // Attenurandomizers (-1..+1, 0 = noon)
    float time_ar = 0.0f, pitch_ar = 0.0f, shape_ar = 0.0f;

    // Clock / tap: sample offset of a rising edge within this block, -1 none
    int   clock_tick_offset = -1;
    bool  clock_connected = false;  // CLOCK jack patched
    bool  freeze = false;

    QualityMode quality = QualityMode::kBrightDigital;
    // True when the input is effectively mono (R jack unpatched). Drives the
    // recording buffer's channel count: mono doubles buffer duration.
    bool mono_input = false;
    TimeChangeMode time_change_mode = TimeChangeMode::kTape;
    bool  envelope_pre_feedback = false;  // false = feedback taps post-envelope
    float input_trim_db = 0.0f;           // -12..+12
    float slew_seconds = kSlewSecondsDefault;  // 0.01..1.0
    float random_lfo_hz = kRandomLfoHz;        // fixed at 0.1 Hz; no UI sets this
};

} // namespace retours_delay_dsp

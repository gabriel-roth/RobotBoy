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
// --- Crossfade-mode splice alignment ---
// When a Crossfade-mode fade starts, its destination may be nudged by up to
// +/-kAlignSearchFrames so the two taps being blended line up in phase; see
// AlignedFadeTarget in engine/echo_engine.cpp for the mechanism and
// docs/superpowers/2026-07-26-crossfade-variants-measurements.md for the
// measurements that picked these values. All distances are BUFFER frames (host
// samples divided by the quality mode's decimation factor).
//
// The comparison window is kAlignWindowFrames of buffer history sampled every
// kAlignWindowStride frames, i.e. kAlignWindowTaps points. Cost per fade is
// kAlignWindowTaps * (coarse candidates + refine candidates) multiply-adds:
// 64 * (49 + 6) = 3520.
//
// A fade lasts kJumpCrossfadeFrames HOST samples, not buffer frames -- ReadWet
// advances fade_pos_ once per host sample regardless of decimation -- so the
// worst-case cadence is ~47 fades/s at 48 kHz in EVERY quality mode, i.e.
// ~165k MAC/s. That worst case is not exotic: any patch modulating TIME (CV or
// the attenurandomizer) changes the requested delay every block, so a fade
// starts every kJumpCrossfadeFrames continuously and ~165k MAC/s is the steady
// state, not a transient. It falls to zero only when the requested delay is
// genuinely static.
static constexpr int    kAlignWindowFrames = 512;  // ~11 ms of history @48k
static constexpr int    kAlignWindowStride = 8;    // -> 3 kHz correlation band
static constexpr int    kAlignWindowTaps = kAlignWindowFrames / kAlignWindowStride;
static constexpr int    kAlignSearchFrames = 96;   // +/- ~2 ms @48k, dec 1
static constexpr int    kAlignSearchStride = 4;    // coarse lag step
static constexpr int    kAlignRefineRadius = 3;    // whole-frame refine window
// Search radius is also capped relative to the delay itself (a short delay is
// a tuned comb that a fixed 2 ms would detune) and relative to the size of the
// requested move (so the delay always travels at least half the distance
// asked for), and alignment is skipped entirely below kAlignMinRadiusFrames of
// usable slack.
static constexpr float  kAlignSearchMaxFraction = 0.05f;
static constexpr float  kAlignMoveFraction = 0.5f;
static constexpr int    kAlignMinRadiusFrames = 16;
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
    float input_trim_db = 0.0f;           // -12..+12
    float slew_seconds = kSlewSecondsDefault;  // 0.01..1.0
    float random_lfo_hz = kRandomLfoHz;        // fixed at 0.1 Hz; no UI sets this
};

} // namespace retours_delay_dsp

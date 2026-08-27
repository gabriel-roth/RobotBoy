#pragma once

#include <cmath>
#include <cstdint>
#include "dsp/LoopEngine.hpp"

namespace loooop {

constexpr float kLinearSpeedMin = -2.0f;
constexpr float kLinearSpeedMax = 2.0f;
constexpr float kVOctSpeedMin = -16.0f;
constexpr float kVOctSpeedMax = 16.0f;

// NaN-suppressing clamp, matching rack::clamp (fmax/fmin based): a non-finite
// v lands on a bound instead of propagating. The engine's param setters rely
// on this — a NaN speed CV must not reach PlayHead::pos. This intentionally
// differs from the codex source, which used std::clamp (NaN-propagating) and
// so regressed the modules' original unqualified (rack::) clamp behavior.
inline float clampSafe(float v, float lo, float hi) {
    return std::fmax(lo, std::fmin(v, hi));
}

inline float speedFromControls(float knob, float cvVolts) {
    return clampSafe(knob + cvVolts * 0.4f, kLinearSpeedMin, kLinearSpeedMax);
}

inline float speedFromVOct(float knob, float cvVolts) {
    const float octave = clampSafe(cvVolts, -5.0f, 5.0f);
    return clampSafe(knob * std::exp2(octave), kVOctSpeedMin, kVOctSpeedMax);
}

// Per-head memo for speedFromVOct: exp2f is a libm call on MetaModule; the
// (knob, cv) pair is static or slow-moving in practice. Exactly equivalent.
struct VOctSpeedMemo {
    float knob = -1e9f, cv = -1e9f, out = 0.f;
    float get(float k, float c) {
        if (k != knob || c != cv) { knob = k; cv = c; out = speedFromVOct(k, c); }
        return out;
    }
};

inline float normalizedControl(float knob, float cvVolts) {
    return clampSafe(knob + cvVolts * 0.1f, 0.0f, 1.0f);
}

inline float panControl(float knob, float cvVolts) {
    return clampSafe(knob + cvVolts * 0.2f, -1.0f, 1.0f);
}

struct StereoInput { float l, r; };

inline StereoInput normalledStereo(bool leftConnected, float left,
                                   bool rightConnected, float right) {
    return {
        leftConnected ? left : right,
        rightConnected ? right : left,
    };
}

inline float dryWet(float dry, float wet, float mix) {
    return dry * (1.0f - mix) + wet * mix;
}

// True while actively recording the very first pass (no loop exists yet):
// the Wet bus has nothing to read back, so hosts should monitor the dry
// signal instead of the (currently silent) head output. False once a loop
// exists, even during a later overdub pass -- "first time" only.
inline bool monitorDryWhileEmpty(bool recording, bool hasLoop) {
    return recording && !hasLoop;
}

inline float panLeftGain(float pan) {
    return pan <= 0.0f ? 1.0f : 1.0f - pan;
}

inline float panRightGain(float pan) {
    return pan >= 0.0f ? 1.0f : 1.0f + pan;
}

// Grid menu choice (0..5: Off/4/8/16/32/64) -> LoopEngine::setGrid segment count.
inline int gridSegments(int choiceIdx) {
    constexpr int kGridChoices[6] = {0, 4, 8, 16, 32, 64};
    return (choiceIdx < 0 || choiceIdx > 5) ? 0 : kGridChoices[choiceIdx];
}

// Engine write mode for an overdub choice index, in the 5-state Overdub order
// (Layer, Decay, Add, Replace). Index 4 (Lock) is never passed here — while
// Locked the hosts leave the last write mode set (see OverdubControl.hpp);
// out-of-range indices fall back to Layer.
// Shared so VCV's Overdub button and MetaModule's Write-mode alt-param
// map a given index to the same behavior — the write-mode analogue of
// gridSegments. Both hosts default to index 0 = Layer (MM's alt-param loader
// zero-inits unset params, so index 0 is the fresh/legacy default).
inline LoopEngine::WriteMode overdubWriteMode(int choiceIdx) {
    constexpr LoopEngine::WriteMode kModes[4] = {
        LoopEngine::WriteMode::Layer, LoopEngine::WriteMode::Decay,
        LoopEngine::WriteMode::Add,   LoopEngine::WriteMode::Replace};
    return kModes[(choiceIdx < 0 || choiceIdx > 3) ? 0 : choiceIdx];
}

// Overdub state colors (Layer/Decay/Add/Replace/Lock), shared by VCV's Overdub
// LED bezel and the MetaModule cores' RGB button. Kept here (Rack-free) so both
// hosts index the same table.
// Fully-saturated primaries/secondaries (channels snapped to 0/0.5/1), matching
// the Particules Quality button. Mixed sub-channel values wash an RGB LED out and
// blur the states together; clean saturated hues read distinctly on the bezel.
static constexpr float kOverdubColors[5][3] = {
    {0.f, 0.5f, 1.f},   // Layer   - blue
    {1.f, 0.5f, 0.f},   // Decay   - orange
    {0.f, 1.f, 0.f},    // Add     - green
    {1.f, 0.f, 0.f},    // Replace - red
    {1.f, 0.f, 1.f},    // Lock    - magenta
};

// Apply the 5-state Overdub index to the engine: 0..3 = write modes
// (Layer/Decay/Add/Replace), 4 = Lock (overdub off, loop untouchable).
inline void applyOverdub(LoopEngine& engine, int od) {
    engine.setOverdub(od != 4);   // 4 = Lock
    if (od >= 0 && od < 4)
        engine.setWriteMode(overdubWriteMode(od));
}

// One-pole smoother for zipper-noise suppression (mirrors mf20/dsp_utils.hpp
// so the loooop headless lane stays self-contained). alpha 1 = passthrough.
struct OnePoleSmoother {
    float value = 0.f;
    float alpha = 1.f;
    float process(float target) { value += alpha * (target - value); return value; }
    void reset(float v) { value = v; }
};

// One-pole alpha for a time constant (seconds); saturates to 1 at low rates.
inline float smootherAlpha(float sampleRate, float tauSec) {
    return 1.f - std::exp(-1.f / (tauSec * sampleRate));
}

// Decides record actions from the Record button + jack per sample, shared by
// all four Loooop/Löp hosts (VCV + MetaModule). Trigger mode reproduces
// today's VCV behavior byte-for-byte: `bool recBtn = recordBtn.process(...);
// bool recTrig = recordTrig.process(...); if (recBtn || recTrig) ...` -- an OR
// of TWO INDEPENDENT edges (each input has its own rising-edge latch), NOT an
// edge of the OR'd level. So a button press while the jack is already held
// high still fires, and a jack rise while the button is already held still
// fires too; both rising on the same sample still collapses to exactly ONE
// Toggle (Action is a single value per step() call). This also changes
// MetaModule's prior behavior (which used one combined-level latch, so the
// button was dead while the jack was held high) -- a deliberate improvement,
// not just parity.
//
// Gate mode reinterprets only the jack: its own rising and falling edges
// drive Toggle/Close/None per the spec's gate-mode table. A rising edge while
// already recording is ignored (no "punch" action -- LoopEngine::toggleRecord
// called twice back-to-back doesn't behave sanely at audio sample rates, and
// can't tell an ongoing overdub pass apart from an initial pass still being
// recorded, so a synthesized punch risks arming a stop nobody asked for).
// The panel button stays an independent press-to-toggle escape hatch; if a
// button press and a jack edge land on the same sample in Gate mode, the
// button wins.
//
// prevButton_/prevJack_ track each input's own raw previous level in BOTH
// modes (not a mode-dependent combined latch), so a mode switch never needs
// special-case resyncing: the edge detectors keep meaning the same thing
// across the flip. Only patch load needs priming (see syncTo()).
struct RecordGateHelper {
    enum class Action : uint8_t { None, Toggle, Close };

    // gateMode: current "Record jack" option (false = Trigger, true = Gate);
    // jackHigh: the jack comparator's output this sample (already past
    // whatever threshold/hysteresis the host applies); buttonPressed: the
    // panel Record button's momentary state this sample; engineRecording:
    // LoopEngine::isRecording() as of the START of this sample, i.e. before
    // this call's action (if any) is applied to the engine.
    Action step(bool gateMode, bool jackHigh, bool buttonPressed, bool engineRecording) {
        const bool buttonRising = buttonPressed && !prevButton_;
        const bool jackRising = jackHigh && !prevJack_;
        const bool jackFalling = !jackHigh && prevJack_;
        prevButton_ = buttonPressed;
        prevJack_ = jackHigh;

        if (!gateMode) {
            // Trigger mode: OR of two independent edges (see class comment).
            return (buttonRising || jackRising) ? Action::Toggle : Action::None;
        }

        // Gate mode: the jack's own edges drive Toggle/Close; a rising edge
        // while already recording is ignored (None).
        Action action = Action::None;
        if (jackRising && !engineRecording) action = Action::Toggle;
        else if (jackFalling && engineRecording) action = Action::Close;

        if (buttonRising) action = Action::Toggle;   // button always wins
        return action;
    }

    // Call on patch load / first process: primes the jack edge detector to
    // the current level so a high gate doesn't fire a phantom edge. (Not
    // needed on an ordinary mode-param change -- see the class comment.)
    void syncTo(bool jackHigh) { prevJack_ = jackHigh; }

private:
    bool prevButton_ = false;
    bool prevJack_ = false;
};

}  // namespace loooop

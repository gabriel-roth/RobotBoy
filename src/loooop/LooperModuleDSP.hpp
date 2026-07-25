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
// today's single combined edge byte-for-byte: `bool recEdge = (recPressed ||
// recTrig) && !recPrev_` -- ONE Toggle per combined rising edge, never one
// per input, so a button press while the jack is already high does NOT fire
// (and vice versa). Gate mode reinterprets only the jack: its own rising and
// falling edges drive Punch/Close/None per the spec's gate-mode table, while
// the panel button stays an independent press-to-toggle escape hatch (if a
// button press and a jack edge land on the same sample in Gate mode, the
// button wins).
struct RecordGateHelper {
    enum class Action : uint8_t { None, Toggle, Close, Punch };

    // gateMode: current "Record jack" option (false = Trigger, true = Gate);
    // jackHigh: the jack comparator's output this sample (already past
    // whatever threshold/hysteresis the host applies); buttonPressed: the
    // panel Record button's momentary state this sample; engineRecording:
    // LoopEngine::isRecording() as of the START of this sample, i.e. before
    // this call's action (if any) is applied to the engine.
    Action step(bool gateMode, bool jackHigh, bool buttonPressed, bool engineRecording) {
        if (!primed_) {
            // First call ever: adopt the incoming mode as-is, WITHOUT
            // treating it as a "flip" (there's nothing to flip from yet) --
            // otherwise a freshly-constructed helper's first Gate-mode call
            // would stomp whatever syncTo() just primed with this same
            // sample's jack level, cancelling the very edge being read.
            prevMode_ = gateMode;
            primed_ = true;
        } else if (gateMode != prevMode_) {
            // Mode just flipped: resync the edge detector to the level it
            // would already show had the module always been in the new mode,
            // so the flip itself can never fire a phantom edge (a high gate
            // at load/mode-flip must fire nothing).
            prevJack_ = gateMode ? jackHigh : (buttonPressed || jackHigh);
            prevMode_ = gateMode;
        }

        const bool buttonRising = buttonPressed && !prevButton_;
        prevButton_ = buttonPressed;

        if (!gateMode) {
            // Trigger mode: byte-for-byte today's behavior. prevJack_ here
            // latches the combined (button||jack) level, not the bare jack
            // level -- that's what makes the OR'd edge single-fire.
            const bool combinedHigh = buttonPressed || jackHigh;
            const bool rising = combinedHigh && !prevJack_;
            prevJack_ = combinedHigh;
            return rising ? Action::Toggle : Action::None;
        }

        const bool jackRising = jackHigh && !prevJack_;
        const bool jackFalling = !jackHigh && prevJack_;
        prevJack_ = jackHigh;

        Action action = Action::None;
        if (jackRising)
            action = engineRecording ? Action::Punch : Action::Toggle;
        else if (jackFalling && engineRecording)
            action = Action::Close;

        if (buttonRising) action = Action::Toggle;   // button always wins
        return action;
    }

    // Call on a mode-param change and on patch load / first process: primes
    // the jack edge detector to the current level so a high gate doesn't
    // fire a phantom edge. (step() also self-syncs on any mode flip it
    // observes, so this is a defensive belt-and-suspenders call for the
    // moment before the very first step() -- e.g. a patch loaded with the
    // Record jack already high.)
    void syncTo(bool jackHigh) { prevJack_ = jackHigh; }

private:
    bool prevButton_ = false;
    bool prevJack_ = false;
    bool prevMode_ = false;   // false = Trigger, matching the param default
    bool primed_ = false;     // true once step() has run at least once
};

}  // namespace loooop

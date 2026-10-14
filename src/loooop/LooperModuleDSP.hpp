#pragma once

#include <cmath>
#include <cstdint>
#include "dsp/LoopEngine.hpp"
#include "../shared/dsp_utils.hpp"

// Rack-free control mapping shared by the VCV modules and the MetaModule
// cores of Loooop and Löp, so both hosts behave identically.
namespace loooop {

constexpr float kLinearSpeedMin = -2.0f;
constexpr float kLinearSpeedMax = 2.0f;
constexpr float kVOctSpeedMin = -16.0f;
constexpr float kVOctSpeedMax = 16.0f;

// NaN-suppressing clamp, like rack::clamp: a non-finite v lands on a bound
// instead of propagating. The engine's setters rely on this; a NaN speed CV
// must not reach a playhead position.
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

// Per-head memo for speedFromVOct: exp2f is a libm call on MetaModule, and
// the (knob, cv) pair is static or slow-moving in practice. Exactly
// equivalent.
struct VOctSpeedMemo {
	float knob = -1e9f, cv = -1e9f, out = 0.f;
	float get(float k, float c) {
		if (k != knob || c != cv) {
			knob = k;
			cv = c;
			out = speedFromVOct(k, c);
		}
		return out;
	}
};

constexpr float kSpeedNotchTargets[2] = {1.f, -1.f};
constexpr float kSpeedNotchWidth = 0.05f;  // raw units each side; range is -2..2

// Locks the Speed knob to exactly 1.0 or -1.0 within kSpeedNotchWidth of
// either; identity elsewhere. The two windows are isolated, so no
// hysteresis is needed.
inline float applySpeedNotch(float rawKnob) {
	for (float target : kSpeedNotchTargets)
		if (std::fabs(rawKnob - target) <= kSpeedNotchWidth)
			return target;
	return rawKnob;
}

inline float normalizedControl(float knob, float cvVolts) {
	return clampSafe(knob + cvVolts * 0.1f, 0.0f, 1.0f);
}

inline float panControl(float knob, float cvVolts) {
	return clampSafe(knob + cvVolts * 0.2f, -1.0f, 1.0f);
}

struct StereoInput { float l, r; };

// An unpatched input follows the patched one (mono -> both).
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

// True while recording the very first pass (no loop exists yet): there is
// nothing to play back, so hosts monitor the dry signal instead of the
// silent heads. False once a loop exists, even during a later overdub pass.
inline bool monitorDryWhileEmpty(bool recording, bool hasLoop) {
	return recording && !hasLoop;
}

inline float panLeftGain(float pan) {
	return pan <= 0.0f ? 1.0f : 1.0f - pan;
}

inline float panRightGain(float pan) {
	return pan >= 0.0f ? 1.0f : 1.0f + pan;
}

// Grid choice (0..5: Off/4/8/16/32/64) -> LoopEngine::setGrid segment count.
inline int gridSegments(int choiceIdx) {
	constexpr int kGridChoices[6] = {0, 4, 8, 16, 32, 64};
	return (choiceIdx < 0 || choiceIdx > 5) ? 0 : kGridChoices[choiceIdx];
}

// Overdub is one 5-state control: four write modes (Layer, Decay, Add,
// Replace) plus Lock (index 4: overdub off, loop untouchable). While Locked
// the last write mode stays set; the engine ignores it with overdub off.
// Index 0 = Layer is the default on both hosts (MetaModule zero-inits unset
// alt-params). Out-of-range indices fall back to Layer.
inline LoopEngine::WriteMode overdubWriteMode(int choiceIdx) {
	constexpr LoopEngine::WriteMode kModes[4] = {
		LoopEngine::WriteMode::Layer, LoopEngine::WriteMode::Decay,
		LoopEngine::WriteMode::Add,   LoopEngine::WriteMode::Replace};
	return kModes[(choiceIdx < 0 || choiceIdx > 3) ? 0 : choiceIdx];
}

// Overdub state colors (Layer/Decay/Add/Replace/Lock) for the RGB LED.
// Channels snapped to 0/0.5/1, matching the Particules Quality button:
// mixed sub-channel values wash an RGB LED out and blur the states together.
static constexpr float kOverdubColors[5][3] = {
	{0.f, 0.5f, 1.f},   // Layer   - blue
	{1.f, 0.5f, 0.f},   // Decay   - orange
	{0.f, 1.f, 0.f},    // Add     - green
	{1.f, 0.f, 0.f},    // Replace - red
	{1.f, 0.f, 1.f},    // Lock    - magenta
};

// Apply the 5-state Overdub index to the engine.
inline void applyOverdub(LoopEngine& engine, int od) {
	engine.setOverdub(od != 4);   // 4 = Lock
	if (od >= 0 && od < 4)
		engine.setWriteMode(overdubWriteMode(od));
}

// Decides the record action from the Record button and jack, per sample.
//
// Trigger mode: a rising edge on either input toggles recording. The two
// inputs have independent edge detectors, so a button press while the jack
// is held high still fires (and vice versa); both rising on the same sample
// is one toggle.
//
// Gate mode reinterprets only the jack: its rising edge starts recording and
// its falling edge stops it. A rising edge while already recording is
// ignored: there's no punch-in, since a toggle can't tell an overdub pass
// from an initial pass still being recorded. The button stays an
// independent toggle and wins if it lands on the same sample as a jack edge.
//
// Each input's previous level is tracked in both modes, so switching modes
// needs no resync; only patch load does (syncTo()).
struct RecordGateHelper {
	enum class Action : uint8_t { None, Toggle, Close };

	// gateMode: "Record jack" option (false = Trigger, true = Gate).
	// jackHigh: the jack comparator's output this sample. buttonPressed: the
	// Record button's state this sample. engineRecording:
	// LoopEngine::isRecording() before this sample's action is applied.
	Action step(bool gateMode, bool jackHigh, bool buttonPressed, bool engineRecording) {
		const bool buttonRising = buttonPressed && !prevButton_;
		const bool jackRising = jackHigh && !prevJack_;
		const bool jackFalling = !jackHigh && prevJack_;
		prevButton_ = buttonPressed;
		prevJack_ = jackHigh;

		if (!gateMode)
			return (buttonRising || jackRising) ? Action::Toggle : Action::None;

		Action action = Action::None;
		if (jackRising && !engineRecording)
			action = Action::Toggle;
		else if (jackFalling && engineRecording)
			action = Action::Close;

		if (buttonRising)
			action = Action::Toggle;   // button always wins
		return action;
	}

	// Call on patch load / first process: primes the jack edge detector to
	// the current level so a high gate doesn't fire a phantom edge.
	void syncTo(bool jackHigh) { prevJack_ = jackHigh; }

private:
	bool prevButton_ = false;
	bool prevJack_ = false;
};

}  // namespace loooop

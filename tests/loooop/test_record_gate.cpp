// Unit tests for loooop::RecordGateHelper (header-only) plus an integration
// sequence against a real LoopEngine, exercising the spec's gate-mode action
// table end to end (see Loooop.md, "Record jack").

#include "../../src/loooop/LooperModuleDSP.hpp"
#include "../../src/loooop/dsp/LoopEngine.hpp"

#include <cstdio>

static int failures = 0;
static void check(bool condition, const char* name) {
    if (!condition) { std::printf("FAIL: %s\n", name); ++failures; }
    else std::printf("ok:   %s\n", name);
}

using loooop::RecordGateHelper;
using Action = RecordGateHelper::Action;

static const char* actionName(Action a) {
    switch (a) {
        case Action::None:   return "None";
        case Action::Toggle: return "Toggle";
        case Action::Close:  return "Close";
    }
    return "?";
}

// ---------------------------------------------------------------------------
// Trigger-mode parity: the helper must reproduce today's VCV behavior
// byte-for-byte -- an OR of TWO INDEPENDENT edges (`recBtn.process() ||
// recTrig.process()`, each with its own Schmitt latch), NOT an edge of the
// OR'd level. That means a button press while the jack is already held high
// still fires, and vice versa; both rising on the same sample still
// collapses to exactly ONE Toggle (Action is single-valued per call).
// ---------------------------------------------------------------------------

static void test_trigger_button_only() {
    RecordGateHelper h;
    check(h.step(false, false, false, false) == Action::None, "trigger/button-only: idle -> None");
    check(h.step(false, false, true,  false) == Action::Toggle, "trigger/button-only: button rise -> Toggle");
    check(h.step(false, false, true,  true)  == Action::None,  "trigger/button-only: held -> no refire");
    check(h.step(false, false, false, true)  == Action::None,  "trigger/button-only: release -> None");
    check(h.step(false, false, true,  true)  == Action::Toggle, "trigger/button-only: re-press -> Toggle");
}

static void test_trigger_jack_only() {
    RecordGateHelper h;
    check(h.step(false, false, false, false) == Action::None,   "trigger/jack-only: idle -> None");
    check(h.step(false, true,  false, false) == Action::Toggle, "trigger/jack-only: jack rise -> Toggle");
    check(h.step(false, true,  false, true)  == Action::None,   "trigger/jack-only: held high -> no refire");
    check(h.step(false, false, false, true)  == Action::None,   "trigger/jack-only: jack falls -> None");
    check(h.step(false, true,  false, true)  == Action::Toggle, "trigger/jack-only: jack re-rises -> Toggle");
}

static void test_trigger_simultaneous() {
    RecordGateHelper h;
    // Button and jack rise on the exact same sample: exactly ONE Toggle, not
    // two, even though this is an OR of two independent edges.
    check(h.step(false, true, true, false) == Action::Toggle, "trigger/simultaneous: both rise together -> one Toggle");
    check(h.step(false, true, true, true)  == Action::None,   "trigger/simultaneous: both held -> no refire");
}

// VCV baseline (pre-existing code, still true today): recBtn and recTrig are
// each their own independent Schmitt trigger, so a button press does NOT get
// suppressed just because the jack's trigger is already latched high.
static void test_trigger_button_press_while_jack_high_fires() {
    RecordGateHelper h;
    check(h.step(false, true,  false, false) == Action::Toggle, "trigger/button-while-jack-high: jack rise fires");
    check(h.step(false, true,  true,  true)  == Action::Toggle, "trigger/button-while-jack-high: button press while jack still high FIRES (independent edges, VCV baseline)");
    check(h.step(false, true,  false, true)  == Action::None,   "trigger/button-while-jack-high: button release, jack still high -> None");
    check(h.step(false, false, false, true)  == Action::None,   "trigger/button-while-jack-high: jack falls -> None");
}

// Symmetric case: jack rising while the button is already held must fire too.
static void test_trigger_jack_rise_while_button_held_fires() {
    RecordGateHelper h;
    check(h.step(false, false, true,  false) == Action::Toggle, "trigger/jack-while-button-held: button press fires");
    check(h.step(false, true,  true,  true)  == Action::Toggle, "trigger/jack-while-button-held: jack rising while button still held FIRES (independent edges, VCV baseline)");
}

// ---------------------------------------------------------------------------
// Gate mode: jack rising/falling edges drive Toggle/Close/None per the spec
// table; the button remains an independent press-to-toggle escape hatch. A
// rising edge while already recording is ignored (no synthesized "punch" --
// see the design doc for why).
// ---------------------------------------------------------------------------

static void test_gate_initial_start_on_jack_rise() {
    RecordGateHelper h;
    h.syncTo(false);
    check(h.step(true, false, false, false) == Action::None,   "gate/initial-start: idle -> None");
    check(h.step(true, true,  false, false) == Action::Toggle, "gate/initial-start: jack rise, not recording -> Toggle");
}

static void test_gate_close_on_jack_fall_while_recording() {
    RecordGateHelper h;
    h.syncTo(true);   // jack already high (as if we just started recording)
    check(h.step(true, false, false, true) == Action::Close, "gate/close: jack fall while recording -> Close");
}

static void test_gate_falling_while_stopped_noop() {
    RecordGateHelper h;
    h.syncTo(true);
    check(h.step(true, false, false, false) == Action::None, "gate/falling-while-stopped: jack fall, not recording -> None");
}

// No Punch: a rising edge while already recording (e.g. mid a rolled-on
// overdub pass, or even mid the still-open initial pass) is ignored.
static void test_gate_ignores_rise_while_recording() {
    RecordGateHelper h;
    h.syncTo(false);
    check(h.step(true, true, false, true) == Action::None, "gate/ignore-rise-while-recording: jack rise while already recording -> None (no punch)");
}

static void test_gate_button_always_toggle() {
    RecordGateHelper h;
    // Button press-to-toggle is unconditional in Gate mode: not recording...
    check(h.step(true, false, true, false) == Action::Toggle, "gate/button: press while not recording -> Toggle");
    // ...and while recording (button never synthesizes a Close/ignore).
    RecordGateHelper h2;
    check(h2.step(true, false, true, true) == Action::Toggle, "gate/button: press while recording -> Toggle (jack alone would ignore this)");
}

// Jack and button both land on the same sample while recording -- jack alone
// would report Close (falling edge, recording), but the button (the
// documented escape hatch) must win and report a plain Toggle instead.
static void test_gate_button_priority_over_jack_same_sample() {
    RecordGateHelper h;
    h.syncTo(false);
    check(h.step(true, true, false, false) == Action::Toggle, "gate/priority: jack rise starts recording");
    check(h.step(true, false, true, true) == Action::Toggle, "gate/priority: button pressed while jack falls -> Toggle (button wins over Close)");
}

// ---------------------------------------------------------------------------
// syncTo / mode-switch: prevButton_/prevJack_ track raw levels in BOTH modes,
// so a mode flip needs no special-case resync -- only patch load (where the
// detector hasn't seen any level yet) needs syncTo().
// ---------------------------------------------------------------------------

static void test_sync_to_suppresses_phantom_edge() {
    RecordGateHelper h;
    h.syncTo(true);   // simulate patch load with the jack already high
    check(h.step(true, true, false, false) == Action::None, "syncTo: high gate at load fires no phantom edge");
}

static void test_mode_switch_trigger_to_gate_no_phantom_edge() {
    RecordGateHelper h;
    // Start in Trigger mode with jack high (an edge already consumed).
    check(h.step(false, true, false, false) == Action::Toggle, "mode-switch: initial trigger edge fires once");
    check(h.step(false, true, false, true)  == Action::None,   "mode-switch: held high, no refire");
    // Flip to Gate mode while the jack is STILL high: must not fire again --
    // prevJack_ already equals the current level, no special-casing needed.
    check(h.step(true, true, false, true) == Action::None, "mode-switch trigger->gate: high gate at flip fires no phantom edge");
}

static void test_mode_switch_gate_to_trigger_no_phantom_edge() {
    RecordGateHelper h;
    h.syncTo(true);
    check(h.step(true, true, false, false) == Action::None, "mode-switch: prime gate mode with jack already high");
    // Flip back to Trigger mode while jack is still high: must not fire.
    check(h.step(false, true, false, false) == Action::None, "mode-switch gate->trigger: high gate at flip fires no phantom edge");
}

// ---------------------------------------------------------------------------
// Integration: drive a real LoopEngine through the spec's gate-mode timeline
// for both "When recording ends" settings.
// ---------------------------------------------------------------------------

static void applyAction(LoopEngine& e, Action a, bool setting) {
    switch (a) {
        case Action::Toggle:
        case Action::Close:
            e.toggleRecord(setting);
            break;
        case Action::None:
            break;
    }
}

static void stepEngine(LoopEngine& e, RecordGateHelper& h, bool jackHigh, bool setting,
                       const char* label) {
    const bool engineRecording = e.isRecording();
    const Action a = h.step(true, jackHigh, false, engineRecording);
    applyAction(e, a, setting);
    std::array<LoopEngine::HeadOut, LoopEngine::NUM_HEADS> heads;
    e.process(0.f, 0.f, heads);
    std::printf("  [%s] jack=%d -> %s (recording=%d, loopLen=%zu)\n",
                label, jackHigh, actionName(a), e.isRecording(), e.loopLength());
}

// Low test sample rate (matches test_loop_engine.cpp's convention), where the
// write-declick ramp is inactive (xfadeSamples_ == 0 at 10 Hz) and Close
// resolves within the same sample, so engine-state assertions are simple.
static void test_integration_gate_plays_back(bool setting, const char* label) {
    LoopEngine e;
    e.reset(10.f, 100.f);
    RecordGateHelper h;
    h.syncTo(false);

    // Rising edge, not recording -> Toggle: starts the initial pass.
    stepEngine(e, h, true, setting, label);
    check(e.isRecording(), "integration/plays-back: recording after initial rise");
    check(e.loopLength() == 0, "integration/plays-back: loop length still 0 mid initial pass");

    for (int i = 0; i < 3; ++i) stepEngine(e, h, true, setting, label);   // 4 samples written total

    // Falling edge, recording (initial pass) -> Close: freezes loop length.
    stepEngine(e, h, false, setting, label);
    check(e.loopLength() == 4, "integration/plays-back: loop length frozen at 4");
    check(!e.isRecording(), "integration/plays-back: 'Plays back' setting stops recording");

    // Rising edge, not recording, loop exists -> Toggle: rolled-on overdub.
    stepEngine(e, h, true, setting, label);
    check(e.isRecording(), "integration/plays-back: overdub pass started");
    check(e.loopLength() == 4, "integration/plays-back: loop length unchanged by overdub start");

    // Falling edge, recording (overdub pass) -> Close: ends the overdub pass.
    stepEngine(e, h, false, setting, label);
    check(!e.isRecording(), "integration/plays-back: overdub pass closed");
    check(e.loopLength() == 4, "integration/plays-back: loop length unchanged by overdub close");

    // Falling edge while already stopped -> no-op.
    stepEngine(e, h, false, setting, label);
    check(!e.isRecording(), "integration/plays-back: falling-while-stopped is a no-op");
}

// The real "Keeps overdubbing" gate lifecycle (no punch): gate 1 rising
// starts the initial pass, falling freezes the loop length AND rolls into
// continuous overdub; gate 2 rising is IGNORED (already recording, state
// must be unchanged), falling stops that overdub pass -> playback; gate 3+
// are plain overdub punch-in/out passes (rising starts, falling stops), same
// as the "Plays back" setting from then on. Run at a REAL sample rate
// (48 kHz, xfadeSamples_ > 0) so the stop-ramp is actually exercised, not
// bypassed the way the 10 Hz tests above bypass it.
static void test_integration_gate_keeps_overdubbing_lifecycle_48k() {
    LoopEngine e;
    e.reset(48000.f, 0.02f);   // maxSamples = 960; small but real crossfade ramp
    RecordGateHelper h;
    h.syncTo(false);
    const bool setting = true;   // "Keeps overdubbing"
    const char* label = "keeps-overdubbing-48k";

    // Gate 1 rising: not recording -> Toggle, starts the initial pass.
    stepEngine(e, h, true, setting, label);
    check(e.isRecording(), "48k/gate1-rise: recording after initial rise");
    for (int i = 0; i < 49; ++i) stepEngine(e, h, true, setting, label);   // 50 samples written

    // Gate 1 falling: recording (initial pass) -> Close. Freezes loop length
    // at 50 AND rolls straight into continuous overdub ("Keeps overdubbing").
    stepEngine(e, h, false, setting, label);
    check(e.loopLength() == 50, "48k/gate1-fall: loop length frozen at 50");
    check(e.isRecording(), "48k/gate1-fall: rolls into continuous overdub, still recording");
    const std::size_t loopLenAfterGate1 = e.loopLength();

    for (int i = 0; i < 5; ++i) stepEngine(e, h, false, setting, label);   // jack stays low, overdub continues

    // Gate 2 rising: ALREADY recording (the continuous overdub from gate 1)
    // -> ignored. State must be completely unchanged.
    stepEngine(e, h, true, setting, label);
    check(e.isRecording(), "48k/gate2-rise: still recording (rise while recording is ignored)");
    check(e.loopLength() == loopLenAfterGate1, "48k/gate2-rise: loop length unchanged by the ignored rise");

    // Gate 2 falling: recording -> Close. At a real sample rate this ARMS the
    // async stop-declick ramp (LoopEngine::toggleRecord's odGainStep_ > 0
    // branch); isRecording() does NOT flip immediately -- it only clears once
    // the ramp completes over the next xfadeSamples_ samples of process().
    stepEngine(e, h, false, setting, label);
    check(e.isRecording(), "48k/gate2-fall: still recording immediately after Close (ramp just armed, not complete)");
    for (int i = 0; i < 300; ++i) stepEngine(e, h, false, setting, label);   // let the ~240-sample ramp finish
    check(!e.isRecording(), "48k/gate2-fall: stop-overdub ramp completed -> playback (rec=0)");
    check(e.loopLength() == loopLenAfterGate1, "48k/gate2-fall: loop length still unchanged");

    // Gate 3 rising: not recording, loop exists -> Toggle, a plain overdub
    // punch-in pass.
    stepEngine(e, h, true, setting, label);
    check(e.isRecording(), "48k/gate3-rise: punch-in pass started");
    for (int i = 0; i < 9; ++i) stepEngine(e, h, true, setting, label);

    // Gate 3 falling: recording -> Close, punch-out (same ramp-arm-then-
    // finish behavior as gate 2's fall).
    stepEngine(e, h, false, setting, label);
    check(e.isRecording(), "48k/gate3-fall: still recording immediately after Close (ramp just armed)");
    for (int i = 0; i < 300; ++i) stepEngine(e, h, false, setting, label);
    check(!e.isRecording(), "48k/gate3-fall: punch-out ramp completed -> stopped");
    check(e.loopLength() == loopLenAfterGate1, "48k/gate3-fall: loop length still unchanged by a plain overdub pass");
}

int main() {
    test_trigger_button_only();
    test_trigger_jack_only();
    test_trigger_simultaneous();
    test_trigger_button_press_while_jack_high_fires();
    test_trigger_jack_rise_while_button_held_fires();

    test_gate_initial_start_on_jack_rise();
    test_gate_close_on_jack_fall_while_recording();
    test_gate_falling_while_stopped_noop();
    test_gate_ignores_rise_while_recording();
    test_gate_button_always_toggle();
    test_gate_button_priority_over_jack_same_sample();

    test_sync_to_suppresses_phantom_edge();
    test_mode_switch_trigger_to_gate_no_phantom_edge();
    test_mode_switch_gate_to_trigger_no_phantom_edge();

    test_integration_gate_plays_back(false, "plays-back");
    test_integration_gate_keeps_overdubbing_lifecycle_48k();

    return failures == 0 ? 0 : 1;
}

// Unit tests for loooop::RecordGateHelper (header-only) plus an integration
// sequence against a real LoopEngine, exercising the spec's gate-mode action
// table end to end.
//
// Spec: docs/superpowers/specs/2026-07-25-loooop-record-gate-design.md

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
        case Action::Punch:  return "Punch";
    }
    return "?";
}

// ---------------------------------------------------------------------------
// Trigger-mode parity: the helper must reproduce today's single combined-edge
// behavior `(recPressed || recTrig) && !recPrev_` exactly -- ONE Toggle per
// combined rising edge, never one per input.
// ---------------------------------------------------------------------------

static void test_trigger_button_only() {
    RecordGateHelper h;
    // gateMode=false (Trigger), jack low throughout, button rises then falls.
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
    // Button and jack rise on the exact same sample: exactly ONE Toggle, not two.
    check(h.step(false, true, true, false) == Action::Toggle, "trigger/simultaneous: both rise together -> one Toggle");
    check(h.step(false, true, true, true)  == Action::None,   "trigger/simultaneous: both held -> no refire");
}

// Hard requirement from the design review: a button press while the jack is
// already high must NOT fire a second edge (matches today's single recPrev_
// latch on the OR'd signal).
static void test_trigger_button_press_while_jack_high_no_fire() {
    RecordGateHelper h;
    check(h.step(false, true,  false, false) == Action::Toggle, "trigger/button-while-jack-high: jack rise fires");
    check(h.step(false, true,  true,  true)  == Action::None,   "trigger/button-while-jack-high: button press while jack still high does NOT fire");
    check(h.step(false, true,  false, true)  == Action::None,   "trigger/button-while-jack-high: button release, jack still high -> None");
    check(h.step(false, false, false, true)  == Action::None,   "trigger/button-while-jack-high: jack falls -> None");
    // Both released now; a fresh button press should fire normally.
    check(h.step(false, false, true,  true)  == Action::Toggle, "trigger/button-while-jack-high: fresh button press after both released -> Toggle");
}

// Same corner, phrased the other way: jack rising while the button is already
// held must NOT fire a second edge either (symmetry of the OR'd latch).
static void test_trigger_jack_rise_while_button_held_no_fire() {
    RecordGateHelper h;
    check(h.step(false, false, true,  false) == Action::Toggle, "trigger/jack-while-button-held: button press fires");
    check(h.step(false, true,  true,  true)  == Action::None,   "trigger/jack-while-button-held: jack rising while button still held does NOT fire");
}

// ---------------------------------------------------------------------------
// Gate mode: jack rising/falling edges drive Toggle/Punch/Close/None per the
// spec table; the button remains an independent press-to-toggle escape hatch.
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

static void test_gate_punch_while_recording() {
    RecordGateHelper h;
    h.syncTo(false);
    check(h.step(true, true, false, true) == Action::Punch, "gate/punch: jack rise while already recording -> Punch");
}

static void test_gate_button_always_toggle() {
    RecordGateHelper h;
    // Button press-to-toggle is unconditional in Gate mode: not recording...
    check(h.step(true, false, true, false) == Action::Toggle, "gate/button: press while not recording -> Toggle");
    // ...and while recording (button never synthesizes a Punch/Close).
    RecordGateHelper h2;
    check(h2.step(true, false, true, true) == Action::Toggle, "gate/button: press while recording -> Toggle (not Punch)");
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
// syncTo / mode-switch phantom-edge suppression.
// ---------------------------------------------------------------------------

static void test_sync_to_suppresses_phantom_edge() {
    RecordGateHelper h;
    h.syncTo(true);   // simulate patch load with the jack already high
    check(h.step(true, true, false, false) == Action::None, "syncTo: high gate at load fires no phantom edge");
}

static void test_mode_switch_trigger_to_gate_suppresses_phantom_edge() {
    RecordGateHelper h;
    // Start in Trigger mode with jack high (an edge already consumed).
    check(h.step(false, true, false, false) == Action::Toggle, "mode-switch: initial trigger edge fires once");
    check(h.step(false, true, false, true)  == Action::None,   "mode-switch: held high, no refire");
    // Flip to Gate mode while the jack is STILL high: must not fire again.
    check(h.step(true, true, false, true) == Action::None, "mode-switch trigger->gate: high gate at flip fires no phantom edge");
}

static void test_mode_switch_gate_to_trigger_suppresses_phantom_edge() {
    RecordGateHelper h;
    h.syncTo(true);
    check(h.step(true, true, false, false) == Action::None, "mode-switch: prime gate mode with jack already high");
    // Flip back to Trigger mode while jack is still high: must not fire.
    check(h.step(false, true, false, false) == Action::None, "mode-switch gate->trigger: high gate at flip fires no phantom edge");
}

// ---------------------------------------------------------------------------
// Integration: drive a real LoopEngine through the spec's gate-mode timeline
// for both "When recording ends" settings. Uses a low test sample rate
// (matches test_loop_engine.cpp's convention) so the write-declick ramp is
// inactive (xfadeSamples_ == 0 at 10 Hz) and Close/Punch resolve within the
// same sample -- the same low-rate convention the rest of this test dir uses
// to keep engine-state assertions deterministic.
// ---------------------------------------------------------------------------

static void applyAction(LoopEngine& e, Action a, bool setting) {
    switch (a) {
        case Action::Toggle:
        case Action::Close:
            e.toggleRecord(setting);
            break;
        case Action::Punch:
            e.toggleRecord(setting);
            e.toggleRecord();
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

static void test_integration_gate_keeps_overdubbing(bool setting, const char* label) {
    LoopEngine e;
    e.reset(10.f, 100.f);
    RecordGateHelper h;
    h.syncTo(false);

    // Rising edge, not recording -> Toggle: starts the initial pass.
    stepEngine(e, h, true, setting, label);
    for (int i = 0; i < 3; ++i) stepEngine(e, h, true, setting, label);   // 4 samples written

    // Falling edge, recording (initial pass) -> Close: "Keeps overdubbing"
    // rolls straight into a continuous overdub pass instead of stopping.
    stepEngine(e, h, false, setting, label);
    check(e.loopLength() == 4, "integration/keeps-overdubbing: loop length frozen at 4");
    check(e.isRecording(), "integration/keeps-overdubbing: setting keeps recording (continuous overdub)");

    for (int i = 0; i < 2; ++i) stepEngine(e, h, false, setting, label);   // jack stays low, still overdubbing

    // Rising edge while already recording (rolled-on overdub) -> Punch:
    // closes the current pass and immediately opens a fresh one. At this low
    // test sample rate (xfadeSamples_ == 0) the two toggleRecord() calls
    // resolve synchronously, so isRecording() reads true on both sides of the
    // punch and loopLength() is unaffected -- the observable state is
    // identical to "nothing happened" via these two accessors alone; the
    // punch's effect (a fresh write pass from the loop start) isn't visible
    // through isRecording()/loopLength(), only through the recorded content.
    stepEngine(e, h, true, setting, label);
    check(e.isRecording(), "integration/keeps-overdubbing: still recording across the punch");
    check(e.loopLength() == 4, "integration/keeps-overdubbing: loop length unaffected by punch");
}

int main() {
    test_trigger_button_only();
    test_trigger_jack_only();
    test_trigger_simultaneous();
    test_trigger_button_press_while_jack_high_no_fire();
    test_trigger_jack_rise_while_button_held_no_fire();

    test_gate_initial_start_on_jack_rise();
    test_gate_close_on_jack_fall_while_recording();
    test_gate_falling_while_stopped_noop();
    test_gate_punch_while_recording();
    test_gate_button_always_toggle();
    test_gate_button_priority_over_jack_same_sample();

    test_sync_to_suppresses_phantom_edge();
    test_mode_switch_trigger_to_gate_suppresses_phantom_edge();
    test_mode_switch_gate_to_trigger_suppresses_phantom_edge();

    test_integration_gate_plays_back(false, "plays-back");
    test_integration_gate_keeps_overdubbing(true, "keeps-overdubbing");

    return failures == 0 ? 0 : 1;
}

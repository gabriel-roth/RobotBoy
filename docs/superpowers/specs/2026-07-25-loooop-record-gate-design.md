# Loooop / Löp: Record-jack Gate mode

**Date:** 2026-07-25. **Status:** approved design (user-selected semantics);
implemented on branch `loooop-record-gate`; merge after the user's VCV
listening pass.

## Summary

A new patch-persistent option, **"Record jack": Trigger (default) / Gate**,
on both Loooop and Löp, both hosts. Gate mode reinterprets only the Record
jack: recording starts on the rising edge and the falling edge closes the
pass the same way today's second trigger does. The panel Record button
keeps press-to-toggle behavior in both modes.

**Trigger mode correction (post-review):** Trigger mode is an OR of the
button's and the jack's rising edges tracked **independently** — each input
has its own edge latch, matching VCV's existing code
(`recBtn.process() || recTrig.process()`, two separate `dsp::SchmittTrigger`s)
byte-for-byte. This is NOT an edge of the OR'd level: a button press while
the jack is already held high still fires, and a jack rise while the button
is already held still fires too (both rising on the same sample still
collapses to one Toggle). MetaModule's prior code used a single combined-
level latch instead (`(recPressed||recTrig) && !recPrev_`), under which the
button went dead while the jack was held high; adopting VCV's two-
independent-edges behavior for MetaModule too is a deliberate behavior
change, not just parity — noted for the changelog/manual.

The existing "Trigger when recording" setting is renamed **"When recording
ends"** with options **"Plays back"** (was "Stops recording") and **"Keeps
overdubbing"** (was "Starts overdubbing"). Same param, same stored 0/1
values — labels only, so existing patches keep their choice.

## Gate-mode edge rules

All expressed through the existing `LoopEngine::toggleRecord(bool
continueOverdub)`; the engine is not modified.

| Event | Engine state | Action |
|---|---|---|
| Rising edge | not recording | `toggleRecord(setting)` — start initial record, or an overdub pass if a loop exists |
| Rising edge | recording (already open, initial or an overdub pass) | ignored — no action |
| Falling edge | recording | `toggleRecord(setting)` — "Plays back": freeze + play; "Keeps overdubbing": freeze length, roll into continuous overdub |
| Falling edge | not recording | no-op |

**No "punch" action.** An earlier draft of this spec proposed synthesizing a
punch (`toggleRecord(setting)` then `toggleRecord()`) when the jack rose
while already recording, to "close the pass and immediately open a fresh
one." Reviewed and rejected: `LoopEngine::toggleRecord` calling itself twice
in the same sample doesn't behave sanely at real (non-zero-crossfade) sample
rates — the two calls just arm and then immediately disarm the same
`stopPending_` flag before any audio sample advances the ramp, so it's a
silent no-op in practice, and worse, if the jack rises while the module is
still recording its still-open INITIAL pass (say, started from the panel
button), the first of the two calls closes that initial pass — so the second
call can arm an unwanted stop instead of "reopening" anything. The engine
can't be changed to support a synchronous punch (out of scope), so a rising
edge while already recording is simply ignored.

**Real gate lifecycle under "Keeps overdubbing"** (no punch, worked through
in full — this is what the manual describes): gate 1 rising starts the
initial recording pass; gate 1 falling freezes the loop length AND rolls
straight into a continuous overdub pass (per "Keeps overdubbing"). Gate 2
rising is ignored (already recording); gate 2 falling stops that overdub
pass, and the loop plays back. From gate 3 on, the gate is a plain overdub
punch-in/punch-out control: rising starts a fresh overdub pass, falling
stops it — exactly the same as the "Plays back" setting's steady-state
behavior. There is no separate "escape hatch" needed: every gate pulse from
the second one on does something sensible and reachable purely from the
gate, because a rising edge while recording is now a safe no-op rather than
a state that traps the loop in perpetual overdub.

Mode switches and patch loads must initialize the jack edge detector to the
current jack level so a high gate at load / mode-flip fires no phantom edge.
The gate threshold matches each host's EXISTING trigger comparator exactly —
these already differ between hosts and that difference is preserved, not
unified: VCV's `dsp::SchmittTrigger` (0.1 V fall / 2.0 V rise hysteresis) on
both modules; MetaModule's plain `> 1.0f` level comparison (no hysteresis)
on both cores.

## Structure: one shared helper, four hosts

Extract the edge logic into `loooop::RecordGateHelper` in
`src/loooop/LooperModuleDSP.hpp` (shared by Loooop.cpp, Lop.cpp,
LoooopCore.cc, LopCore.cc):

```cpp
// Decides record actions from the Record button + jack per sample.
// Trigger mode: button and jack rising edges tracked INDEPENDENTLY, OR'd ->
// Toggle (VCV's existing two-independent-Schmitt-triggers behavior).
// Gate mode: button rising edge -> Toggle; jack edges per the spec table.
struct RecordGateHelper {
    enum class Action : uint8_t { None, Toggle, Close };
    // gateMode: current option; jackHigh: comparator output; buttonPressed;
    // engineRecording: LoopEngine::isRecording() this sample.
    Action step(bool gateMode, bool jackHigh, bool buttonPressed,
                bool engineRecording);
    void syncTo(bool jackHigh);   // call on patch load / first process
    // (members: prevButton_, prevJack_ -- each input's raw previous level,
    // tracked the same way in both modes, so a mode switch needs no special
    // resync)
};
```

Hosts map the Action: `Toggle` → `toggleRecord(setting)` (button path —
identical to today, including using the setting when it closes the initial
pass); `Close` → `toggleRecord(setting)`. In Trigger mode the helper
reproduces VCV's existing OR-of-two-independent-edges exactly (one `Toggle`
per sample even when both edges fire together — Action is single-valued per
call — but each input's edge is detected on its own, so neither input can
suppress the other's edge the way a combined-level latch would).

## Params / persistence / ordering

- VCV: new `configSwitch` `REC_GATE_MODE_PARAM` ("Record jack",
  {"Trigger", "Gate"}, default 0, `randomizeEnabled = false`), **appended at
  the END of the ParamId enum** in both modules (param reorder scrambles old
  patches — hard requirement). Context-menu submenu "Record jack" mirrors
  the existing "Trigger when recording" submenu pattern; that submenu's
  strings change to "When recording ends" / {"Plays back", "Keeps
  overdubbing"}.
- MetaModule: new Alt element ("Record jack": Trigger/Gate) **appended at
  the end** of both cores' element Info lists; options-list label ASCII
  only. `TrigWhenRecAlt`'s labels updated to the new wording.
- Follow-up, out of scope: add the new param to `param_ranges.json` in
  yml-to-vcv (see `add-vcv-plugin-params` skill).

## Tests

`tests/loooop/` gains `test_record_gate.cpp` (picked up by the run.sh glob;
links `../src/loooop/dsp/LoopEngine.cpp` via `.extra` for the integration
part): drive `RecordGateHelper` through scripted edge/state sequences and
assert the Action stream for: trigger-mode parity (button-only, jack-only,
simultaneous, AND the two corner cases where one input's edge must fire even
while the other is already held high/low — the independent-edges behavior),
gate-mode initial record start/stop both settings, a rising edge while
already recording being ignored, falling-while-stopped no-op, and `syncTo`
suppressing the phantom edge. Plus two integration sequences against a real
`LoopEngine` (link via `.extra`): the "Plays back" timeline at a low test
sample rate, and the full "Keeps overdubbing" gate lifecycle (gate 1/2/3+ as
described above) at a REAL sample rate (48 kHz) so the write-declick ramp is
actually exercised, asserting `isRecording()`/`loopLength()` at each step —
including that a rising edge while already recording leaves state completely
unchanged, and that a falling edge only clears `isRecording()` once the ramp
completes.

## Docs

`Loooop.md`: context-menu section — rename the setting, describe both its
options in the new wording, add the "Record jack" entry describing Trigger
mode (independent OR'd edges) and the real Gate-mode lifecycle above (no
punch, no escape-hatch caveat needed), plus a note that the gate threshold
differs slightly per host (VCV: 2.0 V rise / 0.1 V fall hysteresis; MM: plain
`> 1 V`). Escape literal tildes as `\~` if any are added.

## Out of scope

Engine changes; MM `param_ranges.json`; any change to Clear or per-head
Trig jacks; a gate mode for the Clear jack.

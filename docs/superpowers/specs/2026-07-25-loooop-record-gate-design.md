# Loooop / Löp: Record-jack Gate mode

**Date:** 2026-07-25. **Status:** approved design (user-selected semantics);
implemented on branch `loooop-record-gate`; merge after the user's VCV
listening pass.

## Summary

A new patch-persistent option, **"Record jack": Trigger (default) / Gate**,
on both Loooop and Löp, both hosts. Trigger mode is byte-for-byte today's
behavior. Gate mode reinterprets only the Record jack: recording starts on
the rising edge and the falling edge closes the pass the same way today's
second trigger does. The panel Record button keeps press-to-toggle behavior
in both modes.

The existing "Trigger when recording" setting is renamed **"When recording
ends"** with options **"Plays back"** (was "Stops recording") and **"Keeps
overdubbing"** (was "Starts overdubbing"). Same param, same stored 0/1
values — labels only, so existing patches keep their choice.

## Gate-mode edge rules (the "literal translation" semantics)

All expressed through the existing `LoopEngine::toggleRecord(bool
continueOverdub)`; the engine is not modified.

| Event | Engine state | Action |
|---|---|---|
| Rising edge | not recording | `toggleRecord()` — start initial record, or an overdub pass if a loop exists |
| Rising edge | recording (e.g. rolled-on overdub) | punch: `toggleRecord(setting)` then `toggleRecord()` — close the pass, immediately open a fresh one |
| Falling edge | recording | `toggleRecord(setting)` — "Plays back": freeze + play; "Keeps overdubbing": freeze length, roll into continuous overdub |
| Falling edge | not recording | no-op |

Known and accepted: with "Keeps overdubbing", the gate alone can never
reach plain playback (falling re-enters overdub, rising punches); the
panel button is the escape hatch. Documented in the manual.

Mode switches and patch loads must initialize the jack edge detector to the
current jack level so a high gate at load / mode-flip fires no phantom edge.
The gate threshold matches the existing trigger comparator (`> 1.0f`, with
whatever hysteresis the current code has — match it exactly).

## Structure: one shared helper, four hosts

Extract the edge logic into `loooop::RecordGateHelper` in
`src/loooop/LooperModuleDSP.hpp` (shared by Loooop.cpp, Lop.cpp,
LoooopCore.cc, LopCore.cc):

```cpp
// Decides record actions from the Record button + jack per sample.
// Trigger mode: button||jack rising edge -> Toggle (today's behavior).
// Gate mode: button rising edge -> Toggle; jack edges per the spec table.
struct RecordGateHelper {
    enum class Action : uint8_t { None, Toggle, Close, Punch };
    // gateMode: current option; jackHigh: comparator output; buttonPressed;
    // engineRecording: LoopEngine::isRecording() this sample.
    Action step(bool gateMode, bool jackHigh, bool buttonPressed,
                bool engineRecording);
    void syncTo(bool jackHigh);   // call on mode change / patch load
    // (members: prevButton_, prevJack_, prevMode_)
};
```

Hosts map the Action: `Toggle` → `toggleRecord(setting)` (button path —
identical to today, including using the setting when it closes the initial
pass); `Close` → `toggleRecord(setting)`; `Punch` → `toggleRecord(setting)`
then `toggleRecord()`. In Trigger mode the helper reproduces today's OR'd
edge exactly (one `Toggle` per combined rising edge — NOT one per input —
preserving current behavior when both fire in the same sample).

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
add a `.extra` file only if it links engine sources — it shouldn't need to,
the helper is header-only): drive `RecordGateHelper` through scripted
edge/state sequences and assert the Action stream for: trigger-mode parity
(button-only, jack-only, simultaneous), gate-mode initial record start/stop
both settings, punch while rolled-on overdub, falling-while-stopped no-op,
and `syncTo` suppressing the phantom edge. Plus an integration sequence
against a real `LoopEngine` (link via `.extra`) asserting
`isRecording()`/`loopLength()` transitions across the spec's timeline for
both settings.

## Docs

`Loooop.md`: context-menu section — rename the setting, describe both its
options in the new wording, add the "Record jack" entry including the
gate-mode table in prose and the escape-hatch note. Escape literal tildes
as `\~` if any are added.

## Out of scope

Engine changes; MM `param_ranges.json`; any change to Clear or per-head
Trig jacks; a gate mode for the Clear jack.

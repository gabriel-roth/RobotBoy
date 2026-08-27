# Loooop / Löp: Wet monitors Dry while the buffer is empty

**Date:** 2026-08-27. **Status:** approved design; implementation not started.

## Summary

Today, while the record buffer is empty (no loop exists yet), each engine
head's output is silence — `LoopEngine::process()` zeroes `HeadOut` whenever
`loopLen_ == 0`. That silence feeds straight into the Wet side of the
Dry/Wet crossfade, so during the very first recording pass the Wet output is
silent no matter where the Dry/Wet knob sits: there's nothing recorded yet
for a head to read back.

This spec makes the Wet side of the Mix output monitor the live Dry signal
while actively recording the first pass, so a 100%-wet Mix output still lets
you hear what's being recorded. As soon as a loop exists (`hasLoop()` true),
Wet reverts to reading the buffer as it does today — including during a
later overdub pass, which is not "the first time" and is unaffected.

Applies to both Loooop (4-head) and Löp (single-head) — VCV and MetaModule
builds of each, four host files total.

## Behavior

| State | `isRecording()` | `hasLoop()` | Mix/Wet output |
|---|---|---|---|
| Idle, empty buffer (fresh patch / just after Clear) | false | false | silent (unchanged) |
| Recording the first pass | true | false | **dry passthrough (new)** |
| Loop closed, not recording | false | true | buffer read (unchanged) |
| Overdubbing an existing loop | true | true | buffer read (unchanged — "first time" only) |

Scope is the Mix/Wet bus only. Loooop's four individual head outputs
(`HEAD1–4` L/R jacks) are unaffected and stay silent until a loop exists —
they represent actual head reads, which genuinely don't exist yet.

**Closing-transition edge case:** when the first pass ends (`hasLoop()`
flips false→true, whether by hitting the max-buffer ceiling or by closing
the loop manually), Wet snaps from live dry input to whatever the read
head's window position produces. This snap already exists today (silence →
buffer content); this feature doesn't add a new discontinuity, it just makes
the "before" state audible instead of silent.

**Dry/Wet knob interaction:** since `dryWet(dry, dry, w) == dry` for any
mix `w`, once triggered the Mix output equals the dry input regardless of
knob position — "Wet is the same as Dry" holds literally, not just at one
knob setting.

## Structure: one shared helper, four hosts

No `LoopEngine` (native-core) changes. `isRecording()` and `hasLoop()`
already exist and are exactly the two facts needed. Add one small
Rack-free, MetaModule-free helper to `src/loooop/LooperModuleDSP.hpp`,
alongside `dryWet()` et al.:

```cpp
// True while actively recording the very first pass (no loop exists yet):
// the Wet bus has nothing to read back, so hosts should monitor the dry
// signal instead of the (currently silent) head output. False once a loop
// exists, even during a later overdub pass -- "first time" only.
inline bool monitorDryWhileEmpty(bool recording, bool hasLoop) {
    return recording && !hasLoop;
}
```

Call sites, immediately before each host's existing `dryWet()` call:

- `src/loooop/Loooop.cpp` — override `wetL`/`wetR` (the 4-head weighted sum)
  with `inL`/`inR` when `monitorDryWhileEmpty(engine.isRecording(),
  engine.hasLoop())`, before `outputs[MIX_L_OUTPUT]`/`MIX_R_OUTPUT`.
- `metamodule/loooop/LoooopCore.cc` — same override, before
  `setOutput<MixOutL>`/`<MixOutR>`.
- `src/loooop/Lop.cpp` — override the `hs[0].l`/`hs[0].r` operand fed into
  `dryWet()` for `OUT_L_OUTPUT`/`OUT_R_OUTPUT`.
- `metamodule/loooop/LopCore.cc` — same override, before
  `setOutput<OutL>`/`<OutR>`.

All four call sites use the same `monitorDryWhileEmpty()` symbol so VCV and
MetaModule can't drift out of sync, matching the existing pattern for
`dryWet`, `normalizedControl`, `panControl`, etc. in this file.

## Tests

`tests/loooop/test_module_dsp.cpp` (Lane 1, `tests/run.sh`, zero-dependency
`g++`, no Rack/MetaModule build needed) gains assertions covering all four
rows of the behavior table directly against `monitorDryWhileEmpty()`:

- `monitorDryWhileEmpty(false, false)` → false (idle, empty)
- `monitorDryWhileEmpty(true, false)` → true (recording first pass)
- `monitorDryWhileEmpty(false, true)` → false (loop closed, stopped)
- `monitorDryWhileEmpty(true, true)` → false (overdubbing existing loop)

Manual confirmation in the VCV/MetaModule GUI (listening pass at Wet=100%
during first recording, and across the closing-transition snap) goes on the
user's checklist, not an agent-driven simulator test.

## Out of scope

`LoopEngine` changes; the 4 individual `HEAD1–4` outputs on Loooop; any
change to Löp's single head output outside the Mix bus; idle-with-empty-
buffer behavior (stays silent, per the behavior table).

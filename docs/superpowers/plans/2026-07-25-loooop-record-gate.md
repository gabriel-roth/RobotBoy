# Loooop Record-Gate Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: superpowers:subagent-driven-development or executing-plans. Spec is authoritative: `docs/superpowers/specs/2026-07-25-loooop-record-gate-design.md`

**Goal:** Implement the spec on branch `loooop-record-gate`.

## Global Constraints

- New params appended at END of enums/element lists (old-patch safety — hard requirement).
- Trigger mode must be behavior-identical to today (helper parity tests pin it).
- Engine (`LoopEngine`) untouched.
- Commit style: short, one sentence, no AI attribution. Never stage user root .md files other than Loooop.md (which THIS plan legitimately edits for the manual section).
- MM labels ASCII only.

### Task 1: RecordGateHelper + tests (TDD)

**Files:** Modify `src/loooop/LooperModuleDSP.hpp` (add helper per spec's sketch). Create `tests/loooop/test_record_gate.cpp` (helper is header-only — no `.extra` needed for the unit part; the integration sequence links LoopEngine via a `.extra` file listing `../src/loooop/dsp/LoopEngine.cpp`, same as `test_loop_engine.cpp.extra`).

Steps: write the unit tests from the spec's test list first (they fail to compile — helper missing); implement the helper; green. Then the LoopEngine integration sequence (both settings' timelines from the spec table). Run `tests/run.sh` → exit 0. Commit: `Loooop: shared record-gate edge helper with tests`.

### Task 2: Wire the four hosts + rename setting + menu

**Files:** Modify `src/loooop/Loooop.cpp`, `src/loooop/Lop.cpp` (new `REC_GATE_MODE_PARAM` appended at ParamId END, `configSwitch("Record jack", {"Trigger","Gate"})`, `randomizeEnabled=false`; route record handling through `RecordGateHelper` incl. `syncTo` on mode-param change and on `dataFromJson`/first process; rename `TRIG_WHEN_REC_PARAM` display strings and the context-menu submenu to "When recording ends" {"Plays back","Keeps overdubbing"}; add "Record jack" submenu mirroring the existing pattern). Modify `metamodule/loooop/LoooopCore.cc`, `metamodule/loooop/LopCore.cc` (new Alt element appended at END of the Info element list — first READ how existing Alt elements are declared in the Info struct and options list; update TrigWhenRecAlt labels; route through the helper identically).

Steps: VCV first, `make -C vcv` clean + `tests/run.sh` green; then MM cores, MM cmake build clean ("All symbols found!"). Verify with `git diff` that no param/element was reordered — only appended. Commit: `Loooop/Lop: Record jack Trigger/Gate mode, rename end-of-recording setting`.

### Task 3: Manual

**Files:** Modify `Loooop.md` context-menu section per spec Docs paragraph. Commit: `Loooop manual: Record jack gate mode and renamed setting`.

### Task 4: Verification gate

`tests/run.sh` exit 0; `make -C vcv -B` clean; MM cmake clean; grep both `.cpp` diffs to confirm enum append-only. No commit (or fixups if needed).

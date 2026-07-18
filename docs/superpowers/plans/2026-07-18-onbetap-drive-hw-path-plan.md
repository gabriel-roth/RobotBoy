# Plan — Onbetap Drive straight-to-hardware output gain

Design: `docs/superpowers/specs/2026-07-18-onbetap-drive-hw-path-design.md`

Small, self-contained DSP change: remove the Drive dependence from the output
makeup gain. TDD — write the red test first, then make it green with the change,
then update docs. Commit at each checkpoint.

## Task 1 — Failing test (red)

Add `tests/onbetap/test_drive_level.cpp`, wired into `tests/run.sh`
(the `onbetap` dir is already iterated). It mirrors the module per-sample glue
(drive → 2× oversampled core → `DecimFir13` decimate → LP tap → makeup → `DCBlock`
→ `9·tanhish(v/9)` VCA), replicating `Onbetap.cpp::processSide` for the 2×/LP
path, and computes gains via `onbetap::driveGains(...)` (added in Task 2).

Because the helper doesn't exist yet, write the test against a **local forward
declaration / temporary include ordering** that compiles once Task 2 lands — OR
sequence it: create `drive.hpp` with the *current* (drive-dependent) formula
first so the test compiles and goes **red**, then Task 2 flips the formula to
constant to make it **green**. Use the second approach (cleaner):

1a. Create `src/onbetap/drive.hpp` with `driveGains(...)` using the **current**
    `makeup = pow(0.25/driveGain, kMakeupExp) * kOutScale * exp2(outDb/6.0206f)`
    formula (kMakeupExp = 0.75), moving the four constants in.
1b. Point `Onbetap.cpp` at the helper (no behavior change yet) — verify build.
1c. Write `test_drive_level.cpp` with the spec's assertions:
    - `level(0.5) ≥ level(0.0) − 0.5 dB` at res ∈ {0.0, 0.30, 0.70}
    - `level(1.0) ≥ level(0.0) − 1.0 dB` at res ∈ {0.0, 0.30, 0.70}
    - `THD(0.5) ≥ THD(0.0) + 2 pp` at res ∈ {0.0, 0.30, 0.70}
1d. Run `tests/run.sh` (or just the onbetap binaries) → confirm the level
    assertions FAIL (red) with the current drive-dependent makeup.

**Checkpoint:** commit test + helper-with-old-formula (red state documented in
commit body).

## Task 2 — Make it green

2a. In `src/onbetap/drive.hpp`, change `makeup` to the constant
    `kOutScale * exp2(outDb/6.0206f)`; delete `kMakeupExp`.
2b. Update the `Onbetap.cpp` file-header comment block (lines ~21-27) to describe
    the constant makeup + core self-compensation rationale.
2c. Run the onbetap tests → all green. Run the FULL `tests/run.sh` → no
    regressions (core, engine, python guards).

**Checkpoint:** commit the fix.

## Task 3 — Verify end-to-end in the real build

3a. Build + install the VCV plugin (`build-robotboy-plugin` skill: `make -C vcv`
    + copy dylib/json/res into the Rack2 plugins dir).
3b. Confirm it compiles clean against the real Rack headers (the helper include,
    the removed constants). This is the real-code check the host-free test can't
    give — the module actually links and loads.
3c. Re-run the host-free measurement harness (constant path) to reconfirm the
    spec's tables reproduce from the shipped formula.

**Checkpoint:** no commit unless build fixes needed.

## Task 4 — Docs

4a. `docs/research/onbetap-worklog.md`: dated entry — double-compensation finding,
    before/after tables, +6 dB target retired.
4b. `2026-07-15-onbetap-dsp-spec.md` §5: one-line "superseded by
    2026-07-18-… (constant makeup)" pointer.
4c. `Onbetap.md:15`: add grit clause to the Drive bullet.

**Checkpoint:** commit docs.

## Task 5 — Finish

5a. `superpowers:requesting-code-review` on the branch diff.
5b. Address findings if any.
5c. Summarize for the user; leave on branch `onbetap-drive-hw-path` for their
    merge decision (do not merge autonomously — matches house PR-review habit).

## Notes / risks

- Determinism: constant makeup is a pure scalar; no state, no RNG — determinism
  and NaN-sanitize behavior unaffected.
- MetaModule: the change is in shared `src/`; the `.mmplugin` build picks it up
  automatically. No MM-specific edits needed. Building the MM plugin is optional
  here (VCV build is the verification of record).
- The `tuneHeadroom`/`tuneOutDb`/`tuneDriveDb` menu sliders keep working — they
  feed straight into `driveGains(...)`.

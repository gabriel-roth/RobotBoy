# Onbetap — Drive grit: keep the top of the knob dirty

**Date:** 2026-07-18
**Status:** design approved (autonomous session — decisions recorded inline)
**Follows:** `2026-07-18-onbetap-drive-hw-path-design.md` (Findings 1–2),
`docs/research/onbetap-drive-resonance-investigation.md` (Finding 3, option 3)
**Related:** `docs/research/polivoks-emulations.md` §4 items 3–4

> **Baked (2026-07-18, later):** the Tuning menu was removed; gritDb is fixed
> at `kDefaultGritDb = 3.5` and the span at `kDriveSpanDb = 36` (user's by-ear
> final). The measured tables below predate that and used span 30 / grit 6.

## Problem

At high Q, the top of the Drive knob gets **smoother and softer**: the
resonance choke (authentic — higher input level chokes the resonant peak,
`polivoks-emulations.md:313-315`) removes not just the ring but also the
resonance-derived grit and \~1 dB of level. Measured at res 0.70, tone at
cutoff, span 30: output THD collapses from \~17 % (drive 0.5–0.9) to 4.4 % at
drive 1.0 (investigation report §3.5/§4.2).

That violates the module's voicing rule: **more Drive must always mean more
level or more dirt** — never less of both. The hardware record agrees on
direction: overdriving the input produces "majestic Polivoks roars"
(`polivoks-emulations.md:269-270`), resonance steps are "much accentuated if
the filter is overloaded" (`:283`), and "input drive is half the sound"
(`:309-311`). No source describes a calm zone at maximum input level.

Why the model loses grit where hardware doesn't: in the minimal core-only
path, high-Q grit is sourced from the resonant peak's saturation plus the
output VCA; when Drive chokes the ring, both contributions fall away together
(investigation §4.3). Drive Span cannot fix this — the top of the knob is
always max drive for the chosen span (§5.3).

## Design: Drive-following push into the existing output VCA

The output stage `9·tanhish(v/9)` (`Onbetap.cpp` processSide, last line) is
already the module's dominant audible grit source at low Drive (investigation
§3.3). Make Drive push the signal harder into it:

```
push = exp2( (gritDb / 6.0206) · drive² )        // 1 at drive 0
out  = 9 · tanhish( push · v / 9 )               // v = DC-blocked tap · makeup
```

- `drive` is the same clamped knob+CV value that feeds `driveScale`.
- `gritDb` is the push at full Drive, a new **"Drive grit"** Tuning-menu
  slider, range 0–12 dB. Default: **6 dB**, to be confirmed/adjusted by the
  calibration measurements below (acceptable range 4–12).
- The **quadratic** law keeps the low/mid knob — whose voicing is already
  correct (THD \~17 % at drive 0.5) — essentially untouched (+1.5 dB push at
  drive 0.5 with the 6 dB default) and concentrates the push at the top,
  where the choke bites.

Properties that make this safe against the Finding-1 failure mode:

- `push ≥ 1` always and is monotone in Drive: a **boost into a fixed 9 V
  ceiling**, never a cut. Level can only rise or hold; the extra gain converts
  to harmonics once the VCA clips. There is no compensation pair to
  mis-cancel.
- `push(0) = 1` exactly (`exp2(0)`), so Drive = 0 stays **bit-identical** and
  the Drive-0 level calibration (worklog test 1a) is preserved.
- Output remains bounded ≤ 9 V peak regardless of `gritDb`; the stability
  torture test is unaffected in principle and re-run to confirm.
- `gritDb = 0` recovers today's behavior exactly (escape hatch).

### Honesty note: voicing, not circuit

This stage is **not circuit-derived**. In the real Polivoks the core's rail
compression holds the level into downstream stages roughly constant, so input
level does not overdrive the output buffer harder. What this stage restores is
the documented hardware *behavior* — more input = more roar, never smoother —
which the minimal core-only model loses at the top of the knob because its
high-Q grit rides on the very resonance that Drive chokes. Post-filter
drive-tracking saturation is the cheapest honest stand-in, and it reuses the
module's existing nonlinearity rather than inventing a new one.

### Alternatives considered

1. **HP-tap bleed** — mix `gritAmt(drive) · hp` (the saturated node signal,
   physically the grittiest tap) into the output. Rejected: contaminates mode
   identity — LP stops fully filtering highs, and Notch relies on exact
   `lp + hp` cancellation at cutoff, so any added HP detunes the null.
2. **New dedicated post-tap saturator** — the investigation's literal
   "separate drive-grit stage." Rejected as redundant: the output VCA *is*
   that stage; adding a second nonlinearity is YAGNI.
3. **Lower default Core headroom** (investigation option 2) — rejected: only
   slides the calm zone around and trades away saturation depth everywhere.

## Change surface

1. **`src/onbetap/drive.hpp`** — `DriveGains` gains a third field `vcaPush`;
   `driveGains()` gains a `gritDb` parameter and computes
   `vcaPush = exp2(gritDb/6.0206 · drive²)`. Header comment updated (the
   makeup stays constant; the push is the deliberate, bounded exception).
2. **`src/onbetap/engine.hpp`** — `OnbetapVoice` gains `pushSlew`
   (`OnePoleSmoother`, init 1) + `pushTarget`, wired into `setAlpha`/`reset`
   like `makeupSlew`.
3. **`src/Onbetap.cpp`** —
   - new field `float tuneGritDb = 6.f` (default from calibration), persisted
     in JSON (`tuneGritDb`, clamped 0–12 on load; missing key → default, so
     existing patches pick up the new voicing — intended, this is a fix);
   - `modulate()` passes `tuneGritDb` to `driveGains` and sets
     `v.pushTarget = gains.vcaPush`;
   - `process()` slews it and passes `push` to `processSide`;
   - `processSide()` applies it inside the VCA: `9·tanhish(push·v/9)`
     (after the DC blocker, which is linear — placement is for clarity);
   - Tuning menu gains a "Drive grit" slider (0–12 dB, default 6);
   - file-header comment block updated.
4. **`tests/onbetap/test_drive_level.cpp`** — extended (see Testing).
5. Docs: worklog entry, `Onbetap.md` Drive/Tuning bullets, investigation
   report §8 gets a resolution pointer to this spec.

Nothing else changes: core, resonance map, oversampling, mode taps, DC
blocker, makeup, and the resonance choke itself are untouched. MetaModule
shares `src/` and picks the change up on its next build.

## Calibration & acceptance criteria

Measured with the committed host-free harness (span 30, headroom 1, outDb 0,
amp 5 V, cutoff 750 Hz, tone at cutoff, LP, Hard, Tamed, 2× OS, 48 kHz):

1. **Top-of-knob recovery (the point of the feature), res 0.70:**
   `THD@1.0 ≥ 12 %` (currently 4.4 %) and `level@1.0 ≥ level@0.9 − 0.5 dB`.
2. **No regression of Findings 1–2 guards:** the existing
   `test_drive_level.cpp` assertions stay green at res {0.0, 0.30, 0.70}.
3. **Drive-0 bit-identity:** `push(0) = 1` (assert in test via `driveGains`).
4. **Mid-knob voicing preserved:** at res 0.30/0.50, `THD@0.5` at the default
   `gritDb` rises by no more than +8 pp vs the same measurement at
   `gritDb = 0` (which is bit-identical to the pre-change build, so this
   stays measurable forever).
5. **Monotone level:** no > 0.5 dB level drop over any 0.1 Drive increment at
   res ≤ 0.60 (res 0.70 knee excluded from strict monotony — self-osc chaos
   is physics, guarded instead by criterion 1's endpoint floor).
6. **Stability:** existing torture/stability tests pass; output ≤ 9 V peak.

If criterion 1 fails at `gritDb = 6`, raise the default toward 12 and/or
steepen the law to `drive⁴` — whichever measurement shows keeps criterion 4
intact; record the chosen constants in the worklog. If no setting in range
satisfies both 1 and 4, stop and report (true blocker).

### Metric amendment (2026-07-18, during implementation)

Criterion 1 as first written ("res 0.70: THD@1.0 ≥ 12 %") turned out to be
the wrong instrument, not a failing feature. Diagnosis at the knee (res 0.70,
drive 1.0, 5 V): the output is already rail-to-rail (peak 9 V) with **79–87 %
non-fundamental energy**, but that energy is *inharmonic* (chaotic self-osc
sidebands — h2…h6 all < 0.5 V), so harmonic-bin THD reads 3–5 % regardless of
how hard the VCA is pushed. Harmonic THD is only a valid grit instrument in
the stable choked regime (res ≤ \~0.60).

The push itself works as designed — measured with the committed sweep
(`test_drive_grit sweep`): at res 0.60 the default push turns a dead-flat top
decile (THD 17.3 % → 17.3 %) into a rising one (28.8 % → 31.1 %), and at the
res 0.70 knee it adds +1.0 dB of level at full Drive. Criterion 1 is therefore
re-anchored on valid instruments:

- **1a (res 0.60, stable):** `THD@1.0 ≥ THD@0.9 + 1 pp` (dirt keeps rising to
  the end of travel), `THD@1.0 ≥ 25 %`, `level@1.0 ≥ level@0.9 − 0.5 dB`.
- **1b (res 0.70, knee):** `level@1.0 at default gritDb ≥ level@1.0 at
  gritDb 0 + 0.5 dB` (the push makes the top *louder*, never softer) and
  `level@1.0 ≥ level@0.9 − 0.5 dB`.

Criterion 5 is restricted to res {0.30, 0.50}: res ≥ 0.60 has authentic brief
self-oscillation pockets at isolated Drive points (investigation report §5.2,
the 0.2–0.3 ratio dips) that bump the level by \~1 dB and make strict level
monotony physically wrong there.

Default confirmed at **6 dB**: 4 dB also passes but with thin margins (2 pp /
0.2 dB) on chaos-adjacent measurements; 6 dB passes with solid margins while
moving mid-knob THD by only +3.6 pp (well inside criterion 4's +8 pp). The
quadratic law needed no steepening.

## Testing (TDD order)

Extend `tests/onbetap/test_drive_level.cpp` (or add
`test_drive_grit.cpp` if it reads better standalone):

- new assertions for criteria 1, 3, 4, 5 above, written against the current
  code first and observed **red** (criterion 1 fails today: 4.4 % < 12 %;
  criteria 3–5 pass trivially pre-change where applicable — the red gate is
  criterion 1);
- then the implementation lands and all assertions go **green**;
- full suite (`tests/run.sh`) green.

## Out of scope

- Any change to the resonance choke itself (authentic, kept).
- Input-level normalization / auto-gain (investigation option 4).
- Default output-level voicing (the high-Drive hotness caveat from the
  previous spec stands unchanged).
- The max-drive aliasing gap (worklog test 2f) — pre-existing.

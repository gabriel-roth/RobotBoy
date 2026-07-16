# Quality-mode fidelity inversion fix — design

- **Date:** 2026-07-16
- **Status:** Approved
- **Modules affected:** Particules and Retours (shared `particules_dsp` quality engine; `retours_delay_dsp` aliases the enum)

## Problem

The four audio-quality modes are meant to reproduce the Mutable Instruments Beads
quality ladder, which the Beads manual names (in order of *increasing* degradation
and *increasing* recording time):

| # | Beads label       | rate / bits        | recording time |
|---|-------------------|--------------------|----------------|
| 0 | Bright digital    | 48 kHz / 16-bit    | 4 s            |
| 1 | Cold digital      | 32 kHz / 12-bit    | 8 s            |
| 2 | Sunny tape        | 24 kHz / 12-bit    | ~11 s          |
| 3 | Scorched cassette | 24 kHz / 8-bit µ-law | 32 s         |

The code models fidelity with an integer sample-rate decimation factor and derives
buffer duration as `192000 frames × decimation / 48000` (a doubling ladder). The
current `DecimationFactorForQuality` returns `1 / 2 / 8 / 4`, so modes 2 and 3 are
**inverted**: "Sunny tape" gets the heaviest decimation (8× → 6 kHz, 32 s) while
"Scorched cassette" gets 4× (→ 12 kHz, 16 s). In the real Beads, Scorched cassette
is the most degraded mode with the longest (32 s) buffer, and Sunny tape is milder.

This was inherited verbatim from the upstream `nosuch_texture` repo (commit
`f32bbd0`, a Cortex-M7 optimization pass) where the developer's internal model
(`kHiFi/kClouds/kCleanLoFi/kTape`, labeled "LoFi 32s / Tape 16s") diverged from the
Beads manual: it treated mode 2 as a clean digital lo-fi rather than a tape. We have
already diverged from `nosuch_texture`, so this fix does not need to preserve upstream
parity.

Two secondary symptoms of the same inversion:
- The µ-law / mono-sum / wow-flutter / hiss "tape character" is attached to enum
  member `kTape` (index 3, Scorched cassette) — which is *correct* — but mode 2
  ("Sunny tape") has no tape character at all despite its name.
- The internal enum member names do not match the UI/Beads labels, a standing
  source of confusion.

## Goal

Make the four modes a monotonic degradation ladder matching the Beads manual, keep
each mode correctly anti-aliased at its new rate, give Sunny tape gentle tape motion
so it earns its name, and rename the enum members to the Beads labels.

## Design

### 1. Decimation ladder

`DecimationFactorForQuality` (`src/particules/dsp/include/particules_dsp/types.h`):
`1 / 2 / 8 / 4` → **`1 / 2 / 4 / 8`**.

Resulting effective rates / Nyquist / buffer durations (by index):

| # | mode              | decim | eff. rate | Nyquist | buffer |
|---|-------------------|-------|-----------|---------|--------|
| 0 | Bright digital    | 1×    | 48 kHz    | 24 kHz  | 4 s    |
| 1 | Cold digital      | 2×    | 24 kHz    | 12 kHz  | 8 s    |
| 2 | Sunny tape        | 4×    | 12 kHz    | 6 kHz   | 16 s   |
| 3 | Scorched cassette | 8×    | 6 kHz     | 3 kHz   | 32 s   |

Retours' `effective_seconds` (`retours_processor.cpp`) and the delay engine call
`DecimationFactorForQuality`, so they inherit the new durations with no further
change. Update the duration comment in `types.h` accordingly.

### 2. Anti-aliasing filters follow the rate

The **input** low-pass is the anti-alias guard: it must sit below the decimated
Nyquist so content does not fold when the recording buffer sample-and-holds on write.
The **output** low-pass is tonal shaping at host rate and stays with each mode's
character. The current `kTapeLpHz` (5 kHz) is shared for both the tape input and
output filters; split it so the input anti-alias filter can move independently.

Constant changes in `src/particules/dsp/src/quality/quality_processor.h`
(names shown post-rename; see §4):

| purpose                         | current name / value        | new name / value                 |
|---------------------------------|-----------------------------|----------------------------------|
| Cold digital input LP           | `kCloudsInputLpHz` = 10000  | `kColdDigitalInputLpHz` = 10000  |
| Sunny tape input (anti-alias)   | `kCleanLoFiInputLpHz` = 2500| `kSunnyTapeInputLpHz` = **5000** |
| Sunny tape output (tone)        | `kCleanLoFiLpHz` = 10000    | `kSunnyTapeOutputLpHz` = 10000   |
| Scorched input (anti-alias)     | (was `kTapeLpHz` = 5000)    | `kScorchedInputLpHz` = **2500**  |
| Scorched output (tone)          | (was `kTapeLpHz` = 5000)    | `kScorchedOutputLpHz` = 5000     |

`InputCutoffForMode` / `OutputCutoffForMode` are updated to return the split
constants. The `kSunnyTape` input branch is no longer "anti-aliasing for 8× (Nyquist
3 kHz)" — update its comment to reflect 4× / 6 kHz Nyquist; `kScorchedCassette` is
now the 8× / 3 kHz-Nyquist mode.

### 3. Gentle wow on Sunny tape

`GetPitchModulation` currently early-returns `1.0` for every mode except `kTape`.
Change it to apply modulation for both tape modes with a per-mode depth scale:

- Scorched cassette: full depth — `kWowSemitones` (0.02) / `kFlutterSemitones`
  (0.003), unchanged from today.
- Sunny tape: **half depth** (0.5×).
- All other modes: depth 0 → return `1.0` (no-op, phases not advanced).

Implement with a small `WowDepthForMode(mode)` helper returning `1.0 / 0.5 / 0.0`.
Multiply the summed semitone deviation by the depth before `SemitonesToRatio`. LFO
phase advancement is unchanged in form.

### 4. Rename enum members

Rename across all source and test files (~157 references, 13 source + 8 test files),
in both `particules_dsp` and `retours_delay_dsp`:

| old          | new                 |
|--------------|---------------------|
| `kHiFi`      | `kBrightDigital`    |
| `kClouds`    | `kColdDigital`      |
| `kCleanLoFi` | `kSunnyTape`        |
| `kTape`      | `kScorchedCassette` |

Pure mechanical rename — no behavioral change beyond §1–§3. The enum values
(0/1/2/3) and their order are unchanged, so serialized `qualityState` in saved
patches remains valid.

### 5. UI labels and colors — no change

The desktop dropdown / MetaModule switch labels are already
`{"Bright digital", "Cold digital", "Sunny tape", "Scorched cassette"}` in correct
index order, and the RGB indicator colors are indexed by `quality_state_`. Neither
changes. The MetaModule quality PNG/SVG frame assets (`quality_sunny.svg` etc.) are
also indexed and unchanged.

## Files

- `src/particules/dsp/include/particules_dsp/types.h` — decimation ladder, enum
  rename, duration comment.
- `src/particules/dsp/src/quality/quality_processor.{h,cpp}` — filter constants,
  `InputCutoffForMode` / `OutputCutoffForMode`, `GetPitchModulation` + `WowDepthForMode`,
  enum rename, comments.
- `src/particules/dsp/src/{particules_processor.{h,cpp},fx/saturation.cpp}` and
  `src/particules/dsp/include/particules_dsp/parameters.h` — enum rename only.
- `src/particules/Particules.cpp`, `src/retours_delay/Retours.cpp` — enum rename only.
- `src/retours_delay/dsp/include/retours_delay_dsp/types.h`,
  `src/retours_delay/dsp/src/{retours_processor.{h,cpp},engine/echo_engine.{h,cpp}}`
  — enum rename only.
- `tests/particules_dsp/test_quality_modes.cpp`,
  `tests/retours_delay_dsp/test_quality_modes.cpp` — update decimation/duration
  assertions to the new ladder, plus any other test referencing the old enum names.
- Other tests referencing the enum names — rename only.
- `CHANGELOG.md` — add an entry.

## Verification

1. Catch2 unit-test suites for both `particules_dsp` and `retours_delay_dsp` build
   and pass with the updated assertions.
2. Headless WAV render (via the `test-vcv-module-headless` skill / `vcv-headless`
   host) before-and-after on each quality mode for at least one module, confirming:
   - Scorched cassette (now 8×) no longer folds 3–5 kHz content (no new aliasing
     tones vs. a band-limited reference).
   - Sunny tape (now 4×) is brighter than before, not muffled.
3. VCV plugin builds cleanly and installs; MetaModule build unaffected (labels/enum
   values unchanged).

## Out of scope

- Changing UI labels or RGB colors.
- Re-tuning the DENSITY default (already set to 0.35 for Retours this session).
- Matching Beads' literal 48/32/24/24 kHz rates — the engine only does integer
  decimation of a 48 kHz host; we keep the doubling ladder.
- Preserving `nosuch_texture` upstream parity.

## Impact

Existing patches that use Sunny tape or Scorched cassette will change sound and
buffer length (Sunny 32 s → 16 s; Scorched 16 s → 32 s). Acceptable: both modules
are unreleased. Retours' initialized delay at Sunny tape becomes ~1.6 s (was ~3.2 s)
via the DENSITY-default fraction; still expected behavior. Requires a CHANGELOG entry.

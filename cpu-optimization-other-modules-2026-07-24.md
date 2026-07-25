# Loopers + Beads-family CPU optimization — findings

**Date:** 2026-07-24
**Scope:** MetaModule CPU cost of the remaining modules: **Loooop / Löp** (loopers),
**Particules**, **Ondes**, **Retours**. Companion to
`cpu-optimization-2026-07-24.md` (the Vespid / Onbetap / MF-20 pass, merged to
main earlier today).
**Status:** Analysis only. **No code changed.** Findings are source-level audits
using the cost model established and measured in the filter pass; the headline
claims were spot-verified against the source by a second reader.

Platform cost model (from the filter pass, unchanged): the MetaModule SDK
compiles with `-O3 -fno-math-errno` but **not** `-ffast-math`, so every
`x / constant` is a real `VDIV.F32` (\~10–14 cycles, not pipelined; consecutive
divides serialize). `VDIV.F64` is \~29 cycles. libm calls (`exp2f`, `fmodf`,
`lroundf`, `sinf`, `powf`, `floor` on double…) are out-of-line newlib calls,
order 10² cycles plus register spills. Cortex-A7 has no `VRINT`, so
`std::floor`/`std::lround`/`std::round` are all libm.

---

## 1. Cross-cutting themes

Four patterns recur across all five modules:

1. **`lroundf` per sample on a block-rate value.** Particules
   (`Particules.cpp:406`), Retours (`Retours.cpp:327`), and both MM looper
   cores (`LoooopCore.cc:47`, `LopCore.cc:44`) all call `std::lround` on a
   switch/knob param every sample inside the hot path. In Particules and
   Retours it is the `#ifdef METAMODULE` Quality read sitting *above* the
   `BlockReady()` gate even though the result is consumed only at block rate.
   Fix everywhere: move under the block gate and/or use `(int)(x + 0.5f)`
   (params are non-negative). Exactly equivalent.

2. **`fmodf`/`floor` where a bounded wrap or mask suffices.** Ondes' phase
   accumulator (`wavetable_oscillator.cpp:84`), Retours' delay-line wrap
   (`echo_engine.cpp:14`, called 1–3× per sample), Loooop's interpolated read
   (5+ double `std::floor` per channel per head per sample). All replaceable
   with exact integer/conditional-subtract forms.

3. **Loop-invariant work at audio rate.** Ondes recomputes pitch → frequency →
   phase-increment plus all bank/wave setup (7 virtual calls) every sample;
   Loooop recomputes `windowBounds` and `1/loopLen` per head per sample;
   Retours divides by the (block-invariant) decimation factor per sample.
   Change-detection caches / precomputed reciprocals, all exact.

4. **`FastTanh`-family divides** (`dsp_utils.h:47-48`,
   `saturation.cpp:14-16`): one data-dependent VDIV per call, called 2–6× per
   sample in Particules (reverb feedback + limiter) and Retours (lo-fi
   Quality modes). Shared file — one fix pays in both modules.

**Shared-file caveat:** `dsp_utils.h`, `saturation.cpp`, `sample_codec.h`,
`quality_processor.*`, `recording_buffer.*` are compiled into both Particules
and Retours (`metamodule/CMakeLists.txt`). Changes there need both modules'
regression lanes.

---

## 2. Loooop / Löp (`src/loooop/`)

MM audio path: `metamodule/loooop/LoooopCore.cc` / `LopCore.cc` +
`src/loooop/dsp/LoopEngine.cpp` (the VCV adapters `Loooop.cpp`/`Lop.cpp` are
desktop-only). Hot path per sample: per-head param setters →
`LoopEngine::process()` → per playing head: `windowBounds` → `readHead`
(Catmull-Rom read ×2ch) → `advanceHead`.

### High

- **H1 — Release-fence atomic per recorded sample.** `LoopEngine.cpp:473`
  and `:500` call `bumpWaveformRevision()` on every sample for the entire
  duration of any record/overdub pass; the store (`LoopEngine.hpp:151`) is
  `memory_order_release` = `dmb ish; str` on ARMv7 — a full memory barrier
  per sample. Second-order cost: the revision changes every sample, so the MM
  GUI cache check misses every frame and `renderWaveform` re-scans the whole
  recorded region (up to 2.88 M samples × 2 ch, twice — peak scan + envelope)
  per GUI frame, hammering the memory bus the audio core shares.
  **Fix:** throttle the bump to every \~1k–5k samples plus on every state
  transition (freeze/stop/clear call sites already exist). Audio exactly
  equivalent; display lags ≤ one frame. *Biggest recording-time win.*

- **H2 — ≥10 double `floor` libm calls per head per sample in the
  interpolated read.** `readInterpolated` (`LoopEngine.cpp:309-322`) floors
  once, then makes 4 taps; `readRaw` (`:331-333`) floors the same value
  **twice**; L and R channels redo identical positional math independently.
  With 4 heads playing that is \~40+ libm `floor(double)` calls per sample,
  plus the taps at `ip-1`, `ip+1`, `ip+2` are already integral — their floors
  and double lerps are pure waste.
  **Fix:** per head, compute `i0`/`frac` once, take an interior fast path
  (integer indices, shared by both channels) with the current wrapped path as
  the rare seam fallback. Exactly equivalent on the fast path.
  *Biggest steady-state playback win.*

- **H3 — Two double VDIVs per playing head per sample in crossfade math.**
  `fadeLen` (`LoopEngine.cpp:342`, `winLen / sp`) and `readHead`'s seam test
  (`:393-395`, `outToSeam = … / sp`) divide every sample just to test
  proximity. **Fix:** cache `fadeLen`/`invSp` per head (recompute on
  speed/window change), and compare `distToSeam` against `F * sp` (multiply
  instead of divide) — exactly equivalent for finite positive `sp`.

- **H4 — `1.f / loopLen_` per head per sample.** `LoopEngine.cpp:457`, `:536`.
  Cache `invLoopLen_` wherever `loopLen_` is assigned. Trivial, exact.

- **H5 — `windowBounds` at audio rate.** `LoopEngine.cpp:256-289` runs per
  head per sample; with Grid on it adds 3 double VDIVs + 2 `std::lround`
  per head per sample (≈12 VDIV.F64 + 8 libm/sample on Loooop). All inputs
  change at control rate and already funnel through setters.
  **Fix:** dirty-flag cache of `(winStart, winLen)` per head. Exact. This is
  the enabling refactor for H3.

### Medium

- **M1 —** `std::lround(gridKnob * 5.f)` per sample in both MM cores
  (`LoooopCore.cc:47`, `LopCore.cc:44`) → `(int)(x * 5.f + 0.5f)`, exact here.
- **M2 —** `exp2f` per head per sample when Speed CV mode is V/Oct
  (`LooperModuleDSP.hpp:26-29`; ×4 heads on Loooop). Memoize on bit-identical
  `(knob, cv)`. Exact.
- **M3 —** `/ 5.f` input scaling (`LoooopCore.cc:81-82`, `LopCore.cc:92-93`)
  → `* 0.2f`. Float-noise (±1 ulp).
- **M4 —** double-precision lerp arithmetic in `readRaw` (`:335`) can be
  float once `frac` is extracted (keeping the position itself double is
  justified — 2.88 M-sample loops exceed the f32 mantissa). Subsumed by H2.
- **M5 —** display-mirror stores + parked-head `windowBounds`
  (`LoopEngine.cpp:457-461`, `:529-541`) can update every N≤256 samples
  instead of every sample. Visually identical.

### Desktop-only (Low)

`std::round` per sample on Overdub/Grid params (`Loooop.cpp:141,145`,
`Lop.cpp:93,97`); `std::sin` per sample for the Lock-mode LED pulse
(`OverdubControl.hpp:26`); duplicated `inL/5.f` computations
(`Loooop.cpp:201,219,220`). None compile into the MM build.

### Already fine (do not re-propose)

Clear() doesn't memset the 23 MB buffer; rate-dependent coefficients all
precomputed at rate change; jitter RNG is xorshift; buffer wrap is conditional
subtract (no fmod); seam crossfade is smoothstep (no sqrtf, deliberate);
`winLen / 2.0` is a power-of-two divide (compiled to multiply); no per-sample
allocations; no shift-register histories; display code is off the audio thread
on both platforms (except the H1 revision churn); `std::isfinite` inlines.

---

## 3. Ondes (`src/particules/Ondes.cpp` + `dsp/src/wavetable/`)

Structural root of everything: `Ondes.cpp:73` calls
`osc_.Process(pitch, bank, position, &out, 1)` — **`num_frames = 1`**, so all
of `WavetableOscillator::Process`'s "block setup" executes per sample, across
a TU boundary (no LTO → nothing inlines). The useful DSP (7 lerps + mask wrap)
is \~10–15% of the per-sample work.

### High

- **H1 — `exp2f` + `/12.0f` per sample for pitch** (`wavetable_oscillator.cpp:29`
  → `dsp_utils.h:27-29`), even when pitch is static. **Fix (layered):**
  (1) change-detect on the incoming pitch — exact, removes everything in the
  common case; (2) `* (1/12.f)` — float-noise; (3) fast exp2 only if
  audio-rate V/oct FM matters — \~1e-6 octave error.
- **H2 — `frequency / sample_rate_` per sample** (`:30`), serializing with
  H1's divide. Precompute `phase_scale_ = kWavetableSize / sample_rate_` in
  `Init()`. Float-noise or absorbed by H1's cache.
- **H3 — `std::fmod` phase wrap per sample** (`:84`). The loop already masks
  the integer part; reconstruct `phase_` from the masked int + frac (or
  conditional subtract — increment is bounded to ≤\~1429 by the ±120 st
  clamp). **Bit-exact either way.**

### Medium

- **M1 — All bank/wave setup + 7 virtual calls per sample**
  (`wavetable_oscillator.cpp:22-62`): `NumBanksAvailable` is even called twice.
  Cache provider constants in `SetProvider()`; change-detect on `(bank, wave)`
  to cache the four waveform pointers + fracs. Exact; \~50–80 cycles/sample.
- **M2 — per-sample cross-TU call overhead**: mostly subsumed by H1–H3/M1;
  a control-rate `SetPitch()` + inline `ProcessSample()` split only if
  profiling still shows call overhead.

### Low

Display bilinear setup redone per point, 257×/frame (`Ondes.cpp:94-95`,
`WavetableFrame.hpp:11-38`) — GUI thread only.

### Already fine

Pitch-knob map lookup already cached behind a knob-motion check
(`Ondes.cpp:54-58`); table index wrap already power-of-two masked; linear +
bilinear interpolation is already the cheap scheme; no f64 promotions; mono
module, no isConnected/setChannels issues; 4 KB working set fits L1.

Estimated combined effect of H1–H3 + M1 for a static-pitch patch: \~60–80%
cut in Ondes' per-sample MM cost.

---

## 4. Particules (`src/particules/`)

Rack wrapper runs per sample; every 64 samples a block runs:
input loop (auto-gain → quality → feedback/HP/limit → write) → grain render
(≤30 grains × 48 kHz — the dominant cost) → output loop (quality → dry/wet →
reverb).

### High

- **H1 — `lroundf` per sample on the MM Quality param** (`Particules.cpp:406`,
  above the block gate). Move under `BlockReady()`. Exact. Trivial.
- **H2 — Per-grain-per-sample format/channel dispatch in the grain read**
  (`grain.h:73` → `recording_buffer.h:77-140`): `ReadHermiteStereoFrac`
  re-loads `format_`/`channels_`/LUT pointer and switches per sample per
  grain; the compiler can't prove no aliasing against the `output[i] +=`
  stores, so nothing stays in registers. Format/channels are constant for a
  whole block (quality transitions kill all grains). **Fix:** hoist the
  dispatch to block level (templated/specialized `ProcessBlock<Format>` or
  pre-resolved-pointer read variants). Exact. Hottest item at high grain
  counts.
- **H3 — `isfinite` ×2 per grain-sample + no early break**
  (`grain.h:109-118`): NaN sources are already fenced at spawn/encode; demote
  the check to per-grain-per-block (containment granularity moves from sample
  to block; the wrapper's own `isfinite` guard still stands). Also
  `if (!Process(...)) break;` — a deactivated grain can't reactivate but the
  loop keeps calling it for the rest of the block (that part is exact).
- **H4 — `lroundf` per stored sample in Int12 encode** (`sample_codec.h:20`,
  Cold/Sunny write path, \~1 call per host sample stereo at 2× decimation).
  Manual round-half-away form. Shared with Retours. Float-noise at worst
  (tie behavior matches).

### Medium

- **M1 — `FastTanh` divides, 4–6 VDIV/sample** (`dsp_utils.h:47-48`): reverb
  feedback SoftClip ×2 (always, unless reverb is asleep), `LimitFeedback` ×2
  in Cold/Sunny/Scorched, auto-gain `SoftLimit` up to ×2 when hot. Worst case
  \~85 serialized cycles/sample. NEON `vrecpe` + 2 Newton steps is the
  float-noise option; one fix pays at four call sites and in Retours too.
- **M2 — Dead SVF ticks in pass-through Quality modes**
  (`quality_processor.cpp:89-90`, `146-147`): Bright discards all four filter
  outputs (4 dead ticks/sample in the default mode). Skipping requires a
  state reset at the mode-change apply point; behavior change confined to the
  64-sample crossfade window (which starts from raw input anyway). Shared
  with Retours.
- **M3 — Scale quantizer does double `pow`×2 + double `log` per spawn**
  (`pitch_quantizer.cpp:52-113`) — and the pow→log2 round trip is redundant
  since the input is already log2: normalize with
  `floor(pitch / log2_period_)` and scan directly. ≤131 spawns/s so absolute
  impact is modest; also removes a degenerate-scale divide loop.
- **M4 — `powf` per block for the overlap coefficient**
  (`grain_engine.cpp:334-335`): `slope_coeff` has exactly two possible values
  and `num_frames` is always 64 → two cached constants. Exact.
- **M5–M7 —** per-spawn constant-divides and cacheable log2 in grain-size
  math (`grain_engine.cpp:21-30,136,158,185`); `cosf`/`sinf` per spawn for
  equal-power pan (`grain.cpp:101-102`) → the existing `CosLookup`;
  `NormalizedSoftClip`/`SoftLimit` constant-drive divides
  (`saturation.cpp:15`, `auto_gain.cpp:35`) → reciprocal constants.

### Low

Transition-fade constant divides (`particules_processor.cpp:260-261,292-293`);
reverb knob-derived scalars recomputed per sample (`reverb.cpp:132-142` —
hoist into the setters); conditioner quantize divide (`control_conditioner.h:46`);
scheduler per-block `/sample_rate_`; tape-wobble sines per block →
`CosLookup`; `QuantizePitchLock` `/12` (`parameters.h:64-79`); FREEZE LED
`setBrightness` per sample.

### Already fine

Block-rate hoisting of all knob/CV/`isConnected` reads is done; `CosLookup`
already used for envelope/dry-wet/reverb trig; SVF tick is divide-free with
cached coefficients; grain positions are Q32.32 integer (no fmod); mu-law is
LUT/integer; reverb delay lines are branch-wrapped rings with idle-sleep;
dry/wet smoothing caches its pow correctly (the pattern M4 should copy);
grain-major rendering for locality; FTZ/DN armed on the MM audio thread;
NaN ingestion guards are per block and load-bearing.

---

## 5. Retours (`src/retours_delay/`)

Same wrapper/block structure as Particules (64-sample blocks). Per-sample
chain: `ReadWet` → `RotaryShifter` → quality → repeat envelope →
`LimitFeedback` → 2× SVF HP → quality input → `SaturateWrite` → write → mix.

### High

- **H1 — `fmodf` per sample in `EchoEngine::WrapPosition`**
  (`echo_engine.cpp:14`): 1× per sample tape mode, 2× crossfade mode, +1
  multi-tap, 1–2× frozen. The argument is provably bounded in
  `(−size, size+64)`, so a branchy conditional wrap is **exactly equivalent**
  (Sterbenz). Add a bounded-range wrapper for `ReadWet`; keep the general one
  for the block-rate freeze callers. Likely the single largest per-sample
  cost in the module.
- **H2 — `/ decimation` per sample in `ReadWet`** (`echo_engine.cpp:191`,
  `194`): decimation is block-invariant and only ever 1 or 2 → precomputed
  `advance_` (exact — reciprocal of a power of two). Also hoists a per-sample
  `std::max` call.
- **H3 — `lroundf` per sample on the MM Quality param** (`Retours.cpp:327`,
  above the block gate — same fix as Particules H1). Exact.
- **H4 — clock-light decay divides per sample** (`Retours.cpp:404`,
  `sampleTime / 0.08`): multiply by 12.5f (exactly representable), or move
  the whole light block to block rate (also drops two `setBrightness` calls
  per sample). Imperceptible.

### Medium

- **M1 — Saturation divides in the feedback/write path**: Bright 0, Cold 6,
  Sunny 4–6, Scorched 6 serialized VDIVs/sample via `FastTanh` /
  `NormalizedSoftClip` (`dsp_utils.h:46-49`, `saturation.cpp:14-16`).
  (a) constant-drive reciprocals — float-noise; (b) fold
  `SoftClip(y)/drive` into a single divide — float-noise, halves the rest.
  Shared with Particules.
- **M2 — Int12 encode `lroundf`** (Cold write path) — same as Particules H4.
- **M3 — `std::round` per sample for the multi-tap position**
  (`echo_engine.cpp:268`, active free-running with density > 0.55): wrap
  first, then `(float)(int)(pos + 0.5f)`.
- **M4 — QualityProcessor dead SVF ticks in Bright** — same as Particules M2;
  Retours additionally masks mode switches with its 2048-sample
  fade-out/clear/fade-in machine, so a state reset at the apply point is
  well covered.
- **M5 — block-rate `powf` for a baked-zero input trim**
  (`retours_processor.cpp:222`: `DbToGain(0)` every block) — short-circuit
  `db == 0 → 1.0f` or cache. Exact.
- **M6 — block-rate `expf` for a constant slew coefficient**
  (`echo_engine.cpp:66-67`) — cache until sample rate changes. Exact.
- **M7 — per-sample `isConnected()` ×2 in the wrapper** (`Retours.cpp:338`,
  `343`) — cache per block (≤64-sample cable-insertion latency).

### Low

`/5.f` CV scaling ×5 per block and `/12.f` in the pitch exp2 argument
(`retours_processor.cpp:197-259`); `SlowRandomLfo` divide + `cos` per block ×3
(`slow_random_lfo.h:16-18` — and its `SetRate` is re-called with identical
constants every block); tape wow/flutter trig per block → `CosLookup`;
frozen-seam per-sample `1/fade_len` (`echo_engine.cpp:210`); repeat-envelope
edge precompute; block-rate `floorf` on phase wrap (leave it).

### Already fine

`ReadHermiteStereoFast/Frac` (no fmod, no divides, shared index math, LUT
mu-law); SVF divide-free tick; **RotaryShifter avoids the per-sample-trig
trap entirely** (triangular windows are pure arithmetic, power-of-two masked
ring, full-bypass short-circuit); wow/flutter LFOs advance at block rate;
pitch-knob search cached; OnePole smoothing is multiply-add; quality-fade
`/2048` folds to multiply; block runtime has no shuffles; no f64 promotions
in per-sample code; all param/CV conditioning genuinely block-rate.

---

## 6. Suggested order of attack

By expected MM impact per unit of risk:

1. **Trivial exact wins, one sitting:** Particules H1 + Retours H3 (block-gate
   the Quality `lround`), Loooop H4 (`invLoopLen_`), loopers M1/M3, Retours
   H2/H4, Ondes H2. All exact or float-noise, all one-to-three-liners.
2. **Loooop H1** (revision-bump throttle) — biggest recording-time win, also
   stops the GUI whole-buffer rescan churn.
3. **Ondes H1/H3 + M1** (pitch change-detection, fmod removal, bank/wave
   cache) — collapses Ondes' per-sample cost for static patches.
4. **Retours H1** (bounded wrap replaces fmodf) — largest steady-state Retours
   item.
5. **Loooop H5 then H3** (cached windowBounds, then divide-free seam test) —
   removes essentially all per-sample F64 divides and grid lrounds.
6. **Loooop H2** (integer-tap Catmull-Rom fast path shared across channels) —
   deepest change, biggest steady-state looper win.
7. **Particules H2/H3** (grain-read dispatch hoist, isfinite demotion) — the
   high-grain-count win; H2 is a real refactor.
8. **Shared saturation pass** (Particules M1 / Retours M1 + Int12 encode) —
   validate against both modules.
9. Everything else opportunistically.

## 7. Verification

Same methodology as the filter pass (`cpu-optimization-2026-07-24.md` §6/§9.2):

- `tests/run.sh` stays green (loooop, particules suites included).
- **Per-step resync** A/B for anything touching feedback paths (Retours
  feedback loop, Particules reverb/limiter, Loooop overdub) — free-running
  diffs are chaos-amplified and meaningless there.
- Bit-exactness is assertable only for the items marked *exact* (bounded
  wraps, caches of unchanged values, block-gating of block-consumed reads);
  reciprocal swaps are float-noise (\~1 ulp).
- Headless WAV lane (`test-vcv-module-headless`) for VCV↔MM parity; note the
  5:1 scale difference between hosts before comparing.
- A7 static op counts (`arm-none-eabi-objdump` probe from the filter pass)
  before/after for the hot functions: `LoopEngine::process`/`readHead`,
  `WavetableOscillator::Process`, `EchoEngine::ReadWet`,
  `Grain::ProcessBlock`.
- GUI/listening checks on hardware remain user-run items.

---

## 8. Implemented (2026-07-25, branch `cpu-opt-2`)

Tasks 1-15 from `.superpowers/sdd/2026-07-24-cpu-optimization-other-modules/plan.md`
are implemented on branch `cpu-opt-2` -- with one deliberate exception noted in
§8.1 below -- one commit per concern, on top of `main` at `8213c9a` (the filter
pass, already merged):

```
b8a90d9 Record loopers/Beads-family CPU findings and implementation plan
e3274dc Add divide-free fast exp2 to particules dsp tree with accuracy test
3c1330c Ondes: cache pitch and bank setup, fast exp2, exact phase wrap
fc30668 Loopers: reciprocal loop length, integer rounding, V/Oct speed memo
151e959 Loooop: throttle waveform revision bumps during recording
6ccaf30 Loooop: value-compare cache for per-head window bounds
81f515b Loooop: divide only inside fade windows, multiply-first seam tests
fc41949 Loooop: shared-index interior fast path for head reads
be306f5 Loooop: throttle display mirror stores to 750 Hz
21ded3e Particules/Retours: block-rate Quality reads and light updates
17cb6ad Retours: bounded wraps, reciprocal decimation, libm-free tap snap
f9422cb Retours: fix pinning wrap coverage and a mid-block quality-change OOB read
6058e46 Retours: correct WrapBounded root-cause comment, NaN-safe fallback check
589c20f Retours: cache block-rate conversions, hoist jack checks, reciprocal constants
06369b3 Particules: block-resolved grain reads, single finite check, dead-grain break
89fa978 Particules: force-inline ctx read into legacy path, fix tautological test
a47706d Particules: spawn-rate reciprocals, float pitch quantizer, table pan, libm-free int12
c09981e Particules: sanitize NaN pitch before Exp2Fast, fix codec comment
52b8aa8 Fold saturation and auto-gain normalization to one divide
1e29997 Particules: hoist reverb coefficients, exact fade reciprocals
```

Each task went through the project's standard implement→review cycle (see the
ledger at `.superpowers/sdd/2026-07-24-cpu-optimization-other-modules/progress.md`);
every task landed "review clean," several after one or two fix rounds. Tasks 10
and 13 each turned up a genuine bug beyond the optimization itself — see
[8.3](#83-two-bugs-found-along-the-way) below.

### 8.1 Per-task equivalence classification

Pulled from the ledger (`progress.md`) and each task's own commit message/review
notes. "Exact" means bit-identical on the changed path; "float-noise" means a
sub-ulp rounding difference from reassociation (reciprocal multiply, fused
computation, or compiler-reduction reordering); "behavior-noted" means a small,
deliberate, documented behavior change (always a display/throttle latency, never
an audio-path amplitude change).

| Task | Scope | Classification |
|---|---|---|
| 1 | Fast `exp2f`/`Exp2Fast`/`SemitonesToRatioFast` utility + accuracy test | New code; accuracy bound tested (\<2e-5 max rel err), not bit-exactness |
| 2 | Ondes: pitch/bank/wave change-detect cache, fast exp2, exact phase wrap | Change-detect caches are exact (recompute only on real input change); phase wrap (fmod → masked conditional-subtract) is bit-exact; fast exp2 is the Task-1 accuracy bound |
| 3 | Loopers: `invLoopLen_` reciprocal, integer rounding, V/Oct speed memo | Exact (reciprocal of the same divisor computed once, not per sample; `(int)(x+0.5f)` for non-negative params; memoized `exp2f` on bit-identical inputs) |
| 4 | LoopEngine revision-bump throttle | Behavior-noted: display waveform cache can lag up to \~2048 samples (\~43 ms) during a recording pass; audio path untouched |
| 5 | LoopEngine `windowBounds` value-compare cache | Exact: cache recomputes whenever any input actually changes; a stale hit is impossible by construction |
| 6 | LoopEngine fade/seam divide reordering (divide only inside fade window, multiply-first seam test) | Exact for finite positive speed (algebraic restatement of the same comparison/division, guarded identically) |
| 7 | LoopEngine interior fast path (shared L/R index, no floor/libm) | Exact on the interior branch (direct buffer loads reproduce the old per-channel Catmull-Rom read bit-for-bit); unchanged fallback path within 2 samples of a window edge |
| 8 | LoopEngine display-mirror throttle (\~750 Hz) | Behavior-noted: display position/window mirrors lag up to \~64 samples; `dispPlaying_` (one-shot end) stays unthrottled |
| 9 | Particules/Retours: block-gate the Quality `lround`, hoist per-sample jack checks, cache light-decay reciprocal | Exact (block-rate-consumed reads moved under the existing block gate) |
| 10 | Retours `EchoEngine` rework: bounded wrap, reciprocal decimation, libm-free tap snap | Mostly exact (bounded conditional wrap is provably equal to `fmod` in-range; decimation reciprocal exact for 1/2); the multi-tap nearest-frame snap is float-noise at an exact tie. Also fixed a real crash — see 8.3 |
| 11 | Retours block-rate polish (cached `expf` slew coefficient, short-circuited `DbToGain(0)`, etc.) | Exact (cache-until-input-changes; `db==0` short-circuit to `1.0f` is definitionally the same value) |
| 12 | Particules grain hot loop: block-resolved `ReadContext`, single finite check, dead-grain early break | Exact: `ReadContext` resolves the same format/channel dispatch once per block instead of per sample-per-grain (inputs are block-invariant); combined `isfinite(gl+gr)` check covers the identical rejection set as the old two-check form; early break is safe because a dead grain cannot reactivate mid-block |
| 13 | Particules spawn/block-rate items (spawn-rate reciprocals, float pitch quantizer, table pan, libm-free int12) | Mostly exact/float-noise; found and fixed a real UB path — see 8.3 |
| 14 | Shared saturation/auto-gain divide fold (`dsp_utils.h`, `saturation.cpp`) | Float-noise (single-divide algebraic fold of `SoftClip(y)/drive`, \~1 ulp), re-verified against both Particules and Retours regression lanes since the file is shared |
| 15 | Particules reverb/fade polish (hoisted reverb coefficients, exact fade reciprocals) | Exact (reciprocals of block-invariant constants, hoisted to the setter) |

Deferred by design, not overlooked: Findings §4 L7's freeze/slice per-sample `setBrightness` calls (`Particules.cpp:480`, `Retours.cpp:420`) were deliberately left off the block-rate pass -- Task 9's review verified they read an audio-rate Schmitt-trigger gate (`freeze_gate_.isHigh()`), so moving the call to block rate would change behavior (missed rapid gate transitions), not just move a rate.

### 8.2 Verification run

- `tests/run.sh` — exit 0 (all module suites: `mf20`, `loooop`, `particules`,
  `onbetap`, `vespid` + the Python guards).
- `tests/particules_dsp/run.sh` — exit 0 (Catch2 granular-DSP suite).
- `tests/retours_delay_dsp/run.sh` — exit 0 (Catch2 delay-DSP suite).
- VCV: `make -C vcv -B -j4` — clean rebuild, exit 0, `plugin.dylib` produced
  (only pre-existing `-Wdeprecated-this-capture` warnings from Rack's
  `helpers.hpp`, unrelated to this branch).
- MetaModule: full clean rebuild (`rm -rf metamodule/build`, fresh `cmake ..
  -G Ninja` + `cmake --build .`) — exit 0, **All symbols found!**,
  `metamodule/metamodule-plugins/RobotBoy.mmplugin` created (837,632 bytes).

### 8.3 Two bugs found along the way

Both were caught by the review/fix-round process during implementation, not
by this verification pass — recorded here per the task brief since they are
the most safety-relevant outcomes of the whole effort.

1. **Task 10 — real out-of-bounds read, fixed in `f9422cb`.** While pinning
   `EchoEngine`'s bounded-wrap replacement for `WrapPosition`/`fmodf`, review
   found (and a new stress test in
   `tests/retours_delay_dsp/test_hardening.cpp` reproduced) a genuine SIGSEGV:
   a quality-mode change that **shrinks** the buffer mid-flight (e.g. Scorched
   → Bright, 768,012 → 192,000 frames) leaves the slewed/latched delay state
   (`delay_frames_` in tape mode, `fade_from_frames_`/`target_frames_` mid-fade
   in crossfade mode) holding the old, larger buffer's magnitude for tens of
   milliseconds to whole seconds — observed at 65+ blocks post-`Configure()`
   with `delay_frames_ ≈ 292,057` against a `size_f` of 192,000. That pushes
   the read position outside the single-conditional wrap's `(-size, 2·size)`
   domain, so the naive bounded wrap emits a value outside `[0, size_f)` and
   sends the bounds-unchecked `ReadHermiteStereoFast` out of bounds. Fix:
   `WrapBounded` now falls back to the always-correct `WrapPosition` (which has
   its own `isfinite` guard) whenever the fast-path conditionals fail to land
   in range — two cheap, well-predicted, never-taken-in-the-common-case
   comparisons, with no cost to the exact-equivalence claim for genuinely
   in-domain input. This is pre-existing behavior surfaced by the review's
   stress testing, not a regression this branch introduced elsewhere — but the
   optimization's own bounded-wrap replacement is exactly the code that needed
   the fallback to stay safe.
2. **Task 13 — NaN reaching `Exp2Fast` undefined behavior, fixed in
   `c09981e`.** `Exp2Fast`'s bit-trick float construction
   (`std::memcpy`-based exponent-field manipulation) is undefined behavior on
   a NaN or out-of-range input: the existing `isfinite` fence guards against
   `Inf`, but a *finite* garbage pitch value (already possible upstream from
   other NaN-adjacent paths this codebase defends against elsewhere) could
   still reach the conversion and produce an unbounded/garbage result instead
   of a safely clamped one. Fix: sanitize the pitch value (clamp/replace
   non-finite or out-of-range input with a safe unity-ratio fallback) before
   it reaches `Exp2Fast`, with a pinned regression test asserting the unity
   fallback specifically. (A related minor finding in the same round — an
   Int12 codec comment overstating how closely its manual round matches
   `std::lround` — was corrected alongside.)

### 8.4 A7 static op-count measurements

Methodology: filter-pass §8 (`cpu-optimization-2026-07-24.md`) — cross-compile
the target translation unit with `arm-none-eabi-g++` 12.3.rel1 using the
**exact** MetaModule SDK release flags from
`metamodule-plugin-sdk/cmake/arch_mp15xa7.cmake` (`-O3 -fno-math-errno
-mcpu=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=hard -mtune=cortex-a7` plus the
full cache/PIC/section flag set) with `-DMETAMODULE`, disassemble with
`arm-none-eabi-objdump -d -C`, and count `vdiv.f32`/`vdiv.f64` (all ARM
IT-block condition-suffixed forms, e.g. `vdivlt.f64`, not just the
unconditional mnemonic) and `bl`-to-libm instructions
(`floor`/`fmod`/`lround`/`exp2f`/`round`/`pow`/`sin`/`cos`/`log`/`sinh`/`cosh`/
`tanh` families) per symbol. **Before** = `main` @ `8213c9a` (scratch git
worktree, removed after measurement); **after** = this branch's HEAD
(`1e29997`). None of the four target files have any `METAMODULE`-conditional
code paths, and none depend on VCV/Rack headers (confirmed against the same
files `tests/run.sh`/`tests/particules_dsp`/`tests/retours_delay_dsp` already
cross-compile with host g++), so each compiles standalone at both function-
and whole-translation-unit granularity without a wrapper —with one exception:
`Grain::ProcessBlock` is header-inline (`grain.h`), so it needed a
`noinline`-wrapper probe (same technique as the filter pass's `armprobe.cpp`)
to force a standalone A7 symbol; the before/after wrapper signatures differ
because Task 12 changed `ProcessBlock`'s parameter from `const
RecordingBuffer&` to `const RecordingBuffer::ReadContext&`.

| Function (granularity) | Before (main `8213c9a`) | After (`cpu-opt-2` HEAD) |
|---|---|---|
| `WavetableOscillator::Process` (per-symbol) | 263 insns / **2** `vdiv.f32` / **1** `exp2f` | 288 insns / **0** / **0** |
| `WavetableOscillator.cpp` (whole TU) | 272 insns / 2 vdiv.f32 / 1 exp2f | 334 insns / 1 vdiv.f32 / 0 |
| `EchoEngine::ReadWet` (per-symbol) | 337 insns / **2** `vdiv.f32` / **1** `roundf` | 388 insns / **0** / **0** |
| `echo_engine.cpp` (whole TU) | 969 insns / 5 vdiv.f32 / 1 roundf | 1098 insns / 4 vdiv.f32 / 0 |
| `Grain::ProcessBlock` (+ fused `ReadHermiteStereoFrac`, noinline-wrapper probe) | 564 insns (251 wrapper + 313 separate `ReadHermiteStereoFrac` call) / 0 vdiv / 0 libm | 558 insns, now **one fused function** (dispatch resolved once via `ReadContext`, no separate call) / 0 vdiv / 0 libm |
| `GrainEngine::ComputeGrainParams` (bonus, per-symbol) | 703 insns / **9** vdiv.f32 / 2 exp2f + 1 roundf | 742 insns / **1** vdiv.f32 / 1 exp2f + 1 roundf |
| `GrainEngine::Process` (bonus, per-symbol, block render) | 557 insns / 7 vdiv.f32 / 1 exp2f + 1 powf | 890 insns / 5 vdiv.f32 / 1 exp2f + 1 powf |
| `LoopEngine::process(inL,inR,heads)` (per-symbol, whole engine) | 256 insns / 1 vdiv.f32 / 0 libm | 296 insns / 1 vdiv.f32 / 0 libm |
| `LoopEngine::readHead` (per-symbol) | 139 insns / 3 vdiv.f64 / 0 libm | 135 insns / 2 vdiv.f64 / 0 libm |
| `LoopEngine::advanceHead` (per-symbol) | 150 insns / 1 vdiv.f32 / 0 libm | 162 insns / 0 vdiv / 0 libm |
| `LoopEngine::windowBounds(Uncached)` (per-symbol) | 116 insns / 3 vdiv.f64 / 2 lround | 116 insns / 3 vdiv.f64 / 2 lround (**unchanged body** — see caveat) |
| `LoopEngine::windowBoundsCached` (new dispatcher, after only) | — | 58 insns / 0 vdiv / 0 libm |
| `LoopEngine.cpp` (whole TU) | 1823 insns / 10 vdiv.f32 + 12 vdiv.f64 / 2 floor + 2 lround | 2093 insns / 10 vdiv.f32 + 11 vdiv.f64 / 3 floor + 2 lround |

**LoopEngine caveat, stated plainly:** the whole-TU static count is essentially
flat (even slightly up in raw instruction count and `floor` call *sites*)
despite Loooop being the largest single win in the plan. That is expected, not
a measurement failure: the plan's dominant Loooop wins (H1/Task 4, H5/Task 5,
M5/Task 8) are **call-frequency** reductions — `windowBoundsUncached`'s body
(3 `vdiv.f64` + 2 `lround`) is bit-for-bit unchanged, it is just no longer
called every sample per playing head, only on an actual value change, via the
new 58-instruction, zero-divide `windowBoundsCached` dispatcher. A static
per-instruction count over a translation unit cannot see call frequency; it
can only see that a small new function was added. The one part of the Loooop
work that *is* visible as a static, per-call reduction is the interior fast
path (Task 7): `readInterpolatedLR`'s interior branch does one `std::floor`
shared across both channels, versus the old per-channel `readInterpolated` +
`tapWrapped`/`readRaw` path's five `floor` calls per channel (ten per head per
sample, the number Findings §2 H2 cites) — confirmed directly in the
disassembly (`readInterpolated`/`readRaw` unchanged at 1 `floor` each, still
present as the near-edge fallback; the new `readInterpolatedLR` interior branch
adds exactly one more `floor` call site to the TU while removing roughly ten
per executed head-sample in the common case). `readHead`'s seam-test divide
(Task 6) and `advanceHead`'s wrap divide both show a small real per-call
reduction (3→2 `vdiv.f64` and 1→0 `vdiv.f32` respectively). The pre-existing
1 `vdiv.f32` in `process()` itself is the rare buffer-ceiling auto-end path
(`invLoopLen_ = 1.f / loopLen_` when a recording pass hits the buffer limit)
— identical in both versions, unrelated to this pass, executed once per
recording pass rather than per sample.

### 8.5 User-run hardware checklist

Per project convention, agent-driven GUI-simulator testing is out of scope —
these are for the user to run on real MetaModule hardware (and/or the desktop
simulator) before merging `cpu-opt-2`:

- [ ] **Loooop / Löp**: load a patch with all 4 heads active, one recording/
  overdubbing continuously. Confirm the CPU relief, and listen for seam
  behavior at loop wrap points (crossfade declick, one-shot end-fades, grid
  quantization) and jitter — nothing should sound different from before.
- [ ] **Ondes**: confirm static-pitch patches are cheaper and audio-rate V/Oct
  FM pitch tracking still sounds correct (fast-exp2 accuracy).
- [ ] **Retours**: check all four lo-fi Quality modes (Bright/Cold/Sunny/
  Scorched), both Tape and Crossfade time-change modes, Freeze, and the
  multi-tap texture mode — listen specifically at a Quality-mode change while
  Time is actively slewing (the Task 10 OOB-fix corner).
  Also stress a Quality change **during Freeze** if easily reachable.
- [ ] **Particules**: listen across grain Shape/Pan/Size sweeps, high grain
  counts, and both Int12-encoded Cold/Sunny quality modes, checking for any
  new clicks/dropouts under sustained high grain-count load (the CPU-relief
  target).
- [ ] **Desktop VCV**: A/B each of the four modules against the previous
  build at normal settings to catch anything the automated suites wouldn't
  (this pass has no VCV-desktop-only code paths, so parity is expected).

Merge decision: left on `cpu-opt-2` for the user to merge after the hardware
checklist above.

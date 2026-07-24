# Vespid / Onbetap CPU optimization — findings

**Date:** 2026-07-24
**Scope:** MetaModule CPU cost of `Vespid` (Wasp VCF) and `Onbetap` (Polivoks VCF).
**Status:** Analysis and measurement complete. **No code changed yet.** Two scope
decisions are open (see [Open decisions](#open-decisions)).

Context: MetaModule defaults were recently moved to 1x oversampling for both
modules, and Vespid's high-accuracy (Newton) mode was locked off on MetaModule
(commit `e6801dd`). So everything below targets the **1x, standard-accuracy**
path — the one MetaModule actually runs.

---

## 1. Method

Two independent measurements, because neither alone is trustworthy:

### 1a. Host micro-benchmark

A harness driving `WaspFilter::process` and `OnbetapFilter::processG` directly
at 48 kHz / os = 1, across modes, resonance settings and input amplitudes
(4 M samples each, warmed up, input signal precomputed into a table so the
benchmark's own `sinf` doesn't pollute the measurement).

### 1b. Cortex-A7 static op counts

The hot functions cross-compiled with the MetaModule SDK's **exact** release
flags, taken from `metamodule-plugin-sdk/cmake/arch_mp15xa7.cmake`:

```
-O3 -fno-exceptions -fno-math-errno -mcpu=cortex-a7
-mfpu=neon-vfpv4 -mfloat-abi=hard -mtune=cortex-a7
```

then disassembled with `arm-none-eabi-objdump -d` and the expensive ops counted
per symbol. This is the more decision-relevant of the two, for the reason in
§2.

**Note on `-ffast-math`:** the SDK does **not** pass it (only `-fno-math-errno`).
This matters more than anything else in this document — it means GCC is
forbidden from turning any `x / constant` into `x * (1/constant)`, because that
is not an IEEE-exact transformation. Every division by a constant in our source
compiles to a real hardware divide.

---

## 2. Measurements

### Cortex-A7 static op counts (per sample, per channel, 1x, standard accuracy)

| Hot function | A7 instructions | `vdiv.f32` | libm calls |
|---|---|---|---|
| `WaspFilter::process` (Vespid) | 411 | **26** | **2 × `sinhf`** |
| `OnbetapFilter::processG` (Onbetap) | 397 | **25** | 0 |

These are *static* counts over the whole function, so they include divides
duplicated across mutually-exclusive branches; the count actually executed on a
typical sample is lower (source-level analysis in §3 puts Vespid at \~17 and
Onbetap at \~11–16). Either way, the shape of the problem is unambiguous.

**Why this dominates:** `VDIV.F32` on Cortex-A7 is \~10–14 cycles and
**not pipelined** — it occupies the FP divide unit, so consecutive divides
serialize. \~15 executed divides is on the order of 150–210 cycles of pure
divide latency per sample per channel, before any of the actual arithmetic.
Stereo doubles it. `sinhf` from newlib is a full libm call on top of that.

### Host micro-benchmark (Apple Silicon, ns per sample per channel)

| Configuration | ns/sample | % of one 48 kHz core |
|---|---|---|
| Vespid, British, rho 0.00 | 59.4 | 0.29% |
| Vespid, British, rho 0.95 | 51.7 | 0.25% |
| Vespid, German, rho 0.95 | 43.4 | 0.21% |
| Onbetap, Hard, res 0.00 | 39.3 | 0.19% |
| Onbetap, Soft, res 0.90 | 45.3 | 0.22% |

Primitive costs on the same host, for attribution: `sinhf` 1.60 ns,
float divide 0.50 ns.

**Read these numbers with care — they systematically understate the win.**
Apple Silicon's FP divide is cheap and pipelined (0.50 ns measured) and its
`sinhf` is fast. The Cortex-A7 penalizes exactly the two things this
optimization removes. Host timing is useful for confirming *no regression* and
for catching algebra mistakes; it is not a proxy for the MetaModule gain.

This is the same caveat already recorded in `tests/vespid/mm-sim-notes.md` §4,
which measured host-relative load and could not calibrate it to real A7
hardware. Nothing here resolves that gap — no MM device is available in this
environment.

---

## 3. Findings — Vespid (`src/vespid/`)

Per-sample source-level divide count on the common path (diode conducting,
states not railed): **\~17 divides + 2 `sinhf`**.

### 3.1 `sinhf` is called twice on the same argument (exact duplication)

- Secant pass: `WaspFilter.hpp:116-119` computes
  `sd = 1 + kKD*sinh(clamp(ydPrev/kVD))/ydPrev`.
- Commit pass: `WaspFilter.hpp:177-178` computes
  `dArg = clamp(yd/kVD)` and `kKD*sinh(dArg)`.
- `WaspFilter.hpp:184` then sets `ydPrev = yd`.

So next sample's `ydPrev` *is* this sample's `yd`, and the secant's `sinh`
argument is bit-identical to the one the commit block already evaluated.
Caching `kKD*sinh(dArg)` in the commit block and forming
`sd = 1 + diode/yd` next sample is **exactly** equivalent.

**Saves: 1 `sinhf` + 1 divide per sample.** No fidelity cost whatsoever.

### 3.2 `finv()` and `finvSlope()` recompute the same `tanhApprox`

`WaspFilter.hpp:120-121` calls both on the same argument `vgPrev`.
`finv` (`:198-202`) and `finvSlope` (`:205-210`) each compute
`tanhApprox(±invA0*vg/vHi_or_vLo)` — the *same* value, on the same branch.
`finvSlope` then returns `-invA0*(1-t*t)`.

Fusing them into one call returning `{F0, A}` is **exactly** equivalent.

**Saves: 1 `tanhApprox` (1 divide) + 1 divide (the `/vHi`).**

### 3.3 Next sample's `tanhXdX` recomputes a ratio the commit block already formed

This is the subtlest one and worth stating precisely.

- Commit: `WaspFilter.hpp:172` computes `Sh = tanhApprox(m.c*hp) / m.c`.
- Next sample: `WaspFilter.hpp:112` computes `th = tanhXdX(m.c*hpPrev)`, and
  `hpPrev == hp` (set at `:184`).

By definition `tanhXdX(u) == tanhApprox(u)/u`. So if the commit block instead
computes

```
u  = m.c * hp;
rh = (|u| > 3) ? 1/|u| : (27 + u*u)/(27 + 9*u*u);   // == tanhXdX(u)
Sh = hp * rh;                                       // == tanhApprox(u)/m.c
```

then `rh` **is** next sample's `th`, and the `/m.c` disappears. Verified for
both regions:

- `|u| > 3`: `Sh = hp/|u| = hp/(c·|hp|) = sign(hp)/c` — matches `tanhApprox`'s
  ±1 clamp exactly.
- `|u| <= 3`: algebraically identical, differing only in float rounding
  (\~1e-7).

Same applies to `tb` / `Sb` (`:113` and `:174`). `hpPrev`/`bpPrev` are then only
needed for the `stateFinite()` check.

**Saves: 4 divides per sample** (2 `tanhXdX` calls, 2 `/m.c`).

Requires `reset()` to seed `th = tb = 1.0` (= `tanhXdX(0)`) and `sd = 1.0`.
Holds for the high-accuracy path too, since `hpPrev` is the post-Newton,
post-clamp `hp` in both cases.

### 3.4 Divisions by per-mode constants

All of these are loop-invariant and can be precomputed as reciprocals in
`ModeConfig`:

| Division | Site | Reciprocal to precompute |
|---|---|---|
| `/ m.nInv` (×2) | `WaspFilter.hpp:128-129`, `:178-179` | `invNInv` |
| `invA0*vg / m.vHi` | `:200`, `:207` | `a0OverVHi` |
| `invA0*vg / m.vLo` | `:201`, `:208` | `a0OverVLo` |
| `/ m.c` (×2) | `:172`, `:174` | eliminated outright by §3.3 |
| `yd / kVD` | `:143`, `:177` | `constexpr invKVD` |
| `(v-hi) / w`, `w = 0.3` | `wasp_dsp_utils.hpp:169-170` | `constexpr invW` |

**Safety check:** `ModeConfig` is only ever taken by const reference — grep
confirms the sole references outside `WaspFilter.hpp` are
`Vespid.cpp:207` and six `const ModeConfig&` parameters in
`tests/vespid/test_wasp_filter.cpp`. Nothing constructs one, so adding derived
fields cannot break a call site.

**Saves: \~5 divides per sample** (more when the rail clamp engages).

### 3.5 Module-level items (`src/Vespid.cpp`)

- **`isConnected()` inside the per-voice function.** `Vespid.cpp:324` tests
  `inputs[AUDIO_INPUT_R].isConnected()` inside `processChannel`, which runs once
  per voice per sample — so 16× per sample on a 16-voice patch.
  `Onbetap.cpp:318` already hoists the equivalent check out of its voice loop.
  Vespid should match.
- **`setChannels` on 8 outputs every sample.** `Vespid.cpp:348-349` loops all
  `NUM_OUTPUTS` unconditionally. Cheap per call, but it is 8 calls/sample for a
  value that changes only when the patch changes.
- **The `highAcc` block is dead code on MetaModule.** `_highAcc` is
  compile-time `false` there (`Vespid.cpp:82-86`), but the Newton block
  (`WaspFilter.hpp:134-161`, containing `sinh` *and* `cosh`) is still a runtime
  `if`. The compiler cannot prove it unreachable, so it stays in the function —
  costing a branch, register pressure, and I-cache footprint in the hot path.
  Guarding it with `#if !defined(METAMODULE)` shrinks the hot function on the
  target that needs it most.

### 3.6 Vespid projected result

| | Divides | `sinhf` |
|---|---|---|
| Now (common path) | \~17 | 2 |
| After §3.1–3.4 | **\~5** | **1** |

---

## 4. Findings — Onbetap (`src/onbetap/`)

Per-sample source-level divide count: **\~11–16**.

### 4.1 `satGain` pays two divides where the algebra needs one

`OnbetapFilter.hpp:117-121` computes `sat(v)/v`, and `sat()` (`:109-114`)
internally calls `tanhish()` (`:99-104`), which itself divides. So each
`satGain` is two divides — and `processG` calls it **four times** per sample
(`:137-138` and `:141-142`).

But the division by `v` cancels. In the linear region (`0 <= v <= 3`,
`over == 0`):

```
sat(v)/v == v*(27+v²)/(27+9v²) / v == (27+v²)/(27+9v²)
```

— one divide, no division by `v`, and the `if (a < 1e-4) return 1.f` guard
becomes unnecessary (the closed form is well-behaved at 0; it returns
`1 - 8v²/27`, i.e. within \~3e-9 of the old hard-coded `1.f`).

Working through all four regions:

| Region | Optimized `satGain` |
|---|---|
| `0 <= v <= 3` | `(27+v²)/(27+9v²)` |
| `v > 3` | `(1 + kSatLeak·(v-3))/v` |
| `-2.79 <= v < 0`, `w = v·(1/kAsymNeg)` | `(27+w²)/(27+9w²)` |
| `v < -2.79` | `(-kAsymNeg + kSatLeak·(v+3·kAsymNeg))/v` |

(`core` at the positive clamp is exactly `3·36/108 == 1.0` in float, which is
why the `v > 3` row is that clean.)

**Saves: 4 divides per sample.** Also replaces `v / kAsymNeg` with a
multiply by a precomputed reciprocal in the two negative branches.

### 4.2 The 1x path runs a no-op upsampler and a no-op decimator

`Onbetap.cpp:275-284` is the fall-through path that MetaModule now takes by
default. At `oversample == 1` it:

- computes `t = (float)i / oversample` → **a hardware divide** to produce 1.0;
- computes `x = xPrev + (x1 - xPrev) * t` → an interpolation that returns `x1`;
- accumulates into `lp/bp/hp`, then computes `inv = 1.f / oversample` →
  **a second divide** — to multiply by 1.0.

A dedicated `if (oversample == 1)` branch calling `processG(x1, g, kEff)`
directly is **bit-identical** and removes both divides plus the surrounding
arithmetic. `xPrev` must still be updated so switching oversampling later stays
correct.

### 4.3 Divisions by literal constants

| Division | Site | Fix |
|---|---|---|
| `push * v / 9.f` | `Onbetap.cpp:304` | multiply by `1/9` |
| `v / kAsymNeg` (×5) | `OnbetapFilter.hpp:111` via `sat`/`satGain` | precomputed reciprocal |
| `(av - kSoftKnee) / (kSoftMax - kSoftKnee)` | `OnbetapFilter.hpp:181` | span is `0.6`; precompute `1/0.6` |

`t/(1+t)` at `:182` is a genuine divide and stays. `softLimitOne` only runs in
Soft limit mode and only when `|v| > 3.4`, so those two are conditional.

### 4.4 4x path: stage-A FIR computes outputs it discards

`Onbetap.cpp:247-259`. The `fir4*` `DecimFir9` instances are pushed on all four
substeps — correct, an FIR must see every input sample — but their output is
only *consumed* on even substeps (`if ((i & 1) == 0)`).

`DecimFir9::push` (`engine.hpp:87-93`) does the history update **and** the
9-tap MAC every call. On odd substeps the MAC is pure waste: 2 substeps × 3
taps × 9 MACs = **54 wasted MACs per side per sample** at 4x. Splitting `push`
into "advance history" and "advance + compute" is exactly equivalent.

### 4.5 The decimation FIRs still shift their whole history

`DecimFir13::push` (`engine.hpp:58-64`) and `DecimFir9::push` (`:87-93`) both do

```
for (int i = 12; i > 0; i--) z[i] = z[i-1];
```

This is precisely the inefficiency that `tests/vespid/mm-sim-notes.md` §4
flagged as a follow-up item and that **has already been fixed on the Vespid
side** — `wasp_dsp_utils.hpp:99-163` now uses ring buffers with a polyphase
decomposition, and its comment block documents the equivalence to within
\~1e-7. Onbetap's FIRs never got the same treatment.

Converting them to ring buffers is exactly equivalent and removes the per-push
data movement (13 or 9 moves × 3 taps × per-substep). **Affects 2x/4x only —
no effect on the 1x MetaModule default.**

### 4.6 The HP tap is computed even when the selected mode discards it

`Onbetap.cpp:146` computes `hp = -sat(node)` unconditionally, but the tap
lambda (`:289-297`) only uses `hp` for modes 2 (HP), 3 (notch) and 4 (peak).
In LP and BP mode it is computed and thrown away — one divide per sample at 1x,
and at 2x/4x an entire `DecimFir13` (plus a `DecimFir9` at 4x) per side.

Unlike `lp`/`bp`, `hp` is *not* part of the state update (`:148-149`), so
skipping it is exactly equivalent at 1x. See [Open decisions](#open-decisions)
for the 2x/4x caveat.

### 4.7 Onbetap projected result

| | Divides |
|---|---|
| Now | \~11–16 |
| After §4.1–4.3 | **\~7–9** |

Plus §4.4/4.5 for 2x and 4x users.

---

## 5. Explicitly ruled out

Recorded so these don't get re-proposed later.

- **Thresholding the commit-path `sinh` the way the secant already does.**
  `WaspFilter.hpp:116` skips the diode's `sinh` term entirely when
  `|ydPrev| <= 0.2`, but the commit path (`:177-178`) always evaluates it. Making
  the commit path match would kill the last `sinhf` on most samples — but it is
  the wrong move. In the secant the threshold only shifts a *linearization
  pivot* (second-order effect); in the commit path it would put a real
  discontinuity in the output waveform. At the threshold the dropped term is
  `kKD·sinh(0.2/kVD) ≈ 5.6e-3`, which through `/nInv` and the inverter slope
  lands around **33 mV** on `hp` — a step, not a rounding difference. That
  means added harmonic distortion and possible zipper on resonance sweeps.
  Do not do this.

- **Reducing Onbetap's two refinement passes to one.** `processG` runs the
  secant solve twice (`:136-144`). Dropping to one pass would roughly halve the
  core cost, but the number of solver passes *is* the fidelity axis here — it is
  the same trade Vespid exposes as its Standard/High accuracy menu. This is a
  voicing change, not an optimization.

- **Reusing the previous sample's pass-2 secant gains as pass 1's predictor.**
  Tempting (it would remove pass 1's two `satGain` calls) but it changes the
  solver's fixed point, so it changes the sound.

- **Vespid halfband resampler efficiency.** Already done —
  `wasp_dsp_utils.hpp:88-97` documents the ring-buffer + polyphase rework. The
  complaint in `mm-sim-notes.md` §4 is stale with respect to Vespid (though
  still live for Onbetap, §4.5 above).

---

## 6. Verification plan

Behavioral, not bit-exact — which the existing suites already are (they assert
dB tolerances and pass/fail thresholds, e.g. `test_wasp_filter.cpp` compares
against `tests/vespid_ref/golden.json` in dB, `test_onbetap.cpp:118` asserts
`rms < 1e-3`). The float-reassociation differences introduced above are
\~1e-7, which is three orders of magnitude below Vespid's own 1e-4 dither seed
(`Vespid.cpp:115`).

1. `tests/run.sh` — full suite, must stay green (`mf20`, `loooop`,
   `particules`, `onbetap`, `vespid` + the Python guards).
2. Re-run the A7 static op count and record the before/after `vdiv`/libm
   numbers in this document.
3. Re-run the host micro-benchmark to confirm no regression and catch algebra
   errors.
4. A direct A/B: render identical input through old and new cores and check the
   difference is at float-noise level, per mode / limit / resonance / drive
   corner. This is the real equivalence test — the unit suites check behavior,
   not sample-level equality.
5. VCV `.dylib` + `.mmplugin` both still build clean.
6. GUI/listening check on hardware is a **user-run** item — per project
   convention, agent-driven GUI-simulator testing is out of scope.

---

## 7. Open decisions

Both are genuine fidelity-vs-CPU calls, not routine judgment. Everything in
§3.1–3.5 and §4.1–4.5 proceeds regardless.

### 7.1 Vespid's remaining `sinhf`

After §3.1 dedupes the second call, one `sinhf` per sample remains
(`WaspFilter.hpp:178`) and cannot be removed without approximating it (§5 rules
out thresholding it away). Options:

- **Keep newlib `sinhf`.** Exact, and respects the fact that this is a fitted
  circuit model with a committed golden reference and documented constants
  (`tests/vespid_ref/fitted_constants.md`).
- **Fast-exp `sinh`.** Odd polynomial for `|x| <= 3`, `0.5·e^x` above it (that
  approximation is within 0.25% at `x = 3` and improves rapidly), with a
  bit-trick `exp2`. Roughly \~0.2% error on the diode term — which is itself a
  small correction to the `vg` node, so the output effect is smaller than that.
  Probably the single largest remaining A7 win, since it removes the last libm
  call from the hot path.

### 7.2 Onbetap's unused HP tap (§4.6)

Exactly equivalent at 1x. At 2x/4x, skipping it also skips the HP decimation
FIRs, so their history goes cold; switching *into* an HP-using mode then gives
\~6 samples (the 13-tap FIR's group delay) of ramp-in. That lands under a
crossfade weight below 3% (`modeXfStep` is 5 ms ≈ 240 samples at 48 kHz,
`Onbetap.cpp:139`), so it should be inaudible — but it is a real, if tiny,
behavior change.

Note Vintage character switches modes hard rather than crossfading
(`Onbetap.cpp:298`, deliberate — it matches the factory panel switch and is
documented as "DC step and all"), so there the ramp-in is not covered by a
crossfade at all. It would sit alongside a DC step that is already louder than
it.

Conservative alternative: gate the skip on `oversample == 1`, keeping the
MetaModule default fast and leaving 2x/4x untouched.

---

## 8. Reproducing the measurements

Scratch harnesses (not committed — they live in this session's scratchpad):

- `bench.cpp` — host micro-benchmark. Build:
  `g++ -std=c++20 -O2 -o bench bench.cpp`
- `armprobe.cpp` — `noinline` wrappers around the two hot functions so each
  gets its own symbol, plus `count.py` to tally `vdiv`/`bl` per symbol from
  `objdump` output.

The A7 probe needs `-D_GNU_SOURCE -DM_PI=3.14159265358979323846`: bare
`arm-none-eabi-g++` doesn't define `M_PI`, which `wasp_dsp_utils.hpp:44`,
`WaspFilter.hpp:74` and `:213` rely on. The real MetaModule build gets it via
the Rack compatibility headers, so this is a harness-only detail, not a
portability bug.

If these are worth keeping, `tests/` is the natural home — a committed
`vdiv`/libm-count regression check would keep this class of problem from
creeping back in.

---

## 9. Full-investigation addendum (2026-07-24, second pass)

Independent verification of everything above: all source claims re-read against
the code, the A7 op counts re-measured from scratch
(`arm-none-eabi-g++ 12.3.rel1`, exact SDK release flags, `-DMETAMODULE`), and —
new — both optimized cores **prototyped and measured**, including a fast-`sinh`
prototype that resolves open decision §7.1. Prototypes and harnesses live in
this second session's scratchpad (`armprobe*.cpp`, `bench_equiv.cpp`,
`test_step_equiv*.cpp`, `fastsinh.hpp`, `opt*/`).

### 9.1 Correction: the shipping baseline is worse than §2 says

The §2 table was measured with the Newton block already stripped. The function
**as currently compiled into the MetaModule build** — where `_highAcc` is a
runtime `bool` the compiler can't fold — measures:

| Variant | A7 insns | `vdiv.f32` | libm |
|---|---|---|---|
| `WaspFilter::process` **as shipping** (Newton block present) | **677** | **43** | **3 sinhf + 1 coshf** |
| + §3.5 Newton `#if` guard only | 414 | 26 | 2 sinhf |
| + §3.1–3.4 (optimized prototype) | 367 | 12 | 1 sinhf |
| + fast `sinh` (§9.4, resolves §7.1) | 446 | 12 | **0** |
| `OnbetapFilter::processG` shipping | 463 | 28 | 0 |
| + §4.1/§4.3 (optimized prototype) | 358 | 21 | 0 |

(Static counts include untaken branches; per-sample *executed* divides:
Vespid \~17 → \~5, Onbetap core \~11 → \~7, plus §4.2/§4.3 removing \~3 more at
module level.) The Newton dead code costs mostly register pressure and I-cache,
not executed divides — but it is ~40% of the hot function's static footprint,
so §3.5's `#if !defined(METAMODULE)` guard matters more than §3.5 implied.
The Onbetap baseline delta vs §2 (463/28 vs 397/25) is presumably a
toolchain-version difference; the shape is unchanged.

Host micro-benchmark (Apple Silicon, `-O2`, 4M samples, mid settings) — even
where divides are cheap the rework wins, and there is no desktop regression:

| | orig ns/sample | opt ns/sample |
|---|---|---|
| Vespid (British, rho 0.5) | 58.0 | 41.5 (43.0 with fast sinh) |
| Onbetap (Soft, kEff 0.4) | 44.4 | 35.9 |

### 9.2 Equivalence, measured — and a verification-methodology warning

Free-running A/B comparison (old vs new core on identical input) shows **large**
trajectory differences at some corners (Vespid max 23 V instantaneous). This is
**not an algebra error**: these are high-gain nonlinear feedback loops, and at
self-oscillating / deep-overdrive corners a 1-ulp rounding difference grows
exponentially until the two (statistically identical) trajectories drift out of
phase. Any recompile that changes FP contraction would do the same. §6's step 4
("difference at float-noise level") is therefore **unrunnable as stated** for
those regimes; use one of:

- **per-step resync** (implemented in `test_step_equiv.cpp`): run the old core
  freely, force the new core to the old core's pre-step state each sample
  (caches recomputed per their definitions), compare one-step outputs;
- or the existing dB-domain behavioral suites, which are chaos-immune.

Per-step results (worst case over the full mode/rho/fc grid, 24k samples each):

| Input amplitude | Vespid per-step output diff | with fast sinh |
|---|---|---|
| 0.5 V | 4.8e-6 V | 4.8e-6 V |
| 5 V | 4.6e-5 V | 7.8e-5 V |
| 50 V (high drive) | 4.4e-4 V | 4.7e-4 V |
| 320 V (max drive corner) | 8.8e-4 V | 2.0e-3 V |

All below or near the module's own deliberate 1e-4 V dither at real signal
levels, and \~−75 dB against the rail-clipped multi-volt output at the abuse
corners. **Onbetap: 1.9e-6 core units worst case** across the full
limit/kEff/g/amp/vintage grid — pure float noise; §4.1/§4.3 are as safe as
claimed.

Two precision nuances found while measuring:

- The overdrive-corner sensitivity is **not** the `yd/kVD` → `yd*kInvVD` swap
  (a variant keeping that one divide measures identically). It is the solver
  itself: near inverter saturation `A = -invA0·(1−t²)` catastrophically
  cancels, and `vg` reaches \~1e8 when the diode `sinh` clamps, so *any* 1-ulp
  change (fusion, any reciprocal) is amplified. Nothing to fix — the original
  is equally sensitive to its own rounding there and `hp` is rail-clamped —
  but it means only §3.1 and §3.5 are strictly bit-exact; §3.2/§3.3/§3.4 are
  "float-noise per step", which the table above quantifies.
- §4.2's "bit-identical" claim is a slight overstatement: `xPrev + (x1−xPrev)`
  is not always bit-equal to `x1` (ulp-level rounding when magnitudes differ).
  Same conclusion — the dedicated `oversample == 1` branch is still right —
  just don't assert bit-equality in tests.

### 9.3 Implementation notes (found while prototyping)

- `reset()` must seed the new caches: `th = tb = 1.f` (= `tanhXdX(0)`),
  `diodePrev = 0.f`.
- **Mode switch carries state without reset**, so after a Character change the
  cached `th`/`tb` were computed with the old mode's `c` for one sample. Either
  accept it (secant pivots are approximations; it self-corrects next sample) or
  recompute the two pivots in `setMode()` when the pointer actually changes —
  once per user action, two divides, clean. Prototype does the former;
  recommend the latter.
- `ModeConfig` gains three derived fields (`invNInv`, `a0OverVHi`,
  `a0OverVLo`) computable as constexpr arithmetic in the `kGerman`/`kBritish`
  initializers — no runtime init needed, and §3.4's grep already confirmed no
  other constructors exist.
- The desktop Newton block keeps `std::sinh`/`std::cosh` and can keep the
  un-fused `finv`/`finvSlope` calls (it already recomputes everything from
  scratch; High accuracy stays exact).

### 9.4 §7.1 resolved: a divide-free `sinh` that is 200× tighter than proposed

§7.1 sketched a fast `sinh` with \~0.2% error. The prototype
(`fastsinh.hpp`) does much better: **max relative error 1.1e-5 over the full
clamped ±30 range** (5,000× tighter than 0.25% at the handoff), no divide, no
libm, \~35 straight-line flops:

```cpp
inline float exp2Fast(float y) {
    int n = (int)y;
    n -= ((float)n > y);          // floor for negative y
    float f = y - (float)n;       // f in [0,1)
    // 2^f: Taylor of e^(f ln2) through degree 6 (max rel err ~1.5e-5)
    float p = 1.f + f * (0.69314718056f + f * (0.24022650696f
              + f * (0.05550410866f + f * (0.00961812911f
              + f * (0.00133335581f + f * 0.00015403530f)))));
    uint32_t bits;
    std::memcpy(&bits, &p, 4);
    bits += (uint32_t)n << 23;    // scale by 2^n
    float r;
    std::memcpy(&r, &bits, 4);
    return r;
}
inline float sinhFast(float x) {
    float ax = (x < 0.f) ? -x : x;
    if (ax < 0.5f) {              // odd Taylor: avoids E+ − E− cancellation
        float x2 = x * x;
        return x * (1.f + x2 * (1.f / 6.f + x2 * (1.f / 120.f)));
    }
    float y = x * 1.44269504089f;
    return 0.5f * (exp2Fast(y) - exp2Fast(-y));
}
```

Per-step output impact is in the §9.2 table: invisible at real signal levels.
It removes the **last libm call** from the standard-accuracy hot path — on the
A7, newlib `sinhf` is a full out-of-line call (order 10² cycles) plus the
register spills around it, so this is likely the single largest remaining win
after the divides.

**Recommendation:** use `sinhFast` in the standard-accuracy path on **both**
platforms (so the host test suites actually exercise it, and VCV/MM stay in
parity), keep `std::sinh`/`cosh` only inside the desktop-only Newton block.
MetaModule-gating it instead would preserve desktop bit-exactness but leave the
fast path untested on host.

### 9.5 §7.2 recommendation

Take the conservative option already suggested: skip the `hp` tap only when
`oversample == 1` **and** neither crossfade endpoint (nor Vintage's hard
switch target) needs it — exactly equivalent, no FIR ramp-in question, and 1x
is the only place the CPU matters. Cost of the tap at 1x is one `sat()` call
(\~1 divide + \~15 insns) per side per sample in LP/BP modes — worth taking,
not worth any behavior risk at 2x/4x.

### 9.6 Everything else in §3–§5 re-verified

§3.1 (bit-exact dedupe), §3.2, §3.3 (including both `tanhXdX` regions), §3.4's
table, §4.1's four-region algebra (`core` at the positive clamp is exactly 1.0
in float, confirmed), §4.4, §4.5, §4.6, and all four §5 rule-outs check out
line-for-line against current `src/`. The §3.4 safety grep still holds.

---

## 10. Implemented (2026-07-24, branch `cpu-opt`)

Everything in §3, §4, and the §7 decisions as recommended in §9.4/§9.5 (user-
approved) is implemented on branch `cpu-opt`, one commit per concern:

- `Vespid: add divide-free fast sinh with accuracy test` — `src/vespid/fastsinh.hpp` + `tests/vespid/test_fastsinh.cpp`
- `Vespid: replace railClamp knee divides with reciprocal multiply`
- `Vespid: cache secant pivots, fuse inverter eval, reciprocals, fast sinh` — §3.1–3.4, §3.5 Newton guard, §7.1; `setMode()` refreshes the cached pivots on an actual mode change (§9.3)
- `Vespid: hoist stereo check out of voice loop, gate setChannels` — §3.5 module items
- `Onbetap: single-divide satGain closed forms, reciprocals, optional HP tap` — §4.1, §4.3, `processG(..., bool needHp = true)`
- `Onbetap: ring-buffer decimation FIRs with history-only push` — §4.5 + the §4.4 `pushHistory` hook
- `Onbetap: direct 1x path, gated HP tap, FIR history-only substeps` — §4.2, §4.4, §4.6/§7.2 (gate only ungates at 1x when neither crossfade endpoint reads hp)

### Final measurements (same probes as §9)

A7 static, SDK release flags, `-DMETAMODULE`:

| Hot function | before (shipping) | after |
|---|---|---|
| `WaspFilter::process` | 677 insns / 43 vdiv / 3 sinhf + 1 coshf | **446 / 12 / 0 libm** |
| `OnbetapFilter::processG` | 463 insns / 28 vdiv | **358 / 21 / 0** |

Executed-path divides: Vespid \~17 → \~5 per sample per channel, Onbetap
\~14 → \~7 including the module level (§4.2 removed 2 more at 1x, §4.3 one).

Host (Apple Silicon, `-O2`, 4M samples): Vespid 58.7 → 44.0 ns/sample (−25%),
Onbetap 46.8 → 27.7 ns/sample (−41%). No desktop regression.

### Verification run

- `tests/run.sh` exit 0 — all module suites + Python guards, including the
  new `test_fastsinh` (max rel err 1.117e-5, limit 2e-5).
- Per-step resync equivalence vs `main` (methodology §9.2):
  - Vespid outputs: 4.77e-6 V @ 0.5 V, 7.79e-5 @ 5 V, 4.68e-4 @ 50 V,
    1.63e-3 @ 320 V — all within the §9.2 budgets.
  - Onbetap: 1.907e-6 cu worst case over the full grid.
  - `needHp = false`: lp/bp and both states bit-identical, hp forced 0.
- Ring FIRs vs shift-register originals: ≤ 9.5e-7 on ±5 noise (compiler
  vectorization reorders the reduction; scalar op order is identical —
  same class as the Vespid halfband rework's documented \~1e-7).
- VCV `make -C vcv -B` clean, installed to Rack2 plugins dir; MetaModule
  cmake build clean, `All symbols found!`, `.mmplugin` created.

### Remaining user-run checklist

- [ ] MetaModule hardware: load a Vespid + Onbetap patch at 1x, confirm the
  CPU relief, listen for character changes — German self-osc onset/pitch,
  British drive rasp, Onbetap self-osc pitch and onset, mode crossfades
  into HP/notch/peak, Vintage hard mode switches.
- [ ] Desktop VCV: A/B against the previous build at normal and max drive.

### 10.1 MF-20 port (same branch, follow-up request)

The same audit applied to MF-20 (`src/mf20/`). Portable items, implemented:

- **`processVCV`/`processVCVG` divided by `kVCVScale` (0.2f) three times per
  call** — now `* kInvVCVScale`. This was the largest cost: each stereo voice
  makes four filter calls per sample (HP→LP × L/R), so 12 constant divides.
- **Both solvers spent a divide classifying the clip region** (trial
  `rhs / D1`, then a second divide when clipped). The test
  `|k·rhs| ≤ T·D1` classifies identically (D1 > 0 is established on both
  paths — for OTA, D1 = 1 + g·(2−k) + g² > 0 up to the resTaper max
  k = 2.05), so exactly one divide executes per solve. A7 disassembly
  confirmed GCC was really executing two divides on clipped samples.
- **K35's forward clip divided by the slewed threshold** (`in/clipThreshold`)
  — `setDriveCharacterFromThreshold` gained a two-argument form taking the
  precomputed reciprocal (the module's existing `_driveSqrt` smoother); the
  clip is now a multiply. Transient nuance: a slewed reciprocal ≠ the
  reciprocal of the slewed threshold mid-Drive-sweep (\~5 ms, inaudible).
- **`isConnected()` hoisted out of the per-voice loop** (same as Vespid).

Measurements:

- A7 static (`processVCVG`, both mode branches): 152 insns / 9 vdiv →
  160 / 5; executed divides per filter call 4–6 → **exactly 1** (plus zero
  from the VCV rescale). Per stereo voice: \~16–24 → 4 per sample.
- Host with the **actual VCV desktop flags** (`-O3
  -funsafe-math-optimizations`): 7.6 → 4.6 ns/sample per filter call (−40%).
  Note: at plain `-O2` the region change *looks* like a regression (clang
  if-converts the old code into two pipelined divides, branchless, which
  beats a data-dependent branch on Apple Silicon) — that flag set ships on
  neither platform; measured and disregarded.
- Per-step resync equivalence vs `main` over mode/res/g/drive/amp grid
  (incl. res = 1.025, the K35 D1 ≤ 0 regime): max **2.86e-6 V**.
- Full suite green; VCV + MM builds clean.

Not portable: sinh/tanh caching (piecewise-linear clips, no transcendentals),
FIR ring buffers (no oversampling), the Newton `#if` (no accuracy modes).

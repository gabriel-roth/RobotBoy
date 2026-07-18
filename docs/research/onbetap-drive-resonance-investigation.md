# Onbetap — Drive / resonance interaction: investigation report

**Date:** 2026-07-18
**Module:** Onbetap (Polivoks-style nonlinear multimode filter)
**Branch:** `onbetap-drive-hw-path` (unmerged at time of writing)
**Related docs:** `docs/superpowers/specs/2026-07-18-onbetap-drive-hw-path-design.md`,
`docs/superpowers/specs/2026-07-15-onbetap-dsp-spec.md`,
`docs/research/onbetap-worklog.md`, `docs/research/polivoks-emulations.md`

---

## 0. TL;DR

The Onbetap Drive control was documented to make the sound *"louder and dirtier
while the resonant peak rings less."* Three things were investigated, in order:

1. **Drive made the module quieter and cleaner** (the opposite of the promise)
   at moderate/high Q. **Root cause:** the output makeup gain double-compensated
   the filter core's own rail-clamp level compression. **Fixed** by making the
   makeup a constant buffer gain (Drive-independent).

2. **After the fix, Drive got smoother/quieter again past \~90%** of the knob at
   high Q. **Root cause:** the "drive suppresses resonance" feature reaching its
   extreme — at max drive the resonant peak (which supplies most of the high-Q
   level/edge) is fully choked. **Mitigated** by trimming the default Drive span
   36 → 30 dB, which moves the onset of that zone later on the knob.

3. **The calm zone at the top of the knob cannot be removed by any Drive Span
   setting.** **Root cause (intrinsic):** Drive Span only rescales knob → gain;
   the top of the knob is always max drive; for any normal input level max drive
   saturates the core; a saturated core has no resonance. The zone's *position*
   depends on **input level** (a loud signal rings less than a quiet one — an
   authentic, level-dependent behavior). No change made; this is the feature at
   its limit.

Nothing is broken. Items 1 and 2 produced code changes; item 3 is inherent
physics of the model (and of the real hardware).

---

## 1. Background: what Onbetap is and how Drive works

Onbetap emulates the Soviet Polivoks VCF: two К140УД12 programmable op-amps run
as current-controlled integrators, closed into a two-integrator state-variable
loop. The defining nonlinearity is the op-amp input **diff-pair saturating**;
the integrator states clip at the output-swing rails. See
`src/onbetap/OnbetapFilter.hpp` (core model) and
`docs/research/polivoks-emulations.md` for the circuit derivation.

**Drive is not a hardware control.** The real Polivoks has an *input level* pot;
its manual advises attenuating hot modular signals to \~−10 dB to avoid
permanent overload (`polivoks-emulations.md:285`). Onbetap's "Drive" knob is that
input level, mapped to gain into the nonlinear core. The documented,
circuit-authentic target behaviors (`polivoks-emulations.md:307-315`) are:

- **Input drive is half the sound** — hot input = asymmetric-clipping grit.
- **Drive suppresses resonance** — higher input level chokes the resonant peak
  ("natural compression between signal and self-osc").
- Clean at low Q, feral at high Q; early/gradual then sudden self-oscillation.

So "louder, dirtier, rings less" as you push Drive is authentic hardware
behavior — the module should reproduce it.

### 1.1 The signal chain (per stereo side, per voice)

Host sample → **input drive gain** → 2× oversample (linear interp) → nonlinear
core solve (`OnbetapFilter::processG`) → 13-tap decimation FIR → tap select
(LP/BP/HP/Notch/Peak) → **output makeup gain** → DC blocker → **output VCA**
(`9·tanhish(v/9)`) → host output.

Key code locations (current, on the branch):

| Element | Location |
|---|---|
| Drive → gains mapping | `src/onbetap/drive.hpp:32` (`onbetap::driveGains`) |
| gains applied | `src/Onbetap.cpp:184-186` |
| resonance (res → k) map | `src/Onbetap.cpp:178` |
| cutoff phase-lag (kEff) | `src/Onbetap.cpp:283` |
| per-side oversampled solve | `src/Onbetap.cpp:213-262` (`processSide`) |
| core solver | `src/onbetap/OnbetapFilter.hpp:99` (`processG`) |
| output VCA | `src/Onbetap.cpp:261` |

### 1.2 The two gains that fight

`driveGains` (`drive.hpp:32-38`) produces two numbers from the Drive knob:

```cpp
spanOct    = driveDb / 6.0206;                 // dB → octaves; driveDb default 30
driveGain  = exp2(-2 + spanOct * drive);       // 0.25×  …  (drive 0→1)
driveScale = driveGain * kBaseTrim * kVoltsToCore * headroom;  // INPUT gain
makeup     = kOutScale * exp2(outDb / 6.0206); // OUTPUT gain (constant)
```

Constants: `kVoltsToCore = 1/2.4`, `kBaseTrim = 0.4`, `kOutScale = 20.5`
(`drive.hpp:19-21`). With defaults (span 30, headroom 1, outDb 0):
`driveScale` runs 0.083 → 2.63 across the knob; `makeup` is a constant 20.5.

The core's integrator states clamp at `±kRailPos/kRailNeg ≈ ±4.4/−4.1`
(`OnbetapFilter.hpp:62-63`). **This clamp is a level compressor** — that fact is
central to all three findings.

---

## 2. Methodology

The module wrapper (`Onbetap.cpp`) depends on the VCV Rack SDK, so behavioral
measurement was done with **host-free C++ harnesses** that replicate the exact
per-sample signal path (input drive → 2× oversampled `processG` → `DecimFir13`
decimate → tap → makeup → DC block → output VCA) and call the *real*
`onbetap::driveGains` / `OnbetapFilter` code. Signals were steady sine tones
held at cutoff (the worst/most-audible case for resonance interaction),
analysed by coherent DFT (integer cycles, no leakage) for fundamental,
harmonics (2–12), THD, and total RMS. All figures below are output RMS in dB
re 1 V and THD = harmonic-RMS ÷ fundamental, unless noted.

Reproduction: the committed regression test
`tests/onbetap/test_drive_level.cpp` embodies the same harness (see §7). The
one-off exploratory harnesses lived in the session scratchpad and are not
committed; they can be regenerated from the tables here.

Standard conditions unless stated: cutoff 750 Hz, tone at cutoff, 48 kHz host
rate, 2× oversampling, Hard limiting, Tamed character, LP mode.

The **res → k** map (`Onbetap.cpp:178`) is `k = -0.06 + 1.08·(1-res)^2.3`. Q = 1/k;
self-oscillation onset (kEff < 0) sits near res ≈ 0.72 at this cutoff. So
"res 0.70" in the tables is *right at the self-osc knee* — the most sensitive
regime.

---

## 3. Finding 1 — Drive made it quieter and cleaner (fixed)

### 3.1 Symptom

User, by ear: hold a note at \~cutoff, Q \~70%, sweep Drive up. Ring
suppression was audible and correct, but past \~50% the sound got **quieter and
cleaner**, not louder and dirtier.

### 3.2 Measured (pre-fix, drive-dependent makeup `(0.25/driveGain)^0.75·kOutScale`)

Tone at cutoff, output level / THD:

| Drive | res 0.70 | res 0.30 |
|------:|---------:|---------:|
| 0.0 | +16.5 dB / 11.6% | +15.4 dB / 7.9% |
| 0.5 | +8.3 dB / 1.3% | +8.3 dB / 1.2% |
| 1.0 | −0.7 dB / 1.4% | −4.8 dB / 1.4% |

Output dropped \~17–20 dB and THD collapsed — the inverse of the intent.

### 3.3 Root cause — double compensation

Two Drive-linked gains moved in opposite directions: input `driveScale`
(0.25×→\~16× at the old span) and output `makeup` (a \~27 dB *cut* across the
sweep). They were designed to cancel so "sweeps don't just get louder"
(`2026-07-15-onbetap-dsp-spec.md`, original §5) — and were calibrated at **res 0,
cutoff 20 kHz** (filter wide open, sub-saturation), where the core output grows
\~linearly with input and the two do cancel (+1.35 dB across the sweep, the old
worklog test 1b).

But the **core already compensates level on its own** via rail clamping. At
moderate/high Q the resonant peak pins the core at its rails even at Drive 0, so
more input can't raise the level — it only adds harmonics and chokes the ring.
The makeup then compensated a *second* time on top of that, overshooting into
net attenuation. Worse, by shrinking the signal below the **output-VCA knee**
(`9·tanhish(v/9)`) — the dominant grit source at low Drive — it stripped the dirt
on the way down. Confirmed by isolating the raw core tap: at res 0.70 the core's
own THD is only \~1.0–1.5% and roughly flat with Drive, while the *output* THD at
Drive 0 was 11.6% — i.e. most of the low-Drive grit came from the output VCA,
which the shrinking makeup then removed.

The makeup's drive-dependence was also **not circuit-derived** — real Polivoks
outputs are low-level and clones add a *fixed* ×11 output buffer
(`polivoks-emulations.md:64`).

### 3.4 Fix

Make the output makeup a **constant** buffer gain (the ×11 analog),
Drive-independent, leaving the core's rail clamp as the sole level compressor.
`drive.hpp:37`. Drive 0 is bit-identical to before (`pow(1, 0.75)=1`), so the
existing Drive-0 level calibration is preserved.

### 3.5 Measured (post-fix, constant makeup)

| Drive | res 0.70 | res 0.30 | res 0.00 |
|------:|---------:|---------:|---------:|
| 0.0 | +16.5 dB / 11.6% | +15.4 dB / 7.9% | +10.3 dB / 2.0% |
| 0.5 | +17.3 dB / 16.8% | +17.3 dB / 16.7% | +17.3 dB / 16.5% |
| 1.0 | +17.6 dB / 4.4%\* | +17.4 dB / 17.4% | +17.4 dB / 17.4% |

Level holds at high/mid Q (core self-limits) and rises then plateaus at low Q;
THD rises with Drive across the range. Matches the promise.
(\*res 0.70 top-of-sweep is chaotic self-osc — see §5.)

### 3.6 Accepted caveat

At high Drive the output now runs **hot** (\~+17 dB ≈ 7 V RMS; peaks clip into
the 9 V output VCA — the intended "ferocious" behavior, bounded within ±10 V).
If ever too hot, use a *constant* trim (`kOutScale`, or the Output-trim menu
slider), never a Drive-dependent one. Default level unchanged.

---

## 4. Finding 2 — Smoother/quieter again past \~90% (mitigated)

### 4.1 Symptom

After the fix: Drive adds level and grit up to \~90% of the knob, then gets
**smoother and quieter again** toward the top.

### 4.2 Measured — it is Q-dependent

Fine sweep, tone at cutoff. Raw **core tap** (pre makeup/VCA) alongside output:

**res 0.50** — no reversal; core is flat:

| Drive | out THD | core fundamental | core THD |
|------:|--------:|-----------------:|---------:|
| 0.70 | 17.2% | −1.0 dB | 1.5% |
| 0.85 | 17.3% | −1.0 dB | 1.5% |
| 1.00 | 17.4% | −1.0 dB | 1.5% |

**res 0.70** (self-osc knee) — erratic; e.g. amp 2 V: THD 19.7% (d0.80) →
16.0% (d0.85) → 14.6% (d0.90) → 7.5% (d1.00); the core fundamental drops out at
isolated points (−11.7 dB) and output THD spikes toward 100% — the fingerprint
of the filter flirting with self-oscillation. Output stays bounded (\~7 V);
the stability torture test still passes.

### 4.3 Root cause

At high Q a large fraction of the perceived level and edge comes from the
**resonant peak / near-self-oscillation**, not from the input signal. Drive
suppresses resonance (by design). Pushed to the top, it chokes the resonance so
completely that its level/grit contribution is mostly gone → smoother, \~1 dB
quieter. Because grit is sourced from the core's own saturation (the minimal
design path), maxing Drive suppresses resonance *and* the resonance-derived grit
together — there is no separate drive-grit stage to keep the top dirty.

At moderate Q there is no reversal (the core tap is flat), because there is
little resonant contribution to choke.

### 4.4 Mitigation — Drive span 36 → 30 dB

Trim the default Drive span (max +24 → +18 dB). At span 36 the Drive-1.0 THD at
res 0.70 collapses to \~7.5%; at span ≤ 32 it recovers to \~16–20%. Span 30 maps
the last-productive knob point off the end of travel, so the reversal region
shrinks. `Onbetap.cpp:70`, menu default `Onbetap.cpp:444`. Menu range unchanged
(24–48 dB; raise it for the wilder, self-suppressing top). Existing patches keep
their stored span (persisted in JSON) — only new instances change.

**This reduces severity but does not cure it — see Finding 3.**

---

## 5. Finding 3 — The calm zone is intrinsic (span-independent)

### 5.1 Symptom / question

User: *"No matter where I set the Drive Span, there's an area at the top of the
knob where the resonance calms down."*

Correct. And it exposed that the Finding-2 "falls off the end" framing was
over-optimistic.

### 5.2 Measured — resonance survival vs knob position

Metric: core LP fundamental at cutoff with high Q (res 0.60) ÷ the same with
Q = 0. **1.0 means resonance is fully gone.** (0.2–0.3 dips = brief self-osc.)

**Hot input (5 V):**

| Span | d0.0 | d0.3 | d0.5 | d0.7 | d0.9 | d1.0 |
|-----:|-----:|-----:|-----:|-----:|-----:|-----:|
| 36 | 2.8 | 1.2 | 1.0 | 1.0 | 1.0 | 1.0 |
| 30 | 2.8 | 1.3 | 1.0 | 0.3\* | 1.0 | 1.0 |
| 24 | 2.8 | 1.5 | 1.1 | 1.0 | 0.3\* | 1.0 |

**Quiet input (1 V):**

| Span | d0.0 | d0.3 | d0.5 | d0.7 | d0.9 | d1.0 |
|-----:|-----:|-----:|-----:|-----:|-----:|-----:|
| 36 | 8.4 | 3.7 | 2.0 | 1.2 | 1.0 | 0.2\* |
| 30 | 8.4 | 4.2 | 2.6 | 1.6 | 1.1 | 1.0 |
| 24 | 8.4 | 4.9 | 3.3 | 2.2 | 1.5 | **1.3** |

### 5.3 Root cause — intrinsic to the knob/feature coupling

Three facts force the calm zone:

1. Resonance suppression is monotonic in drive (more drive → more core
   saturation → less resonance). This is the signature feature.
2. The top of the knob is, by definition, maximum drive for the chosen span.
3. For a normal hot input, even the *minimum* span's max gain saturates the
   core.

Therefore full knob = max drive = saturated core = no resonance, at **every
span** — see the 5 V rows all landing at 1.0 near the top. Drive Span only moves
*where the calming begins* (sp36 by \~d0.5, sp24 by \~d0.85); it cannot remove
the zone. That is why the span trim helped but could not cure it.

### 5.4 The real determinant — input level

The controlling quantity is *how hard the core is driven relative to the
resonance*, which depends on **input level**, not just the knob. Compare the 1 V
rows: resonance survives much further up, and at span 24 it never fully dies
(1.3 at max). This is the authentic Polivoks *"a loud signal rings less than a
quiet one"* behavior — the calm zone slides with the input level.

Levers that genuinely shrink the calm zone (all reduce core-drive depth):

- **Attenuate the input** before Onbetap — resonance survives higher up the knob.
- **Core headroom** menu slider — at 0.5 it preserves noticeably more resonance
  at the top for quieter inputs (measured: span 30, 1 V input, Drive 1.0 →
  ratio 1.3 at headroom 0.5 vs 1.0 at headroom 1.0). For hot inputs even
  headroom 0.5 still saturates at the top.

What *cannot* fix it: Drive Span. What *would* fully remove it: decoupling grit
from resonance (a separate drive-grit saturation stage) — but that also gives up
the "full drive = maximally tamed" endpoint, which is arguably the feature. That
option was considered and deferred (see §8).

---

## 6. Consolidated mental model

- The **core's rail clamp is a compressor.** It self-limits level and, at high
  Q, is already near its rails from the resonant peak alone.
- **Drive = input gain into that nonlinearity.** More Drive → more saturation →
  (a) more asymmetric-clip harmonics, (b) less resonance, (c) no extra level
  once clamped.
- **Grit and resonance-suppression are coupled** because both come from the same
  core saturation. You cannot, in the current design, add grit at the top
  without also removing resonance there.
- **Everything is level-dependent.** The same knob position behaves differently
  for a 1 V vs 5 V input. This is authentic and is the single most useful thing
  to understand when the knob "feels" inconsistent.
- The **output makeup is now a constant** and does not participate in any of
  this — it is just a fixed output buffer.

---

## 7. Current state / what changed

On branch `onbetap-drive-hw-path` (unmerged; left for the user to audition and
merge):

- `src/onbetap/drive.hpp` — new header; Drive→gain mapping extracted for
  testability; **makeup made constant** (Finding 1 fix).
- `src/Onbetap.cpp` — calls `driveGains`; local gain constants removed; **default
  Drive span 36 → 30 dB** (Finding 2 mitigation); header comment updated.
- `tests/onbetap/test_drive_level.cpp` — new host-free regression test: at
  res ∈ {0.0, 0.30, 0.70}, tone at cutoff, asserts level does **not** drop with
  Drive (`level@0.5 ≥ level@0 − 0.5 dB`, `level@1.0 ≥ level@0 − 1.0 dB`) and grit
  **rises** (`THD@0.5 ≥ THD@0 + 2 pp`). Verified **red** (9/9 fail) against the
  old drive-dependent makeup, **green** after. Full suite passes.
- Docs: `2026-07-18-onbetap-drive-hw-path-design.md` (design + Follow-up),
  worklog entry, `2026-07-15` spec §5 pointer, `Onbetap.md` Drive/Tuning bullets.
- VCV plugin rebuilt clean and installed (restart Rack to load).

**Not done:** MetaModule rebuild (shared `src/`, picks up automatically on next
MM build); GUI audition (user checklist).

---

## 8. Open questions / options going forward

1. **Leave as is.** Findings 1 & 2 are addressed; Finding 3 is the feature at its
   limit. Document the level-dependence (this report) and move on.
2. **Lower default Core headroom** (e.g. 1.0 → \~0.7) so the top stays more
   resonant for typical inputs — at the cost of less overall saturation/grit.
   Cheap, reversible, already a menu slider.
3. **Revisit the deferred grit-decoupling.** Add a Drive-dependent saturation
   path (fed from a gritty tap, e.g. BP/HP) so the top of the knob keeps
   dirtying even as resonance is choked. Restores "more = dirtier" everywhere;
   changes the character; needs its own brainstorm/spec. This is the only route
   that removes the calm-zone/grit-loss coupling.
4. **Input-level normalization / auto-gain** so the knob feels consistent across
   input levels — larger scope, arguably un-Polivoks (the level-dependence is
   part of the character).

Recommendation: (1) unless the calm-at-top is judged a real playability problem,
in which case (3) is the principled fix and (2) is the cheap partial one.

**Resolution (2026-07-18, later the same day):** option (3) implemented — a
Drive-following push into the existing output VCA; see
`docs/superpowers/specs/2026-07-18-onbetap-drive-grit-design.md`.

---

## 9. Reproduction notes

- Build/run committed tests: `cd ~/Dev/RobotBoy/tests && ./run.sh` (g++ only, no
  Rack SDK needed). The `onbetap/test_drive_level.cpp` output prints the level
  and THD numbers used in §3.5.
- Exploratory sweeps (§3.2, §4.2, §5.2) used ad-hoc harnesses compiled with
  `g++ -std=c++20 -O2 -Isrc -Isrc/mf20`, each including
  `onbetap/{OnbetapFilter,engine,drive}.hpp` and replicating `processSide`'s
  2×/LP path. They read gains from the real `onbetap::driveGains`, so they track
  any change to the shipped formula. Regenerate from the signal-chain description
  in §1.1 and the metric definitions in §2/§5.2.
- Key knobs for reproduction: cutoff 750 Hz, tone at cutoff, Hard limit, 2× OS.
  res 0.70 sits at the self-osc knee (expect chaos); use res 0.50/0.60 for clean
  trends.

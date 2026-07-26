# Retours crossfade sweep garble: variant measurements

**Date:** 2026-07-26. **Branch:** `retours-crossfade-chase`.

The measurement record behind Crossfade-mode splice alignment
(`AlignedFadeTarget` in `src/retours_delay/dsp/src/engine/echo_engine.cpp`).
Cited from `types.h`, `echo_engine.cpp`, `test_echo_engine.cpp`, and the
superseded spec `specs/2026-07-26-retours-crossfade-chase-design.md`, so it
lives here (tracked) rather than in the gitignored `.superpowers/` tree.

**Winner:** **V2 — correlation-aligned splices**: each fade's destination is
nudged onto the best cross-correlating offset within \~2 ms. V1 (adaptive fade
length) measured clearly *worse* and was dropped; V3 (V1+V2) is worse than V2
alone; the bounded ratio chase that preceded this work measured as a no-op on
sweeps and a responsiveness regression on steps, and was removed.

**Commits:** `81a7250` (mechanism, tests, spec banner), then the review
fix-round commit that added this file — see section 8, or
`git log --oneline -- docs/superpowers/2026-07-26-crossfade-variants-measurements.md`
(a document cannot cite its own hash, since recording it changes it).

---

## 1. First finding: the old chaos metric was measuring the wrong thing

Before any variant could be judged, the harness had to be fixed. The metric in
`crossfade-chase-report.md` — "steady-but-unvoiced" windows — came out
**identical for tape mode and crossfade mode, and identical across every engine
variant**, on exactly the fast-sweep scenarios it was supposed to indict.

Cause: the harness began measuring on block 0 with an empty 4 s recording
buffer. Every fast-sweep scenario reaches a \~1.9 s delay within 0.4 s, so its
first \~2 s of output reads frames that had never been written. That
cold-start read, not any splice behaviour, is what the chaos count was
counting. (The slow 2 s sweeps score 0 chaos for the same reason in reverse:
they only reach the long delay after enough history exists.) The earlier
report's "fast sweeps \~20× worse on a chaos metric" conclusion is an artifact
and should not be relied on.

Three harness changes (`scratchpad/chirp_harness_var.cpp`):

1. **Pre-roll.** 4.5 s of unmeasured processing at the starting knob position
   fills the whole buffer before the sweep starts. Chaos drops to 0 for every
   crossfade scenario, in every variant — the metric had no signal left in it.
2. **Energy gate** on the chaos count (a silent window is not a chaotic one).
3. **A direct garble metric — inharmonic residual.** The input is a pure
   220 Hz tone burst and a delay, however long and however spliced, cannot
   change its frequency. So *any* output energy at non-harmonically-related
   probe frequencies (137, 291, 353, 487, 619, 761, 907, 1103 Hz) was
   manufactured by the engine: phase-incoherent splices, cancellation notches,
   and chirpy sidebands. Reported as `10*log10(sum(probe power) / 220 Hz
   power)` in dB per 45.5 ms window over the sweep, as a mean and a p95 (worst
   5% of windows). **Lower is cleaner.** A held delay reads about -40 dB; a
   garbled splice reads above -10 dB.

The glide-event rate is retained for continuity but is noisy and non-monotonic
against audible quality — the inharmonic numbers are the ones that discriminate.

**Reading tape mode:** tape's numbers (`c`, `g`) are *not* a target. Its
continuous Doppler glide genuinely moves the tone off 220 Hz, so the probes
light up legitimately (-6 dB mean, +45 dB p95). Tape is context, not a bar.

---

## 2. Per-variant results

`inh` columns are dB (lower = cleaner). `settle` is the time from the knob
stopping until `DelayTimeSeconds()` enters and stays in a ±2% band, in fade
cycles (1 cycle = `kJumpCrossfadeFrames` = 21.3 ms at 48 kHz, decimation 1).
`hold_ms` is the delay actually landed on during the hold.

Variants: **base** = no chase/adaptive/alignment (identical to `main`);
**chase** = the bounded 0.75-octave ratio chase already on this branch
(238616d); **V1** = adaptive fade length (256 frames while retargets keep
arriving, 1024 when settled); **V2** = correlation-aligned splices, shipped
config; **V3** = V1+V2.

### Inharmonic residual, mean / p95 (dB)

| scenario | base | chase | V1 | **V2 (win)** | V3 |
|---|---|---|---|---|---|
| a  slow 2 s CCW, single tap, fb 0.3 | -25.1 / -4.1 | -25.1 / -4.1 | -10.2 / +36.9 | **-31.7 / -16.0** | -23.5 / +4.2 |
| d  slow 2 s CCW, single tap, fb 0.0 | -25.6 / -4.2 | -25.6 / -4.2 | -11.7 / +35.2 | **-30.6 / -6.0** | -26.5 / +3.5 |
| e  fast 0.4 s CCW, single tap, fb 0.3 | -21.7 / -9.9 | -21.7 / -9.9 | -11.9 / +25.3 | **-30.0 / -20.2** | -21.7 / -6.6 |
| j  fast 0.4 s CCW, single tap, fb 0.0 | -21.7 / -9.4 | -21.7 / -9.4 | -15.3 / +25.1 | **-28.5 / -17.2** | -23.8 / -4.2 |
| i  ultrafast 0.2 s CCW, fb 0.3 | -20.8 / -6.5 | -18.0 / -6.9 | +6.7 / +17.6 | **-24.4 / -8.8** | -7.9 / +8.4 |
| b  slow 2 s CW, multi-tap, fb 0.3 | -15.2 / +3.4 | -15.2 / +3.4 | -6.6 / +20.5 | **-16.1 / +1.2** | -13.2 / +8.5 |
| f  fast 0.4 s CW, multi-tap, fb 0.3 | -12.4 / +4.8 | -12.4 / +4.8 | -7.4 / +20.6 | -12.2 / +5.3 | -14.0 / +3.4 |
| h  instant step (0.05→0.45 knob) | -40.0 / -34.5 | -32.1 / -20.6 | -40.0 / -34.5 | **-40.0 / -34.5** | -40.0 / -34.5 |
| c  tape 2 s CCW *(context only)* | -6.1 / +45.6 | — | — | — | — |
| g  tape 0.4 s CCW *(context only)* | -0.8 / +41.1 | — | — | — | — |

### Responsiveness (fade cycles to settle after the knob stops) and landed delay

| scenario | base | chase | V1 | **V2 (win)** |
|---|---|---|---|---|
| a | 0.94 / 1866.06 ms | 0.94 / 1866.06 | 0.00 / 1866.06 | **0.94 / 1865.65** |
| d | 0.94 / 1866.06 | 0.94 / 1866.06 | 0.00 / 1866.06 | **0.94 / 1865.69** |
| e | 1.25 / 1866.06 | 1.25 / 1866.06 | 0.25 / 1866.06 | **1.25 / 1865.90** |
| j | 1.25 / 1866.06 | 1.25 / 1866.06 | 0.25 / 1866.06 | **1.25 / 1866.61** |
| i | 1.56 / 1866.06 | **5.31** / 1866.06 | 0.31 / 1866.06 | **1.56 / 1868.07** |
| b | 0.88 / 4.19 | 0.88 / 4.19 | 0.00 / 4.19 | **0.88 / 4.19** |
| f | 1.19 / 4.19 | 1.19 / 4.19 | 0.25 / 4.19 | **1.19 / 4.19** |
| h instant step | 0.94 / 1866.06 | **11.88** / 1866.06 | 0.94 / 1866.06 | **0.94 / 1866.06** |

V2's landed-delay error is at most **2.0 ms** (scenario `i`), and exactly zero
on the short-delay CW scenarios where the min-radius guard declines to align.

### Unit-scale confirmation of the mechanism

A single retarget, pure sine input, measuring the minimum RMS through the fade
relative to the steady level before it (`scratchpad/dipprobe.cpp` — the same
measurement is now a unit test):

| retarget | tone | no alignment | with alignment |
|---|---|---|---|
| 0.10 → 0.80 s | 466.164 Hz | **0.557** | **0.997** |
| 0.15 → 0.16 s | 277.183 Hz | **0.753** | **1.003** |
| 0.30 → 0.90 s | 138.591 Hz | 0.817 | 0.895 |
| 0.05 → 0.40 s | 277.183 Hz | 0.988 | 0.991 |
| 0.007 → 0.009 s | 277.183 Hz | 0.239 | 0.239 *(guard: not aligned)* |

The 0.10 → 0.80 s case is a 5 dB cancellation notch punched by an antiphase
splice, gone entirely once aligned. (Note: pick tone frequencies that do *not*
divide evenly into the delays. My first pass used 220 Hz with 0.05/0.40 s
delays — both exact whole numbers of periods — so both variants scored
identically and the probe looked useless.)

---

## 3. Why each loser lost

**V1, adaptive fade length: rejected, measurably worse.** Shortening fades to
256 frames while the knob moves does shrink each fade's content jump 4×, but it
also *quadruples the number of splices* and makes each amplitude-modulation
transient 4× faster, i.e. 4× wider in the spectrum. Both effects dominate: V1
is 10–27 dB worse on p95 than baseline on every crossfade sweep, and on the
ultrafast sweep it pushes inharmonic energy *above* the fundamental (+6.7 dB
mean). Its one genuine advantage — the delay tracks the knob 4× more finely,
settling in 0–0.25 fade cycles instead of \~1 — was never a requirement (\~1
cycle already satisfies the brief) and is not worth the trade. Combining it
with V2 (V3) drags V2 back down by 4–16 dB, confirming the cause is V1 itself.

**The existing bounded ratio chase: removed.** Two measurements condemn it:

1. **It does nothing at human sweep speeds.** Base and chase columns above are
   *bit-identical* for every 2 s and 0.4 s sweep, both feedback settings, both
   knob halves. The 0.75-octave-per-fade cap only bites when a full-travel
   twist takes under \~0.2 s, which is faster than a hand — hence the chase
   changes only scenario `i` (0.2 s) and the instant step.
2. **It breaks the responsiveness requirement where it does engage.** An
   instant retarget (a CV jump or clock change, not a hand sweep) takes
   **11.9 fade cycles ≈ 253 ms** to arrive instead of 0.94 cycles ≈ 20 ms, and
   it audibly *worsens* that step: -20.6 dB p95 versus -34.5 dB for a plain
   single fade. The chase turns a clean instant jump into a 12-stage glide.

`BoundedFadeTarget` and `kCrossfadeMaxStepOctaves` are therefore deleted, not
neutralised — leaving dead code that only fires on unreachable inputs and
regresses steps would be worse than removing it. The three ratio-cap unit tests
are replaced by six alignment tests (below), so the suite stays green and
non-vacuous. The spec
`docs/superpowers/specs/2026-07-26-retours-crossfade-chase-design.md` carries a
SUPERSEDED banner; its problem statement is still accurate and still the reason
for the work.

---

## 4. What shipped

`AlignedFadeTarget` in `src/retours_delay/dsp/src/engine/echo_engine.cpp`,
called from both fade-start sites (`SetTargets`' idle→fade branch and
`ReadWet`'s fade-complete dequeue). At each fade start it takes
`kAlignWindowTaps` samples of the OLD tap's recent buffer history, cross-
correlates them against the same window around the NEW tap for every candidate
lag in the search range, and moves the new tap to the best-matching lag. The
blend then sums two phase-coherent copies instead of two random-phase ones.

Constants, all named in `types.h` with the rationale in place:

| constant | value | role |
|---|---|---|
| `kAlignWindowFrames` | 512 | history compared (\~11 ms at 48 kHz) |
| `kAlignWindowStride` | 8 | window decimation → 64 taps, 3 kHz correlation band |
| `kAlignSearchFrames` | 96 | absolute search radius, \~2 ms — the inaudibility budget |
| `kAlignSearchStride` | 4 | coarse lag step |
| `kAlignRefineRadius` | 3 | whole-frame refine around the coarse winner |
| `kAlignSearchMaxFraction` | 0.05 | radius cap relative to `min(cur, want)` |
| `kAlignMoveFraction` | 0.5 | radius cap relative to the requested move |
| `kAlignMinRadiusFrames` | 16 | below this much slack, don't align at all |

Three design points worth knowing:

- **The move-relative cap is load-bearing, not a nicety.** For a requested move
  smaller than the search radius, the *best-correlating* offset is always the
  one that cancels the move outright — identical content correlates perfectly.
  Without `kAlignMoveFraction` small knob nudges would land nowhere and the
  knob would feel dead. Capping the nudge at half the move guarantees the delay
  always travels at least half the distance asked for.
- **Fade triggering moved off `target_frames_` onto a new `requested_frames_`.**
  Alignment deliberately leaves `target_frames_` a few frames from what was
  asked for; comparing the knob against it would restart a fade every block
  with a static knob.
- **Correlation is unnormalised.** The search spans \~2 ms, over which signal
  level is essentially flat, so there is no loud-region bias to divide out and
  no per-candidate divide to pay.

Also added: `RecordingBuffer::MonoSampleAt(ctx, frame)` — a public static
nearest-frame, non-interpolated read through the existing `ReadContext`. Purely
additive; no existing reader routes through it, so it cannot perturb any
rendered output. The aligner needs a few hundred raw frames per fade and must
not pay Hermite cost per sample.

### Reviewer hardening (applied, independent of variant choice)

Both fade-start sites now re-clamp to `[0, max_frames]` computed from the
**live** buffer:

- `SetTargets`: `fade_from_frames_ = clamp(target_frames_, 0, max_frames)` —
  `target_frames_` can be stale-oversized when a quality-mode size shrink lands
  mid-fade (the shrink queues rather than applies), and it becomes the next
  fade's start point.
- `ReadWet` dequeue: same clamp on both `cur` (the fade's start) and the aligned
  target, with `max_frames` recomputed from `size_f` at that instant.

No pinning hash moved from this clamp alone (it is a no-op whenever the state is
in range, which it is in every non-shrink scenario).

### CPU cost on Cortex-A7

Per fade: `kAlignWindowTaps × (coarse + refine candidates)` = 64 × (49 + 6) =
**3520 multiply-adds**, plus 64 reference loads. The inner loop is one load,
one MAC, one index decrement and one never-taken wrap conditional per tap, so
call it \~4 ops per MAC → \~14k ops per fade.

A fade lasts `kJumpCrossfadeFrames` **host samples**, not buffer frames:
`ReadWet` advances `fade_pos_` once per host sample, and the processor calls
`ReadWet` once per host sample regardless of the quality mode's decimation
factor. So the worst-case cadence is **46.9 fades/s at 48 kHz in every quality
mode** — not half that in the decimated modes, as an earlier draft of this
document and the `types.h` comment both claimed. (Corrected in the review
fix-round; verified by reading the `fade_pos_ += fade_step_` site in `ReadWet`
against its per-sample call site in `retours_processor.cpp`.)

That worst case is also not a transient. Any patch that modulates TIME — CV
into the jack, or the TIME attenurandomizer — changes the requested delay every
block, so `changed` is true on every `SetTargets` call, a fade starts every
`kJumpCrossfadeFrames` continuously, and **165k MAC/s is the steady state** in
those patches. The cost falls to zero only when the requested delay is
genuinely static; "only while the knob moves" understated it.

- **165k MAC/s** worst case (\~0.66 M ops/s), inside the "few hundred k"
  budget.
- Burst shape: 3520 MACs land inside one sample's `ReadWet`, once per 21 ms.
  At \~2–3 cycles per scalar MAC on an A7 that is \~9–11 µs, roughly 1% of a
  64-sample block period — bounded and infrequent.
- Measured on this desktop (`scratchpad/cpubench.cpp`, 120 s of continuous 2 s
  triangle sweep in crossfade mode, i.e. a retarget pending at every fade
  boundary): 0.1066% of real time with alignment versus 0.0993% without —
  **+7% of Retours' own crossfade-mode engine cost, worst case.**

Two measured cheaper configurations, if MetaModule CPU turns out tighter than
expected: `kAlignWindowStride = 16` (32 taps, half the MACs) costs \~3 dB of p95
on scenario `a` and is otherwise equal; coarsening `kAlignSearchStride` to 8
with `kAlignRefineRadius = 4` (33 candidates) costs \~3 dB on `a` and \~2 dB on
`e`.

---

## 5. Tests

`tests/retours_delay_dsp/test_echo_engine.cpp`: the three ratio-cap chase tests
are removed and six alignment tests added, sharing two new helpers
(`ResolvedDelaySeconds`, which reads the exact delay a density resolves to via
the first-target snap, and `RunSplice`, which drives a retarget with a sine
filling the buffer):

1. **a phase-cancelling retarget keeps its level** — the 0.10 → 0.80 s /
   466.164 Hz case; asserts the fade's min-RMS ratio > 0.9 (measured 0.557
   before, 0.997 after — a real regression test, not a tautology).
2. **landed delay stays inside the search budget** — within
   `kAlignSearchFrames + 1` frames of the request, and *not equal* to it
   (proving the aligner ran rather than passing through).
3. **no added lag, one fade reaches the target** — in budget after exactly one
   `kJumpCrossfadeFrames` fade, and then bit-exactly unchanged one fade later
   (no residual chase).
4. **silence in the buffer leaves the target exact** — the `best <= 0`
   pass-through path.
5. **very short delays are left exactly on target** — the
   `kAlignMinRadiusFrames` guard.
6. **a small retarget still travels most of the way** — the
   `kAlignMoveFraction` guarantee.

### Which pins moved

**One of five**, and legitimately: `pin_crossfade_retarget`
`0xb0f74744c671a30c` → `0x363c09549efdc497`. It is the only pin whose scenario
enters `kCrossfade` mode; the other four run in `kTape` (two set it explicitly,
the two frozen ones inherit `RetoursParameters`' default) and are bit-exact
unchanged — verified by running the suite before regenerating. Its retarget is
720 → 480 buffer frames on a noise-filled buffer, giving a radius of
`min(96, 0.05×480 = 24, 0.5×240 = 120) = 24` frames, above the min-radius
guard, so the fade now lands on the best-correlating offset within ±24 frames of
480 instead of exactly on 480. The regenerated value carries a comment with
this derivation and the previous hash.

### Suites

- `tests/retours_delay_dsp/run.sh`: **green**, 76 test cases / 4.87 M
  assertions, 0 failed.
- `tests/run.sh`: **exit 0** (mf20, loooop, particules, onbetap, vespid C++
  suites plus the python guard tests; this script does not include the CMake/
  CTest-based `retours_delay_dsp` lane, run separately above).
- `RACK_DIR=~/Dev/Rack-SDK make -C vcv -j8`: **exit 0**, no errors. MetaModule
  build not run (VCV listening first, per the earlier prototype's scope).

---

## 6. WAV renders

All 8 s, 48 kHz, stereo, 100% wet. Rendered into the session scratchpad
`/private/tmp/claude-501/-Users-gabrielroth-Dev-RobotBoy/c05ea864-b363-40ee-9e76-e53d5769602a/scratchpad/wav/`
— **temporary; regenerate with the harness if that directory is gone** (see
`scratchpad/chirp_harness_var.cpp` and `build_variant.sh` in the same place, or
rebuild the harness against this branch).
Scenario prefixes are as in the tables above (`a_`…`j_`, plus
`h_crossfade_instant_retarget`).

| suffix | build |
|---|---|
| `-baseline` | no chase, no alignment (== `main`) |
| `-v2-align` | **winner, shipped config** |
| `-v1` | bounded ratio chase (238616d) |
| `-v2` | V1 adaptive fade length |
| `-v3` / `-v4` | early untuned alignment / alignment+adaptive |
| `-tA`…`-tH`, `-K`, `-L`, `-M`, `-W` | alignment tuning grid |

Note the `-v0`…`-v4` suffixes predate the final naming and follow the harness's
internal variant IDs (0 = baseline, 1 = chase, 2 = adaptive, 3 = align,
4 = both); `-baseline` and `-v2-align` are the two to listen to first.

---

## 7. Concerns / open items

1. **Very short delays are not improved.** Below \~7 ms the inaudibility budget
   (5% of the delay) is smaller than one period of most content, so alignment
   is either skipped by the guard or too small to help — the 0.007 → 0.009 s dip
   probe reads 0.239 either way. The CW multi-tap scenarios (`b`, `f`) are
   correspondingly near-neutral: they are dominated by the golden-ratio tap-2
   comb, not by splice phase. If the short-delay end still sounds bad by ear,
   that is a *different* mechanism and needs its own investigation.
2. **The correlation window is decimated 8× with no anti-alias filter**, so
   content above \~3 kHz aliases into the score. The peak is still driven by
   dominant low/mid partials, and the measured result is as good as the
   undecimated 128-tap configuration, but this has only been tested on tones
   and LCG noise — not on real HF-rich material. Worth a listen with cymbals.
3. **`kAlignSearchMaxFraction = 0.05` permits up to 5% delay error** (≈0.85
   semitone of comb detune) at delays between \~7 ms and \~40 ms, where the
   relative cap governs. This is the interval that `pin_crossfade_retarget`
   lives in. It measured better than a tighter cap, but it is the one knob here
   with an audible failure mode if set wrong.
4. **`inh_hold` (the metric's own floor during a held knob) sits at -36 to
   -42 dB**, so the p95 numbers in the -16 to -20 dB range still have real
   headroom left. Alignment removed most of the garble, not all of it; a
   further \~15 dB is theoretically available. Whether any of it is audible is
   a listening question.
5. **Not measured: decimated quality modes.** Everything above runs
   `kBrightDigital` (decimation 1). At decimation 2 the frame-denominated
   constants are twice as long in real time — `kAlignSearchFrames` becomes
   \~4 ms of landing slack. That is probably still inaudible, but it is an
   untested assumption.
6. **The `main`-branch harness and its result tables are unreliable** for the
   chaos metric, per section 1. Any future comparison should use
   `chirp_harness_var.cpp`, not `chirp_harness.cpp` / `chirp_harness_chase.cpp`.

---

## 8. Review fix-round (2026-07-26)

Adversarial review of `81a7250` found no Critical issues and verified the
mechanism sound (10-mutant campaign; the dip numbers in section 2 independently
reproduced across all four quality modes). Two Important findings and four
minors, all addressed here.

### Important 1 — the `kAlignMoveFraction` test was vacuous

The "small retarget still travels most of the way" case operated at
1.000 → 1.003 s / 466.164 Hz, which is *outside* the region where the cap binds:
deleting the `kAlignMoveFraction` line left all 76 tests green. Reproduced and
measured (travelled/asked fraction, shipped vs a build with the cap line
deleted):

| move | tone | shipped | cap deleted |
|---|---|---|---|
| 0.5 ms | 180 Hz | 1.000 | **-0.042** |
| 1.0 ms | 180 Hz | **0.583** | **-0.000** |
| 2.0 ms | 180 Hz | 0.500 | **0.000** |
| 3.0 ms | 180 Hz | 1.472 | 1.667 |
| 3.0 ms | 466.164 Hz | 0.715 | 0.715 ← *old operating point: guards nothing* |

The dead-knob region is wider than the review stated — it covers moves up to
\~2 ms at both tones, not just at 180 Hz. The cap binds only where the uncapped
radius (96 frames = 2 ms at a 1 s delay) exceeds the move itself, so the test
now runs at **1.000 → 1.001 s / 180 Hz**. Mutation-verified both ways: green
with the cap, and the single expected failure at
`test_echo_engine.cpp:576` with the cap line deleted.

### Important 2 — dangling citations into a gitignored path

`.gitignore:32` ignores `.superpowers/`, so the five code and doc comments
citing `.superpowers/sdd/crossfade-variants-report.md` resolved to nothing for
anyone cloning the repo. This file is that report, relocated to a tracked path;
all five citations (`types.h:23`, `echo_engine.cpp:112`,
`test_echo_engine.cpp:388` and `:666`, and the superseded spec's banner) now
point here.

### Minors

- **Unnormalized-correlation justification cited the wrong quantity.** It
  argued from the \~2 ms search span rather than from candidate-window energy
  variation. Rewritten to cite the review's measurement: the unnormalized
  argmax differed from the normalized one in 0/400 stationary and 7/400
  percussive trials, worst-case correlation-coefficient loss 0.37, mean loss
  \~0, and the normalized argmax measured no better on the audio metrics. The
  real reason it holds is that the window (`kAlignWindowFrames`) is an order of
  magnitude wider than the span the candidates cover, so successive candidate
  windows overlap almost entirely and their energies barely differ.
- **`cur` is now clamped at the `SetTargets` fade-start site too**, symmetric
  with the `ReadWet` dequeue. `delay_frames_` is the slewed/latched value, which
  a tape-mode buffer shrink can leave larger than the new size for the whole
  slew decay; handing that to the aligner would have it correlate against a
  bogus wrapped position.
- **CPU claims corrected** (see section 7): the fade cadence is host-sample
  driven, so 165k MAC/s is the worst case in *every* quality mode, and it is the
  steady state — not a transient — in any patch modulating TIME.
- **Stale bound comment** at the crossfade read now enumerates every assignment
  that can reach `fade_from_frames_`/`target_frames_` and why each is clamped.

### New: re-clamp coverage, and three traps it walked into first

The two fade-start re-clamps had zero test coverage. A new case,
`corner stress: crossfade retargets across quality shrinks stay bounded` in
`tests/retours_delay_dsp/test_hardening.cpp`, runs quality churn in kCrossfade
mode with a large retarget queued every block.

Worth recording that **the first three versions of this test passed while
covering essentially nothing**, each found only by instrumenting
`AlignedFadeTarget` and the fade-start sites with counters rather than trusting
the test's name:

1. **Freeze churn every 4 blocks → 1 fade start in 5 s, 0 dequeues.**
   `ReadWet`'s frozen branch returns before the crossfade code, so `fade_pos_`
   only advances while unfrozen, and `NotifyFreeze`'s unfreeze edge resets it to
   0. A 4-block freeze period restarted every fade long before its 1024 samples
   could elapse. Fixed by pulsing freeze 64 blocks in every 512.
2. **Density swinging to the mapping extremes → aligner skipped every time.**
   The radius cap is `kAlignSearchMaxFraction * min(cur, want)`, so with either
   end at the \~2 ms minimum the radius collapses under
   `kAlignMinRadiusFrames`. Fixed by sweeping 0.30..0.50 (both ends long).
3. **Square-waving the knob → `want == cur` at 93% of dequeues.** A fade is
   exactly 16 blocks, so any alternation whose period divides 16 is sampled at
   the same phase by every dequeue; the move-relative cap then collapses to
   zero. Measured 221 of 237 calls bailing at the radius guard. Fixed with a
   continuous triangle sweep — which is also what the feature is actually for.

Final instrumented coverage: 237 dequeues, 237 alignment calls, 236 reaching a
decision (max radius 96, the cap), 12 actually moving a target. The counters are
recorded in the test's comment so a future edit can be re-checked against them.

The case asserts robustness rather than a tight numeric bound — during a quality
transition the live quality lags the requested one, so the exact live buffer size
is not observable from the public API at that instant, and the clamps are
defence-in-depth behind `WrapBounded`'s existing fallback. Its value is executing
these paths under churn, in all four quality modes including the decimated ones.

### Re-verification

- `tests/retours_delay_dsp/run.sh`: green, **77** test cases, 0 failed.
- `tests/run.sh`: exit 0.
- `RACK_DIR=~/Dev/Rack-SDK make -C vcv -j8`: exit 0, no errors.
- Whole suite under `-fsanitize=address,undefined`: **clean**, 77 cases /
  4,875,400 assertions, no sanitizer reports. This is the real evidence that the
  aligner's wrap arithmetic and the clamped paths stay in bounds across all four
  quality modes.
- The four non-crossfade pinning hashes are bit-exact unchanged;
  `pin_crossfade_retarget` keeps the value regenerated in `81a7250` (the
  fix-round touched no code on its path).

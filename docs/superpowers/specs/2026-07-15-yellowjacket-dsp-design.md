# Yellowjacket DSP design — EDP Wasp VCF emulation

Spec for the filter engine behind `src/Yellowjacket.cpp` (panel/shell already
built; **do not touch the panel or widget positions**). Companion research:
`2026-07-15-yellowjacket-research-notes.md` (all constants sourced there).

## Goal

A stereo, polyphonic, circuit-faithful model of the EDP Wasp state-variable
filter as characterized in Köper/Holters/Esqueda/Parker (DAFx 2022), with
two character modes:

- **Tame** — original EDP Wasp '78: +5 V rails, gentler OTA drive; the diode
  limiter holds the filter at the verge of oscillation (whistle/chirp, no
  runaway).
- **Screaming** — Doepfer A-124 with the documented 10k self-oscillation
  mod: +12 V rails, hotter OTA drive, boosted LP feedback → true
  self-oscillation, bounded by OTA rail clipping.

## Signal model (per channel, at the oversampled rate)

All voltages in real circuit volts, centered on the inverter operating
point (mid-supply). States: `bp`, `lp` (integrator outputs), `h1s` (H1
network state), `hin_s` (input coupling state), 4 output DC blockers.

```
hin  = HPF_22Hz( driveGain · v_in )                  // C1/level-pot coupling
yd   = H1(s) ∘ bp                                    // pole-zero damping network
fd   = yd + Rf·2·Is·sinh(yd / (η·VT))                // diode pair across R4
sum  = w_in·hin + fd + kR2·lp + kC2·S(bp)            // currents into IC1, ×R3
hp   = invSat( −sum )                                // CMOS inverter summing stage
bp'  = ωc · S(hp)     then rail-clamp bp             // OTA + integrator 1
lp'  = ωc · S(bp)     then rail-clamp lp             // OTA + integrator 2
```

- `S(v) = tanh(c·v)/c` — OTA input nonlinearity. `c = β·k_div/(2VT)`:
  **0.296 V⁻¹ Screaming** (k_div=0.01626), **0.155 V⁻¹ Tame**
  (k_div≈0.0085). β=0.9408, VT=25.85 mV.
- `invSat` — CMOS inverter saturator: soft asymmetric clamp with finite-gain
  compression. Shape fitted offline (Python) from the paper's Table 2 MOSFET
  model at +12 V; implemented as a cheap smooth closed form (tanh-based, knee
  params γH≈16, γL≈30 per Table 4). Same normalized shape scaled to the rail
  span in Tame.
- Rail clamp on states: same asymmetric soft clamp — headroom ±≈5.2 V
  (Screaming, 12 V supply), ±≈2.2 V (Tame, 5 V supply).
- `H1(s) = (b1·s + b0)/(a1·s + a0)` from the Δ-Y closed form (research
  notes; verified against paper Fig 3):
  `b1 = R3·Rb·C7, b0 = R3, a1 = C7·(Rb·(Za+Zc)+Za·Zc), a0 = Za+Zc`
  with Za/Zb/Zc from ρ (res knob + CV). Computed at modulate rate,
  discretized TPT (one state), coefficient-slewed.
- Diodes: Is=2.52 nA, η=1.752, Rf=R3=27k. Use sinh with a clamped argument
  (|yd·19.34| capped ~30) to avoid overflow; equivalently piecewise beyond.
- `kR2 = R3/R2eff` — **1.0 in Tame**, **27/7.297 ≈ 3.70 in Screaming**
  (10k mod), with knob calibration compensated (see λ below): ω0 =
  ωc·√(λ·kR2), so the code uses ωc_int = 2π·fc_knob/√(λ·kR2).
- `kC2 = R3·C2·ωc_int` — cutoff-proportional damping (C2=100 pF).
- `w_in = 1` (H_in passband gain at full level; drive handles the rest).

### Revision 1 — loop-gain factor λ and the self-oscillation mechanism

Two consequences of the summing-stage physics that the first draft missed
(surfaced by the reference-sim work):

1. **Loop-gain factor λ.** The inverter's finite open-loop gain A0 with the
   summing-node divider N gives every loop term an effective gain
   `λ = A0/(N + A0)` (linearized). All small-signal formulas must carry it:
   `D(s) = s² + λ·(H1 + kC2eff)·ωc·s + λ·kR2·ωc²`, resonant frequency
   `ω0 = ωc·√(λ·kR2)`, LP passband gain = w_in/kR2 (λ cancels at DC; the
   Screaming mod really does cost ≈11 dB of LP passband — hardware-true).

   **Final decision (this revision):** `makeup` is **Tame = 1.0** (unity —
   Tame's raw LP passband is already unity since kR2=1, so no makeup is
   needed there) and **Screaming = 2.0** as a musical compromise (raw
   Screaming LP is ≈−11.4 dB; ×2.0 makeup brings it to ≈−5.4 dB, not full
   parity with Tame — full parity would need per-output gains, which were
   deliberately rejected because they'd disturb the Mix output's shared
   notch structure). The residual gap (Tame LP 0 dB vs Screaming LP ≈−5.4 dB;
   Tame HP ≈−1.4 dB vs Screaming HP ≈+3.2 dB, so Screaming's HP is louder by
   ≈4.6 dB relative to Tame's) is left as-is in the per-mode constants and is
   instead user-addressable via the **Output level** context-menu control
   (±12 dB, linear post-filter gain applied to all outputs in both modes,
   persisted as `outputLevelDb`) — a rebalancing tool, not a physical
   correction.
2. **Self-oscillation needs the inverter's phase lag.** With purely real
   loop gain the damping term is always > 0 — the model can NEVER
   self-oscillate, contradicting hardware. The physical mechanism is the
   CMOS inverter's finite bandwidth: a one-pole lag at ωp converts part of
   the ω² feedback into negative damping. To first order this folds into
   the kC2 coefficient with **zero extra states**:

   `kC2eff = R3·C2·ωc_int − kR2·ωc_int/ωp`   (ωp = 2π·fPole)

   With fPole ≈ 80 kHz this reproduces the lore quantitatively: stock
   filter (kR2=1) sits at the verge of oscillation (whistle/chirp, decays);
   modded (kR2=3.7) crosses into sustained oscillation for mid/high
   cutoffs, amplitude bounded by the rails (±≈5 V, cf. DAFx Fig 9).
   `fPole` is exposed as a context-menu slider ("Inverter bandwidth",
   60–300 kHz, default 80 kHz) for tuning. The floor is 60 kHz because
   Task 1's corrected sweep found Tame free-runs (breaks its
   no-self-oscillation promise) below a threshold between 55 and 60 kHz.

   Tame mode (kR2=1) therefore also sits at the verge — matching the
   original's service-manual behavior — but its tighter rails and gentler
   OTA keep it tamer still.

### Discretization & solver

TPT/trapezoidal. Prewarp `g = tan(π·fc_int/fs_int)` per voice at modulate
rate, slewed per-sample (MF-20 pattern). Per sample this is a **scalar
implicit equation in `hp`** (bp, lp, yd, fd all chain off it):

1. **Fixed-pivot (mystran) pass** — replace `tanh(x)/x` terms with pivot
   gains at last sample's arguments; the loop becomes linear in `hp`;
   solve closed form. This is the base solver (always).
2. **Newton refinement (optional, "High accuracy")** — 1–2 Newton
   iterations on the scalar residual with analytic derivative, warm-started
   from the pivot solution, step-clamped; fall back to pivot answer if a
   step misbehaves. Never NaN, never loop.

State update `s = 2·mid − s`, then apply the rail clamp to `bp`/`lp`
states (soft, asymmetric). tanhXdX uses a rational approximation with the
series limit at 0 (crib the Fundamental VCF form, reimplemented).

### Oversampling

Adaptive on host rate: **4× ≤48 kHz, 2× ≤96 kHz, 1× above** (VCV).
Polyphase-allpass-style halfband up/down (small fixed-coefficient IIR,
self-contained header — no external dep). Context menu override:
Auto / 1× / 2× / 4×. MetaModule (48 kHz): default decided by CPU
measurement in the simulator; the `screaming`-style saved setting applies
per-module.

### Outputs

- HP = `hp` node, BP = `bp`, LP = `lp`, Mix = `(1−m)·LP + m·HP`
  (equal-weight crossfade → −6 dB notch at center, like the A-124's
  LP/HP Mix pot). m from Blend knob + Blend CV (0..1, clamped).
- Each output through a one-pole DC blocker (~8 Hz) — the hardware is
  AC-coupled and the asymmetric nonlinearities generate DC.
- Per-mode makeup gain: circuit volts map 1:1 to VCV volts in Screaming's
  raw LP (before makeup); Tame's `makeup = 1.0` (unity) since its raw LP is
  already unity (kR2=1); Screaming's `makeup = 2.0` partially compensates
  its ≈11 dB raw LP loss from the kR2=3.70 mod (see Revision 1 above). A
  separate user-facing **Output level** context-menu control (±12 dB,
  applied post-filter to all outputs, both modes) is the tool for closing
  any remaining inter-mode loudness gap or general level trim — it does not
  touch the per-mode makeup constants or the Mix output's notch structure.

## Parameter mapping

| Panel control | Mapping |
|---|---|
| Freq knob | log2 20 Hz–20 kHz (existing configParam), default 750 Hz |
| Freq CV + atten | 1 V/oct added to knob log2 value; final fc clamped [1 Hz, 0.45·fs_int] |
| Res knob | ρ = 0..1 directly into the H1 network math |
| Res CV + atten | ρ += CV/10·atten, clamped 0..1 |
| Drive knob | input gain = 2^(3·drive): 1×..8× (default 0 → unity) |
| Drive CV + atten | adds to drive 0..1 pre-mapping, clamped |
| Blend knob + CV | m = knob + CV/10·atten, clamped 0..1 |
| Character menu | Tame / Screaming (already persisted as `screaming`) |

Stereo: R input normalled to L (mirror L's outputs without recomputing when
R unpatched — MF-20 pattern). Polyphonic: EnginePool of 16 voices by value;
channels = max(L,R) channels.

## Context menu additions (module menu, after Character)

- **Accuracy**: Standard (pivot) / High (pivot + 2 Newton). Default High.
- **Oversampling**: Auto / 1× / 2× / 4×. Default Auto.
- **Input trim**: slider ±12 dB (tuning aid; default 0 dB).
- Existing: Character (Tame/Screaming), Panel theme. All persisted to JSON.

## File plan

- `src/yellowjacket/WaspFilter.hpp` — header-only core: one `WaspFilter`
  (per audio channel), no Rack includes. Holds constants for both modes as
  static config structs (`WaspMode::Tame` / `::Screaming`).
- `src/yellowjacket/wasp_dsp_utils.hpp` — tanhXdX, softclip shapes,
  halfband resampler, DC blocker, H1 coefficient math (shared with tests).
- `src/yellowjacket/engine.hpp` — VoiceEngine (L+R WaspFilter, smoothers,
  sanitize) + EnginePool, mirroring `src/mf20/engine.hpp`.
- `src/Yellowjacket.cpp` — module wiring: modulate()-rate control math,
  per-sample process, menu items. Keep the existing enums/positions
  untouched.
- `tests/yellowjacket/test_wasp_filter.cpp` (+ more) — plain-g++ tests.
- `tests/yellowjacket_ref/` — Python reference/design scripts (h1_check.py
  already there; add golden-behavior generator).

## Verification plan

1. **Python reference sim first** (`tests/yellowjacket_ref/wasp_ref.py`):
   implement the same equations in numpy at 8× oversampling with brute
   Newton; use it to (a) validate small-signal response against the linear
   transfer functions, (b) tune invSat/rail constants, (c) print golden
   behavioral numbers (peak gain vs ρ, self-osc amplitude & frequency in
   Screaming, no-self-osc in Tame, THD at drive extremes).
2. **C++ unit tests** (tests/run.sh lane): small-signal frequency response
   within tolerance of linear theory at ρ=0.3 across cutoffs; resonance
   peak monotone in ρ; Screaming self-oscillates at ρ=1 (bounded amplitude,
   correct fc ±10%); Tame does NOT free-run after input removed; all
   states finite under 10 V square input at fc extremes; DC blocked at
   outputs; NaN sanitize recovery.
3. **VCV build** (`make -C vcv`) + install; **MetaModule build** +
   **headless simulator** run with a test patch (test-vcv-module-headless /
   build-simulator skills): sine sweep through the filter, compare VCV vs
   MM outputs, check CPU load for oversampling default.
4. User-run GUI checklist at the end (per repo convention — no agent-driven
   GUI tests).

## Risks / open questions

- invSat closed-form fit quality vs the MOSFET model → mitigated by the
  offline fit script + audio comparison; keep fit params in one struct.
- Fixed-pivot accuracy at Screaming ρ=1 → Newton mode exists; compare in
  Python ref.
- MetaModule CPU at 4× → measure in simulator; Auto policy may differ per
  platform (compile-time default for MM).
- Tame mode's resonance network uses A-124 values (not EDP's 100k/47k
  set) — acceptable first cut; revisit if character is off.

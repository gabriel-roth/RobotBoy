# Yellowjacket research notes — EDP Wasp VCF

Working notes for the Yellowjacket DSP (stereo Wasp filter emulation).
Primary source: Köper, Holters, Esqueda, Parker, "A Virtual Analog Model of
the EDP Wasp VCF," DAFx20in22 (2022). PDF digested 2026-07-15.
<https://dafx2020.mdw.ac.at/proceedings/papers/DAFx20in22_paper_34.pdf>

## Circuit summary (from the paper)

Second-order state-variable filter, all stages powered from a **unipolar
supply** (+5 V in the original EDP Wasp; +12 V in the Doepfer A-124 version
the paper models). Three unusual choices define the sound:

1. **CMOS inverters (CD4069UBE) instead of op-amps.** IC1 is the summing
   stage; IC3 and IC5 are the amplifier halves of the two integrators.
   An unbiased CMOS inverter acts like an inverting op-amp with the + input
   tied to the (mid-supply) reference — but it saturates softly and
   asymmetrically toward the rails.
2. **CA3080 OTAs (IC2, IC4) as the cutoff control elements** feeding the
   integrator caps (C3, C4 = 330 pF). iOTA = α·i_bias·tanh(β·v/(2VT)),
   fitted α = 0.8635, β = 0.9408 (paper Table 3, measured CA3080).
   **Crucially, the OTA output current also clips when the output voltage
   nears the supply rails** — measured "rail plateau" behavior, paper
   eqs. (36)–(41), Table 4. This rail clipping bounds the states at high
   resonance and is what makes screaming resonance sound right; the plain
   tanh model diverges (states hit ±50 V, square-ish output, wrong cutoff
   shift — paper Figs. 9/11).
3. **Diode limiter (2× 1N4148, anti-parallel) in the resonance feedback
   path**, plus a resistive network (R13/R14/R15/R_res pot) that makes the
   damping frequency-dependent. Shockley fit: Is = 2.52 nA, η = 1.752.

### Component values (paper Table 1, Doepfer-flavored)

| Element | Value |
|---|---|
| R1,R2,R3,R4,R7,R8,R11,R12 | 27 kΩ |
| R5,R9 | 47 kΩ |
| R6,R10,R14 | 1 kΩ |
| R13 | **1 MΩ (Doepfer) / 100 kΩ (original EDP)** |
| R15 | 100 kΩ |
| C1,C7 | 0.22 µF |
| C2 | 100 pF |
| C3,C4 | 330 pF (integrator caps) |
| C5,C6 | 10 µF |
| R_level, R_res | 50 kΩ linear pots |
| D1,D2 | 1N4148 |
| IC1,IC3,IC5 | CD4069 inverters |
| IC2,IC4 | CA3080 OTAs |

Supply: paper/Doepfer +12 V unipolar; original EDP +5 V. The two changes
(R13 100k→1M, 5V→12V) "accentuate the characteristic erratic and dirty
sounding behavior."

### Linear analysis (paper §2.2)

- OTA input attenuator: V+ − V− ≈ −16.26e-3 · v_IC1 (voltage divider
  R7‖R8 + R5 + R6 → R6).
- Cutoff: ω_c = i_bias/(2·C3·VT) · R6/(R8‖R7 + R6 + R5) ≈ **9.855e8 ·
  i_bias** (rad/s per amp).
- Input path H_in(s): first-order highpass (AC coupling C1 + level pot),
  cutoff 18.3–26.8 Hz depending on level setting, passband gain ≤ 1.
- Transfer functions (of the core, after H_in):
  H_HP = −s²/D, H_BP = −ω_c·s/D, H_LP = −ω_c²/D with
  **D(s) = s² + (H1(s) + R2·C2·ω_c)·ω_c·s + ω_c²**.
- H1(s) = R3/Z1(s) = (b1·s + b0)/(a1·s + a0), a first-order pole-zero
  section set by the resonance pot ρ ∈ [0,1] via a Δ-Y transform of
  {ρR_res, (1−ρ)R_res, R13, R14, R15, C7, R4} — paper eqs. (21)–(29).
  At ρ=0 H1 ≈ flat high damping; at ρ=1 H1 magnitude drops (≈ −40 dB by
  10 kHz slope in Fig. 3) → resonance rises. The R2·C2·ω_c term adds
  damping proportional to cutoff (C2 = 100 pF across the summing stage) —
  this is a *cutoff-dependent* extra damping that limits Q at high cutoff.
- Effective Q ≈ 1/(Re{H1(jω_c)} + R2·C2·ω_c) (paper eq. 32). The SVF
  approximation is good except **low cutoff + high resonance**, where the
  exact model peaks harder and slightly higher in frequency.

### Nonlinear models (paper §3)

- **CMOS inverter**: NMOS+PMOS pair, each iD piecewise (off/linear/sat),
  with α(vGS) and vT(vGS) fitted quadratics (Table 2; from the "Red Llama"
  DAFx-2020 paper by Esqueda/Kuznetsov/Parker). DC transfer measured at
  +12 V: output swings ~12→0 V, transition centered near vin ≈ 5.5–6 V,
  slope (gain) finite (≈ −16 dB? see Fig. 5 — steep but not vertical),
  saturating smoothly to the rails.
- **OTA**: iOTA = α·ibias·tanh(β·vOTA/(2VT)) away from rails; near rails,
  output current jumps to plateau levels L(vOTA, ibias) (low rail) and
  H(vOTA, ibias) (high rail), fitted as bilinear surfaces (Table 4), with
  tanh transitions of steepness γL = 30.14, γH = 16.0255 centered at
  ΔvL = 0.6093 V and ΔvH = 10.8105 V (i.e. within ~0.6–1.2 V of each rail
  at 12 V supply). Total: iOTA = fM + fL + fH (eqs. 38–41).
- **Diodes**: Shockley, Is = 2.52 nA, η = 1.752.

### Behavioral takeaways (paper §4)

- Resonance ↑ → strongly asymmetric nonlinear behavior (sweep up ≠ sweep
  down), because diodes + R4 feedback engage.
- At res = 100%, bias 7.42 µA: states clip near the supply rails (±≈6 V
  swing about the mid-supply operating point). Simple tanh-only OTA model
  fails badly here; rail model matches measured envelope.
- The filter (Doepfer version) **does self-oscillate** at high resonance
  (screaming); the diode limiter shapes/limits it.

## Implications for Yellowjacket DSP

- Structure: nonlinear SVF in TPT/trapezoidal form like MF-20
  (src/mf20/MF20Filter.hpp) but with Wasp-specific nonlinearities:
  1. input AC-coupling HP (~20 Hz) with drive gain into the core;
  2. summing stage = CMOS inverter saturator (soft, asymmetric, bounded
     by virtual rails);
  3. integrators = tanh OTA nonlinearity + state/rail clipping (soft clamp
     of integrator states at rail-equivalent levels);
  4. resonance feedback through first-order H1 pole-zero section + diode
     pair soft limiter;
  5. cutoff-proportional damping term R2·C2·ω_c.
- Character switch mapping (already in scaffold): **Tame = original EDP**
  (R13 = 100 kΩ, +5 V rails → tighter headroom, diode limiter dominates,
  little/no self-oscillation) vs **Screaming = Doepfer** (R13 = 1 MΩ,
  +12 V rails → self-oscillates, wilder). This is physically grounded, not
  an arbitrary limiter toggle.
- The unipolar operating point matters mainly as asymmetry: model states
  as deviations from mid-supply, with asymmetric headroom constants.
- Real-time: paper used offline ACME state-space; we need a TPT core with
  fixed-point/Newton iterations, or MF-20-style piecewise closed form where
  possible; oversample 2–4x.

## Numerical verification of the linear model (2026-07-15, h1_check.py)

Computed Z1 via Δ-Y-Δ directly (complex arithmetic) and cross-checked a
first-order fit. Results match paper Fig. 3 (|H1| ≈ −2.6 dB flat at ρ=0.1;
falling to −36…−57 dB at high frequency for ρ=1).

Closed form for the code (multiply Z1 num/den by sC7; triangle sides
s1 = ρ·Rres + R14, s2 = (1−ρ)·Rres, s3 = R13 + R15, tot = s1+s2+s3):

    Ra = s1·s3/tot   Rb = s2·s3/tot   Rc = s1·s2/tot
    Za = Ra          Zb = Rb + 1/(sC7)   Zc = Rc + R4
    H1(s) = (R3·Rb·C7·s + R3) / (C7·(Rb·(Za+Zc) + Za·Zc)·s + (Za+Zc))

i.e. b1 = R3·Rb·C7, b0 = R3, a1 = C7·(Rb·(Za+Zc)+Za·Zc), a0 = Za+Zc —
all real, cheap, computable at modulate rate from ρ.

Q(ρ, ωc) = 1/(Re{H1(jωc)} + R2·C2·ωc):

| ρ | Q@100Hz | Q@400Hz | Q@1kHz | Q@5kHz | Q@15kHz |
|---|---|---|---|---|---|
| 0.0 | 1.06 | 1.05 | 1.04 | 0.97 | 0.83 |
| 0.5 | 2.93 | 2.94 | 2.86 | 2.39 | 1.70 |
| 0.9 | 8.7 | 10.8 | 9.9 | 5.9 | 2.95 |
| 1.0 | 18.4 | 94.3 | 56.9 | 11.8 | 3.9 |

- Resonance is strongest in the midrange (400 Hz–1 kHz); tamed at high
  cutoff by R2C2ωc and at low cutoff by H1's pole (14.9→41.6 Hz as ρ 0→1).
- **R13 = 100k vs 1M changes linear Q almost not at all** — the
  Tame/Screaming (EDP vs Doepfer) difference must come from supply
  headroom (+5 V vs +12 V: rail clip levels ~4x tighter) and nonlinear
  interaction, not the linear network.
- H1's zero: 15 Hz (ρ=0) → ∞ (ρ=1); H1(∞) → 0 at ρ=1 (pure integrator-ish
  lag → damping vanishes at audio rate, hence self-osc).

## Draft nonlinear model (pre-spec sketch, 2026-07-15)

Continuous-time, voltages centered on the inverter operating point:

    HP  = invSat( −(hin + f_d(yd) + LP + R2C2·ωc·S(BP)) )   [IC1 summing]
    BP' = ωc · S(HP)          [OTA IC2 + C3 + inverter IC3]
    LP' = ωc · S(BP)          [OTA IC4 + C4 + inverter IC5]
    yd  = H1(s) ∘ BP          [resonance network, first-order pole-zero]
    hin = HPF_20Hz(driveGain · v_in)   [C1/R_level input coupling]

- S(v) = (1/c)·tanh(c·v), c = β·k_div/(2VT) ≈ 0.306 V⁻¹ (OTA input
  divider k_div = 16.26e-3, β = 0.9408, VT = 25.85 mV). Saturates when
  node swings exceed ~3–5 V — interacts with rail clip at 12 V supply,
  dominates at 5 V.
- invSat = CMOS inverter output saturator: soft asymmetric clamp at the
  rail headroom (±≈5 V @ 12 V supply; ±≈2.2 V @ 5 V), soft top knee
  (γH ≈ 16), harder bottom knee (γL ≈ 30) per paper Table 4. The same
  clamp bounds BP and LP (IC3/IC5 are inverters too + OTA rail plateau).
- f_d(yd) = yd + R3·I_s·sgn(yd)·(exp(|yd|/(η·VT)) − 1) — diode pair
  (D1,D2 ∥ R4 leg). Voltage across the R4 leg equals yd exactly since
  R4 = R3 = 27k and the far side is IC1's virtual ground. Knee ~0.4 V.
  Engages in overdriven mid-resonance regimes; at ρ→1 the rails do the
  limiting instead (yd is tiny because |H1| collapses).
- Rail clamp is what bounds self-oscillation (paper Figs 9/11): plain
  tanh-only model diverges to ±50 V and sounds/behaves wrong.

Discretization: TPT/trapezoidal. Per sample it reduces to a SCALAR
implicit equation in HP:

    BP = sBP + g·S(HP);  LP = sLP + g·S(BP);  yd = cb·BP + c_state
    HP = invSat(−(hin + f_d(yd) + LP + kc2·S(BP)))

Fixed-point iterate 2–4× from last sample's HP (all maps monotone,
bounded slope; cheap tanh approx), then TPT state update + rail clamp,
H1 state update, output DC blockers (outputs are AC-coupled via C5/C6
in hardware; asymmetric clipping creates DC offsets we must block).

Character switch: Screaming = +12 V rails (Doepfer A-124); Tame = +5 V
rails (original EDP Wasp) — headroom ~2.4× tighter, OTA tanh relatively
more dominant, resonance amplitude limited earlier. (R13 barely matters
linearly — keep 1M for both, or make it part of the mode if research
says otherwise.)

Open per-sample cost estimate: ~3 tanh-approx evals × ~3 iterations × 2×
oversampling per channel — fine for VCV, plausible for MetaModule; keep
a 1×-oversampling escape hatch.

## Research agent findings (2026-07-15)

### Existing emulations
- **No usable open-source nonlinear Wasp exists.** Only
  Griffinboy24/Griffin_Wasp_VCF (HISE node, no license) — linear-only
  biquads from the paper's small-signal math with a fudge factor. Read for
  the Δ-Y math transcription, nothing else.
- The paper's framework is ACME.jl (MIT, github.com/HSU-ANT/ACME.jl); no
  Wasp netlist shipped. Red Llama companion code (Julia) with the CMOS
  model: https://lkoeper.gitlab.io/dafx-2020-cmos-llama/
- **VCV sells an official Doepfer A-124 model by Andrew Simper (Cytomic)**,
  closed source, polyphonic, with a "High detail mode" (cheap vs expensive
  solver, user-selectable) — pattern worth copying via context menu.
  Cytomic's The Drop has a "WSP" model deliberately altered to self-osc
  across the full range.
- **Cherry Audio's 2025 EDP Wasp synth emulation is also named
  "Yellowjacket"** (synthanatomy.com 2025-04). Name collision to be aware
  of; theirs is a full synth, ours a Eurorack-style filter module.
- Structural C++ templates: VCVRack/Fundamental `src/VCF.cpp` (mystran
  fixed-pivot tanhXdX, inline polyphase halfband 2x, ~78 dB);
  surge-synthesizer/sst-filters `include/sst/filters/TriPoleFilter.h`
  (OTA filter, fixed 3 Newton iterations, analytic Jacobian, 2x);
  jatinchowdhury18/ChowDSP-VCV Werner GenSVF (state-space + per-state tanh).

### Real-time method recommendations (agent report, corroborates draft)
- **Solver: mystran fixed-pivot linearization as the base** — replace each
  tanh with gain tanh(x̂)/x̂ at pivot x̂ = previous sample's argument, solve
  the ZDF loop linearly in closed form. Optionally re-solve with updated
  pivots ≈ quasi-Newton step. Then optional true Newton (1–3 iterations,
  cap 4–8, warm start) as a "High accuracy" mode. **Plain Picard/fixed-point
  iteration is NOT safe at self-oscillation (loop gain ≥ 1)** — revise the
  earlier sketch accordingly.
- Keep everything C¹-smooth: fit the CMOS inverter to a smooth asymmetric
  sigmoid instead of piecewise MOSFET regions; use the paper's tanh rail
  blends verbatim (γH=16.03, γL=30.14) — Newton-friendly, less aliasing.
- Diode pair: i = 2·Is·sinh(v/(ηVT)); prefer sinh/asinh form, avoid raw exp.
- tanhXdX must have a series/rational form near 0 (limit 1).
- Step limiting: clamp Newton steps; on failure fall back to fixed-pivot
  answer — never NaN, never loop.
- **Oversampling adaptive on host rate: 4x at 44.1/48k, 2x at 88.2/96k,
  1x at ≥176.4k.** Polyphase allpass IIR halfband (hiir-style or
  Fundamental's inline one) beats windowed-sinc FIR here. Recompute g and
  H1/Hin coefficients at the internal rate.
- ADAA inside the feedback loop is poison (half-sample delay); smooth
  nonlinearities + oversampling is the answer. ADAA only worth it for
  output-path shapers, which we don't have.
- Reference iteration counts: Diva 1–2 typical/15 worst; sst TriPole fixed
  3; Fundamental 0 (pivot only).

## Circuit-lore agent findings + schematic reads (2026-07-15)

Primary sources saved in scratchpad: EDP Wasp service manual
(polynominal.com), Haible clone schematic (original values), Sandelinos
A-124 KiCad trace (rev V4 2025), Gnat schematics (Stinchcombe).

### Topology settled (A-124 trace + Haible, read directly)
- Audio in → Lev pot (A50k log) → 27k → 220n → 4069 summing inverter
  (IC1A). Output = **HP node**.
- Global feedback **LP → 27k ∥ 100pF → summing junction** (this is the
  PCB part Doepfer calls R13 — NOT the paper's R13!). The famous
  PatchPierre self-osc mod parallels 10k across this 27k → boosts the ω²
  feedback ~3.7×, Q ×≈1.9 → self-oscillation. A real, documented mod we
  can use for Screaming mode.
- HP → 47k → CA3080 (divider 27k/27k+1k) → 4069 + 330p integrator → BP.
  BP → 47k → CA3080 → 4069 + 330p → LP. Expo converter (BC557 pair) feeds
  both bias pins through 1k — A-124 tracks ~1V/oct ("not very precise").
- Resonance network: BP → 1k (R14, BP Out also tapped here via 10µ) →
  50k pot triangle w/ 100k (R15), 1M (paper R13), 220n (C7) → **27k (R4)
  into the summing junction, with the antiparallel 1N4148 pair ACROSS
  that 27k**. Diodes see v_X = yd (since R4 = R3); conducting shunts R4 →
  damping jumps from ~0 to H1≈0.55. This is the overload limiter.
  f_d(yd) = yd + R3·2·Is·sinh(yd/(η·VT)).
- Original EDP (Haible/Gnat): same shape, values 33k feedback, 33k in,
  100k OTA feeds, 33k/33k+1k dividers (k_div=0.0085 → OTAs "on the safe
  side", Haible), 1nF integrators, 68pF across 33k global feedback,
  resonance: 100k series / 100k shunt / 47k pot + 220n / 33k return,
  diodes same role. +5V rails, mid-rail 2.5V.

### Behavioral facts
- **Original: no stable self-oscillation** — diode limiter holds it at
  the "verge": hollow whistle, chirps on envelope transients (service
  manual test steps 46/53/70; SOS: "overload limiter prevents
  oscillation"). Factory tuning gets a "near-perfect sinewave" ringing.
- **A-124 stock: also no self-osc** (manual p.1), but DAFx Fig 9 shows
  blooming ±6V nonlinear resonance at 100%. With the 10k mod: self-osc.
- Original cutoff range ~3 Hz–16 kHz (SOS). Original has NO expo
  converter — multiplicative keyboard tracking, nonlinear law. We use
  clean 1V/oct instead (A-124-style, standard for Eurorack).
- LP idles at ~+3 V DC in original; bias wander with cutoff sweeps is
  normal (thumps on fast modulation — a modelable "erratic" charm, via
  the asymmetric operating point).
- CMOS inverters are the dominant distortion (Haible), audible well
  before clipping → model finite-gain softness, not just hard rails.
- Unbuffered CD4069UBE mandatory in hardware; brand-to-brand variation
  audible. (Justifies a "character trim" slider if we want one.)

### Character switch decision (updated)
- **Tame = original EDP '78**: +5 V rails (≈±2.2 V headroom around
  2.5 V), k_div 0.0085 (OTA gentler), makeup gain at output; diode
  limiter dominates → verge-of-oscillation whistle only.
- **Screaming = A-124 + 10k self-osc mod**: +12 V rails, k_div 0.0163,
  R2_eff = 27k∥10k (ω0 = 1.92·ωc compensated in knob calibration) →
  true self-oscillation, rails+tanh bound the amplitude.
- Both share the A-124/DAFx H1 network (verified); EDP's slightly
  different resonance-network values are close enough in shape — revisit
  only if Tame sounds wrong.

## Repo conventions to follow (MF-20 precedent)

- Header-only core `src/yellowjacket/WaspFilter.hpp` (pure DSP, no Rack),
  normalized ±1 domain with processVCV wrapper (kVCVScale 0.2).
- Modulate-rate control math (tan/exp2 every ~2.5 ms), audio-rate slew of
  g; OnePoleSmoother from src/mf20/dsp_utils.hpp.
- EnginePool by value, 16 voices, NaN sanitize per modulate block.
- Tests: tests/yellowjacket/test_*.cpp, plain g++ harness via tests/run.sh.

## Open questions (for further research agents)

- Original EDP cutoff knob range and CV/tracking law (exp converter?).
- A-124 spec: freq range, input level, self-osc claims (Doepfer docs).
- Existing open-source implementations to compare against.
- Best realtime solver for tanh-integrator SVF (Zavalishin, Simper,
  Mystran) and oversampling practice.

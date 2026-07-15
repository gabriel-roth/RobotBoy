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

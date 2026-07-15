# Polivoks VCF — web & academic literature notes

Research notes for a virtual-analog model of the Formanta Polivoks filter
(designer: Vladimir Kuzmin, 1982). Compiled 2026-07-15 from web sources; all
claims are cited. Reference-style link definitions are collected at the bottom.

---

## 1. Executive summary

1. **No Polivoks-specific academic paper exists** (as of this search). I
   searched the DAFx paper archive (dafx.de, full-text and title search),
   Google Scholar, Semantic Scholar, Crossref, Zenodo, and aaltodoc-adjacent
   queries for "Polivoks"/"Polyvox" + filter/VCF/virtual analog. The only
   scholarly hits are musicological (Oxford Music Online entry, production
   theses). The belief that "a DAFx paper on the Polivoks exists" appears to
   be a conflation with the closely analogous **DAFx20in22 paper on the EDP
   Wasp VCF** ([Köper, Holters, Esqueda & Parker 2022][wasp-pdf]), which
   models the *same class of circuit*: a 2-pole SVF whose integrators are
   cheap misused amplifiers with current-controlled bandwidth and hard rail
   clipping. That paper is digested in detail in §2 — it is the best academic
   anchor for a Polivoks model, down to reusable equations.
2. The Polivoks circuit itself is exhaustively documented by
   hobbyist/engineer sources: the original Russian schematic and a modernized
   clone by [Marc Bareille][bareille] (schematic [PDF][bareille-pdf], original
   [GIF][bareille-gif]), the [electro-music DIY thread][em-thread], two
   ModWiggler threads with the definitive "how it works" explanations
   ([clone-kit thread][mw-clone], [Harvestman VCF thread][mw-questions]), the
   [Mutable Instruments Shruthi Polivoks board][shruthi], and the
   [Harvestman/IME Model 1982 manual][harvestman-manual] (made "with the
   blessing of" Kuzmin, royalties paid).
3. The core mechanism: **the filter has no capacitors** — each К140УД12
   (K140UD12 / КР140УД1208; a Soviet copy of the µA776, substitutable by
   LM4250) programmable op-amp *is* the integrator. Its internal \~30 pF
   Miller compensation capacitor is the integrating cap; the op-amp runs
   essentially open-loop, so its open-loop response A(s) ≈ ω_t/s integrates,
   and ω_t (gain-bandwidth) is proportional to the current pulled from the
   Iset pin. CV → Iset current → cutoff. Sources: [Learning Modular
   glossary][learningmodular], [nigel & 2thick4uni on ModWiggler][mw-clone],
   [Bareille][bareille], [SOS Polivoks review][sos].
4. Quantitative device model (§5): for this one-dominant-pole class of
   op-amp, unity-gain (integrator) frequency f_u ∝ Iset (LM4250: \~200 kHz at
   Iset = 6 µA, C_comp = 30 pF, per [TI AN-71][an71]); slew rate is locked to
   f_u because both come from the same tail current charging the same cap:
   SR = I_tail/C_c and f_u = I_tail/(4π·V_T·C_c), hence **SR ≈ 4π·V_T·f_u**
   (\~0.33 V/µs per MHz; AN-71's measured example is within \~2× of this).
   The integrator nonlinearity is the diff-pair tanh with linear range
   ±\~2V_T ≈ ±50 mV — slew limiting and tanh saturation are *the same
   phenomenon*.
5. Recommended discretization (§6): Zavalishin-style TPT/zero-delay-feedback
   SVF ([The Art of VA Filter Design, v2.1.2][zavalishin], ch. 4 for the TPT
   SVF, ch. 6 for embedded nonlinearities) with (a) tanh saturation at each
   integrator input whose voltage scale is fixed (\~2V_T referred to the
   op-amp input, i.e. \~48× larger referred to the signal node because of the
   47k/1k input dividers), and (b) hard asymmetric clamps on the integrator
   *states* at the output-swing rails — the Wasp paper demonstrates the rail
   clamp on states is what makes high-resonance behavior right.

---

## 2. The key academic anchor: DAFx20in22 Wasp VCF paper

**"A Virtual Analog Model of the EDP Wasp VCF"**, Lasse Köper & Martin
Holters (Helmut Schmidt Univ.), Fabián Esqueda & Julian D. Parker (Native
Instruments), Proc. 25th DAFx (DAFx20in22), Vienna, 2022 — [PDF][wasp-pdf],
[archive entry][wasp-entry]. Everything below is extracted from the paper.

Why it matters for the Polivoks: the Wasp is also a 2-pole SVF built from
cost-cut parts behaving badly — CMOS inverters in lieu of op-amps as the
integrator gain elements, OTAs (CA3080) as the cutoff-control elements, and a
unipolar supply. The modeling lessons transfer almost verbatim to the
Polivoks, whose "bad part" is the programmable op-amp itself.

### 2.1 Linear analysis (their §2)

- Ideal RC integrator: v_out = −∫ v_in/(RC) dt, crossover f_b = 1/(2πRC).
- OTA-based integrator: i_OTA = i_bias·(v₊ − v₋)/(2·V_TH), V_TH ≈ 25 mV,
  giving crossover **f_b = i_bias/(4π·V_TH·C)** — cutoff proportional to
  bias current. (Same law governs the Polivoks' УД12 with C = internal comp
  cap; see §5.)
- Wasp input network scales the inverter output down by ≈ 16.26×10⁻³ before
  the OTA inputs (voltage divider R5/R6/R7‖R8) — compare Polivoks' 47k:1k
  divider ≈ 20.8×10⁻³ (§4). Their cutoff constant: ω_c ≈ 9.855×10⁸ · i_bias.
- Full transfer functions: H_HP = −s²/D(s), H_BP = −ω_c·s/D(s),
  H_LP = −ω_c²/D(s) with D(s) = s² + H₁(s)·ω_c·s + H₂(s)·ω_c², where H₁ is a
  frequency-dependent resonance-network term (star-delta transformed pot
  network) and H₂ ≈ 1 + sR₂C₂. Near ω_c the system reduces to a plain SVF
  denominator s² + (ω_c/Q)s + ω_c² with Q = 1/(ℜH₁(jω_c) + R₂C₂ω_c); the
  approximation fails for high resonance + low cutoff, where the real
  circuit's resonance peak is *higher and shifted up* vs. the ideal SVF.

### 2.2 Nonlinear models (their §3) — the transferable part

- **OTA saturation**: i_OTA = α·i_bias·tanh(β·v_OTA/(2V_TH)) with fitted
  α = 0.8635, β = 0.9408 (their eq. 35, Table 3). I.e., a scaled tanh on the
  differential input, exactly the diff-pair large-signal law.
- **Rail/output-clipping extension** (their key contribution): measurements
  show the OTA output current jumps to a different plateau when the output
  voltage nears the supply rails. They model output current as
  i_OTA = f_M + f_L + f_H where f_M is the tanh above, and
  f_H = (H − f_M)/2 · [tanh(γ_H(v_out − Δv_H)) + 1],
  f_L = −(L − f_M)/2 · [tanh(γ_L(v_out − Δv_L)) − 1],
  with plateau surfaces L(v_OTA, i_bias), H(v_OTA, i_bias) fitted as
  second-order polynomials (their eqs. 36–41, Table 4). This makes the
  integrator current collapse when the state hits the rail.
- CMOS inverters modeled as complementary MOSFET pair with fitted
  polynomial-parameterized α(v_GS), v_T(v_GS) and channel-length modulation
  (their eq. 33–34, Table 2), fitted by differentiable white-box optimization
  ([Esqueda, Kuznetsov & Parker, DAFx20in21][diffwhitebox]).
- Feedback diodes: Shockley law, fitted I_s = 2.52 nA, η = 1.752.

### 2.3 Results & conclusions

- Simulated with the [ACME.jl][acme] nonlinear state-space framework
  ([Holters & Zölzer, EUSIPCO 2015][holters-zolzer]).
- At low resonance the simple tanh OTA model ≈ rail model. **At high
  resonance the simple model is wrong**: the operating point drifts to the
  rails, and without the rail model the cutoff shifts and the output clips
  incorrectly; with it, state trajectories show clean "state clipping" at the
  supply voltage and the simulated envelope matches measurement.
- Takeaway for the Polivoks: model (1) tanh on the integrator input, (2) a
  mechanism that bounds the integrator *state* at the output swing limit.
  Those two nonlinearities, with amplitude scales taken from the real device,
  reproduce the "dirty/erratic" behavior class.

### 2.4 Related academic lineage (for the bibliography)

- [Huovilainen, DAFx-04][huovilainen]: nonlinear Moog ladder (tanh-embedded
  integrators) — the prototype for "cheap tanh in each integrator".
- Fontana & Civolani, IEEE TASLP 2010: EMS VCS3 as nonlinear filter network.
- Rest, Parker & Werner, DAFx-17: WDF model of Korg MS-50 diode-bridge VCF.
- Bogason & Werner, DAFx-17: OTA circuits in wave digital filters.
- [Parker, Esqueda & Bergner, DAFx-19][dafx19]: neural state-space (MS-20
  filter example) — the gray/black-box alternative if white-box misses.
- [Köper & Holters, DAFx-2020 "Taming the Red Llama"][redllama]: their CMOS
  inverter model derivation.

---

## 3. Discretization anchor: Zavalishin's TPT SVF

[The Art of VA Filter Design v2.1.2 (PDF)][zavalishin] (Vadim Zavalishin,
Native Instruments). Just the essentials we build on:

- **Chapter 4 (state-variable filter)**: the TPT (topology-preserving
  transform) 2-pole SVF, Fig. 4.15. With trapezoidal integrators,
  g = tan(ω_c·T/2) (prewarped), damping 2R = 1/Q, states s₁, s₂, the
  zero-delay feedback solution is closed-form:

      HP = (x − (2R + g)·s₁ − s₂) / (1 + 2Rg + g²)
      v₁ = g·HP;  BP = v₁ + s₁;  s₁ = BP + v₁
      v₂ = g·BP;  LP = v₂ + s₂;  s₂ = LP + v₂

  4 multiplies, 6 adds per sample; unconditionally stable for g > 0,
  R > −1 (denominator ≥ (1−g)² > 0). Time-varying cutoff/resonance are safe
  because trapezoidal integration preserves passivity.
- **Chapter 6 (nonlinearities)**: embedding saturators in the integrator
  inputs and feedback paths; §6.10 "multinonlinear feedback" (several
  nonlinearities in one ZDF loop → iterate or use "cheap" methods), §6.11
  antisaturators, §6.12 **asymmetric saturation** — directly applicable,
  since the Polivoks clipping is supply-asymmetric (class-B output stage,
  ±12.5 V rails, \~±10 V swing).
- Practical recipe used across the VA literature: keep the linear TPT SVF
  skeleton, insert tanh at the integrator inputs (Huovilainen-style), solve
  the 2-nonlinearity ZDF loop with 1–2 Newton or fixed-point iterations (or
  use the previous-sample nonlinearity state as predictor), and oversample
  2–4× to control distortion aliasing.

---

## 4. The actual Polivoks VCF circuit

### 4.1 Topology (from the original schematic)

The original Russian schematic (scan hosted by [Bareille][bareille-gif]; his
[redrawn clone PDF][bareille-pdf] keeps the topology) shows the entire VCF:

- **А1 = КР140УД8Б** (≈ µA740/741-class): the cutoff CV amplifier. It drives
  **V1 = КТ315Г** (NPN, ≈ 2N3904), which sources the control current into
  the **Iset pins of both УД12s through two 110 kΩ resistors** (R5, R8).
- **А2, А3 = К140УД12**: the two filter stages. Signal path: input →
  **47 kΩ** series resistor (R3) into А2's noninverting input, which has a
  **1 kΩ resistor to ground** (R4) → А2 out → **47 kΩ** (R6) into А3's + input
  with its own **1 kΩ to ground** (R7) → А3 out. Feedback returns from А3's
  output to А2 (global loop), and the **resonance pot** (panel connector
  "Рег.резон.") injects А2's output (the BP node) back into А2's inverting
  side. No capacitors anywhere in the core. Marked signal levels: \~0.5 V at
  both stage outputs; supply **±12.5 V**.
- Connector list on the schematic: CV in (Вх.упр.), signal in (Вх.сигн.),
  resonance pot (Рег.резон.), signal out (Выход сигн.), mode switch
  (Вкл.режима — selects LP or BP takeoff), frequency pot.
- [Sound On Sound's Polivoks review][sos] summarizes it as Kuzmin's
  12 dB/oct filter of "just eight components: two op-amp ICs and six
  resistors", with LP and BP responses. A \~1985 revision "eliminated a
  little of the nastiness".

So it *is* an SVF in disguise: the first УД12's differential pair does the
summing (input + LP feedback + resonance feedback across its two inputs), and
each УД12's open-loop dominant-pole response supplies one integration. The
47k:1k dividers (ratio ≈ 1/48 ≈ 0.0208) set the cutoff two decades below the
op-amps' unity-gain frequency and, critically, scale node voltages down to
diff-pair scale (0.5 V × 1/48 ≈ 10 mV — inside, but not far inside, the
±\~50 mV linear range; hot signals push straight into tanh).

### 4.2 How the "no capacitors" trick works

- [Learning Modular][learningmodular]: "a Russian K140УД12 programmable
  op-amp as a current-controlled slew generator … placed in a typical
  two-pole state variable multimode filter design"; "known for its eager and
  occasionally unstable resonance."
- ModWiggler clone-kit thread ([archived][mw-clone]), user **nigel**: "any
  op-amp has a maximum slew rate … once the frequency of the input signal
  rises past a certain point, the amp can't respond fast enough, so the
  output signal is smaller — in other words, the signal is low pass filtered.
  The components in this circuit are chosen so that the limiting frequency is
  in the audio range, and the control signal into pin 8 varies that
  frequency." User **2thick4uni** (correction/refinement): "the op-amp
  actually has quite a fast maximum slew rate but it is controllable. **The
  gain bandwidth is controlled according to the current at the Iset pin.**
  The original design by Vladimir Kuzmin used the Soviet K140UD12 … an exact
  copy of the UA776."
- [North Coast Synthesis, "Modular synthesis intro part 8"][northcoast]:
  the Soviet programmable op-amps "have built-in [compensation] capacitors,
  and they are being used as the integrator capacitors here"; the chips
  "accept a control current and function to some extent as VCAs"; "the
  behaviour of the programmable op amp voltage-controlled integrators in the
  Polivoks VCF circuit is far from the mathematical ideal."
- A ModWiggler search snippet widely quoted (clone-kit thread): "Kuzman
  designed the Polivoks filter to work by slew limiting in the programmable
  op amps (a linear voltage change rate) rather than by RC or LC (which are
  exponential processes). So, the circuit is JUST resistors and
  slew-programmable op amps." — Note §5.3: the small-signal behavior is
  still a true first-order pole (linear integrator); "slew" is the
  large-signal regime. Both views are the same tanh integrator at different
  amplitudes.

### 4.3 Control (cutoff CV)

- Original: УД8 op-amp + KT315 NPN as a V→I converter feeding Iset through
  110 kΩ — approximately **linear current → linear cutoff**, not 1 V/oct;
  the panel pot taper does the musical scaling. The Harvestman module's
  designer ("governor blacksnake" = Scott Jaeger) confirms the original has
  "a limited range of the frequency control … thanks to the taper of the
  potentiometers", and his module "has the control laws adjusted"
  ([ModWiggler t=11650][mw-questions]).
- Clones that want 1 V/oct add an exponential converter: the [Shruthi
  Polivoks board][shruthi] uses "a rudimentary exponential converter" and an
  LM4250, cutoff spanning \~10 Hz–10 kHz (16 Hz–16 kHz with resistor mods,
  which also "increases distortion intensity").
- LM4250 vs µA776/УД12 sensitivity: [Bareille][bareille] found the LM4250's
  GBW-vs-Iset curve "much more sensitive", so the 110 kΩ Iset resistors must
  become ≥ 1 MΩ (he used 1.2 MΩ); the electro-music thread's builders
  likewise re-valued R10/R11 to 110 k for µA776/MC1476 vs LM4250
  ([electro-music thread][em-thread]).

### 4.4 Reported behavior (targets for the model)

- **Levels**: core signal levels are low (\~0.5 V); Bareille added ×11
  output buffers "because the audio level on outputs is rather low"
  [Bareille][bareille]. The Harvestman module adds mixer gain so it screams
  more easily than the original keyboard ([mw t=11650][mw-questions]).
- **Self-oscillation onset** \~2/3 of the resonance pot; "the resonance
  sound progress with a series of harmonic steps when the Q pot value is
  increased … much accentuated if the filter is overloaded! With Q set to
  max, the sound suddenly becomes extremely 'harsh'" [Bareille][bareille].
- **Amplitude-dependent instability**: "At high settings the filter will
  self-oscillate, but this is very unstable and heavily dependent on the
  amplitude and other characteristics of the input signals … There is a
  'sweet spot' [of input mix level] where the instability of the filter
  resonance is very interesting. Below this zone, the self-oscillation is
  much simpler, and above it, it is absent."
  ([Harvestman Model 1982 manual][harvestman-manual]) — i.e. big inputs
  saturate the integrators, killing loop gain and quenching oscillation: a
  direct consequence of tanh loop-gain compression.
- **No low-end loss at high resonance** — "it seems to ADD a bit more beef"
  (user johnnymad, [mw t=11650][mw-questions]). Consistent with saturating
  integrators (like Huovilainen's Moog result: nonlinearity limits resonance
  amplitude instead of stealing passband).
- **Distortion is the point**: "raw, abrasive … the harshness stems from
  overdriving the filter input" and (per Kuzmin, on the synth as a whole)
  much aggression also comes from the overdriven VCA downstream
  ([SOS review][sos]). The filter is "of the second order and … sounded a
  little bit wildish" (attributed to Kuzmin, [analogik article][analogik]).
- **LP is 12 dB/oct, BP 6 dB/oct** per side ([Harvestman
  manual][harvestman-manual]). An HP tap exists in the SVF but the Harvestman
  ships it only as an option because it "puts an American opamp into the
  filter core … the standard HP/BP configuration is very authentic"
  ([mw t=11650][mw-questions]).
- **The exact ICs matter**: Ģirts Ozoliņš (Erica Synths) built several
  LM4250/µA776 clones, then "built this with original Russian parts … And —
  it sounded way different!" ([ModWiggler clone-kit thread][mw-clone]); that
  kit became the [Erica Synths Polivoks VCF][erica-diy] line (their DIY
  manual PDF; product now "Black Polivoks VCF V2" using matched
  K140UD12s, [Perfect Circuit listing][erica-pc]). Plausible physical basis:
  УД12 typ/min specs differ from LM4250 (different Iset→GBW constant,
  different output stage), shifting both cutoff calibration and clip
  asymmetry.

### 4.5 Existing digital emulations

- **Vult "Vortex"** (Leonardo Laguna Ruiz), VCV Rack: "a detailed simulation
  of the original circuit with a few small tweaks", drive control saturates
  the input, LP+BP outputs ([Vortex docs][vortex]). Laguna Ruiz calls his
  filters "simulations … I try to improve the behavior … by adding or
  removing behaviors that I like or dislike"; he published A/B videos of
  Vortex vs. the Erica Synths Polivoks VCF ([Synthtopia][synthtopia-vult]).
  KVR consensus: "the Polivoks filter is heavily non-linear and depends on
  abusing an opamp essentially … no digital filter really nails the tone"
  ([KVR thread][kvr]).
- Cherry Audio's Filtomika includes a Polivoks-type mode
  ([docs][cherry-filtomika]). Reaktor "Vodka Filter" ensemble exists in the
  NI user library ([KVR thread][kvr]).

---

## 5. The programmable op-amp: K140UD1208 / µA776 / LM4250

### 5.1 Device facts

- **LM4250** ([datasheet][lm4250-ds], [TI AN-71][an71]): one external
  resistor from pin 8 sets Iset, which "programs the input bias current,
  input offset current, quiescent power consumption, slew rate, input noise,
  and the gain-bandwidth product". Internally (AN-71): Iset flows through a
  diode-connected transistor whose mirror sets the input-stage tail current
  (⇒ g_m, slew, GBW all ∝ Iset); the class-B output pair is biased "to the
  verge of conduction" (crossover region exists but is small);
  **compensation is a fixed internal 30 pF** cap across the second stage,
  giving 6 dB/oct single-pole rolloff. AN-71's worked example: **Iset = 6 µA
  → GBWP = 200 kHz**. AN-71 also gives the standard slew formulas
  (S_r = 2π·f_max·V_p; ramp output when the demanded dV/dt exceeds S_r) and
  a measured micropower point: Iset = 0.44 µA → SR ≈ 11 V/ms (+) / 12.8 V/ms
  (−) (note the slight built-in asymmetry).
- **К140УД12 / КР140УД1208** ([Russian datasheet summary][rudatasheet]):
  internal compensation, short-circuit-protected output; guaranteed minima at
  ±15 V, R_L = 75 kΩ: at I_д (Iset) = 1.5 µA — supply current ≤ 30 µA, unity
  gain frequency ≥ 0.01 MHz, slew ≥ 0.01 V/µs; at I_д = 15 µA — ≤ 190 µA,
  ≥ 0.1 MHz, ≥ 0.1 V/µs; output swing ≥ ±10 V. Allowed I_д up to
  (250 − T)/3 µA. Note both f_u and SR scale ×10 when Iset scales ×10 —
  linear coupling, and the SR/f_u ratio (≥1 V/µs per MHz *as spec minima*)
  brackets the physics-derived value below.
- **µA776**: the УД12 is "an exact copy of the UA776"
  ([2thick4uni, ModWiggler][mw-clone]); equivalents list from the
  [electro-music thread][em-thread]: µA776, MC1776, LM776, LM4250, NJR4250,
  NTE888M. K140UD12 confirmed as the part in Erica's module
  ([Midium listing][erica-midium]).

### 5.2 Quantitative integrator model (derived; flag: derivation ours,
anchored to the cited data points)

Single-dominant-pole op-amp with tail current I₁ ∝ Iset charging internal
C_c ≈ 30 pF:

- Small signal: A(s) ≈ ω_t/s with **ω_t = g_m/C_c** and, for an undegenerated
  BJT diff pair (same convention as the Wasp paper's OTA law
  i = i_bias·v/(2V_TH)): f_u = I₁/(4π·V_T·C_c). With the AN-71 calibration
  point (6 µA → 200 kHz, 30 pF) the effective I₁ ≈ 0.33·Iset — i.e. take
  **f_u ≈ 33 kHz per µA of Iset** for LM4250-class parts and calibrate per
  device (УД12/µA776 need Iset \~10× larger for the same f_u, hence the
  110 kΩ vs 1.2 MΩ CV resistors, [Bareille][bareille]).
- Large signal (the whole nonlinearity in one line):

      C_c · dv_out/dt = I₁ · tanh( (v₊ − v₋) / (2·V_T) )
      ⇔ dv_out/dt = SR · tanh( v_diff / (2·V_T) ),  SR = I₁/C_c = 4π·V_T·f_u

  So SR ≈ 0.33 V/µs per MHz of f_u (AN-71's measured example is \~2× this —
  use it as a fit parameter with that bracket). The linear range of the
  integrator input is fixed at ±\~2V_T ≈ ±50 mV **independent of cutoff**;
  slew limiting *is* the tanh ceiling. Because the Polivoks feeds each + input
  through a 47k:1k divider, the tanh scale referred to the signal node is
  ±\~2.4 V — but resonance feedback and hot inputs act with much less
  attenuation on the first stage, which is why drive and resonance interact
  so strongly.
- Output stage: clamp v_out to the swing limits (≥ ±10 V at ±12.5–15 V rails,
  [rudatasheet][rudatasheet]) — asymmetrically, and per the Wasp paper's
  result (§2.2–2.3), implement this as a clamp/current-collapse on the
  integrator **state**, not a post-saturator, or high-resonance behavior
  (cutoff shift, envelope of the clipped self-oscillation) comes out wrong.
  The slight ± slew asymmetry (11 vs 12.8 V/ms in AN-71) and class-B
  crossover are second-order seasoning.

### 5.3 Reconciling "slew filter" vs "GBW filter"

Both descriptions circulate in the forums. The resolution (consistent with
nigel/2thick4uni above and the device physics): at small amplitude the stage
is a *linear* integrator and the filter is a textbook linear SVF whose cutoff
rides Iset; at large amplitude the integrator output rate saturates at
SR ∝ Iset, which for sine drive turns excess amplitude into triangular
slewing — a 1/f amplitude ceiling above cutoff, i.e. "filtering by slew
limiting". A tanh integrator reproduces both regimes with no extra machinery.
This is why the Polivoks gets *more* low-pass-sounding and gnarlier the
harder it is driven, and why its resonance is amplitude-dependent
(Harvestman "sweet spot", §4.4).

---

## 6. Recommended model skeleton (synthesis of the above)

1. Core: Zavalishin TPT SVF (ch. 4 code, §3) at 2–4× oversampling;
   g = tan(π·f_c·T), damping k = 2R mapped from the resonance pot with the
   pot-network taper (resonance path in the real thing is a pot loading the
   BP node, so k is not linear in rotation; Bareille's "harmonic steps" and
   \~2/3-travel self-osc onset are the calibration targets).
2. Replace each integrator with the §5.2 tanh integrator:
   v̇ = ω_c·(2V_T′)·tanh(v_in_eff/(2V_T′)) with V_T′ the tanh scale referred
   to the node (fit; start ≈ 1–2.4 V per the divider argument, and make the
   first stage's effective drive include input + resonance feedback at lower
   attenuation).
3. Clamp states at asymmetric rails (≈ +10.5/−10 V scaled to the model's
   headroom) Wasp-style; add a gentle current-collapse (tanh transition,
   their eqs. 38–39) rather than a hard min/max if the clamp edge sounds
   too digital.
4. Cutoff control: f_c ∝ Iset (linear-in-CV by default, like the original);
   expose a 1 V/oct expo mapping as the "modern" option (what all clones
   add). Optional: slight cutoff droop at high drive emerges automatically
   from the tanh — verify against the reported behavior rather than adding
   ad-hoc terms.
5. LP out = second state; BP out = first state (6 dB/oct skirts); "mode"
   switch selects tap. Keep an input drive control ahead of the core — most
   of the famous nastiness is input overdrive ([SOS][sos],
   [Harvestman manual][harvestman-manual]).
6. If white-box fidelity stalls, the fallback per the literature is
   measurement-fitted models ([differentiable white-box][diffwhitebox]) or
   neural state-space ([DAFx-19][dafx19]) — but the consensus is the two
   nonlinearities in (2)–(3) carry the character.

---

## 7. Source-quality notes

- The Wasp DAFx paper is peer-reviewed and measurement-based; equations and
  fitted parameters above are quoted from the PDF directly.
- Bareille's page is a build-verified clone (compared side-by-side with a
  real Polivoks by a collaborator: "very very close") — good for component
  values and qualitative behavior, not lab-grade measurements.
- ModWiggler/electro-music quotes are hobbyist testimony; the
  nigel/2thick4uni mechanism description matches device physics and the
  datasheets, so it is treated as reliable; single-poster sonic claims are
  labeled as such. ModWiggler blocks direct fetching; quotes were taken from
  Wayback Machine snapshots of the cited threads.
- The K140UD1208 numbers are guaranteed minima from a Russian datasheet
  aggregator, not typicals; use them as bounds and calibrate typicals from
  the LM4250/AN-71 data.
- Tim Stinchcombe's site (authoritative on MS-20/Steiner filters) has no
  Polivoks analysis; Electric Druid likewise — noted to save future
  searching. No Aalto (Välimäki-group) Polivoks publication exists.
- Background/history (non-technical): [Wikipedia][wikipedia], the
  [RBMA Kuzmin interview][rbma-kuzmin] (design goals, "Russian military
  brutal synthesizer"; no filter engineering detail), and a further
  ModWiggler [discussion thread][mw-filter-thread] on the filter's character.

---

## Sources

[wasp-pdf]: https://www.dafx.de/paper-archive/2022/papers/DAFx20in22_paper_34.pdf
[wasp-entry]: https://www.dafx.de/paper-archive/details/MX0iFIRDuNA243d0W4x7Lg
[dafx19]: https://dafx.de/paper-archive/2019/DAFx2019_paper_42.pdf
[redllama]: https://www.dafx.de/paper-archive/2020/proceedings/papers/DAFx2020_paper_21.pdf
[diffwhitebox]: https://www.dafx.de/paper-archive/2021/proceedings/papers/DAFx20in21_paper_39.pdf
[holters-zolzer]: https://ieeexplore.ieee.org/document/7362548
[acme]: https://github.com/HSU-ANT/ACME.jl
[huovilainen]: https://www.dafx.de/paper-archive/2004/P_061.PDF
[zavalishin]: https://www.native-instruments.com/fileadmin/ni_media/downloads/pdf/VAFilterDesign_2.1.2.pdf
[bareille]: http://m.bareille.free.fr/modular1/vcf_polivoks/vcf_polivoks.htm
[bareille-pdf]: http://m.bareille.free.fr/modular1/vcf_polivoks/polivoks_schem.pdf
[bareille-gif]: http://m.bareille.free.fr/modular1/vcf_polivoks/vcf_polivoks_sh0.gif
[em-thread]: https://electro-music.com/forum/viewtopic.php?highlight=polivoks&t=23246
[mw-clone]: https://www.modwiggler.com/forum/viewtopic.php?t=103415
[mw-questions]: https://www.modwiggler.com/forum/viewtopic.php?t=11650
[mw-filter-thread]: https://www.modwiggler.com/forum/viewtopic.php?t=86928
[shruthi]: https://pichenettes.github.io/mutable-instruments-diy-archive/shruthi/polivoks/
[harvestman-manual]: https://www.analoguehaven.com/the-harvestman/model-1982/manual.pdf
[learningmodular]: https://learningmodular.com/glossary/polivoks/
[northcoast]: https://northcoastsynthesis.com/news/modular-synthesis-intro-part-8-statevariable-filters/
[sos]: https://www.soundonsound.com/reviews/formanta-polivoks-synthesizer
[analogik]: http://analogik.com/articles/180/polivoks-russian-vintage-synth
[rbma-kuzmin]: https://daily.redbullmusicacademy.com/2016/03/vladimir-kuzmin-interview/
[an71]: https://www.ti.com/lit/an/snoa652b/snoa652b.pdf
[lm4250-ds]: https://www.egr.msu.edu/eceshop/Parts_Inventory/datasheets/lm4250cn.pdf
[rudatasheet]: https://rudatasheet.ru/microchips/k140ud12_kr140ud12/
[erica-diy]: http://web.archive.org/web/20240612111004/https://www.ericasynths.lv/media/DIY_VCF_assembly.pdf
[erica-pc]: https://www.perfectcircuit.com/erica-synths-black-polivoks-vcf-v2.html
[erica-midium]: https://themidium.com/products/erica-synths-black-polivoks-vcf-v2
[vortex]: https://modlfo.github.io/VultModules/vortex/
[synthtopia-vult]: https://www.synthtopia.com/content/2018/07/02/analog-vs-digital-with-vcv-rack-the-nasty-details/
[kvr]: https://www.kvraudio.com/forum/viewtopic.php?t=597872
[cherry-filtomika]: https://docs.cherryaudio.com/cherry-audio/effects/filtomika/filter
[wikipedia]: https://en.wikipedia.org/wiki/Polivoks

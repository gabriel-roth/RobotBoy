# Polivoks VCF — circuit analysis from local source materials

DSP-oriented analysis for a virtual-analog emulation of the Polivoks (Поливокс) filter.
All claims are cited to the local documents listed below. Where a value could not be
read with confidence from the scans, it is explicitly flagged.

## Sources (all under `resources/polivoks-sources/`)

- **[F]** `schematics/factory_full/sch_polivoks7-vcf.jpg` — factory schematic of the
  filter board **У4** ("Блок фильтра (У4), схема электрическая принципиальная").
- **[F11]** `schematics/factory_full/sch_polivoks11.jpg` — factory wiring sheet showing
  the filter block's **panel** components (resonance pot R89, mode switch S54, EG mode
  switch S55, cutoff/EG-depth pots) around connector X4.
- **[F9]** `schematics/factory_full/sch_polivoks9-mixer.jpg` — mixer board У6 (source of
  the filter's audio input).
- **[F1]** `schematics/factory_full/sch_polivoks1.jpg` — master interconnection schematic.
- **[TXT]** `schematics/factory_full/polivoks.txt` — one-paragraph description (BP/LP VCF,
  "evil-sounding filter").
- **[E]** `erica_polivoks_vcf_diy/Polivoks VCF DIY/Polivoks VCF assembly/DIY_Polivoks_VCF.pdf`
  — Erica Synths DIY Polivoks VCF schematic, rev v1.0, 2016-12-04 (KiCad, fully legible).
- **[EB]** `.../DIY_Polivoks_VCF_BOM.pdf` — Erica BOM (values, pot tapers, NU parts).
- **[EA]** `erica_polivoks_vcf_diy/Polivoks VCF DIY/DIY VCF assembly.pdf` — Erica assembly
  guide (specs, features, calibration).
- **[S]** `mutable_shruthi_polivoks/Shruthi-Analog-Polivoks-v01.pdf` — Mutable Instruments
  Shruthi "Analog Polivoks" filter board, Émilie Gillet, cc-by-sa.
- **[IME]** `IME_Harvestman_PolivoksVCF_mk2_quickstart.pdf` — Industrial Music Electronics
  (The Harvestman) Polivoks VCF mk2 (R-1982) quickstart.

The zip `schematics/polivoks_factory_schematics.zip` duplicates the extracted
`factory_full/` set and was skipped.

**Reading note:** the Erica DIY schematic [E] is a component-for-component redraw of the
factory core with identical values (47k/1k dividers, 110k bias resistors, 3k/100k control
network, 47k resonance/feedback resistors). Wherever the 1982 scan is ambiguous, [E] was
used as the tiebreaker, and this is noted.

---

## 1. Topology — what this filter actually is

**There are no capacitors anywhere in the filter core.** On the factory board [F] the only
capacitor is C1 5.0 µF, which belongs to the envelope generator that shares the У4 board.
The "integrators" are two **К140УД12** programmable op-amps (Soviet clone of the LM4250)
running **open-loop**, each behaving as an integrator because an op-amp above its dominant
pole is `A(s) ≈ ω_u/s`, where ω_u (unity-gain bandwidth) is set by the chip's programming
current into pin 8. Cutoff control = varying the op-amps' GBW/slew via bias current. This
is the defining Polivoks trick and the source of most of its character.

### Signal chain (factory [F]; Erica designators in parentheses [E])

```
                        +--------------- R3 47k (R26) ----------------+   main 2-pole loop
                        |                                             |
 audio in  ---->--- [node N1] --> A2 K140UD12 --- R6 47k (R30) --> A3 K140UD12 --+--> LP out
 resonance ->--47k--^   |         inverting,      R7 1k (R31)      non-inverting |
 (R54/R25)          R4 1k (R27)   open loop       to ground        open loop     |
                    to GND           |                                           |
                                     +--> BP out (stage-1 output)          (~0.5 V AC
                                                                            marked at both
                                                                            outputs [F])
```

- **Stage 1** (factory A2, Erica DA3A): signal into the **inverting** input (pin 2),
  non-inverting input grounded. Input node N1 has a **1 k shunt to ground** (R4/R27) and
  receives three currents through \~47 k-class resistors: the audio input, the main
  feedback from stage-2 output (47 k, R3/R26), and the resonance return (47 k, R54 on the
  factory panel / R25 on Erica).
- **Stage 2** (factory A3, Erica DA4A): driven at the **non-inverting** input through a
  47 k/1 k divider (R6/R7, R30/R31); inverting input grounded. The alternation
  (inverting → non-inverting) makes the overall two-integrator loop negative feedback.
- **Main loop:** stage-2 output → 47 k → node N1. Closing two cascaded integrators with
  position feedback gives a 2-pole resonant response.
- **Damping/resonance loop:** stage-1 output (the BP node) → resonance pot (100 k) →
  47 k → node N1. Feeding a stage's own (inverted) output back to its input is **negative,
  velocity-proportional feedback = damping**. The resonance pot works by *removing*
  damping (see §3).

This is a textbook two-integrator state-variable loop in disguise, minus the separate
summing amp (summation happens by current addition into the 1 k node) and minus the
capacitors (the op-amps' internal compensation caps do the integrating).

### Transfer function with component values

Let `ω_u = 2π·GBW` of each УД12 at the current programming current (assume matched; in
reality mismatched — see §7). Node resistance `R_N = 1k ∥ 47k ∥ 47k ∥ R_in ≈ 0.94 k`.
Per-source current-divider gain into N1: `a1 = R_N/47k ≈ 1/50`. Stage-2 divider
`a2 = 1k/48k = 1/48`. With β = resonance-pot wiper fraction (0 = wiper at ground end,
1 = wiper at BP-output end) and Erica's input resistor R_in = 39 k:

```
                 -(47k/39k) · ω0²
H_LP(s)  =  ─────────────────────────────           ω0 = ω_u·sqrt(a1·a2) ≈ ω_u / 49
             s² + (ω0/Q)·s + ω0²                    Q  ≈ 1/β      (≈ 1.02/β exactly)

H_BP(s)  =  (48·s/ω_u) · H_LP(s)                    BP peak gain ≈ 1.2·Q at ω0
```

- LP passband gain ≈ −1.2 (Erica, 47k/39k); the factory version has audio and feedback
  entering through same-value 47 k paths, i.e. unity passband gain, **inverted**.
- LP is 12 dB/oct, BP has 6 dB/oct skirts — matching Erica's published spec
  "BP(6dB/oct)/LP(12dB/oct)" [EA].
- Because each integrator is really `A(s) = A0/(1 + s·A0/ω_u)` with finite A0
  (and further internal poles), the ideal formulas above hold in midband; extra phase lag
  at high ω_u destabilizes the loop at high cutoff + high resonance (see §3, §7).

### Mode switch (LP/BP)

- **Factory [F11]:** the panel switch **S54** (positions labeled "ТЛФ" — telephone-band,
  i.e. band-pass — and "СФНЧ", low-pass) selects **which stage output is routed to the
  VCA**: position ТЛФ picks the stage-1/BP node (which is X4 pin 3, the same node the
  resonance pot sits on); position СФНЧ picks X4 pin 4 = stage-2/LP output. The filter
  core itself is identical in both modes, and **the resonance feedback is always taken
  from the BP node** regardless of mode.
- **Erica [E]:** same idea, cleaned up: SW1 selects between the two stage outputs, but each
  tap goes through a 10 µF coupling cap (C11 for BP, C12 for LP) with a 100 k DC-restore
  resistor to ground (R33, R34) before the switch — this is Erica's "eliminated clicks when
  switching filter modes" [EA]. Selected signal → R35 56k → TL074 inverting amp (R37 56k
  feedback, gain −1) → 10 µF out cap → 1 k → jack.

---

## 2. Cutoff control — the К140УД12 bias-current trick

### The V-to-I / quasi-expo converter

Factory [F]: op-amp **A1 (КР140УД8Б**, a TL081-class FET op-amp) + **V1 (КТ315Г** NPN) +
R2 3 k. Erica [E]: DA1A (TL074) + VT1 (2N3904) + R23 3 k — identical circuit:

- A1's inverting input is a virtual-ground summing node. Input currents arrive through
  **100 k** resistors: on Erica, +12 V offset (R22 100k), cutoff-pot buffer (R18 100k),
  CV1 (R19 100k), CV2 (R20 100k). Factory: X4 pin 1 ("Вход упр") → R1 100k, with the
  panel-side sum of the cutoff pot (R90 100k fed from +12.5 V via R87 47k), the EG-depth
  pot ("Огибающая" R88 100k, envelope is 0→+6 V) and the modulation pot [F11].
- Feedback is **R23/R2 = 3 k from output back to the summing node, with the transistor's
  base-emitter junction in parallel** (base on the summing node, emitter on the op-amp
  output). The op-amp output settles about one V_BE *below* ground — the factory
  test-point table confirms **A1 pin 6 sits at −0.7 V DC** [F].
- The transistor's **collector** sources the total control current I_C, which splits
  through **two 110 k resistors (R5/R8 factory, R28/R32 Erica) into pin 8 (I_SET)** of the
  two УД12s. So each op-amp gets `I_set ≈ I_C/2`.

Solving the node (ΣI_in = input current sum through the 100 k resistors):

```
ΣI_in = V_BE/3k + I_C/β_F        with   V_BE = V_T·ln(I_C/I_S)

⇒ low currents  (I_C ≪ β_F·V_T/3k ≈ 0.9 mA):   I_C ≈ I_S · exp( 3k·ΣI_in / V_T )
  high currents:                                I_C ≈ β_F·(ΣI_in − V_BE/3k)   (linear)
```

In the operating region (I_set of order 0.1–50 µA) the **exponential term dominates**, so
this *is* an exponential converter, just an uncompensated, single-transistor one:

- **Scale: V_T·ln2 / (3k/100k) ≈ 0.60 V per octave** of CV at the 100 k inputs
  (at 25 °C), softly compressing toward linear at the top of the range.
- Cutoff pot span (Erica): +12 V through R1 50k trim + R2 4.7k into a 100 k pot ⇒ wiper
  0…\~7.8 V ⇒ ΔV_BE ≈ 0.23 V ⇒ a theoretical **\~13 octave sweep** — consistent with a
  range from sub-audio (fully closed, set by the R1 trim: "turn Cutoff full CCW and adjust
  trimpot so the VCF is fully closed" [EA]) to \~20 kHz.
- **It is NOT volt-per-octave-compensated and has no tempco element** — tracking is
  approximate by design, and the scale drifts with temperature (§7).

### From I_set to cutoff

`f_c = GBW(I_set)/49` (from §1). For f_c = 20 kHz each УД12 needs GBW ≈ 1 MHz; for
f_c = 20 Hz, GBW ≈ 1 kHz. GBW of the LM4250 family is roughly proportional to I_set over
the useful range (order of 50–100 kHz at 1 µA — *approximate, from LM4250 family
characteristics, not from the local documents*). Slew rate scales with I_set the same way,
which matters a lot (§4). CV inputs are specified −10…+10 V on Erica [EA]; at 100 k per
input that is ±100 µA against a +120 µA fixed offset — i.e. CV can both slam the filter
shut and push it past the top of its range.

---

## 3. Resonance

- **Where it taps:** always the **stage-1 (BP) output** — factory X4 pin 3 = A2 output,
  wired on the panel to the "Резонанс" pot R89 100k (one end on the BP node, other end
  grounded); the **wiper returns through R54 47 k into the filter input node** (X4 pin 2)
  [F11]. Erica: identical — R29 100k pot across DA3A output/ground, wiper → R25 47k →
  input node [E].
- **Mechanism:** the fed-back BP signal is *damping* (negative feedback of a stage into
  its own inverting input). With β the wiper fraction toward the BP end:
  **Q ≈ 1/β**. Turning resonance *up* moves the wiper toward the *grounded* end,
  removing damping. Both Erica ("REV.LOG" taper marked on the schematic, CW = ground end
  [E]; BOM ships B100k linear [EB]) and the factory pot work this way.
- **Minimum resonance is already Q ≈ 1** (β = 1) — noticeably peakier than Butterworth
  (Q = 0.707). The filter always has a bit of a hump; it cannot be made fully flat.
- **Self-oscillation:** as β → 0 the modeled damping → 0, so oscillation onset is at the
  very top of pot travel in the ideal model; in reality the УД12's extra internal poles
  add phase lag that *subtracts* damping, so real units start oscillating somewhat before
  the end of travel, earlier at high cutoff (IME: "great instability at higher resonance
  values" [IME]; Erica: "famous for its crazy resonance sweeps and self oscillation"
  [EA]).
- **No diodes or dedicated clipper in the resonance path** (factory or Erica). Oscillation
  amplitude is limited by the core's own nonlinearities, in order of onset:
  1. the УД12 input-stage differential pair saturating (tanh; the input pins only see
     \~1/48…1/50 of the stage outputs, so limiting starts when stage outputs reach a few
     volts peak),
  2. slew-rate limiting (∝ I_set, i.e. ∝ cutoff),
  3. output-stage clipping near the ±12.5 V/±12 V rails.
  Result: strong, raspy, level-dependent self-oscillation whose waveform gets more
  triangular (slew-limited) at high frequencies, not a clean sine.

---

## 4. Nonlinearities relevant to sound

1. **Open-loop integrator input saturation.** Each УД12 input sees `v_out/48`; the bipolar
   input pair's linear window is only a few tens of mV. Stage outputs of \~0.5 V AC
   (the factory's marked nominal level [F]) sit comfortably inside; a hot input
   (Erica accepts "up to 20 Vptp" [EA]; IME: "the mixer can overdrive the filter core
   easily, resulting in unique, bass-heavy thickness" [IME]) drives the pair well into
   tanh saturation. Because the *feedback* signal saturates too, resonance collapses as
   drive increases — the classic Polivoks "the louder you hit it, the less it screams,
   the more it growls" interaction.
2. **Slew limiting proportional to cutoff.** SR ∝ I_set ∝ f_c. At low cutoff settings the
   available slew is tiny, so bright/hot signals hit slew limiting long before amplitude
   clipping — a distinctive dirty, compressed edge that a plain clipper model will not
   reproduce. In an emulation, cap the integrator output rate at
   `SR ≈ k·f_c` (k of order 2π·49·(linear window) — tune by ear/measurement).
3. **Class-B output crossover.** The LM4250/УД12 output stage runs at starvation bias at
   low I_set; crossover distortion is a known family trait (buzz on low-level signals at
   low cutoff). *Family characteristic — flagged as plausible-but-unverified from these
   documents.*
4. **Clipping levels.** Factory rails ±12.5 V [F], Erica ±12 V [E]; op-amp output swing
   \~1.5–2 V inside the rails. The factory core is entirely DC-coupled from mixer to VCA;
   input offsets (mV-scale at the УД12, multiplied by ≈48 to the outputs) give each unit
   its own asymmetry, so clipping and self-oscillation are asymmetric per-unit.
5. **No buffers anywhere in the factory core.** Mixer → node → stage 1 → divider →
   stage 2 → panel switch → VCA, all naked. The resonance pot directly loads the BP node;
   the VCA input loads whichever output the mode switch selects; changing mode changes
   loading. (Erica inserted TL07x buffers at every boundary and AC-coupled the taps, which
   is why their unit is cleaner and click-free [EA].)
6. **Cutoff-dependent DC offsets.** Since stage offsets scale with the ×48 noise gain and
   drift with I_set, sweeping cutoff or resonance pumps DC — audible thump/click on fast
   sweeps in the original; Erica's AC coupling removes it. A "vintage" mode can add a
   small cutoff-tracking DC term at the outputs.
7. **Erica input drive stage.** Audio in → 100 k to ground → 10 µF NP cap → 100 k **log**
   level pot → 10 k → TL074 (DA1D) configured as a follower with R21 10k in the feedback;
   an optional gain resistor **R17 "OPTION INPUT GAIN"** (unpopulated in the BOM [EB])
   turns it into a non-inverting amp, gain = 1 + 10k/R17 — the sanctioned way to add
   drive. 1N4148 pairs clamp the op-amp input to the rails (VD7/VD8), and similar clamps
   protect both CV inputs (VD1/VD5, VD2/VD6) and the output (VD9/VD10) [E]. So the stock
   Erica input path is unity gain; drive comes from simply feeding it modular-level
   signals (the core's "nominal" is only \~0.5 V!).

---

## 5. Version differences

| Aspect | Factory Polivoks У4 [F][F11] | Erica DIY (2016) [E][EB][EA] | Shruthi Polivoks board [S] | IME Polivoks mk2 [IME] |
|---|---|---|---|---|
| Rails | ±12.5 V | ±12 V | **±5 V** (2×7905 + LT1054) | ±12 V Eurorack |
| Core ICs | 2× К140УД12 | 2× K140UD12 (supplied with kit) | 2× **LM4250P** | "original Russian chips where appropriate" |
| Core values | 47k/1k dividers, 110k bias feeds | identical | identical 47k/1k; bias feeds read as \~110k/100k | n/a (no schematic) |
| Control xtor | КТ315Г NPN | 2N3904 NPN | **2N3906 PNP** (referenced to +5 V) | n/a |
| Cutoff CV | linear-sum → quasi-expo, \~0.6 V/oct, no comp | same; adds 50 k closing trim, buffered CV1/CV2 with attenuators | same law; CV from digital control board (FM input via 47k/30k network) | CV1 with **attenuverter**, CV2 plain |
| Resonance | 100 k panel pot, manual only | 100 k pot (rev-log intent, B100k shipped) | **voltage-controlled**: LM13700 OTA replaces the pot in the BP feedback path, driven by its own V→I (2N3906) | manual knob |
| Mode | Panel switch S54 routes BP ("ТЛФ") or LP ("СФНЧ") to VCA; core unchanged | SPDT selects AC-coupled BP/LP taps (clickless) | **4053 analog mux** selects BP/LP electronically | **both LP and BP jacks simultaneously** |
| I/O | no buffers, DC-coupled, inverting | buffered in/out, output re-inverted ("output stage added to eliminate signal inversion"), rail-clamp diodes, AC-coupled | on-board LM13700 VCA + TL072 output amp | 2-input mixer designed to overdrive the core |
| Extras | EG shares the board (0→+6 V envelope) | specs: in ≤20 Vpp, CV ±10 V, 11 mA/rail, BP 6 dB/oct, LP 12 dB/oct | Shruthi ecosystem (digital env/LFO to CV) | made "in cooperation with its inventor Vladimir Kuzmin" |

**What Scott Jaeger (IME) changed / musical behavior [IME]:** the mk2 quickstart is
behavior-only. Key points: authentic recreation with Kuzmin's cooperation; two mixed audio
inputs whose mixer "can overdrive the filter core easily, resulting in unique, bass-heavy
thickness **with great instability at higher resonance values**"; simultaneous LOW PASS
and BAND PASS outputs instead of a mode switch; CV1 has a built-in attenuverter
("counterclockwise rotation inverts"), CV2 direct; cutoff and resonance knobs labeled
МИН/МАКС. No tracking/stability claims are made in this document — treat "improved
tracking/extra modes" claims as **not supported by the local materials**.

**Shruthi specifics worth noting [S]:** with ±5 V rails the core headroom is \~4× smaller
than the original (earlier overdrive at Shruthi's \~1 V signal levels — deliberate);
resonance is a CV-able OTA path (RES1/RES2 nets), so a Shruthi-style emulation can
modulate Q; BP/LP switching is instant/electronic via the 4053; the LM13700 VCA lives on
the same board after the filter.

---

## 6. Signal levels

- **Core nominal:** \~0.5 V AC marked at both stage outputs on the factory schematic
  (measured "with a voltmeter of ≥1 MΩ input resistance", accuracy ±20 %) [F]. The mixer
  board's output is likewise marked \~0.5 V [F9].
- **Factory mixer feed:** У6 mixer = КР140УД8Б inverting summer, 22 k inputs / 22 k
  feedback, output through a 0.22 µF cap to the filter [F9]. The exact series impedance
  between the mixer and the filter's 1 k node could not be conclusively traced in the
  scans (the panel-wiring scan [F11] shows the pin-2 line joining the board node directly;
  a series element of a few tens of kΩ must exist for the loop to function — Erica
  normalizes it to a buffer + 39 k). **Flagged uncertain**; use Erica's 39 k as reference.
- **Erica levels:** audio in up to 20 Vpp; CV −10…+10 V; overall LP passband gain ≈ +1.2
  (core −1.2, output stage −1); power 11 mA/rail [EA][E].
- **Envelope (factory):** the on-board EG outputs 0→+6 V (waveform sketch and A5 test
  points +6/0/−12 V on [F]) into the cutoff sum via a 100 k panel pot — i.e. full EG depth
  ≈ 6 V ≈ 10 octaves of sweep.
- **Test-point DC:** A1 out −0.7 V (V→I transistor conducting); all core op-amp inputs
  ≈ 0 V [F].

## 7. Temperature drift, instability, per-unit quirks (for a "vintage" mode)

1. **Cutoff drifts massively with temperature.** The single-transistor expo converter has
   no compensation: at fixed CV, I_C ≈ I_S(T)·e^(V_BE/V_T), and I_S roughly doubles every
   \~5 °C. Net cutoff drift is on the order of **an octave per \~8–10 °C** (direction: warmer
   ⇒ brighter), plus scale (V/oct) stretching \~+0.33 %/°C from V_T. *Magnitude approximate
   (standard BJT physics), direction certain from the circuit [F][E].*
2. **V/octave is only nominal.** 0.60 V/oct at room temperature, compressing toward linear
   above I_C ≈ β·V_T/3k (β_F of a КТ315Г ranges \~50–350 unit-to-unit) — the top octave of
   the range flattens differently on every unit.
3. **The two integrators never match.** ω_u per stage depends on I_set splitting through
   two 110 k resistors into pin-8 inputs of two unmatched УД12s. Mismatch skews ω0 and
   makes the BP-node phase (and thus resonance onset) unit-specific. Emulate as a random
   ±10–20 % (guess) imbalance between integrator rates.
4. **Resonance onset varies with cutoff.** Extra internal poles of the УД12 reduce phase
   margin as ω_u rises: self-oscillation starts earlier (lower pot position) at high
   cutoff; at very high cutoff + max resonance the loop can get parasitic/chaotic
   ("great instability" [IME]).
5. **Offsets scale ×48.** УД12 input offsets appear at the outputs multiplied by the node
   attenuation, and they change with I_set ⇒ cutoff sweeps produce DC thump; mode
   switching clicks (factory has no AC coupling; Erica added it specifically [EA]).
6. **Minimum Q ≈ 1**: the filter is never flat; the resonance pot's bottom position still
   leaves a \~1–2 dB hump.
7. **Component culture:** МЛТ-0.25 5 % carbon resistors, К50-6 electrolytics, factory
   measurement tolerance ±20 % [F] — generous unit-to-unit spread in every parameter
   above.

## 8. Emulation checklist (condensed)

- Two multiplied-rate leaky integrators (rate = ω_u/48-ish each), stage 1 inverting,
  stage 2 non-inverting; sum currents into a virtual node: `in/39k + lp_fb/47k +
  β·bp_fb/47k`, node shunt 1 k.
- ω0 = ω_u/49; Q = 1/β (β = resonance, 1→0); LP gain −1.2; BP = band-pass tap at stage 1.
- Cutoff: quasi-expo law `I_C from ΣI_in = V_T·ln(I_C/I_S)/3k + I_C/β_F`,
  ΣI_in = (12 + V_cut + CV1 + CV2)/100k; 0.6 V/oct nominal; f_c = GBW(I_C/2)/49.
- Nonlinear elements, in order: tanh at each integrator input (window \~±50 mV referred to
  pin, i.e. \~±2.4 V referred to stage output), slew limit ∝ f_c, hard clip near rails,
  per-unit DC offsets scaling with cutoff.
- Modes: LP = stage-2 out, BP = stage-1 out; factory switches abruptly with DC step,
  Erica crossfade-free but AC-coupled.
- Vintage mode: temperature random-walk of cutoff (octave-scale over a session), integrator
  mismatch, earlier oscillation at high cutoff, minimum-Q hump, mode-switch thump.

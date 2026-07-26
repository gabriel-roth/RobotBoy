# Robot Boy filters: MF-20, Onbetap, Vespid

Robot Boy includes three filters, each modeling a famous analog filter with a strong personality: the Korg MS-20, the Soviet Polivoks, and the EDP Wasp. Each is modeled from the behavior of the original circuit — the way it distorts and rings — rather than a clean filter with a distortion stage added.

The MS-20 has two filters in series and screams when pushed. The Polivoks has an inverse relationship between drive and resonance — the harder you push the input, the *less* it rings. The Wasp sits right at the edge of self-oscillation and can be nudged over it.

- [What all three share](#what-all-three-share)
- [MF-20 — the Korg MS-20 filter](#mf-20--the-korg-ms-20-filter)
- [Onbetap — the Formanta Polivoks filter](#onbetap--the-formanta-polivoks-filter)
- [Vespid — the EDP Wasp filter](#vespid--the-edp-wasp-filter)

---

## What all three share

All of this holds for MF-20, Onbetap, and Vespid; the sections below cover only what's specific to each.

- **Stereo audio in and out.** The right input is normalled to the left, so patching **In L** alone fills both sides.
- **Polyphonic** on both [VCV Rack](https://vcvrack.com) and the [4ms MetaModule](https://4mscompany.com/metamodule).
- **Cutoff** spans 20 Hz – 20 kHz, and every cutoff CV input tracks **1 V/octave.**
- The options listed under each filter below are in the right-click menu in VCV Rack, or under Options on MetaModule.

---

## MF-20 — the Korg MS-20 filter

<img src="screenshots/MF-20.png" alt="MF-20 module" height="420">

### The original

The **Korg MS-20** (1978) is a semi-modular monophonic synth that became one of the most sampled and cloned instruments ever made. Much of its reputation rests on its filter, which is unusually aggressive at high resonance and cuts through a mix.

Korg built it two ways: early units (1978–79) used a Korg-made chip, the **Korg-35,** in a Sallen-Key design; later units switched to the more common **LM13600** transconductance amplifier. The Korg-35 is edgier and rawer; the later chip is smoother, especially at maximum resonance. MF-20 emulates both, switchable in the right-click menu.

### How it works

The signal passes through the **high-pass filter first, then the low-pass.** Sweeping them against each other opens a band or a notch, which is where a lot of the MS-20's vocal, wah-like sounds come from. Both filters self-oscillate cleanly at maximum resonance, so either can be played as a raw sine-ish voice.

The two cutoffs have independent knobs. To sweep them together the way the MS-20's shared cutoff modulation does, patch the **Total** cutoff input — it moves both at once, holding the band or notch shape while shifting it up and down.

### Controls

- **HP Cutoff** — high-pass cutoff, default 120 Hz.
- **HP Peak** — high-pass resonance. Self-oscillates at 100%.
- **LP Cutoff** — low-pass cutoff, default 750 Hz.
- **LP Peak** — low-pass resonance. Self-oscillates at 100%.
- **Drive** — 1× to 8×. At 1× the OTA filter is clean (in Korg35 mode a full-scale ±5 V signal already grazes the clipper); raising it drives the input into soft clipping.

### I/O

- **LP Cutoff CV** and **HP Cutoff CV** — modulate each cutoff independently.
- **Total Cutoff CV** — sweeps **both** cutoffs together — the input to use for sweeps that keep the band or notch intact.

Peak and Drive have no CV inputs: the MS-20 had no resonance modulation.

### Right-click menu

- **Filter revision** — **OTA (revised MS-20)**, based on the later LM13600 design (smoother, more open), or **Korg35 (original MS-20)**, the early chip (edgier, grittier distortion).

### Under the hood

Both revisions use a zero-delay-feedback (TPT) implementation. The OTA mode models the LM13600 topology with diode saturation in the resonance feedback path; the Korg35 mode puts its main nonlinearity in the forward (input) path with slightly asymmetric clipping — producing the even-order harmonics behind the original's character — plus a second clipper in the resonance loop that keeps its scream in check.

### Sources

- Stinchcombe, T. (2006). [*A Study of the Korg MS10 & MS20 Filters*](https://www.timstinchcombe.co.uk/synth/MS20_study.pdf) — circuit analysis of both filter revisions.
- Huovilainen, A. (2010). [*Design of a Scalable Polyphony-MIDI Synthesizer for a Low Cost DSP*](https://aaltodoc.aalto.fi/server/api/core/bitstreams/38b49bc4-2587-4e26-9851-8e05e06894da/content) — the zero-delay-feedback filter discretization.

---

## Onbetap — the Formanta Polivoks filter

<img src="screenshots/Onbetap.png" alt="Onbetap module" height="420">

### The original

The **Polivoks** (1982) is the best-known synthesizer to come out of the Soviet Union. It was designed by engineer Vladimir Kuzmin at the Formanta radio factory — its industrial look styled by his wife, Olimpiada, after Soviet military radios — and built in the tens of thousands for the domestic market, almost unknown in the West until long after the USSR was gone. (Kuzmin died in June 2026.)

Its filter is famous for an extreme, slightly unstable resonance and a thick, buzzy distortion, and the circuit behind it is genuinely unusual: Soviet op-amp chips run in an unconventional way, with **no capacitors** in the usual filter positions. Onbetap models that behavior directly.

### Behavior worth knowing

These four behaviors run counter to what most filters do, and they are the heart of the Polivoks sound:

- **Drive fights resonance.** On most filters, pushing the input harder makes the resonance ring louder. Here it's the reverse: a loud signal rings *less* than a quiet one at the same resonance setting. Play softly and it howls; push hard and the peak ducks while the tone thickens.
- **The self-oscillation point moves with cutoff.** Oscillation begins earlier on the resonance knob when the cutoff is high, so the filter feels more on-edge in its upper range.
- **It can turn suddenly harsh at the top of resonance,** dropping into a lower, harsher tone than the resonant frequency.
- **No bass loss at high resonance.** The low end stays present under the sound even with resonance pushed hard.

### Controls

- **Cutoff** — default 750 Hz.
- **Q** (resonance) — Self-oscillation begins in the top fifth of the knob at typical cutoffs (earlier at high cutoff — see above). Its CV input covers the full range from a 0–5 V envelope at full attenuverter.
- **Drive** — 0–100%, roughly −12 to +24 dB into the core. Adds asymmetric-clipping grit and, characteristically, suppresses resonance as you push it.
- **Mode** — a five-position knob: **Lowpass** (12 dB/oct), **Bandpass** (6 dB/oct — the circuit's two native outputs), **Highpass**, **Notch**, and **Peak**. Lowpass and Bandpass match the original hardware; the other three are new, from the same core. Mode changes crossfade over 5 ms to avoid clicks (except in Vintage — see below), and Mode has no CV input.

### Right-click menu

- **Character** — **Tamed** (default), a calibrated, stable version of the circuit; or **Vintage**, which layers in the imperfections of a real, aging unit: a slow drift of cutoff (independent per channel), a fixed mismatch between the two filter stages, a per-unit DC offset that produces a thump on fast cutoff sweeps, and a hard (unfaded) mode switch.

### Under the hood

The model was derived from factory Polivoks schematics, the Erica Synths DIY Polivoks VCF, and the physics of the К140УД12 programmable op-amp (there are no RC time constants in the core — cutoff is set by the op-amps' own bias current), not from a generic filter-plus-saturator.

### Sources

- Marc Bareille, [*Polivoks VCF*](http://m.bareille.free.fr/modular1/vcf_polivoks/vcf_polivoks.htm) — build-verified circuit analysis, with a [redrawn schematic (PDF)](http://m.bareille.free.fr/modular1/vcf_polivoks/polivoks_schem.pdf) and a scan of the [original factory schematic](http://m.bareille.free.fr/modular1/vcf_polivoks/vcf_polivoks_sh0.gif).
- Erica Synths, [DIY Polivoks VCF schematic](https://github.com/erica-synths/diy-eurorack) — a component-for-component redraw of the factory circuit.
- [К140УД12 / КР140УД12 datasheet](https://rudatasheet.ru/microchips/k140ud12_kr140ud12/) and Texas Instruments, [*Application Note 71: Micropower Circuits Using the LM4250 Programmable Op Amp*](https://www.ti.com/lit/an/snoa652b/snoa652b.pdf) — the programmable-op-amp physics behind bias-current-set cutoff (the К140УД12 is a Soviet µA776 copy; the LM4250 is its Western analogue).

---

## Vespid — the EDP Wasp filter

<img src="screenshots/Vespid-c.png" alt="Vespid module (charcoal)" height="420">
<img src="screenshots/Vespid-y.png" alt="Vespid module (gold)" height="420">

### The original

The **Wasp** synthesizer (1978) was made by **Electronic Dream Plant,** a small British company run by musician Adrian Wagner and engineer Chris Huggett. It was famously make-do: cheap, light, in a black-and-yellow plastic case (hence the name) with a flat touch-plate keyboard and a built-in speaker. It became a cult favorite, used by Devo and the Eurythmics.

Its filter is famous for *how* it was built: to save money, EDP used cheap digital logic chips — meant for on/off switching — as analog amplifiers, well outside their intended use. The result is a gritty, buzzy character all its own. It's been cloned many times, best known as **Doepfer's A-124;** Vespid models both the original and the A-124's modification.

### How it works

Vespid is a multimode state-variable filter, giving you **low-pass, band-pass, and high-pass** as a stereo pair each, plus a **Mix** output whose **Blend** knob crossfades from low-pass, through a notch at center, to high-pass. So you can pull three responses at once, or morph between low and high on one control (with CV).

The original Wasp sits right at the verge of self-oscillation — enough to whistle and chirp, but it never quite runs away. Doepfer's clone added a mod that pushes it over. Vespid models both, switchable in the context menu.

### Controls

- **Freq** — cutoff, default 750 Hz.
- **Res** — resonance, default 0.
- **Drive** — a 30 dB gain sweep into the filter, level-staged per Character mode to match the hardware each models. At drive 0, **German** is a hot Eurorack signal into a Doepfer-style circuit — clean, with clipping just up the knob; **British** reproduces the original Wasp's own oscillator level into its +5 V circuit — already lightly overdriven, the classic "dirty Wasp" rasp. Pushing either further shifts the clipping from ragged and asymmetric toward a harder, odd-harmonic squareness.
- **Blend** — crossfades the **Mix** output: fully counter-clockwise is low-pass, center (default) is a notch, fully clockwise is high-pass. It affects only the Mix output; the dedicated LP/BP/HP outputs are always available unblended.

### I/O

Alone among the three filters, Vespid has four outputs, each a stereo pair and all live at once: **HP**, **BP**, and **LP** carry the three filter responses unblended, and **Mix** carries the LP–notch–HP crossfade set by the Blend knob. **Freq**, **Res**, **Drive**, and **Blend** each have a CV input.

### Right-click menu

- **Character** — **British** (default): the original 1978 Wasp sound, riding the edge of oscillation for whistles and chirps but staying under control. **German**: a self-oscillation mod based on the Doepfer A-124, crossing into full self-oscillation.
- **Self-oscillation pitch (German)** — **Hardware (drifts flat)** reproduces the way the real circuit's oscillation pitch sags at high resonance; **Corrected (tracks knob)** keeps it in tune so you can play the self-oscillation as a voice.
- **Panel** — **Charcoal** (default) or **Gold** faceplate (VCV Rack only).

### Under the hood

Vespid models the Wasp's CMOS-inverter state-variable filter — the logic chips run as amplifiers, plus the diode resonance limiter and supply-rail clipping that shape its sound — following the DAFx-2022 model by Köper, Holters, Esqueda, and Parker. **British** runs the circuit on a +5 V supply (as the original Wasp did); **German** raises it to +12 V, which is what tips it into self-oscillation, bounded by the rails.

### Sources

- Köper, L., Holters, M., Esqueda, F., & Parker, J. D. (2022). [*A Virtual Analog Model of the EDP Wasp VCF*](https://www.dafx.de/paper-archive/2022/papers/DAFx20in22_paper_34.pdf) — Proc. 25th Int. Conf. on Digital Audio Effects (DAFx20in22), Vienna. The model Vespid is built on, including the CD4069 inverter transfer curves and the resonance network at both supply voltages.

---

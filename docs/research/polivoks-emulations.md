# Polivoks VCF — Survey of Existing Emulations

Research notes toward a new virtual-analog Polivoks filter for VCV Rack / MetaModule.
Compiled 2026-07-15. All statements are sourced; links are collected at the bottom.

## 0. The original circuit (baseline for everything below)

The Polivoks (Formanta, 1982, designed by Vladimir Kuzmin) has a 12 dB/oct filter
switchable between low-pass and band-pass ([Wikipedia][wikipedia], [SOS retro review][sos-polivoks]).
Its defining oddity: **the filter core contains no discrete capacitors.** It is a
two-integrator loop — "at first sight it looks like a classic SVF but with a
difference: there is no capacitors!" ([Bareille clone page][bareille]) — built from two
**programmable op-amps** (Soviet К140УД12 / КР140УД1208; Western equivalents
uA776, MC1776, LM4250, NTE888). The op-amps' *internal compensation capacitors*
serve as the integrator caps, and cutoff is set by the chip's **Iset programming
current**, which varies the op-amp's gain-bandwidth — "a variable time-constant RC
element" ([Bareille][bareille]). Andy Simper (Cytomic) confirms the reading: "they most
likely use the ones inside the op-amps that are usually there to maintain the
stability of feedback loops … the internal capacitor of the op-amps in an SVF
structure" ([KVR "Analog Modeling" thread][kvr-analog-modeling]).

Perfect Circuit's description of the Harvestman clone adds the key dynamic insight:
Kuzmin "designed the filter to work by **slew limiting in the programmable op-amps
(a linear voltage change rate)** rather than by RC or LC circuits (which are
exponential processes)" ([Perfect Circuit][perfectcircuit-ime]). In DSP terms the
integrators are gm-style stages whose input differential pair saturates — the same
family of nonlinearity as an OTA diff-pair tanh, which is why the filter distorts
and self-oscillates so readily. Sound on Sound counts the whole core as "two op-amp
ICs and six resistors" and notes a circuit revision around 1985 "eliminated a little
of the nastiness" ([SOS][sos-polivoks]).

Concrete numbers from Doug Slocum's build/measurement page (modularsynthesis.com,
part of his Kuzmin series) ([modularsynthesis.com][modularsynthesis]):

- Stock Iset range \~0.2 µA to 13.76 µA; FM input can pull the current to 0
  (i.e. the filter closes completely — cutoff goes essentially to DC).
- CV scaling of the stock circuit is **\~3.2×/volt, not 2×/volt** — the original
  does not track 1 V/oct without modification (he retrimmed R9 to calibrate).
- **Resonance limiting comes in "hard" and "soft" flavors**: hard resonance
  self-oscillates as a **rail-to-rail square wave**; adding clamping diodes
  (soft limiting) yields a sine — and "the soft limiting increases the
  self-resonance frequency a bit."
- Self-oscillation span (modded unit, soft limiting): **\~2 Hz to 51.4 kHz**.
- With resonance up, output reaches nearly **20 V p-p** — "distorted, dirty and
  gritty behavior," true to the original.

The **Erica Synths DIY Polivoks VCF schematic is public** (KiCad PDF inside
`Polivoks VCF DIY.zip` in [erica-synths/diy-eurorack][erica-diy-repo]) and is the best
free circuit reference. Read from the v1.0 schematic:

- CV path: cutoff pot + two CV inputs with level pots → TL072/TL074 buffers with
  1N4148 clamp diodes to the rails → summing amp → **2N3904 voltage-to-current
  converter** (original used a КТ315) feeding both K140UD12 **Iset pins through
  110 kΩ resistors** (R28/R32).
- Audio input: log-taper level pot → TL074 buffer stage with 1N4148 diode pair
  (input already clips before the core; "INPUT GAIN" resistors are options).
- Core: two K140UD12 stages, 47 kΩ input resistors (R26/R30), 1 kΩ to the
  inverting inputs; no capacitors in the loop.
- Resonance: tapped off the first stage through 47 kΩ (R25) into a 100 kΩ
  **reverse-log** pot (R29) with 39 kΩ (R24) — resonance is a passive feedback
  amount, no dedicated Q amp.
- LP/BP: a simple toggle (SW1) selects which node feeds the TL074 output
  amplifier (56 kΩ gain network); output through 1 kΩ with another 1N4148 clamp
  pair. (Output buffering with gain is a clone addition — Bareille notes the
  vintage outputs are "rather low" level and adds ×11 buffers ([Bareille][bareille]).)

## 1. Open-source software emulations

**Bottom line: no rigorous open-source virtual-analog Polivoks model exists.**
The serious models (Vult Vortex, Cherry Audio Atomika/Filtomika) are closed. What
exists in the open is hobby-grade but still instructive for control mappings and
nonlinearity placement.

### 1.1 savannah-i-g/nanoTracker-Polivoks (JavaScript AudioWorklet, MIT)

A single-file duophonic Polivoks instrument for the nanoTracker web host
([GitHub][nanotracker]). MIT-licensed, 2026. The VCF is a **TPT/trapezoidal cascade of
two one-pole low-passes with global negative feedback and tanh nonlinearities at
three points** (feedback input, inter-stage coupling, output drive). From
`script.js` (comments are the author's):

```js
// Comparator 2-pole filter (Polivoks-style).
const g = Math.tan(Math.PI * fc / sr);          // g = prewarped coefficient
const gClamped = Math.min(1.5, Math.max(0.0001, g));
// Resonance mapping: up to ~4 feedback, pushed past 1 on high reso.
const k = filtReso * 4.2;

// Feedback input — asymmetric soft-clip ... chaotic bite at self-osc.
let fbIn = sig - k * v.f2;
fbIn = Math.tanh(fbIn * 1.3) * 0.9;

// Stage 1 — one-pole (trapezoid integrator).
const v1 = (gClamped * (fbIn - v.f1)) / (1 + gClamped);
const y1 = v1 + v.f1;
v.f1 = y1 + v1;
// Between-stage non-linearity (the "comparator coupling").
const y1c = Math.tanh(y1 * 1.15);

// Stage 2 — one-pole on y1c.
const v2 = (gClamped * (y1c - v.f2)) / (1 + gClamped);
const y2 = v2 + v.f2;
v.f2 = y2 + v2;

let filt;
if (filtMode === FMODE_LP) filt = y2;
else filt = (y1c - y2); // BP approximation
let driven = Math.tanh(filt * driveGain) * 0.9;
```

Assessment: honest hobby model, worth studying only for its choices — TPT one-poles
with `g` clamped at 1.5, feedback gain up to 4.2 (>1 to force self-osc "well before
max resonance"), tanh at the feedback summing node and between stages, BP taken as
`y1c − y2`. Caveats: the topology is a cascade-with-global-feedback (MS-20-like),
**not** a true two-integrator SVF; the "asymmetric" clip in the comment is actually
symmetric in code; there is no oversampling; nothing models the linear-in-current
cutoff, slew limiting, or resonance suppression under drive.

### 1.2 trusch/vibelang `erica_polivoks_vcf.vibe` (declarative, no license)

Part of the vibelang stdlib ([GitHub][vibelang]). Self-described as an "Erica Black
Polivoks VCF V2-inspired mono filter **approximation** … not a clone of the
original K140UD12 IC behavior." Built from stock `rlpf`/`bpf` units. Only the
control mappings are interesting:

- `rq = 1 − resonance·0.94` (floor 0.035);
- **self-oscillation faked** by cross-fading in a sine at the cutoff frequency once
  `resonance > 0.82` (`self = clip((res − 0.82)·5.8823, 0, 1)`);
- drive: `tanh((pre + osc)·(1 + drive·4))`; post-stage tanh "dirt" scaled by
  resonance; output compensation `1 − resonance·0.34`; DC leak 0.995.

### 1.3 Other open items (low value for DSP)

- **oliverstotz/PD-VOKS** — Pure Data clone of the whole synth ([GitHub][pdvoks]).
  The filter subpatch is a generic **3-pole Butterworth coefficient calculator**,
  not a Polivoks model.
- **antler-hat/antler-ms1** — "Polivoks sim" in HTML/Web Audio ([GitHub][antler]);
  the VCF is a stock `BiquadFilter` (and its type switch is highpass/lowpass!).
  Nothing to learn.
- **richardjmarini/CircuitSimulations** — gschem schematics + **ngspice** command
  file for a Polivoks filter transient sim ([GitHub][spice]). Potentially useful as a
  free SPICE starting point for measuring the real circuit's responses.
- **jacopogrecodalceo/CORDELIA** `polivoks.orc` — false positive: Csound drum
  patterns named "polivoks," no filter.
- **Vult's open code** ([vult compiler repo][vult-compiler], vult-examples) contains generic
  `svf`/ladder examples but **no Polivoks model** — Vorg/Vortex sources were never
  published (the modlfo/VultModules repo history now contains only README/artwork).
- **Hardware DIY repos** (schematics/PCBs, useful as circuit cross-checks):
  [L71/PolivoksVCF][l71], [jbeuckm/Polivoks-VCF][jbeuckm], [clarionut/Polivoks_filter][clarionut],
  [erica-synths/diy-eurorack][erica-diy-repo], [float32/ambika-polivoks][ambika-polivoks]
  (a real analog Polivoks voicecard filter for the Mutable Ambika), and the
  [Shruthi Polivoks filter board][shruthi-polivoks] in the Mutable DIY archive.
- No Faust, Csound, or SuperCollider Polivoks implementations were found.

## 2. Commercial software emulations

### 2.1 Vult Vortex (VCV Rack premium; also on MetaModule)

Leonardo Laguna Ruiz's **Vortex** is "based on the circuit found in the Polivoks
synthesizer, which uses a special kind of OP-AMPs that allow you to control their
bandwidth … a detailed simulation of the original circuit with a few small tweaks"
([Vult module page][vult-vortex]; [Wikipedia][wikipedia] credits it as the Polivoks emulation for
VCV). Controls: cutoff (+1 V/oct with attenuverter), resonance (self-oscillates,
usable as a sound source), **Drive** ("saturates the filter and adds interesting
harmonics"). Outputs LP and BP; the page quips the BP is "uninteresting" static but
"magic" when modulated. Closed source, paid. Vortex is also one of the filter
models in the **Vult Freak** Eurorack module firmware ([release notes][vult-freak-firmware])
and ships in Vult's **MetaModule plugin builds** ([vult-dsp/vult-metamodule-plugins][vult-mm])
— i.e. a commercial Polivoks model already exists on the user's target platform.

### 2.2 Cherry Audio Atomika (2024 synth) and Filtomika (filter FX plugin)

DSP by **Mark Barton (MRB)**, who breadboarded the actual circuit before coding
([Cherry Audio, "The (Mostly) Unfiltered Secrets of Atomika"][atomika-secrets]). This is the
most detailed public description of Polivoks nonlinear behavior anywhere:

- Kuzmin "repurposed programmable op-amps … the compensation capacitors inside
  these chips provided the necessary filtering capacitance, enabling minimal
  control currents to achieve audio-range tuning."
- **Asymmetric clipping**: increased input level generates harmonics through
  asymmetrical distortion.
- **Resonance suppression**: "higher input levels can suppress resonance peaks"
  (drive and Q fight each other — a signature interaction).
- Self-oscillation at high resonance (unlike a well-behaved SVF).
- A **"bubbling" / "dripping water" ring** under specific input + resonance
  settings.
- Barton: "most of the aggression associated with the Polivoks sound actually
  comes from the **overdriven VCA**" — the filter alone is not the whole story.

Atomika/Filtomika extend the hardware with Starve (chokes the resonant ringing
without lowering resonance → "oscillating bubbly sound"), Filter Drive (interacts
with the resonance circuit and supply rails, "radical distorted noises" at min
drive + max resonance), Amp Drive, and extra HP/notch/peak modes (original had only
LP/BP; slopes stay 12 dB/oct LP/HP, 6 dB/side BP) ([Filtomika manual][filtomika-docs]).
Cherry's manual: "Filtomika's resonance circuit can result in some **squelching and
screaming** at higher settings," with its distortion "acting as a natural limiter."
Reviews: Synth Anatomy calls the Filtomika core "gnarly, aggressive tones"
([review][filtomika-review]); MusicRadar found Atomika convincing and noted the Starve
control "makes the filter scream" ([review][atomika-review-musicradar]); a KVR owner of real
hardware judged "Atomika is a near perfect emulation of a Polivoks"
([KVR Atomika thread][kvr-atomika]).

### 2.3 Others (minor)

- **Reaktor "Vodka Filter"** — Polivoks-based user-library ensemble, apparently
  since removed; a Polivoks-inspired **MONOVOKS** ensemble also circulated
  ([KVR VST thread][kvr-vst], [KVR MONOVOKS announcement][kvr-monovoks]). A "Polivoks Vodka VCF"
  schematic collection also exists ([Schematics Vault][schematics-vault]).
- **"Flower Child" VST** — its "Russian" filter mode was reportedly Polivoks-based
  ([KVR VST thread][kvr-vst]).
- **Old freeware 32-bit "Polivoks" VSTis** (e.g. SyncerSoft's Polivoks Station)
  exist per the same KVR thread; quality undocumented — not verified further.
- **Hideaway Studio "Polivox"** — Kontakt sample library, not a model ([Loot Audio][hideaway]).
- Hardware owners' consensus in that KVR thread (poster *ampetrosillo*, who owns a
  Polivoks): "the Polivoks filter is **heavily non-linear and depends on abusing an
  opamp** essentially … no digital filter really nails the tone … they don't
  really reproduce the same instability."

## 3. Hardware clones and what their docs say

### 3.1 Industrial Music Electronics / The Harvestman Polivoks VCF (mk1 2009, mk2 2013)

"An authentic recreation of the filter circuit … **produced in cooperation with
its inventor Vladimir Kuzmin**. The original Russian chips are used where
appropriate … The mixer can **overdrive the filter core easily**, resulting in
unique, bass-heavy thickness with **great instability at higher resonance
values**" ([IME quickstart PDF][ime-quickstart]). Panel: cutoff, resonance, CV1 with
attenuverter, CV2, two-input mixer, separate **LP and BP outputs**
(simultaneous, unlike the original's switch). ModularGrid adds: exact original
schematic, NOS Soviet ICs (КР140УД1208, КР140УД608), "gorgeously erratic
resonance," and smooth self-oscillation "from too-high-to-hear down to
infrasonic" thanks to the no-capacitor slew-limiting design
([ModularGrid mk1][ime-mg], [2013 version][ime-mg-2013], [Perfect Circuit][perfectcircuit-ime]).

### 3.2 Erica Synths (DIY Polivoks VCF, DIY VCF II, Black Polivoks VCF V1/V2)

Erica's line was built around stocks of NOS **K140UD12** chips. The Black Polivoks
VCF V2 blurb: "original programmable opamps … original Russian ICs K140UD12 in
the sound circuit"; V2 changes vs V1: "**eliminated clicks when switching filter
modes**," "added an output stage to **eliminate signal inversion**," "much
punchier bass sound" ([ModularGrid][erica-black-v2-mg]). The DIY versions publish full
schematics ([GitHub][erica-diy-repo], [product page][erica-diy-vcf2]) — see §0 for what the
schematic shows. Two module-relevant facts fall out: the original filter **inverts
the signal**, and the original LP/BP **mode switch clicks**. Erica's Acidbox III
desktop unit also carries the Polivoks VCF ([KVR VST thread][kvr-vst]).
(Note: Erica's **Fusion** series is tube-based and unrelated to the Polivoks —
the association sometimes made online is wrong.)

### 3.3 Elta Music (Polivoks Filter pedal, Filter-2, PF-3, PM-02, eurorack module)

Elta builds around "the classic Soviet filter chip" (UD1208) and extends the
original to **LP/BP/HP/notch** ([Elta Polivoks Filter][elta-filter], [Filter-2][elta-filter2],
[eurorack module][elta-module]). The stereo **PF-3** got the deepest review (Sound on
Sound, [review][sos-pf3]):

- Uses original Soviet **UD1208** chips; two independent filters (stereo).
- **Self-oscillation begins around halfway** on the resonance knob; onset is
  gradual — "it's as if the notion is being dragged out of them so they have no
  choice but to expel these oscillating tones"; self-osc tones are playable/tunable.
- **Hard/Soft modes** (matching the hard/soft resonance limiting in §0): Hard =
  "gritty, unstable tones when driven"; Soft loosens the character.
- Compared with the Erica Black Polivoks VCF: "the Erica likes to go into
  **distortion when the resonance is pushed**, whereas the PF-3 is more partial to
  **screaming feedback**"; the PF-3 is "more responsive and predictable."
- Quirk: needs a \~5-minute warm-up to avoid feedback misbehavior at minimum
  cutoff in Soft mode — the circuit is thermally drifty.

An electro-music.com review of the earlier Elta pedal stresses that it "requires
**overdriving the audio input** to perform optimally" and delivers "majestic
Polivoks roars" ([electro-music review][electro-music-elta]). Juno Daily on the PF-3: "famously
raw, aggressive and nasty in a way that's even more extreme than something like
the classic Korg MS-20 filter circuit," yet "a surprisingly versatile filter"
([Juno Daily][juno-pf3]). Elta also makes the PM-02 / Polivoks-M synth voice and the
2026 Polivoks-8 poly ([Elta][elta-p8]).

### 3.4 DIY / boutique

- **Marc Bareille's clone** (2003, with Allan J. Hall) — the page that seeded most
  later clones ([Bareille][bareille]). Sound report from direct A/B with a real
  Polivoks: "very very close." Behavior notes (all [Bareille][bareille]): clean sound at
  low Q; **self-oscillation starts with the Q pot at about 2/3**; "the resonance
  sound progresses with a **series of harmonic steps** when the Q pot value is
  increased … much accentuated if the filter is overloaded!"; at max Q "the sound
  suddenly becomes extremely **harsh**"; "less noisy than OTA based filters";
  input attenuation advised (\~−10 dB signals) "to avoid permanent overload" from
  hot modular VCOs; "frequency modulation on CV inputs works well with this
  filter"; "a bit wild at extreme Q, reminds a little bit the Korg MS20."
- **analog craftsman acVOKS** (5U format) ([analog craftsman][acvoks]).
- **Mutable Instruments Shruthi Polivoks board / Ambika Polivoks voicecard** —
  real analog Polivoks-style filters under digital control ([Shruthi archive][shruthi-polivoks],
  [ambika-polivoks][ambika-polivoks]).
- **Ritual Electronics** — the brief mentions it with a question mark; no
  Polivoks-derived Ritual Electronics product turned up in searches. Treat as
  not-a-thing unless better evidence appears.

ModWiggler threads comparing the clones: ["Ultimate Polivoks Filter in Euro?"][mw-ultimate]
and the long-running ["Polivoks filter" thread][mw-polivoks] (both currently behind an
anti-bot wall; consult manually if needed).

## 4. Sonic-signature checklist for a new emulation

Behaviors that recur across sources, roughly ordered by how load-bearing they are
for "sounding like a Polivoks":

1. **Two-pole (12 dB/oct) SVF-like response, LP and BP** — never steeper.
   BP is nasal/hollow and comes alive under modulation. ([SOS][sos-polivoks], [Vult][vult-vortex])
2. **Clean at low Q, feral at high Q.** Low-resonance behavior is a fairly polite
   "electronic clean sound"; character emerges with drive and resonance. ([Bareille][bareille])
3. **Input drive is half the sound.** The core overdrives easily; hot input =
   asymmetric-clipping harmonics and grit. Original expects \~−10 dB inputs, so a
   Drive/input-level control that pushes into saturation is essential.
   ([IME][ime-quickstart], [Bareille][bareille], [Atomika][atomika-secrets], [electro-music][electro-music-elta])
4. **Drive suppresses resonance** — higher input level chokes the resonant peak
   (natural compression between signal and self-osc). A static resonance boost
   independent of level is wrong. ([Atomika][atomika-secrets])
5. **Early, gradual, then sudden self-oscillation.** Self-osc starts \~2/3 of the
   resonance pot (PF-3: \~half), approached through **stepped harmonic buildup**
   ("series of harmonic steps," "dragged out of them"), then at max Q the sound
   "suddenly become[s] extremely harsh." ([Bareille][bareille], [SOS PF-3][sos-pf3])
6. **Self-oscillation is erratic and wide-range** — "gorgeously erratic
   resonance," oscillates from infrasonic (\~2 Hz) to ultrasonic (\~50 kHz), pitch
   unstable/drifty; hardware even needs warm-up. ([ModularGrid IME][ime-mg],
   [modularsynthesis][modularsynthesis], [SOS PF-3][sos-pf3])
7. **Resonance limiting flavor**: un-clamped feedback self-oscillates as a
   near rail-to-rail **square**; diode-clamped ("soft") version rings as a sine and
   sits slightly higher in frequency. Worth exposing as a character switch (Elta's
   Hard/Soft). ([modularsynthesis][modularsynthesis], [SOS PF-3][sos-pf3])
8. **Instability / chaos at high resonance**: "great instability at higher
   resonance values"; distortion-vs-feedback balance flips between "distortion when
   the resonance is pushed" (Erica) and "screaming feedback" (Elta); at precise
   input/resonance settings a **"bubbling / dripping-water" ring** appears.
   ([IME][ime-quickstart], [SOS PF-3][sos-pf3], [Atomika][atomika-secrets])
9. **Poor tracking**: stock CV response \~3.2×/V, not 1 V/oct; cutoff control is a
   linear programming current (down to fully closed at 0 µA). For a module: offer
   calibrated 1 V/oct but keep the drift/inaccuracy as character (or a "vintage"
   switch). ([modularsynthesis][modularsynthesis])
10. **Audio-rate FM sounds good** — the Iset path is fast; "frequency modulation on
    CV inputs work well with this filter." ([Bareille][bareille])
11. **Signal inversion and mode-switch clicks** in the original circuit (Erica V2
    removed both — decide deliberately whether to keep them). ([ModularGrid Erica V2][erica-black-v2-mg])
12. **Aggression partly lives after the filter**: "most of the aggression … comes
    from the overdriven VCA" — a post-filter saturation stage matters for the
    full cliché. ([Atomika][atomika-secrets])
13. **Low self-noise** relative to OTA filters — don't add hiss for "analog"
    flavor. ([Bareille][bareille])
14. Reference tone: "even more extreme than the MS-20"; "the dual saw bass is the
    meanest thing I've heard from an analog." ([Juno][juno-pf3], [KVR][kvr-analog-modeling])

### Modeling implications (synthesis of the above)

- Topology: two-integrator SVF loop; put the dominant nonlinearity on the
  **integrator inputs** (slew limiting ⇒ tanh/hard-clip of the integrator input,
  as in OTA/MS-20-style models) plus a clipper in the **resonance feedback path**
  (selectable diode-soft vs rail-hard). That combination naturally yields items
  4, 5, 7, 8.
- Slight **asymmetry** in the clippers (item 3; Atomika found asymmetric
  distortion) — the open-source attempts all got this wrong.
- Cutoff mapping: linear-in-current under the hood (gain-bandwidth ∝ Iset), with
  the panel/CV law layered on top; allow closing to \~0 Hz and opening well above
  Nyquist-safe range (oversample; self-osc to \~20 kHz+).
- Feedback gain must exceed the self-osc threshold well before max resonance
  (Vult/nanoTracker both do this; hardware self-osc at \~2/3 pot).
- Nobody in the open has modeled: resonance suppression by input level, the
  bubbling regime, the 3.2×/V tracking, or the square-wave hard self-osc. Those
  are the differentiators available to a new emulation.

[wikipedia]: https://en.wikipedia.org/wiki/Polivoks
[sos-polivoks]: https://www.soundonsound.com/reviews/formanta-polivoks-synthesizer
[bareille]: http://m.bareille.free.fr/modular1/vcf_polivoks/vcf_polivoks.htm
[kvr-analog-modeling]: https://www.kvraudio.com/forum/viewtopic.php?start=30&t=368351
[perfectcircuit-ime]: https://www.perfectcircuit.com/harvestman-polivoks-vcf.html
[modularsynthesis]: https://modularsynthesis.com/kuzmin/polivoks/polivoks_vcf.htm
[erica-diy-repo]: https://github.com/erica-synths/diy-eurorack
[nanotracker]: https://github.com/savannah-i-g/nanoTracker-Polivoks
[vibelang]: https://github.com/trusch/vibelang
[pdvoks]: https://github.com/oliverstotz/PD-VOKS
[antler]: https://github.com/antler-hat/antler-ms1
[spice]: https://github.com/richardjmarini/CircuitSimulations
[vult-compiler]: https://github.com/vult-dsp/vult
[l71]: https://github.com/L71/PolivoksVCF
[jbeuckm]: https://github.com/jbeuckm/Polivoks-VCF
[clarionut]: https://github.com/clarionut/Polivoks_filter
[ambika-polivoks]: https://github.com/float32/ambika-polivoks
[shruthi-polivoks]: https://pichenettes.github.io/mutable-instruments-diy-archive/shruthi/polivoks/
[vult-vortex]: https://modlfo.github.io/VultModules/vortex/
[vult-freak-firmware]: https://github.com/modlfo/VultFreakFirmware
[vult-mm]: https://github.com/vult-dsp/vult-metamodule-plugins
[atomika-secrets]: https://cherryaudio.com/news/secrets-of-atomika
[filtomika-docs]: https://docs.cherryaudio.com/cherry-audio/effects/filtomika/filter
[filtomika-review]: https://synthanatomy.com/2024/12/cherry-audio-filtomika-review-the-polivoks-synthesizer-filter-as-fx-plugin.html
[atomika-review-musicradar]: https://www.musicradar.com/music-tech/soft-synths/cherry-audio-atomika-review
[kvr-atomika]: https://www.kvraudio.com/forum/viewtopic.php?t=614524
[kvr-vst]: https://www.kvraudio.com/forum/viewtopic.php?t=597872
[kvr-monovoks]: https://www.kvraudio.com/forum/viewtopic.php?t=498206
[schematics-vault]: https://sites.google.com/site/schematicsvault/377296
[hideaway]: https://www.lootaudio.com/category/kontakt-instruments/hideaway-studios/polivox
[ime-quickstart]: https://ime-assets.s3.amazonaws.com/uploads/manual/manual/10/polivoksVCFmk2quickstart.pdf
[ime-mg]: https://modulargrid.net/e/industrial-music-electronics-polivoks-vcf
[ime-mg-2013]: https://modulargrid.net/e/industrial-music-electronics-polivoks-vcf-2013
[erica-black-v2-mg]: https://modulargrid.net/e/erica-synths-black-polivoks-vcf-v2
[erica-diy-vcf2]: https://www.ericasynths.lv/shop/legacy-products/diy-polivoks-vcf-ii/
[elta-filter]: https://www.eltamusic.com/polivoks-filter
[elta-filter2]: https://www.eltamusic.com/polivoks-filter-2
[elta-module]: https://www.eltamusic.com/polivoks-filter-module
[elta-p8]: https://www.eltamusic.com/polivoks-8
[sos-pf3]: https://www.soundonsound.com/reviews/elta-music-polyvox-pf-3
[juno-pf3]: https://www.juno.co.uk/junodaily/2023/01/20/elta-music-polyvox-pf-3-review/
[electro-music-elta]: https://electro-music.com/forum/topic-68788.html
[acvoks]: http://analogcraftsman.com/?product=acvolks
[mw-ultimate]: https://www.modwiggler.com/forum/viewtopic.php?t=257071
[mw-polivoks]: https://www.modwiggler.com/forum/viewtopic.php?t=86928

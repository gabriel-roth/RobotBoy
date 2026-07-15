# Yellowjacket — user-run checklist (2026-07-15)

Branch `worktree-wasp`. DSP is complete and test-covered (`tests/run.sh` green,
plus the Task 5 MetaModule headless-simulator WAV/spectrum/CPU pass — see
`tests/yellowjacket/mm-sim-notes.md`). These are the GUI/listening/hardware
checks only a human can do (repo convention: no agent-driven GUI or simulator
tests). Record results below, then merge/release.

Panel layout for reference: Res / Freq (hero) / Drive along the top, each over
its CV attenuverter + jack; stereo Audio L/R inputs in the middle; Blend knob
+ CV, and HP/BP/LP/Mix (L+R) outputs along the bottom. Right-click menu:
Character, Accuracy, Oversampling, Self-oscillation pitch, Input trim /
Output level / Inverter bandwidth, Panel theme.

## 1. VCV Rack listening

- [ ] **1.1 Tame vs Screaming character, several cutoffs** — patch a sawtooth
  or noise source into Audio L, LP (or Mix) to your ears/scope. Sweep Freq
  from \~80 Hz to \~5 kHz with Character = **Tame**: filter should sound gentle,
  slightly soft/rounded, with a whistly edge right at the verge of
  oscillation but never a runaway tone. Switch to **Screaming** at the same
  cutoffs: noticeably hotter/more aggressive OTA drive character, and
  **quieter** on LP — Screaming's self-oscillation mod costs it \~11 dB of
  raw LP passband, and the built-in makeup gain (×2.0) only partially
  compensates, so Screaming's LP sits \~5 dB softer than Tame's (Tame LP is
  unity/0 dB; Screaming LP is \~−5.4 dB). HP goes the other way and gets
  slightly louder switching to Screaming (Tame HP \~−1.4 dB, Screaming HP
  \~+3.2 dB, so \~+4.6 dB relative). This is deliberate — full loudness
  parity would need per-output gains that disturb the Mix output's notch
  shape — and the new **Output level** menu slider (±12 dB, applies to all
  outputs in both modes) is the tool for rebalancing to taste if the gap
  bothers you.

- [ ] **1.2 Verge-of-oscillation whistle (Tame)** — Character = Tame, Res
  near maximum (1.0), no input signal patched. You should hear/see a
  whistle or chirp that decays rather than sustains — Tame must never
  free-run indefinitely. Try a few cutoffs across the range; it should
  behave the same way throughout (this was the reason the Inverter
  bandwidth floor is 60 kHz — see §3 below if it ever sustains instead of
  decaying).

- [ ] **1.3 Resonance sweep into self-oscillation (Screaming)** — Character
  = Screaming, no input patched, sweep Res from 0 to 1. Somewhere in the
  upper range it should cross into a genuine sustained self-oscillation
  (a pure-ish tone that doesn't decay), amplitude bounded (not clipping
  harshly, not blowing up) — this is the rail-clamp doing its job. Confirm
  the pitch roughly tracks the Freq knob (see §1.6 for the precise-tracking
  option).

- [ ] **1.4 Drive behavior** — feed a clean sine or DI signal, sweep Drive
  0 → 1 (knob maps 1×–8× gain). Should go from clean to increasingly driven/
  saturated without harsh digital artifacts or NaNs (watch/listen for
  silence or clicks — should be smooth overdrive character, not broken audio).

- [ ] **1.5 Blend notch at noon** — patch a signal through, listen to Mix
  output, sweep Blend from 0 (pure LP) through 0.5 (noon) to 1 (pure HP).
  At noon you should hear a distinct notch/scoop relative to LP or HP alone
  (equal-weight LP+HP crossfade → −6 dB dip at the cutoff), not just a
  linear crossfade with no dip.

- [ ] **1.6 Self-oscillation pitch: Hardware vs Corrected** — Character =
  Screaming, Res near 1 (self-oscillating), no input. With the menu set to
  **Hardware (drifts flat)** (default), sweep Freq and note the pitch runs
  audibly flat of what 1 V/oct would predict as resonance approaches 1
  (documented drift is \~28% flat at the extreme). Switch to **Corrected
  (tracks knob)**: the same sweep should track 1 V/oct much more accurately.
  A tuner or a reference oscillator an octave apart is the easiest way to
  hear/see the difference.

- [ ] **1.7 Stereo** — patch different signals into Audio L and Audio R
  (e.g. two different oscillators), confirm L and R outputs are
  independently filtered (not just L duplicated). Then unpatch R and
  confirm R outputs mirror L exactly (normalling).

- [ ] **1.8 Polyphony** — patch a polyphonic source (e.g. a poly quantizer
  or poly VCO) into Audio L, confirm each output port carries the expected
  channel count and each voice is filtered independently (e.g. a poly chord
  keeps its distinct pitches through the filter, doesn't collapse to one
  voice).

- [ ] **1.9 CPU meter at each Oversampling setting** — enable Rack's
  per-module CPU meter, cycle the Oversampling menu through Auto / 1× / 2×
  / 4× (a few instances of Yellowjacket patched and playing audio). Confirm
  the ordering matches expectations (1× cheapest, 4× priciest, Auto ≈ 4× on
  a typical desktop sample rate) and that switching settings live doesn't
  glitch, drop out, or leave stale audio.

## 2. MetaModule (on real hardware)

- [ ] **2.1 Module loads** — Yellowjacket appears in the MetaModule module
  browser under RobotBoy, loads without error, both panel themes (Charcoal/
  Gold) display correctly.

- [ ] **2.2 MM menu fallbacks present** — open the module's options menu on
  hardware: Character, Accuracy, Oversampling, Self-oscillation pitch should
  all appear as before. Additionally confirm **Input trim**, **Output
  level**, and **Inverter bandwidth** — which are continuous sliders on
  VCV — show up as MM's discrete 5-step submenus (Input trim and Output
  level: −12/−6/0/+6/+12 dB; Inverter bandwidth: 60/80/120/200/300 kHz)
  rather than being missing entirely.

- [ ] **2.3 CPU headroom at 2× / 4×** — the Task 5 headless-simulator
  measurement was host-relative only (no real Cortex-A7 hardware was
  available to calibrate against) and landed inconclusively between \~25–75%
  of one MM core for 4×/stereo — straddling the usual 60% "too hot"
  threshold. On real hardware: load a patch with a couple of Yellowjacket
  instances (stereo, both channels patched — the more expensive path),
  check the MetaModule's CPU meter at Auto (capped to 2× by default on MM),
  then manually try 4× and note whether it's usable headroom-wise for a
  realistic patch. This is the one number in this task that genuinely needs
  real hardware to settle.

- [ ] **2.4 Knob/CV feel** — twist Freq/Res/Drive/Blend on the hardware
  encoders, patch CV into each CV input: motion should feel smooth (no
  zipper noise/steps from the \~2.5 ms modulate-rate + smoother design),
  matching the feel of MF-20's equivalent controls.

## 3. Tuning sliders, if something sounds off

If Tame ever sustains instead of decaying (§1.2), or Screaming won't cross
into self-oscillation where you'd expect, or the drive/character balance
feels off against a reference for the real hardware, two context-menu
sliders exist specifically as tuning aids (VCV: continuous sliders; MM:
5-step menus, see §2.2):

- **Input trim** (±12 dB, default 0 dB) — a fixed gain ahead of the filter,
  independent of the Drive knob. Use it to compensate if a patch runs the
  filter colder/hotter than the reference recordings without wanting to
  retune Drive's CV range.
- **Output level** (±12 dB, default 0 dB) — a fixed gain after the filter,
  applied to every output in both modes. This is the tool for closing (or
  widening) the Tame/Screaming LP loudness gap described in §1.1, or for
  general output trim; it does not touch the per-mode makeup constants or
  the Mix output's notch shape.
- **Inverter bandwidth** (60–300 kHz, default 80 kHz) — models the CMOS
  inverter's finite bandwidth, which is the mechanism that lets Screaming
  self-oscillate at all (see the DSP spec's Revision 1 section for why).
  Lower values push both modes toward self-oscillating more readily (but
  the floor is clamped at 60 kHz because a corrected sweep found Tame starts
  free-running — breaking its own "verge, never runaway" promise — below
  that); higher values make self-oscillation harder to reach. If Screaming
  feels reluctant to self-oscillate, or Tame ever free-runs unexpectedly,
  this is the first thing to nudge.

## Suggested patches

- **Basic sweep**: VCO (saw) → Yellowjacket Audio L, LFO → Freq CV
  (attenuverter partway up), listen on LP/BP/HP individually, then Mix while
  sweeping Blend.
- **Self-oscillation percussion**: Screaming, Res ≈ 1, short envelope into
  Freq CV for a plucked resonant "ping"; compare Hardware vs Corrected pitch
  tracking against a reference oscillator.
- **Stereo width**: two different oscillators into Audio L/R, Res moderate —
  note Freq/Res/Drive/Blend are shared per module instance (true stereo, one
  set of controls); for independent per-channel modulation, use two module
  instances panned L/R instead.
- **Poly chord filter**: poly quantizer/arpeggiator → Audio L, sweep Freq
  and Res together to hear the whole chord move as one filtered voice.

## Results

(record outcomes here)

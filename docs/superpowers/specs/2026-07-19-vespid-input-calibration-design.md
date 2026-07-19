# Vespid — Per-mode input staging: each Character at its hardware level

**Date:** 2026-07-19
**Status:** implemented (autonomous session — decisions recorded inline)
**Follows:** `2026-07-19-vespid-drive-remap-design.md` (2×–64× drive law)
**Sources:** `resources/wasp-sources/dafx-wasp.pdf`,
`resources/wasp-sources/gnat-schematics.pdf`,
`resources/wasp-sources/haible-wasp-clone.pdf`

## Problem

With a single global input staging, a ±5 V host signal drove both modes at
the same circuit voltage — but the two modeled units never saw the same
levels in hardware. Measured at fc=1500, ρ=0.225, 110 Hz sine (LP out):

| input | Tame THD | Screaming THD |
|---|---|---|
| 2.5 V | 12.5 % | 0.03 % |
| 5 V | 27 % | 0.03 % |
| 10 V | 37 % | 0.04 % |
| 12.5 V | — | 4.4 % |

At VCV levels Tame was permanently crushed (27–37 %) — "the same raspy
character at all drive settings" — because its authentic operating point
is ~4× lower than where the module ran it.

## Hardware anchors (research, routes 2+3)

- **Screaming = Doepfer A-124**: a Eurorack module. VCV ±5 V *is* its
  native signal; the input network (Rlevel pot + R1 into C1) has passband
  gain ≤ 1, max exactly 1 at full level (DAFx paper eq. 18). Anchor: **1:1**.
- **Tame = original EDP Wasp (+5 V)**: fed only by its own voice chain.
  The oscillators are logic chips (TTL/CMOS) swinging rail-to-rail — a
  0–5 V square, i.e. **±2.5 V after AC coupling** (Gnat schematics sheets
  5–6 show the same architecture: divider-chain squares straight to
  FILTER IN), through the same ≤ unity level network. Anchor: **0.5×** a
  VCV full-scale signal.
- The DAFx paper's own measurements used a unit-amplitude (~1 V) sweep on
  the +12 V build and warn to stay within measured ranges — consistent
  with, but less specific than, the schematic-derived anchors.
- Häible on the +5 V circuit: distortion "gradually increasing with input
  level, and you can slightly hear it way before the circuit actually
  clips" — matches Tame @2.5 V ≈ 12.5 % THD as the authentic light rasp
  (the "dirty Wasp" reputation), not the crushed 27 %+.

## Design (user direction: bake it in, keep Input trim as a ±tuning)

Per-mode `inGain` in `ModeConfig` (caller-applied, like `wcComp`):

```
driveTarget = 2 · 2^(5·d) · trimGain · mode.inGain
kScreaming.inGain = 1.0     kTame.inGain = 0.25
```

Drive-0 operating points (5 V host signal):

- **Screaming: 10 V eq.** — the approved knob staging from the drive
  remap (2× above Euro nominal, still clean at 0.04 % THD, clipping onset
  ~6 % up the knob). Unchanged.
- **Tame: 2.5 V eq. = exactly EDP nominal** — 12.5 % THD, the original's
  built-in grit. The 2× knob floor is *not* applied above Tame's nominal
  (0.25 = 0.5/2): adding it would overshoot the hardware's hottest
  possible level by 6 dB and land in the crushed zone (27 %), defeating
  the authenticity goal. Tame needs no dull-stretch fix — its onset sits
  below its own nominal.

Engine drive smoother seeds/reset: 2.0 → 0.5 (module default mode is Tame).

## Consequences

- Same knob positions in Tame are 12 dB quieter into the core and far
  cleaner than before; Character switch now also switches level staging
  (drive re-slews over ~5 ms).
- Input trim (kept, default 0 dB) tunes around the baked staging; −6 dB
  in Screaming reaches true Euro-nominal, +6 dB in Tame reaches the old
  pre-calibration voicing.
- Core untouched: `inGain` is metadata for the module layer; golden
  numbers and all prior tests unaffected. Staged operating points pinned
  by test 11 in tests/vespid/test_wasp_filter.cpp (Screaming @10 V < 1 %
  THD; Tame @2.5 V in 5–20 %).

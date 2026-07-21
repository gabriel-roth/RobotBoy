# Cold Digital as a Clouds Emulation — Research Notes

Research synthesis, 2026-07-21. Sources: Clouds firmware (`~/Dev/eurorack/clouds/`, with
stmlib at pinned commit e3bd7c9), Beads manual (pichenettes.github.io mirror of the
3/14/2021 PDF), MI documentation site hardware pages. Community readings are from
search-engine snippets of ModWiggler/lines threads (direct fetch blocked) — directional,
not verbatim-verified.

## The reference points

**Beads quality table (manual, verbatim figures):**

| Quality | Rate | Resolution | Buffer (stereo) |
|---|---|---|---|
| Bright digital | 48 kHz | 16-bit | 4 s |
| Cold digital | 32 kHz | 12-bit | 8 s |
| Sunny tape | 24 kHz | 12-bit | 10 s |
| Scorched cassette | 24 kHz | 8-bit | 16 s |

Manual: "The Cold digital setting most accurately reproduces the sonic character of the
late Mutable Instruments Clouds." And: "Each quality setting employs a different feedback
amplitude limiting scheme typical of the medium it emulates – from clean brickwall-limiting
to grungy tape saturation."

**Clouds hardware/engine (documentation site + firmware):** STM32F405 @ 168 MHz, WM8731
codec at 32 kHz (actually 32.003 kHz from PLL rounding), 16-bit codec words, float32
internal math, storage 16-bit linear (hi-fi) or 8-bit µ-law (lo-fi, at 16 kHz via 45-tap
polyphase 2× SRC).

Note the interesting asymmetry: Clouds' hi-fi storage was 16-bit, but Beads emulates it at
**12-bit**. Émilie evidently voiced Cold to match Clouds' *effective* real-world resolution
(WM8731 ADC/DAC noise floor, un-dithered hard-clipped storage, fixed-point FX lines), not
its nominal spec.

## What actually made "the Clouds sound" (firmware findings, file:line in the archaeology report)

Ranked by audibility for a delay/feedback context:

1. **32 kHz bandwidth ceiling** — everything tops out around 15 kHz; community describes
   Clouds as "a touch rolled off" next to Beads.
2. **Cubic soft-limit feedback with 1.4× drive** — `granular_processor.cc:197-203`:
   `in += fb_gain * (SoftLimit(fb_gain*1.4*fb + in) - in)`. The feedback contribution is
   boosted 1.4× *into* the limiter, so high feedback goes >1 loop gain and self-oscillates
   into a warm cubic wall — the community's "smudgy pads" at cranked feedback. Clouds
   overload is NOT a hard-clip buzz. `stmlib::SoftLimit` is the same rational polynomial
   `x(27+x²)/(27+9x²)` as our `FastTanh`.
3. **No anti-aliasing on any pitch-transposed read** — pure Hermite/linear resampling.
   ~75% of granular-mode grains use *linear* interpolation. (Looping-delay mode always
   reads Hermite, so for a delay the aliasing character comes mostly from the transposed
   reads of the loop pitch shifter, a 16-bit fixed dual-tap unit with triangular
   crossfade → warble/doubling.)
4. **Un-dithered hard 16-bit quantization** on every buffer write (`Clip16`, no dither —
   dithered mode exists in code but is never used).
5. **A gentle cubic soft-limit on every output sample** (`SoftConvert`: halve, SoftLimit,
   16-bit clip).
6. Feedback HP that rises with feedback (20 → 120 Hz), tone LP/HP in the delay loop with
   Q that *rises as feedback falls*, 12-bit reverb lines with 0.3/0.5 Hz LFO smear,
   diffuser (float, k=0.625) — module-level color, mostly outside a quality-mode's scope.
7. Quirks we deliberately skip: −3 dB dry at full-dry (constant-power crossfade never
   reaches unity), wet ×1.2 post-gain, 32.003 kHz PLL detune, WM8731 codec filters.

## Why our current Cold sounds like Bright

Our Cold today: int12 @ 24 kHz (decimation 2), 10 kHz 2-pole AA input LP, `SoftClip`
feedback limiter, **no write saturation** — so at high feedback its dominant nonlinearity
is the int12 codec's hard ±1 clamp, i.e. the same brickwall character as Bright, just
slightly duller and quieter. 12-bit granulation lives near −70 dB and is masked. There is
no per-mode overload character and no aliasing sheen (the 10 kHz LP does most of its
anti-alias job).

## Proposed Cold voicing (Clouds emulation)

1. **`SaturateWrite` Cold = `NormalizedSoftClip(x, 1.4f)`** (ceiling ≈ 0.714, unity
   small-signal slope). This is literally the Clouds limiter curve and drive constant,
   normalized to keep our uniform loop-gain law. High feedback now lands on a smudgy
   cubic wall — Clouds' overload — instead of the codec clamp. Distinct from Bright
   (hard brickwall) and from tape (asymmetric/deeper drive + darkening).
2. **Raise Cold's anti-alias input LP from 10 kHz to ≈11.5 kHz** (2-pole against the
   24 kHz storage rate's 12 kHz Nyquist). A 2-pole that close to Nyquist deliberately
   leaks fold-back on bright material — the un-anti-aliased "cold digital sheen" that is
   Clouds' signature imperfection. Also keeps Cold brighter than the tape modes, matching
   Beads' ordering (Cold 32k > Sunny/Scorched 24k).
3. **Keep int12 @ 24 kHz storage** — matches Beads Cold's 12-bit exactly; our 24 kHz (a
   host-rate-friendly 2× decimation) vs Beads' 32 kHz makes our Cold slightly darker than
   the reference. A fractional 3:2 resampler could hit 32 kHz but isn't worth the
   complexity; note as a known deviation.
4. **Keep `LimitFeedback` Cold = `SoftClip`** (already the Clouds curve family, unity
   slope).
5. **No hiss, no wow/flutter** in Cold — correct already; Clouds was digital.

Expected audible result at feedback \~75%: Bright = clean stacking into a bright hard
brickwall; Cold = repeats keep most of their brightness (just the top octave dulled),
gain a faint aliased sheen on bright sources, and overload into a warm, congested,
"smudgy" wall rather than a buzz. That matches both the Beads manual's framing and what
the community says Clouds did.

## Open questions / deviations to acknowledge

- 24 kHz vs the reference 32 kHz storage rate (host-rate constraint; slightly darker).
- We do not emulate the always-on output SoftConvert, dry-path −3 dB, diffuser, or reverb
  quantization — those are Clouds module-level traits, out of scope for a quality mode.
- Beads Cold reportedly still doesn't fully nail Clouds to some ears (forum consensus,
  snippet-verified only) — perfection is not the bar; a *distinct, historically grounded*
  Cold is.

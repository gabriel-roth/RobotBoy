# Retours Doppler slew — user smoke tests (listening checks Claude can't run)

Context: defaults changed 2026-07-21 (slew 0.08 → 0.3 s, random LFO 0.15 →
0.1 Hz). The random LFO slider is gone for good; the **Doppler slew** slider
was restored (0.01–1 s, log taper, default 0.300 s) so the new default can be
judged against faster/slower settings before it's frozen in.

Build installed to the Rack2 plugins dir on 2026-07-21 — **restart VCV Rack**
before starting.

## Setup

- Patch a recognizable rhythmic source (drum loop or a short melodic
  sequence) into IN L, listen to OUT L/R, Dry/wet around 70% wet.
- Right-click menu → **Time change response: Tape (doppler)** (the default).
- The slew slider only matters in Tape mode; item 9 checks Crossfade is
  immune.

## 1. Slider basics

- [ ] Menu shows two sliders: Input trim, then **Doppler slew** reading
      0.300 s on a fresh module (Random LFO rate is gone).
- [ ] Drag feels log-tapered: the left half covers the snappy 0.01–0.1 s
      range, the right half 0.1–1 s.
- [ ] Save the patch with slew at \~0.05 s, reload — value survives.
- [ ] Initialize (Ctrl/Cmd-I) resets it to 0.300 s.
- [ ] A patch saved before today (no slewSeconds key) loads at 0.300 s, not
      0.08 s.

## 2. TIME knob (the original context)

- [ ] Slew 0.300 s: sweep TIME between extremes — a deliberate, audible tape
      varispeed bend into the new delay time.
- [ ] Slew 0.010 s: same move is near-instant, only a blip of bend.
- [ ] Slew 1.000 s: a long, dramatic pitch dive/rise, a couple of seconds to
      settle.

## 3. INTERVAL knob

- [ ] Turning INTERVAL (free-running, no clock) re-targets the delay time and
      glides the same way; bend length tracks the slider like TIME does.

## 4. Clock / tap tempo

- [ ] Tap a tempo on CLOCK, then tap a clearly slower one — the repeats bend
      down into the new tempo over roughly the slew time.
- [ ] While clocked, turning INTERVAL through divisions (1/2, 1/4, triplets)
      glides between musical subdivisions rather than snapping.

## 5. Quality switch (long-distance glide)

- [ ] With slew at 0.300 s, switch Quality from Bright digital to Scorched
      cassette while audio runs: the buffer duration changes \~8×, so expect
      \~2 s of pitch-swept travel before the echo settles. Judge whether this
      is characterful or too much — this is the main casualty of the slower
      default.
- [ ] Same switch with slew at 0.010 s settles almost immediately.

## 6. Mono transition

- [ ] Unplug the R input mid-play (with something patched into it first):
      the mono reconfiguration triggers the same long glide as a quality
      switch. Listen for the settle time scaling with the slider.

## 7. Unfreeze

- [ ] Latch SLICE, let the frozen slice loop a few times, then release: the
      playhead glides from the frozen position back to the live delay time —
      longer, more audible bend at 0.300/1.000 s than at 0.010 s.

## 8. Feedback trails

- [ ] Feedback \~70%, move TIME while trails ring: every repeat bends
      smoothly with the head move — no clicks, no zipper noise, at all three
      slider positions.

## 9. Crossfade mode is immune (control)

- [ ] Switch Time change response to **Crossfade**, repeat the TIME moves at
      slider 0.010 s and 1.000 s: identical behavior both times — a clean
      \~20 ms crossfade jump, no pitch bend, slider has no effect.

## 10. Continuous CV

- [ ] Slow triangle LFO into TIME CV (trimpot up): continuous doppler wobble.
      The slider changes its character — snappy tracking at 0.010 s, lazy,
      smeared vibrato at 1.000 s.

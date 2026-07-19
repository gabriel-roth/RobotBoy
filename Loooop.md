# Loooop

**Loooop** is a stereo, RAM-based looper for [VCV Rack](https://vcvrack.com) and the [4ms MetaModule](https://4mscompany.com/metamodule). You feed it audio, capture a loop, and then play that loop back with **four independent playheads,** each with its own speed, position, window size, level, jitter, and stereo pan. It's inspired by [Cutlasses Gloop](https://www.cutlasses.co.uk/product/gloop/). One recorded phrase can become four different loops running at four different speeds and lengths, all layered into a single stereo mix.

<img src="screenshots/Loooop.png" alt="Loooop module" height="500">

A smaller single-playhead version, **Löp**, is described [at the end](#löp) of this document.

---

## Quick start

1. **Patch some audio** into the **In L / In R** jacks at the bottom left. (Patch just one for mono — it feeds both sides.)
2. **Click Record**. The light turns on and Loooop starts capturing.
3. **Click Record again** to stop. The loop is now frozen and immediately starts playing back through all four heads. Monitor the output from **Mix L / Mix R** (bottom right).
4. Adjust the *Size* knobs, then experiment with *Position* and *Speed.*

> Loops can be up to **60 seconds** long. If you keep recording past that, Loooop stops on its own.

---

## The display

The strip across the top shows the recorded audio as a waveform. While you record, it fills in left-to-right. Once you have a loop, you'll see up to four colored **playhead markers** sweeping across it, along with a shaded band showing each head's *window* (the slice of the loop that head plays). This is the easiest way to see what Position, Size, and Speed are actually doing.

---

## The four playheads

All four heads read from the **same recorded loop**, but each has its own controls. At a given time, you might hear:

- Head 1 playing the whole loop forward at normal speed,
- Head 2 playing a short slice near the end, sped up,
- Head 3 running a chunk of the loop backward,
- Head 4 wandering randomly around the middle.

All four are summed into the **Mix** output, each positioned in the stereo field by its own **Pan** knob. Each head also has its own outputs for independent processing.

### Per-head knobs

Each head has these controls, laid out in its own column:

* **Size**: *How much* of the loop this head plays, from a tiny grain up to the whole thing. Small Size + moving Position = scrubbing/granular textures.
* **Position**: *Where* in the loop the head plays — the center of its window. Sweep it to scrub through the recording.
* **Speed**: Playback rate. `1` = normal, `2` = double speed / up an octave, `0` = frozen. Turn it **below zero to play backward**.
* **Jitter**: Random positioning. Turn it up from 0 and the head jumps to a different spot in the loop each time it repeats.
* **Pan**: Where this head sits in the stereo **Mix**. It only affects the Mix outputs; the head's own **Out L / R** jacks are unchanged.
* **Level**: This head's volume in the mix outputs. (Defaults are set so all four heads at once add up to unity gain.)

> **Note that Position and Jitter need room to move.** At the **default Size (fully clockwise = the whole loop)**, a head's window already fills the entire recording, so there is nowhere for it to slide. **Position and Jitter will seem to do nothing until you turn Size down.** Once Size is below maximum, Position slides the window through the loop and Jitter randomizes where that window starts on each repeat. (To jump the *playhead* around inside a full-size window instead, use the **Jump** input — see below.)

---

## Recording controls

The bottom row holds the global controls, left to right:

- **Record** — Starts and stops recording. The first press records a fresh loop. Once a loop exists, pressing Record again **overdubs** — what happens to the audio already in the loop depends on the **Overdub** button. The red light shows when Loooop is recording.
- **Overdub** — A five-way button that selects one of the overdub modes:
    - **Layer** (blue, the default) — sound-on-sound: each overdub pass turns the existing material down slightly (about 1 dB per pass), so older layers gradually sink underneath more recent layers.
    - **Decay** (orange) — like Layer, but older layers also lose a little high end on every pass, giving the effect of tape-style degradation.
    - **Add** (green) — new audio sums with what's there at full level, forever, so you'll get clipping and distortion after a while.
    - **Replace** (red) — new audio **overwrites** the loop as the record head passes. Use this to punch in a new phrase over an old one.
    - **Lock** (flashing magenta) — overdubbing off. The Record button does nothing while a loop exists — the loop is untouchable until you **Clear** it or switch to a different mode. The light flashes to signal that the loop is held.
- **Clear** — Erases the loop.
- **Grid** — Off (the default), 4, 8, 16, 32, or 64. When set, the loop is divided into that many equal segments, shown as vertical bars on the display, and every head's window snaps to them: **Size** becomes a whole number of segments and **Position** (including CV and **Jitter** offsets) lands on segment boundaries. Record a drum loop, set Grid to 16, and heads slice it cleanly on the beat.
- **Dry/Wet** — Sets the blend between incoming audio (dry) and loop playback (wet) at the Mix output. Fully clockwise = loop only.

> **On MetaModule**, Overdub and Grid live in the options list (scroll down past the jacks) instead of on the panel, and Overdub is split into two settings: **Overdub** (On/Off — Off is Lock) and **Write mode** (Add / Replace / Layer / Decay).

---

## Jacks and CV

Nearly every knob has an associated **CV input.** Positive or negative CV is added to the knob setting.

**Global jacks (bottom row):**

- **In L / In R** — audio in. Patch one for mono; it feeds both channels.
- **Record trigger** / **Clear trigger** — a trigger here does the same as clicking the Record / Clear buttons.
- **Mix L / Mix R** — the combined stereo output of all heads plus the dry/wet blend.

**Per-head jacks:**

- **Speed / Position / Size / Level / Jitter / Pan CV** — modulate that head's knob. (Pan is bipolar: about −5 V pushes hard left, +5 V hard right, centered at 0 V.)
- **Out L / Out R** — that head's isolated stereo output.
- **Trig** — retriggers the head (see below).
- **Jump** — a control voltage that snaps the playhead to a spot within its window (0 V = start of window, 10 V = end). Great for stutter and glitch effects driven by an LFO or sequencer.

### Trigger jacks and one-shot mode

By default, a trigger at a head's **Trig** input restarts that head at the beginning of its window — use it to sync heads to a clock.

Checking a playhead under **One-shot on trigger** in the context menu changes that: the head stays silent until triggered, then plays **through its window exactly once and stops**, ending with a short fade so it doesn't click. This turns a head into a sample-triggered one-shot player. While a one-shot head is waiting for a trigger but not playing, its lane on the display **dims.** If a playhead ever seems dead, it's probably in one-shot mode.

---

## Context menu

You can find these options by right-clicking the panel in VCV Rack or scrolling down to Options in MetaModule:

- **Trigger when recording** — sets what a second Record command (button *or* the **Record trigger** input) does while Loooop is still recording its first loop. **Stops recording** (the default) freezes the loop and starts playback — the normal behavior. **Starts overdubbing** instead freezes the loop length at that moment and rolls straight on into an overdub pass, so a single clock or trigger stream can capture a loop and keep layering it without a second press. (In **Lock** overdub mode, overdubbing is off, so it always just stops.)
- **Exclude from Grid** — lets a given playhead move freely even while **Grid** is on. Good for one drifting, texture-making playhead over an otherwise beat-sliced loop.
- **One-shot on trigger** — check a playhead to put it in one-shot mode (see above). Unchecked (the default), a trigger just restarts the playhead at the start of its window.
- **Speed CV = V/Oct** — makes a playhead's Speed CV input track **1 volt per octave**, so you can play the loop chromatically from a keyboard or sequencer (speed then ranges much wider, up to ±16×).
- **Crossfade** — When on (the default), Loooop applies a tiny (\~5 ms) fade at each loop's wrap-around point to hide clicks, and gives **one-shot** passes a matching fade-out at the end instead of a hard stop. Turn it off if you want the raw, seam-exact repeat (useful for rhythmic clicks or very short grains).

---

## Patch ideas

- **Instant harmonizer:** record a sustained note, then set the four heads to different Speeds (e.g. 1, 1.5, 2, and −1) for chords and shimmer.
- **Granular cloud:** shrink every head's **Size** right down, crank **Jitter**, and slowly sweep **Position**.
- **Stutter machine:** patch an LFO or sequencer into a head's **Jump** input.
- **Wide moving field:** spread the four heads across the stereo image with their **Pan** knobs, or patch slow, phase-offset LFOs into the Pan CVs so they drift around each other.
- **Rhythmic re-slicer:** record a drum loop and set each head to **One-shot** with a small window. Trigger them from a sequencer to rearrange the beat, and turn on **Grid** so every slice lands exactly on a division.
- **Separate processing:** take each head's own **Out L/R** into different reverbs, filters, or panners instead of using the combined Mix. Turn **Level** all the way CCW to remove a playhead from the Mix. 

---

## Löp

**Löp** is Loooop with a **single playhead** instead of four. It works exactly like one head of Loooop.

<img src="screenshots/Lop.png" alt="Löp module" height="500">

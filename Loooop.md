# Loooop

**Loooop** is a stereo, RAM-based looper for [VCV Rack](https://vcvrack.com) and the [4ms MetaModule](https://4mscompany.com/metamodule). You feed it audio, capture a loop, and then play that loop back through **four independent playheads** — each with its own speed, position, window size, level, jitter, and stereo pan. It's inspired by [Cutlasses Gloop](https://www.cutlasses.co.uk/product/gloop/).

Think of it as a multi-head tape playground: one recorded phrase can become four different loops running at four different speeds and lengths, all layered into a single stereo mix.

<img src="screenshots/Loooop.png" alt="Loooop module" height="500">

A smaller single-playhead version, **Löp**, is described [at the end](#löp).

---

## Quick start

1. **Patch some audio** into the **In L / In R** jacks at the bottom left. (Patch just one for mono — it feeds both sides.)
2. **Click Record**. The light turns on and Loooop starts capturing.
3. **Click Record again** to stop. The loop is now frozen and immediately starts playing back through all four heads.
4. Turn **Dry/Wet** all the way toward *Wet* if you only want to hear the loop, and monitor the output from **Mix L / Mix R** (bottom right).

> Loops can be up to **60 seconds** long. If you keep recording past that, Loooop stops on its own.

---

## The display

The strip across the top shows the recorded audio as a waveform. While you record, it fills in left-to-right. Once you have a loop, you'll see up to four colored **playhead markers** sweeping across it — **red, green, blue, and yellow**, one per head; the context menu refers to the playheads by these colors — along with a shaded band showing each head's *window* (the slice of the loop that head is allowed to play). This is the easiest way to see what Position, Size, and Speed are actually doing.

---

## The four playheads

Every head reads from the **same recorded loop**, but each has its own set of controls. At a given time, you might hear:

- Head 1 playing the whole loop forward at normal speed,
- Head 2 playing a short slice near the end, sped up,
- Head 3 running the loop backward and quietly,
- Head 4 wandering randomly around the middle.

All four are summed into the **Mix** output, each positioned in the stereo field by its own **Pan** knob. Each head also has its own pair of outputs if you want to process them separately.

### Per-head knobs

Each head has these controls, laid out in its own column:

* **Speed**: Playback rate. `1` = normal, `2` = double speed / up an octave, `0` = frozen. Turn it **below zero to play backward**.
* **Position**: *Where* in the loop this head plays — the center of its window. Sweep it to scrub through the recording.
* **Size**: *How much* of the loop this head plays, from a tiny grain up to the whole thing. Small Size + moving Position = scrubbing/granular textures.
* **Level**: This head's volume in the mix. (Defaults are set so all four heads at once add up to unity gain.)
* **Jitter**: Randomness. At `0` the window stays put; turn it up and the head jumps to a new random spot in the loop each time it repeats.
* **Pan**: Where this head sits in the stereo **Mix**. It only affects the Mix outputs; the head's own **Out L / R** jacks are unchanged.

*Position* and *Size* work together: **Size** sets the length of the slice, **Position** slides that slice around inside the loop. A head only ever loops within its own window.

> **Note that Position and Jitter need room to move.** At the **default Size (fully clockwise = the whole loop)**, a head's window already fills the entire recording, so there is nowhere for it to slide. **Position and Jitter will seem to do nothing until you turn Size down.** Once Size is below maximum, Position slides the window through the loop and Jitter randomizes where that window starts on each repeat. (To jump the *playhead* around inside a full-size window instead, use the per-head **Jump** input.)

---

## Recording controls

The bottom row holds the global controls, left to right:

- **Record** — Starts and stops recording. The first press records a fresh loop. Once a loop exists, pressing Record again **overdubs** — what happens to the audio already in the loop depends on the **Overdub** button (next). Punch-in and punch-out are softened with a \~5 ms ramp, so starting and stopping an overdub doesn't record a click into the loop. The red light shows when Loooop is recording.
- **Overdub** — A five-way button that sets what an overdub pass does to the audio already in the loop. Click it to cycle through the modes; the LED color shows the current one:
  - **Layer** (blue, the default) — sound-on-sound: each overdub pass turns the existing material down slightly (about 1 dB per pass), so older layers gradually sink underneath what you're playing now.
  - **Decay** (amber) — like Layer, but older layers also lose a little high end on every pass — tape-style degradation the longer you keep overdubbing.
  - **Add** (green) — new audio sums with what's there at full level, forever. The classic build-up-layers overdub.
  - **Replace** (red) — new audio **overwrites** the loop as the record head passes. Punch in a new phrase over an old one.
  - **Lock** (purple) — overdubbing off. The Record button does nothing while a loop exists — the loop is untouchable until you **Clear** it or leave Lock.

  Layer and Decay only act **while you're recording** — a loop that's just playing back never fades on its own.
- **Clear** — Erases the loop and starts over.
- **Grid** — Off (the default), 4, 8, 16, 32, or 64. When set, the loop is divided into that many equal segments, shown as vertical bars on the display, and every head's window snaps to them: **Size** becomes a whole number of segments and **Position** (including CV and **Jitter** offsets) lands on segment boundaries. Record a drum loop, set Grid to 16, and heads slice it cleanly on the beat.
- **Dry/Wet** — Global blend between your incoming audio (dry) and the loop playback (wet) at the Mix output. Fully clockwise = loop only.

> **On MetaModule**, Overdub and Grid live in the options list (scroll down past the jacks) instead of on the panel, and Overdub is split into two settings: **Overdub** (On/Off — Off is Lock) and **Write mode** (Add / Replace / Layer / Decay).

---

## Jacks and CV

Nearly every knob has a **CV input** right below or beside it, and a full ±10 V sweep on a CV input covers that knob's entire range (the CV is added to the knob setting).

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

By default, a trigger at a head's **Trig** input simply restarts that head at the beginning of its window — use it to re-sync heads to a clock.

Checking a playhead under **One-shot** in the context menu changes that: the head stays silent until triggered, then plays **through its window exactly once and stops**, ending with a short fade so it doesn't click. This turns a head into a sample-triggered one-shot player.

---

## Context menu

You can find these options by right-clicking the panel in VCV Rack or scrolling down to Options in MetaModule. The per-playhead options name each playhead after its marker color on the display: the **Red**, **Green**, **Blue**, and **Yellow** playheads.

- **Crossfade loop seams** — When on (the default), Loooop applies a tiny (\~5 ms) fade at each loop's wrap-around point to hide clicks, and gives **one-shot** passes a matching fade-out at the end instead of a hard stop. Turn it off if you want the raw, seam-exact repeat (useful for rhythmic clicks or very short grains).
- **One-shot → (playhead)** — check a playhead to put it in one-shot mode (see above). Unchecked (the default), a trigger just restarts the playhead at the start of its window.
- **Speed CV is V/Oct → (playhead)** — makes that playhead's Speed CV input track **1 volt per octave**, so you can play the loop chromatically from a keyboard or sequencer (speed then ranges much wider, up to ±16×).
- **Exclude from Grid → (playhead)** — lets that playhead move freely even while **Grid** is on: its Size, Position, and Jitter stop snapping to segment boundaries, while the other playheads stay locked to the grid. Good for one drifting, texture-making playhead over an otherwise beat-sliced loop.

> **On MetaModule** the per-playhead options appear in the options list numbered rather than by color — 1 = Red, 2 = Green, 3 = Blue, 4 = Yellow (e.g. "Grid 1 exclude" is the Red playhead's Exclude from Grid) — and one-shot mode is a two-choice "Trig N mode" setting (*Loop start* / *One-shot*).

---

## Patch ideas

- **Instant harmonizer:** record a sustained note, then set the four heads to different Speeds (e.g. 1, 1.5, 2, and −1) for chords and shimmer.
- **Granular cloud:** shrink every head's **Size** right down, crank **Jitter**, and slowly sweep **Position**.
- **Stutter machine:** patch an LFO or sequencer into a head's **Jump** input.
- **Wide moving field:** spread the four heads across the stereo image with their **Pan** knobs, or patch slow, phase-offset LFOs into the Pan CVs so they drift around each other.
- **Rhythmic re-slicer:** record a drum loop, set each head to **One-shot** with a small window, and trigger them from a sequencer to rearrange the beat, and turn on **Grid** so every slice lands exactly on a division.
- **Separate processing:** take each head's own **Out L/R** into different reverbs, filters, or panners instead of using the combined Mix.

---

## Löp

**Löp** is Loooop with a **single playhead** instead of four. It works exactly like one head of Loooop — including the note above: turn **Size** down before **Position** and **Jitter** have anything to do. Löp has no Overdub button or Grid knob on its panel; instead its context menu carries **Overdub** (On/Off), **Write mode**, **Grid**, and **Crossfade loop seams**, plus its playhead's **Trigger** mode (*Loop start* / *One-shot*) and **Speed CV is V/Oct** — all with the same behavior as Loooop's.

<img src="screenshots/Lop.png" alt="Löp module" height="500">

# Retours

**Retours** is a stereo delay and beat-slicer for [VCV Rack](https://vcvrack.com) and the [4ms MetaModule](https://4mscompany.com/metamodule). Its repeat time runs from several seconds down to audio rates, so it can sound like a rhythmic delay, a slapback echo, or a plucked resonator. Along the way it can freeze and loop a slice of what it just heard, transpose every repeat, and reshape the echoes in time with a clock. Retours is the delay mode hidden inside Mutable Instruments Beads, and runs on Beads' own DSP code.

<img src="screenshots/Retours.png" alt="Retours module" height="500">

---

## How it works

Retours is always recording your input onto a short loop of tape. A playback head follows behind the record head at a distance you set, and what it reads is your delayed signal — move it further back for a longer echo.

Feedback sends the delayed signal back to be recorded again, so it repeats and decays. The playback heads are **pitch shifters**, so each repeat can come back higher or lower than the last. A tempo-synced **envelope** can chop or swell each repeat. **Slice** stops the tape entirely to loop one frozen recording.

Like Particules, Retours records through one of four **Quality** characters — from clean digital to grungy 8-bit cassette — and the feedback path picks up that same character, so dirt compounds with feedback.

---

## Quick start

1. **Patch audio** into **In** (bottom left). Patch both jacks for stereo; patch just the left and it feeds both sides.
2. **Turn up Feedback** for more than one echo.
3. **Set the delay time with Interval.** Straight up (12 o'clock) gives the longest delay. Turn it either way to shorten the echo; turn it a long way and the repeats blur together into a pitched tone. Turn left for a single delay tap; turn right for an additional tap.
4. **Take your output** from **Out** (bottom right). If you only patch the left output, you get a mono sum of both channels.

---

## Controls

### Delay time

*Interval:* The base spacing between repeats. **Straight up (12 o'clock) is the longest delay** — the whole recording buffer (about 4 seconds in the cleanest Quality mode). Turn **counter-clockwise** to shorten it as a single delay tap; turn **clockwise** to shorten it into a busier, multi-tapped pattern. Go far enough in either direction and the interval drops into audio rates (down to about 1.5 ms): the repeats fuse into a pitch and Retours becomes a Karplus-Strong-style string, playable in volts-per-octave from **Interval CV.** (Near noon, Interval CV simply turns the knob; toward either end it becomes a 1 V/octave pitch control.)

* When a clock is patched or you set a tap tempo, Interval becomes a **musical divider** of the clock tempo: straight up is one repeat per beat, counter-clockwise gives 1/2, 1/4, 1/8 and 1/16, and clockwise gives 1/2, 1/3, 1/4, 1/6, 1/8, 1/10, 1/12 and 1/16, crossfading between neighbouring divisions as you turn.

*Clock:* A **tap-tempo** button. As on Beads, the first press switches Retours from free-running to tempo mode; after that, each tap sets the tempo from the time since the previous one (taps more than 4 seconds apart are ignored). A patched clock sets the tempo in the same way and tolerates a little jitter. A tempo, once set, holds even after you unpatch the clock; to return to free-running, use **Clear saved tempo** in the context menu. Tempo changes crossfade between the old and new delay times rather than gliding.

* The light under Interval flashes at the start of each repeat.

*Time:* A multiplier on top of Interval, from 1× up to 16×. Use Interval to set the ballpark and Time to stretch it out. Time steps through whole multiples, crossfading from one to the next. When Slice is engaged, Time chooses which slice of the buffer loops (see Slice below).

### Repeat character

*Pitch:* Transposes the repeats, −24 to +24 semitones; with feedback, each repeat is transposed again. The knob has flat spots at unison, 1, 3, 4 and 7 semitones, the octave and two octaves, in both directions.

*Shape:* A tempo-synced amplitude envelope applied to each repeat. Fully counter-clockwise (off) leaves the echoes untouched. Turning it up morphs the envelope through three stages: first a percussive **decay** that cuts each repeat's tail, then a symmetric **swell,** and finally a **slow ramp** up to the next repeat.

*Feedback:* How much of the delayed signal is fed back to be recorded again. The small arrow at about **82% of the knob's travel** marks unity gain; past it, the echoes build up until the limiter holds them.

### Slice

*Slice:* Stops recording and loops a frozen slice of the buffer — an instant beat-slicer / stutter. The button latches; the **Slice** gate input (top left) does the same when it receives CV above 1V. While Slice is held, **Time selects which slice** plays and **Interval sets how long each slice is**, so you can step through a recorded phrase or lock onto one chopped fragment.

### Mixing

*Dry/Wet:* Balance between your untouched input (dry) and the delayed output (wet).

Feedback, Dry/Wet, Time, Pitch, and Shape each have their own **CV input** and a small trimpot beside it — see the note on the trimpots below.

### Quality

The **Quality** button (top center, with the multicolor LED) cycles through four recording characters — the same four as Particules. Each changes the recording sample rate, bit depth, and buffer length. Because the feedback loop re-records through this stage on every pass, the lower-quality modes get dirtier the longer the echoes last. Retours converts to and from your engine's sample rate, so these figures hold whatever rate VCV or MetaModule runs at:

- ***Bright digital*** (white LED) — 48 kHz, 16-bit. Cleanest and brightest. 4-second buffer.
- ***Cold digital*** (cyan LED) — 32 kHz, 12-bit, gently saturated feedback. 8-second buffer.
- ***Sunny tape*** (amber LED) — 24 kHz, 12-bit, gently saturated feedback, and soft saturation on the input when the gain is set manually. 10.7-second buffer.
- ***Scorched cassette*** (magenta LED) — 24 kHz, 8-bit µ-law, heavier feedback saturation (and input saturation when the gain is set manually), wow and flutter. Crunchy lo-fi. 16-second buffer.

All buffer lengths double when the input is mono (only one input patched): 8, 16, 21 and 32 seconds respectively. Going between stereo (both inputs patched) and mono, or changing the Quality setting, re-formats the recording buffer, clearing recorded audio and muting the output for a quarter of a second. While Slice is engaged, the Quality button refuses to cycle; choosing a quality from the context menu (or the MetaModule switch) changes the selection, but the buffer reformat waits until Slice releases — either way the frozen slice is protected.

### The trimpots (Time · Pitch · Shape)

Below **Time, Pitch,** and **Shape** sits a small trimpot that does one of two things, exactly as on Particules:

- **With a CV cable patched** into that parameter's input: it's an attenuator for the CV (there's no inverting range — the counter-clockwise half does something else). From center, turn **clockwise for more external modulation**; turn **counter-clockwise to instead spread the value randomly** around the knob position, scaled by the CV.
- **With nothing patched:** it sets how much that parameter drifts on its own from an internal slow-random source. Center is no movement. Counter-clockwise gives a *peaky* wander (mostly near the knob setting, extremes rare); clockwise gives a wide, *uniform* wander.

The **Feedback** and **Dry/Wet** trimpots are plain attenuverters for their CV inputs, not randomizers.

## I/O

- **In (L / R)** — audio in. Patch just L for mono; patch both for stereo.
- **Out (L / R)** — stereo output. Leave **Out R** unpatched and both channels sum to **Out L**.
- **Slice** — gate input, above 1 V; same as the button.
- **Clock** — clock / trigger input for tempo sync.
- **Time / Pitch / Shape CV** — scaled by their trimpots (above).
- **Interval / Feedback / Dry-Wet CV** — modulation for those controls.

---

## Context menu

Open the context menu on the panel in VCV Rack, or open Options on MetaModule:

- **Quality** (VCV Rack only; on MetaModule, use the Quality switch) — the same four recording characters as the front-panel button (**Bright digital**, **Cold digital**, **Sunny tape**, **Scorched cassette**).
- **Auto gain / Manual gain / Input** — Retours sets its input gain the way Beads does, and the way Particules does; see [Particules' context menu](Particules.md#context-menu-options) for details.
- **Clear saved tempo** — abandons a held tempo (from a tap or a patched clock) and returns Retours to free-running, so Interval sets the delay time directly again. Greyed out when no tempo is saved, and while a clock is patched (it would set the tempo again).
- **Clear buffer** — empties the recording buffer immediately. Greyed out when the buffer is already empty.

---

## Patch ideas

- **Karplus-Strong string:** turn Interval counter-clockwise into audio-rate territory for a clean single-tap voice — stop short of the floor, so a V/oct sequence has a couple of octaves of headroom before it clamps — set Feedback high, and send a V/oct pitch sequence into **Interval CV**: the delay length becomes the string's pitch. Feed it short bursts of noise to pluck it: each burst rings out as a decaying string.
- **Spiralling echoes:** set a moderate delay and feedback, then nudge **Pitch** off its center detent. Every repeat climbs (or falls) a step, so a single note becomes an endless ascending shimmer.
- **Beat slicer:** play a drum loop in, patch its clock into **Clock**, turn Feedback down and Dry/Wet up, and gate **Slice** from your sequencer. While sliced, **Interval** sets the slice length in beat divisions and **Time** picks which slice plays — sequence **Time CV** with stepped random to rearrange the loop in tempo.
- **Gated rhythmic delay:** clock Retours, then turn up **Shape** to gate each repeat — the echoes become a rhythmic pattern locked to your tempo.
- **Runaway tape:** choose **Scorched cassette**, push Feedback past unity, and ride Dry/Wet — the loop degrades a little more on every pass into a collapsing wall of lo-fi noise.
- **Dive-bomb sweeps:** with nothing patched into Clock, sweep **Interval** by hand or with a slow LFO for pitch-bending delay swoops.

___
_Retours runs on the source code of [Mutable Instruments Beads](https://github.com/pichenettes/eurorack/tree/master/beads), designed and written by Émilie Gillet and released under the MIT licence._

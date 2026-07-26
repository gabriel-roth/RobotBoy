# Retours

**Retours** is a delay and beat-slicer for [VCV Rack](https://vcvrack.com) and the [4ms MetaModule](https://4mscompany.com/metamodule). At its simplest it's a stereo echo, but its repeat time runs from several seconds down to audio rates, so the same knob that sets a slapback also turns the delay into a plucked, pitched resonator. Along the way it can freeze and loop a slice of what it just heard, transpose every repeat, and reshape the echoes in time with a clock. It's based on the delay mode hidden inside Mutable Instruments Beads and shares its recording engine with Particules.

<img src="screenshots/Retours.png" alt="Retours module" height="500">

---

## How it works

Retours is always recording your input onto a short loop of tape. A playback head follows behind the record head at a distance you set, and what it reads is your delayed signal — move it further back for a longer echo.

Feedback sends the delayed signal back to be recorded again, so it repeats and decays. In the feedback path sits a **pitch shifter**, so each repeat can come back higher or lower, spiralling up or down. A tempo-synced **envelope** can chop or swell each repeat, and **Slice** stops the tape entirely to loop one frozen slice as a stutter.

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

*Interval:* The base spacing between repeats. **Straight up (12 o'clock) is the longest delay** — the whole recording buffer (up to about 4 seconds in the cleanest Quality mode). Turn **counter-clockwise** to shorten it as a single delay tap; turn **clockwise** to shorten it into a busier, multi-tapped pattern. Go far enough in either direction and the interval drops into audio rates (down to \~2 ms): the repeats fuse into a pitch and Retours becomes a Karplus-Strong-style string, playable in volts-per-octave from **Interval CV.**

* When a clock is patched or you set a tap tempo, Interval becomes a **musical divider/multiplier** of the clock tempo: straight up is one repeat per beat, counter-clockwise gives 1/2, 1/4, 1/8, and 1/16, and clockwise adds triplet and other subdivisions (1/3, 1/6, 1/12, and finer).

*Clock:* A **tap-tempo** button: tap it a couple of times to set the delay tempo by hand. **A tempo, once set — by tapping or from a patched clock — holds indefinitely.** It sticks through a stopped clock, an unpatched cable, and any Interval move (Interval just re-selects the subdivision). To return to free-running, use **Clear saved tempo** in the context menu, or establish a new tempo.

* When **clocked** it blinks once per clock beat (or per tapped beat). When **free-running** it blinks once per base Interval, before the Time multiplier — matching the full delay period when Time is at 1×.

*Time:* A multiplier on top of Interval, from 1× up to 16×. Use Interval to set the ballpark and Time to stretch it out. When Retours is clocked, Time snaps to musical multiples (1, 2, 3, 4, 6, 8, 12, 16). **When Slice is engaged, Time chooses which slice of the buffer loops** (see Slice below).

### Repeat character

*Pitch:* Transposes the signal in the feedback path, roughly −24 to +24 semitones, with gentle notches at the octave, fifth, and unison so it settles onto useful intervals. Off at the center detent; away from center, each repeat comes back transposed, so a string of echoes spirals steadily up or down.

*Shape:* A tempo-synced amplitude envelope applied to each repeat. Fully counter-clockwise (off) leaves the echoes untouched. Turning it up morphs the envelope through three stages: first a **gate,** then a smooth **swell,** and finally a **slow ramp.**

*Feedback:* How much of the delayed signal is fed back to be recorded again. The small arrow at the **90% position** marks exact unity gain: below it repeats always decay; at 90% they hold steady; above, the loop grows on its own. Each Quality setting limits feedback differently, so the runaway zone sounds different in each.

### Slice

*Slice:* Stops recording and loops a frozen slice of the buffer — an instant beat-slicer / stutter. The button latches; the **Slice** gate input (top left) does the same from CV (high above 1V). While Slice is held, **Time selects which slice** plays and **Interval sets how long each slice is**, so you can step through a recorded phrase or lock onto one chopped fragment.

### Mixing

*Dry/Wet:* Balance between your untouched input (dry) and the delayed output (wet).

Feedback, Dry/Wet, Time, Pitch, and Shape each have their own **CV input** and a small trimpot beside it — see the note on the trimpots below.

### Quality

The **Quality** button (top center, with the multicolor LED) cycles through four recording characters — the same four as Particules. Each changes the recording sample rate, bit depth, and buffer length. (Cold and Sunny also color the feedback limiter — see Feedback above.) Because the feedback loop re-records through this stage on every pass, the lower-quality modes get dirtier the longer the echoes last. The recording rate is a fixed division of your engine's sample rate, and the buffer holds a fixed number of frames, so the figures below are what both work out to at 48 kHz (at 96 kHz, every length halves):

- ***Bright digital*** (white LED) — full rate (48 kHz), 16-bit or better. Cleanest and brightest. 4-second buffer.
- ***Cold digital*** (cyan LED) — 24 kHz, 12-bit. The classic Mutable *Clouds* grain. 8-second buffer.
- ***Sunny tape*** (amber LED) — 24 kHz, 12-bit, gentle wow and flutter. 16-second buffer.
- ***Scorched cassette*** (magenta LED) — 24 kHz, true 8-bit µ-law, tape hiss, wow and flutter. Crunchy lo-fi. 32-second buffer.

All buffer lengths double when the input is mono (nothing patched into IN R): 8, 16, 32, and 64 seconds respectively. Patching or unpatching IN R re-formats the recording buffer, briefly muting the delayed signal and clearing recorded audio. While Slice is engaged, the Quality button refuses to cycle; choosing a quality from the right-click menu (or the MetaModule switch) changes the selection, but the buffer reformat waits until Slice releases — either way the frozen slice is protected.

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

Right-click the panel in VCV Rack, or open Options on MetaModule:

- **Quality** — the same four recording characters as the front-panel button (**Bright digital**, **Cold digital**, **Sunny tape**, **Scorched cassette**).
- **Time change response** — how the delay reacts when you move the delay time. **Tape (doppler)** (default) glides the playback head like real tape, bending the pitch of anything already in the buffer as it moves. **Crossfade** jumps cleanly to the new time with a short crossfade and no pitch bend.
- **Clear saved tempo** — abandons a held tempo (from a tap or a patched clock) and returns Retours to free-running, so Interval sets the delay time directly again. Greyed out when no tempo is saved.
- **Clear buffer** — empties the recording buffer immediately. Greyed out when the buffer is already empty.

---

## Patch ideas

- **Karplus-Strong string:** turn Interval a long way from center for an audio-rate delay, set Feedback high, and send a V/oct pitch sequence into **Interval CV** — the delay length becomes the string's pitch. Feed it short bursts of noise to pluck it: each burst rings out as a decaying string.
- **Spiralling echoes:** set a moderate delay and feedback, then nudge **Pitch** off its center detent. Every repeat climbs (or falls) a step, so a single note becomes an endless ascending shimmer.
- **Beat slicer:** play a drum loop in, patch its clock into **Clock**, turn Feedback down and Dry/Wet up, and gate **Slice** from your sequencer. While sliced, **Interval** sets the slice length in beat divisions and **Time** picks which slice plays — sequence **Time CV** with stepped random to rearrange the loop in tempo.
- **Gated rhythmic delay:** clock Retours, then turn up **Shape** to gate each repeat — the echoes become a rhythmic pattern locked to your tempo.
- **Runaway tape:** choose **Scorched cassette**, push Feedback past unity, and ride Dry/Wet — the loop degrades a little more on every pass into a collapsing wall of lo-fi noise.
- **Dive-bomb sweeps:** in **Tape** time-change mode, sweep **Interval** by hand or with a slow LFO for pitch-bending delay swoops.

___
_Retours is inspired by Mutable Instruments Beads, designed by Émilie Gillet. It uses some code from [No Such Texture](https://github.com/thorinside/nosuch_texture) by Neal Sanche._

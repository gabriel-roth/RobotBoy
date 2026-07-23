# Retours

**Retours** is a delay and beat-slicer for [VCV Rack](https://vcvrack.com) and the [4ms MetaModule](https://4mscompany.com/metamodule). At its simplest it's a stereo echo, but its repeat time runs all the way from several seconds down to audio rates, so the same knob that sets a slapback also turns the delay into a plucked, pitched resonator. Along the way it can freeze and loop a slice of what it just heard, transpose every repeat, and reshape the echoes in time with a clock. It's built on the delay mode hidden inside Mutable Instruments Beads, and shares its recording engine with Particules; credit to Émilie Gillet for the original design and Neal Sanche for the core DSP.

<img src="screenshots/Retours.png" alt="Retours module" height="500">

---

## How it works

Retours is always recording your input onto a short loop of tape. A playback head follows behind the record head at a distance you set, and what it reads is your delayed signal. Move the head further back and the echo is longer; bring it right up behind the record head and the "echo" arrives so quickly that the repeats fuse into a pitch — the same trick a plucked string uses to make a note.

Feedback sends the delayed signal back to be recorded again, so it repeats and decays. In the feedback path sits a **pitch shifter**, so each repeat can come back higher or lower than the last, spiralling up or down. A tempo-synced **envelope** can chop or swell each repeat, and **Slice** can stop the tape entirely and loop one frozen slice of it as a stutter.

> Like Particules, Retours records through one of four **Quality** characters — from clean digital to grungy 8-bit cassette — and the feedback path picks up that same character. The dirt compounds: every trip around the feedback loop is another pass through the chosen recording flavor.

---

## Quick start

1. **Patch audio** into **In** (bottom left). Patch both jacks for stereo; patch just the left and it feeds both sides.
2. **Turn Dry/Wet** up from center so you can hear the repeats.
3. **Turn up Feedback** a little for more than one echo.
4. **Set the delay time with Interval.** Straight up (12 o'clock) gives the longest delay. Turn it either way to shorten the echo; turn it a long way and the repeats climb into a pitched tone.
5. **Take your output** from **Out** (bottom right). If you only patch the left output, you get a mono sum of both channels.

---

## Controls

### Delay time

*Interval:* The base spacing between repeats. **Straight up (12 o'clock) is the longest delay** — the whole recording buffer (up to about 4 seconds in the cleanest Quality mode). Turn **counter-clockwise** to shorten it as a single delay tap; turn **clockwise** to shorten it into a busier, multi-tapped pattern. Go far enough in either direction and the interval drops into audio rates (down to \~2 ms): the repeats fuse into a pitch and Retours becomes a Karplus-Strong-style string you play with **Pitch**. (When a clock is patched or you tap tempo, Interval works differently — see [Clocking](#clocking-retours).)

*Time:* A multiplier on top of Interval, from 1× up to 16×. Use Interval to set the ballpark and Time to stretch it out. When Retours is clocked, Time snaps to musical multiples (1, 2, 3, 4, 6, 8, 12, 16). **When Slice is engaged, Time chooses which slice of the buffer loops** (see Slice below).

*Pitch:* Transposes the signal in the feedback path, roughly −24 to +24 semitones, with gentle notches at the octave, fifth, and unison so it settles onto useful intervals. At the center detent it's off. Away from center, each repeat comes back transposed, so a string of echoes spirals steadily up or down in pitch — Retours's signature trick. Combined with a very short Interval, Pitch sets the note of the plucked-string tone.

### Repeat character

*Shape:* A tempo-synced envelope drawn over each repeat. Fully counter-clockwise (off) leaves the echoes untouched — a normal delay. Turning it up morphs the envelope through three stages: first a **gate** that chops each repeat shorter and shorter (rhythmic, gated echoes), then a smooth **Hann swell** (each repeat fades in and out), and finally a **slow ramp** that swells across the whole repeat and resets sharply — a reversed-sounding bloom. The envelope's period follows the base Interval (before the Time multiplier — the same as the full delay time when Time is at 1×) and re-syncs to incoming clock ticks, so the chopping stays locked to tempo.

*Feedback:* How much of the delayed signal is fed back to be recorded again — from a single echo up to long, piling repeats and runaway self-oscillation. The small arrow at the **90% position** marks exact unity gain: below it, repeats always decay (the closer to 90%, the longer the tail); park it right at 90% and repeats hold roughly steady while the Quality character keeps compounding; the last 10% of travel pushes past unity (up to about 1.1×), where the loop grows on its own into self-oscillation. Each Quality setting limits feedback differently, from a clean brickwall to grungy tape saturation, so what the runaway zone sounds like depends on the Quality mode.

### Slice

*Slice:* Stops recording and loops a frozen slice of the buffer — an instant beat-slicer / stutter. The button latches; the **Slice** gate input (top left) does the same from CV (high above 1 V). While Slice is held, **Time selects which slice** plays, and **Interval sets how long each slice is**, so you can step through a recorded phrase or lock onto one chopped fragment. Loop seams are crossfaded so the stutter doesn't click.

### Mixing

*Dry/Wet:* Balance between your untouched input (dry) and the delayed output (wet).

Feedback, Dry/Wet, Time, Pitch, and Shape each have their own **CV input** and a small trimpot beside it — see the note on the trimpots below.

### Quality

The **Quality** button (top center, with the multicolor LED) cycles through four recording characters — the same four as Particules. Each changes the recording sample rate, bit depth, and buffer length (Cold and Sunny also color the feedback limiter — see Feedback above). Because the feedback loop re-records through this stage on every pass, the lower-quality modes get dirtier the longer the echoes last. The recording rate is a fixed division of your engine's sample rate; the figures below — buffer lengths included, since the buffer holds a fixed number of frames — are what that works out to at 48 kHz (at a 96 kHz engine rate, all the lengths halve):

*Bright digital* (white LED): full rate (48 kHz at 48 kHz), 16-bit or better — cleanest and brightest. 4-second buffer.

*Cold digital* (cyan LED): rate ÷ 2 (24 kHz at 48 kHz), 12-bit — the classic Mutable *Clouds* grain. 8-second buffer.

*Sunny tape* (amber LED): rate ÷ 2 (24 kHz at 48 kHz), 12-bit, gentle (half-depth) wow and flutter — warm tape. 16-second buffer.

*Scorched cassette* (magenta LED): rate ÷ 2 (24 kHz at 48 kHz), true 8-bit µ-law, tape hiss, wow and flutter — crunchy lo-fi. 32-second buffer.

All buffer lengths double when the input is mono (nothing patched into IN R): 8, 16, 32, and 64 seconds respectively. Patching or unpatching IN R re-formats the recording buffer, briefly muting the delayed signal and clearing recorded audio. Lower-quality settings trade brightness for a **longer buffer**, so the longest available delay time actually grows as the sound gets grungier. While Slice is engaged, the Quality button refuses to cycle; choosing a quality from the right-click menu (or the MetaModule switch) does change the selection, but the buffer reformat waits until Slice releases — either way the frozen slice is protected.

### The trimpots (Time · Pitch · Shape)

Below **Time, Pitch,** and **Shape** sits a small trimpot that does one of two things, exactly as on Particules:

- **With a CV cable patched** into that parameter's input: it's an attenuator for the CV (there's no inverting range — the counter-clockwise half does something else). From center, turn **clockwise for more external modulation**; turn **counter-clockwise to instead spread the value randomly** around the knob position, scaled by the CV.
- **With nothing patched:** it sets how much that parameter drifts on its own from an internal slow-random source. Center is no movement. Counter-clockwise gives a *peaky* wander (mostly near the knob setting, extremes rare); clockwise gives a wide, *uniform* wander.

The **Feedback** and **Dry/Wet** trimpots are plain attenuverters for their CV inputs, not randomizers.

### Lights

- The **Slice** button lights when Slice is active.
- The **Clock** light (center, between the knobs) is a visual metronome. When Retours is **clocked** it blinks once per clock beat, locked to the incoming clock (or to the tapped tempo). When **free-running** it blinks once per base Interval (before the Time multiplier) — matching the full delay period when Time is at 1×.

---

## I/O

*In (L / R):* Audio in. Patch just L for mono (it feeds both sides); patch both for stereo.

*Out (L / R):* Stereo output. If **Out R** is left unpatched, both channels are summed to **Out L** (mono).

*Slice:* Gate input; high (> 1 V) freezes and loops a slice, same as the button.

*Clock:* Clock / trigger input for tempo sync (see below).

*Time / Pitch / Shape CV:* Modulation for those controls, scaled by their trimpots.

*Interval / Feedback / Dry-Wet CV:* Modulation for those controls.

---

## Clocking Retours

Retours can run free or lock to a clock.

- **Nothing patched into Clock, and no tempo set** — *free-running.* Interval sets the delay time directly, as described above.
- **A clock patched into Clock, or tapping the Clock button** — *clocked.* Retours measures the tempo and Interval becomes a **musical divider/multiplier** of it: straight up is 1:1 (one repeat per beat), counter-clockwise gives simple divisions (1/2, 1/4, 1/8, 1/16), and clockwise adds triplet and other subdivisions (1/3, 1/6, 1/12, and finer). Time then multiplies that by a snapped musical factor. The Shape envelope re-syncs to every clock tick, so gated and swelling repeats stay in time.

The **Clock** button doubles as a **tap-tempo** input: tap it a couple of times to set the delay tempo by hand. **A tempo, once set — by tapping or from a patched clock — holds indefinitely.** It sticks through a stopped clock, an unpatched cable, and any Interval move (Interval just re-selects the subdivision). To return to free-running, use **Clear tapped tempo** in the right-click menu, or establish a new tempo.

---

## Right-click options

Right-click the panel in VCV Rack, or open Options on MetaModule:

- **Quality** — the same four recording characters as the front-panel button (**Bright digital**, **Cold digital**, **Sunny tape**, **Scorched cassette**).
- **Time change response** — how the delay reacts when you move the delay time. **Tape (doppler)** (default) glides the playback head like real tape, bending the pitch of anything already in the buffer as it moves — the classic delay-sweep sound. **Crossfade** jumps cleanly to the new time with a short crossfade and no pitch bend.
- **Envelope feedback tap** — whether the Shape envelope is applied **before** or **after** the signal is fed back. Post-envelope (default) shapes only what you hear; Pre-envelope feeds the shaped signal back too, so the chopping compounds with each repeat.
- **Input trim** (desktop only) — ±12 dB of input gain trim.
- **Doppler slew** (desktop only) — how quickly the tape head glides to a new delay time in Tape mode (0.01–1 s, default 0.3 s). Shorter is snappier; longer is a slower, more dramatic pitch sweep.
- **Clear tapped tempo** — abandons a held tap/clock tempo and returns Retours to free-running (Interval sets the delay time directly again).
- **Clear buffer** — empties the recording buffer immediately.

On VCV Rack, menu changes can be undone with Ctrl-Z / Cmd-Z (the sliders and Clear buffer are not undoable).

---

## Patch ideas

- **Karplus-Strong string:** turn Interval a long way from center for an audio-rate delay, set Feedback high, and play notes into **Pitch CV** — each trigger plucks a decaying string. Feed it short bursts of noise for the classic effect.
- **Spiralling echoes:** set a moderate delay and feedback, then nudge **Pitch** off its center detent. Every repeat climbs (or falls) a step, so a single note becomes an endless ascending shimmer.
- **Tempo-locked stutter:** clock the **Clock** input from your sequencer, hold **Slice**, and sweep **Time** to jump between chopped slices of a recorded phrase in time.
- **Gated rhythmic delay:** clock Retours, then turn up **Shape** to gate each repeat — the echoes become a rhythmic pattern locked to your tempo.
- **Runaway tape:** choose **Scorched cassette**, push Feedback past unity, and ride Dry/Wet — the loop degrades a little more on every pass into a collapsing wall of lo-fi noise.
- **Dive-bomb sweeps:** in **Tape** time-change mode, sweep **Interval** by hand or with a slow LFO for pitch-bending delay swoops.

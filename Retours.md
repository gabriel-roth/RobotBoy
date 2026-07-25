# Retours

**Retours** is a delay and beat-slicer for [VCV Rack](https://vcvrack.com) and the [4ms MetaModule](https://4mscompany.com/metamodule). At its simplest it's a stereo echo, but its repeat time runs all the way from several seconds down to audio rates, so the same knob that sets a slapback also turns the delay into a plucked, pitched resonator. Along the way it can freeze and loop a slice of what it just heard, transpose every repeat, and reshape the echoes in time with a clock. It's based on the delay mode hidden inside Mutable Instruments Beads and shares its recording engine with Particules.

<img src="screenshots/Retours.png" alt="Retours module" height="500">

---

## How it works

Retours is always recording your input onto a short loop of tape. A playback head follows behind the record head at a distance you set, and what it reads is your delayed signal. Move the head further back and the echo is longer.

Feedback sends the delayed signal back to be recorded again, so it repeats and decays. In the feedback path sits a **pitch shifter**, so each repeat can come back higher or lower than the last, spiralling up or down. A tempo-synced **envelope** can chop or swell each repeat, and **Slice** can stop the tape entirely and loop one frozen slice of it as a stutter.

Like Particules, Retours records through one of four **Quality** characters — from clean digital to grungy 8-bit cassette — and the feedback path picks up that same character, so the dirt compounds with feedback.

---

## Quick start

1. **Patch audio** into **In** (bottom left). Patch both jacks for stereo; patch just the left and it feeds both sides.
2. **Turn up Feedback** for more than one echo.
3. **Set the delay time with Interval.** Straight up (12 o'clock) gives the longest delay. Turn it either way to shorten the echo; turn it a long way and the repeats blur together into a pitched tone. Turn left to get a single delay tap; turn right to get an additional tap. 
4. **Take your output** from **Out** (bottom right). If you only patch the left output, you get a mono sum of both channels.

---

## Controls

### Delay time

*Interval:* The base spacing between repeats. **Straight up (12 o'clock) is the longest delay** — the whole recording buffer (up to about 4 seconds in the cleanest Quality mode). Turn **counter-clockwise** to shorten it as a single delay tap; turn **clockwise** to shorten it into a busier, multi-tapped pattern. Go far enough in either direction and the interval drops into audio rates (down to \~2 ms): the repeats fuse into a pitch and Retours becomes a Karplus-Strong-style string you play with **Pitch.** 

* When a clock is patched or you set a tap tempo, Interval becomes a **musical divider/multiplier** of the clock tempo: straight up is one repeat per beat, counter-clockwise gives 1/2, 1/4, 1/8, and 1/16, and clockwise adds triplet and other subdivisions (1/3, 1/6, 1/12, and finer). 

*Clock:* A **tap-tempo** button: tap it a couple of times to set the delay tempo by hand. **A tempo, once set — by tapping or from a patched clock — holds indefinitely.** It sticks through a stopped clock, an unpatched cable, and any Interval move (Interval just re-selects the subdivision). To return to free-running, use **Clear saved tempo** in the context menu, or establish a new tempo.

* When Retours is **clocked** it blinks once per clock beat, locked to the incoming clock (or to the tapped tempo). When **free-running** it blinks once per base Interval (before the Time multiplier) — matching the full delay period when Time is at 1×.

*Time:* A multiplier on top of Interval, from 1× up to 16×. Use Interval to set the ballpark and Time to stretch it out. When Retours is clocked, Time snaps to musical multiples (1, 2, 3, 4, 6, 8, 12, 16). **When Slice is engaged, Time chooses which slice of the buffer loops** (see Slice below).

### Repeat character

*Pitch:* Transposes the signal in the feedback path, roughly −24 to +24 semitones, with gentle notches at the octave, fifth, and unison so it settles onto useful intervals. At the center detent it's off. Away from center, each repeat comes back transposed, so a string of echoes spirals steadily up or down in pitch. 

*Shape:* A tempo-synced amplitude envelope applied to each repeat. Fully counter-clockwise (off) leaves the echoes untouched. Turning it up morphs the envelope through three stages: first a **gate,** then a smooth **swell,** and finally a **slow ramp.** 

*Feedback:* How much of the delayed signal is fed back to be recorded again. The small arrow at the **90% position** marks exact unity gain: below it, repeats always decay; at 90%, repeats hold steady; above there, the loop grows on its own. Each Quality setting limits feedback differently, so what the runaway zone sounds like depends on the Quality mode.

### Slice

*Slice:* Stops recording and loops a frozen slice of the buffer — an instant beat-slicer / stutter. The button latches; the **Slice** gate input (top left) does the same from CV (high above 1V). While Slice is held, **Time selects which slice** plays, and **Interval sets how long each slice is**, so you can step through a recorded phrase or lock onto one chopped fragment.

### Mixing

*Dry/Wet:* Balance between your untouched input (dry) and the delayed output (wet).

Feedback, Dry/Wet, Time, Pitch, and Shape each have their own **CV input** and a small trimpot beside it — see the note on the trimpots below.

### Quality

The **Quality** button (top center, with the multicolor LED) cycles through four recording characters — the same four as Particules. Each changes the recording sample rate, bit depth, and buffer length. (Cold and Sunny also color the feedback limiter — see Feedback above.) Because the feedback loop re-records through this stage on every pass, the lower-quality modes get dirtier the longer the echoes last. The recording rate is a fixed division of your engine's sample rate; the figures below — buffer lengths included, since the buffer holds a fixed number of frames — are what that works out to at 48 kHz (at a 96 kHz engine rate, all the lengths halve):

*Bright digital* (white LED): full rate (48 kHz at 48 kHz), 16-bit or better — cleanest and brightest. 4-second buffer.

*Cold digital* (cyan LED): rate ÷ 2 (24 kHz at 48 kHz), 12-bit — the classic Mutable *Clouds* grain. 8-second buffer.

*Sunny tape* (amber LED): rate ÷ 2 (24 kHz at 48 kHz), 12-bit, gentle wow and flutter. 16-second buffer.

*Scorched cassette* (magenta LED): rate ÷ 2 (24 kHz at 48 kHz), true 8-bit µ-law, tape hiss, wow and flutter — crunchy lo-fi. 32-second buffer.

All buffer lengths double when the input is mono (nothing patched into IN R): 8, 16, 32, and 64 seconds respectively. Patching or unpatching IN R re-formats the recording buffer, briefly muting the delayed signal and clearing recorded audio. While Slice is engaged, the Quality button refuses to cycle; choosing a quality from the right-click menu (or the MetaModule switch) does change the selection, but the buffer reformat waits until Slice releases — either way the frozen slice is protected.

### The trimpots (Time · Pitch · Shape)

Below **Time, Pitch,** and **Shape** sits a small trimpot that does one of two things, exactly as on Particules:

- **With a CV cable patched** into that parameter's input: it's an attenuator for the CV (there's no inverting range — the counter-clockwise half does something else). From center, turn **clockwise for more external modulation**; turn **counter-clockwise to instead spread the value randomly** around the knob position, scaled by the CV.
- **With nothing patched:** it sets how much that parameter drifts on its own from an internal slow-random source. Center is no movement. Counter-clockwise gives a *peaky* wander (mostly near the knob setting, extremes rare); clockwise gives a wide, *uniform* wander.

The **Feedback** and **Dry/Wet** trimpots are plain attenuverters for their CV inputs, not randomizers.

## I/O

*In (L / R):* Audio in. Patch just L for mono (it feeds both sides); patch both for stereo.

*Out (L / R):* Stereo output. If **Out R** is left unpatched, both channels are summed to **Out L** (mono).

*Slice:* Gate input; signals above 1V freeze and loop a slice, same as the button.

*Clock:* Clock / trigger input for tempo sync (see below).

*Time / Pitch / Shape CV:* Modulation for those controls, scaled by their trimpots.

*Interval / Feedback / Dry-Wet CV:* Modulation for those controls.

---

## Context menu

Right-click the panel in VCV Rack, or open Options on MetaModule:

- **Quality** — the same four recording characters as the front-panel button (**Bright digital**, **Cold digital**, **Sunny tape**, **Scorched cassette**).
- **Time change response** — how the delay reacts when you move the delay time. **Tape (doppler)** (default) glides the playback head like real tape, bending the pitch of anything already in the buffer as it moves. **Crossfade** jumps cleanly to the new time with a short crossfade and no pitch bend.
- **Clear saved tempo** — abandons a held tempo (from a tap or a patched clock) and returns Retours to free-running, so Interval sets the delay time directly again. Greyed out when no tempo is saved.
- **Clear buffer** — empties the recording buffer immediately. Greyed out when the buffer is already empty.


---

## Patch ideas

- **Karplus-Strong string:** turn Interval a long way from center for an audio-rate delay, set Feedback high, and play notes into **Pitch CV** — each trigger plucks a decaying string. Feed it short bursts of noise for the classic effect.
- **Spiralling echoes:** set a moderate delay and feedback, then nudge **Pitch** off its center detent. Every repeat climbs (or falls) a step, so a single note becomes an endless ascending shimmer.
- **Tempo-locked stutter:** clock the **Clock** input from your sequencer, hold **Slice**, and sweep **Time** to jump between chopped slices of a recorded phrase in time. (For the full version, see the worked example below.)
- **Gated rhythmic delay:** clock Retours, then turn up **Shape** to gate each repeat — the echoes become a rhythmic pattern locked to your tempo.
- **Runaway tape:** choose **Scorched cassette**, push Feedback past unity, and ride Dry/Wet — the loop degrades a little more on every pass into a collapsing wall of lo-fi noise.
- **Dive-bomb sweeps:** in **Tape** time-change mode, sweep **Interval** by hand or with a slow LFO for pitch-bending delay swoops.

### Worked example: the beat slicer

Retours re-chops a drum loop live, in tempo. The one connection that makes it musical is the clock — once Retours knows your tempo, slice lengths land exactly on beat divisions instead of arbitrary milliseconds.

1. **Play a drum loop into In L / In R** — any steady-tempo source, like a sample player looping a one- or two-bar break. (Mono into just In L doubles the recording buffer.)
2. **Patch your master clock into Clock** — quarter notes from the same clock your loop follows. No clock handy? Tap the Clock button in time; a tapped tempo holds until you clear it.
3. **Set the module clean:** Quality on **Bright**, **Feedback** fully counter-clockwise (you're slicing, not echoing), **Pitch** on its center detent, **Shape** fully counter-clockwise, **Dry/Wet** fully wet. Keep **Interval** counter-clockwise of noon so the unsliced signal is a single tap rather than the busier multi-tap pattern.
4. **Let the loop run a couple of bars** — Retours records continuously whenever Slice is off, so the buffer always holds the freshest audio.
5. **Engage Slice.** Latch the button to explore by hand, or send gates to the **Slice** input — a sequencer lane that holds Slice high through the last beat of every bar gives automatic end-of-bar fills.
6. While the slice is held, the two big knobs change jobs: **Interval sets the slice length** (clocked: straight up is one beat, counter-clockwise steps through 1/2, 1/4, 1/8, 1/16 — stutter territory at the short end) and **Time selects which slice plays**. Patch a step sequencer or stepped-random source into **Time CV** and Retours rearranges the drum loop slice by slice, in time.
7. **Release Slice** and the live loop takes over again, seamlessly — alternate Slice on and off to flip between rearranged and live.

From there: nudge **Pitch** off its detent during a slice for a spiralling stutter, or raise **Shape** into its gate zone for chopped, clock-synced repeats.

___
_Retours is inspired by Mutable Instruments Beads, designed by Émilie Gillet. It uses some code from [No Such Texture](https://github.com/thorinside/nosuch_texture) by Neal Sanche._


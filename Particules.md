# Particules

**Particules** is a granular texture processor for [VCV Rack](https://vcvrack.com) and the [4ms MetaModule](https://4mscompany.com/metamodule). It continuously records your incoming audio into a buffer, then plays back a swarm of short overlapping fragments — *grains* — each with its own position, pitch, length, and envelope. Results range from lush self-generating clouds to tightly clocked rhythmic slicing. Particules is the granular mode of Mutable Instruments Beads, and runs on Beads' own DSP code.

<img src="screenshots/Particules.png" alt="Particules module" height="500">

---

## How it works

Picture a tape loop your input is recorded onto over and over. Every time a grain is requested, a little playback head drops onto the tape, reads a short slice, and lifts off. Move it closer to or further from the record head and it reads a different section; retune it and it plays at a different pitch. Run up to 32 at once and you get a texture.

You control **how often** grains are born (Density), **where** on the tape they read from (Time), **how long** and in **which direction** they play (Size), the **amplitude envelope** on each one (Shape), and **how fast** they play back (Pitch). Four of those controls have their own *attenurandomizer* for CV modulation or randomness. Feedback returns some of the processed output to the recording buffer, and a reverb sits at the end of the chain.

__NOTE:__ Grain parameters are sampled **once, when each grain is born,** and held for that grain's whole life. Turning the Pitch knob doesn't bend grains that are already sounding, only the ones that follow — which is why Particules sounds like a cloud rather than a single voice.

---

## Quick start

1. **Patch audio** into **In L** (bottom left). Patch **In R** too for stereo; leave it out and the left channel feeds both sides. Particules sets its input gain automatically over the first few seconds after you patch or unpatch.
2. **Turn Density** away from center. At 12 o'clock no grains are created. Turn it clockwise for a cloud with randomly distributed grains, or counter-clockwise for a steady pulse of grains. The further from center, the more grains.
3. **Take your output** from **Out L / Out R** (bottom right). If you only patch Out L, you get a mono sum of both channels.
4. **Turn up Dry/Wet** toward wet to hear more of the grains and less of your dry input.

---

## Controls

### Grain generation

*Density:* How often grains are born. **Straight up (12 o'clock) = silence.** Turning clockwise generates grains at a randomly modulated rate; counter-clockwise at a constant rate. The further from center, the shorter the gap, up to a continuous stream. (When a clock is patched into **Seed**, or the Seed button is unlocked, Density works differently — see **Seed button and input** below.)

- The white LED under the **Density** CV input flashes on every grain, brightening with the number of grains active.

*Seed button and input:* Seed works as on Beads, and its button has two states.

- **Locked** (the default; the Seed light pulses slowly): **Density** alone makes grains, as described above. A clock or trigger patched into the Seed input turns **Density** into a divider/probability control: at 12 o'clock no grains fire; turn clockwise to raise the *probability* (0–100%) that each incoming trigger spawns a grain, or counter-clockwise to set the clock division (from 1/16 up to 1/1).
- **Unlocked** (press the Seed button once): grains are made only while the button is held or the Seed input is high, at the rate Density sets; at 12 o'clock each press, or each rising edge at the input, fires exactly one grain. The light shows when the button or the input is high. So after one short press, a Particules with nothing in its Seed input goes quiet until you hold the button.
- **Hold the button for 4 seconds** to lock it again.

Each grain starts 2.5 ms after its trigger, as on Beads, so that a CV sent along with the trigger (to Pitch or Time, say) has settled by then. The lock state is saved with the patch; patches saved with the old "Seed CV mode" menu set to Gates load unlocked.

*Freeze:* Stops recording into the buffer so the grains gather sound from a frozen snapshot instead of the live input. The button latches; the **Freeze** gate input does the same when incoming CV goes above 1V.

*Time:* Where in the recorded buffer grains read from. Fully counter-clockwise plays the most recent audio; fully clockwise reaches back to the oldest material in the buffer. Sweep it to scrub through recorded time.

*Pitch:* Transposition of each grain, −24 to +24 semitones. The knob has flat spots at unison, 1, 3, 4 and 7 semitones, the octave and two octaves, in both directions, so it settles onto useful intervals.

*Size:* Grain duration and direction. Around 10 o'clock, grains are shortest (about 8 ms). Turn clockwise for longer grains, up to 4 seconds; counter-clockwise for *reversed* grains, likewise up to 4 seconds.

*Shape:* The amplitude envelope of each grain. Fully counter-clockwise gives clicky, near-rectangular envelopes; fully clockwise gives slow, soft attacks (reversed-sounding swells). The envelope shape is independent of playback direction.

### The attenurandomizers (Time · Size · Shape · Pitch)

Below **Time, Size, Shape,** and **Pitch** sit the *attenurandomizers.* Each does one of two things, depending on whether a cable is in that parameter's CV input:

- **With a CV cable patched:** it's an attenuator for that CV. From center, turn **clockwise for more external modulation**; turn **counter-clockwise to instead spread the value randomly** around the knob position, scaled by the CV.
- **With nothing patched:** it sets how much that parameter is randomized from an internal random source. Center is no randomness. Counter-clockwise gives a *peaky* distribution (values clustered near the knob setting, extremes rare); clockwise gives a *uniform* spread (any value equally likely).

**NOTE:** the **Pitch CV input is scaled by its attenurandomizer** like the others. **Fully clockwise gives 1 V/octave** tracking of the grain's root note; at center the CV is ignored; counter-clockwise turns the CV into pitch *randomization* instead. So set it fully clockwise to sequence or play melodies into Pitch CV.

**HINT:** A touch of Size and Pitch randomness turns a static drone into a living, evolving one.

### Mixing

*Feedback:* Feeds the processed output back into the recording chain.

*Dry/Wet:* Balance between your input (dry) and the granular output (wet). The dry signal passes through the same input stage as the recording (including the auto gain), and in Cold digital and Scorched cassette it takes on their bit depth, so even a fully dry output isn't identical to the input.

*Reverb:* Amount of built-in reverb, applied to the dry/wet mix at the end of the chain. In every Quality setting except Bright digital the reverb is darker and longer.

Each of these has its own **CV input** and a small **CV-amount trimpot** beside it. These trimpots are **attenuverters**: at center the CV is ignored, clockwise scales it normally, and counter-clockwise scales and inverts it. (Unlike the four grain controls' attenurandomizers, they don't add randomization.)

### Quality

The **Quality** button cycles through four recording characters, the same four as Beads. Each runs the whole module (recording, grains, reverb and all) at its own sample rate and bit depth, with its own buffer length. Particules converts to and from your engine's sample rate, so these figures hold whatever rate VCV or MetaModule runs at:

- ***Bright digital*** (white LED) — 48 kHz, 16-bit. Cleanest and brightest. 4-second buffer.
- ***Cold digital*** (cyan LED) — 32 kHz, 12-bit, gently saturated feedback. 8-second buffer.
- ***Sunny tape*** (amber LED) — 24 kHz, 12-bit, gently saturated feedback, and soft saturation on the input when the gain is set manually. 10.7-second buffer.
- ***Scorched cassette*** (magenta LED) — 24 kHz, 8-bit µ-law, heavier feedback saturation (and input saturation when the gain is set manually), wow and flutter, and tape hiss. Crunchy lo-fi. 16-second buffer.

All buffer lengths double when the input is mono (only one input patched): 8, 16, 21 and 32 seconds respectively. Going between stereo (both inputs patched) and mono, or changing the Quality setting, re-formats the recording buffer, clearing recorded audio and muting the output for a quarter of a second. While Freeze is engaged, the Quality button refuses to cycle. (On MetaModule, where Quality is a four-position switch, the switch still changes the selection, but the reformat waits for Freeze to release.)

### Gate output

*Gate:* the jack between **In** and **Out** sends a 10 V gate for every grain, high for the first half of each grain's trigger period (Beads' gate stream). Use it to clock other modules from the grain rate, or to trigger envelopes in time with the cloud.

## Context-menu options

Open the context menu on the panel in VCV Rack, or open Options on MetaModule:

- **Auto gain** (on by default; click to turn it off or on) — Particules normally sets its own input gain (0 to +32 dB), as Beads does: for a few seconds after you patch or unpatch it learns the level of the source and boosts quiet material, then keeps adapting slowly. It never turns a source down: a source hotter than ±5 V goes into the engine at full level, where Beads' limiters hold it back, and the recording itself tops out at full scale. Turning auto gain back on restarts the learning.
- **Gain** — while auto gain is on, shows the gain it's currently applying. With auto gain off, opens a fixed input gain setting (0–32 dB, default 0 dB, which is unity). Turning auto gain off is useful when the source is silent or intermittent and auto gain would otherwise crank up the noise floor. In Sunny tape and Scorched cassette, a hot manual gain drives the input into soft saturation rather than hard clipping.
- **Input** — a live readout of the input level in dB, shown next to the gain options; reads "silent" below −60 dB. On MetaModule it's captured when the menu opens rather than updating live.
- **Tape hiss (Scorched cassette)** — on by default. Beads' Scorched cassette setting runs its audio converter at a low sample rate, which makes it hiss; this adds a similar quiet hiss to the input (and so to the dry signal too). Turn it off for a clean noise floor. It has no effect in the other Quality settings.
- **Lock pitch** — quantize each grain's pitch (after CV and randomization): **Off**, **Octaves**, **Octaves + 5ths**, **Chromatic**, **Major**, **Minor**, **Major pentatonic**, or **Minor pentatonic**. The five scale modes quantize grain pitch to a 12-tone scale; a **Root** submenu (C through B, default C) sets the scale root, and applies only to those modes.
- **Clear buffer** — empties the recording buffer.

---

## Lucarne display

**[Lucarne](Lucarne.md)** is a display expander for Particules. It shows the recording buffer as a waveform, with the write head sweeping across it, and draws every grain as it plays:

- **Position and length:** each grain is a line over the stretch of buffer it reads, so **Time** moves grains across the display and **Size** and **Pitch** set how long the lines are.
- **Height:** pitch. Unison sits just above the waveform, higher grains further up, lower grains below it (±2 octaves).
- **Color:** stereo position, from blue (left) through white to orange (right). With **Density** counter-clockwise and the attenurandomizers at noon, every grain is centered and white; turning Density clockwise or adding randomness fans the colors out.
- **Brightness:** each grain's envelope (**Shape**). A cursor runs along each line as the grain plays.

In VCV Rack, place Lucarne directly beside Particules. On MetaModule (firmware 2.4 or later), add Lucarne and attach it from Particules' **Expanders…** menu; tap the display to see it fullscreen. Lucarne has no effect on the sound. See the [Lucarne manual](Lucarne.md) for details.

---

## Patch ideas

- **Scrubbing / timestretch:** patch a slow ramp-down LFO or a decaying envelope into **Time** CV to sweep through recorded time at whatever speed you like.
- **Playable grains:** set the **Pitch** attenurandomizer fully clockwise and sequence **Pitch CV** (V/oct) to play melodies of grains — or feed a fast arpeggio in to build shimmering chords, each grain landing on a random note of the arp.
- **Speech / phoneme sequencing:** patch a sequencer's CV into **Time** and its gate into **Seed** to step through slices of a recorded phrase.
- **Living drone:** hold a sustained note, freeze the buffer, then add a little **Size** and **Pitch** attenurandomizer for a texture that never quite repeats.
- **Rhythmic slicer:** turn **Size** fully clockwise, clock **Seed** from your sequencer, and use **Density** to pick the subdivision.

___
_Particules runs on the source code of [Mutable Instruments Beads](https://github.com/pichenettes/eurorack/tree/master/beads), designed and written by Émilie Gillet and released under the MIT licence._

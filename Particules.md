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

*Density:* How often grains are born. **Straight up (12 o'clock) = silence.** Turning clockwise generates grains at a randomly modulated rate; counter-clockwise at a constant rate. The further from center, the shorter the gap, up to a continuous stream. (When a clock is patched into **Seed**, Density works differently — see **Seed input** below.)

- The white LED under the **Density** CV input flashes on every grain, brightening with the number of grains active.

*Seed input:* The clock/trigger/gate input for grains. When a clock or trigger is patched into Seed, **Density** becomes a divider/probability control: at 12 o'clock no grains fire. Turn clockwise to raise the *probability* (0–100%) that each incoming trigger spawns a grain; turn counter-clockwise to set the clock division (from 1/16 up to 1/1) at which grains are created. Each grain starts 2.5 ms after its trigger, as on Beads, so that a CV sent along with the trigger (to Pitch or Time, say) has settled by then.

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
- ***Scorched cassette*** (magenta LED) — 24 kHz, 8-bit µ-law, heavier feedback saturation (and input saturation when the gain is set manually), wow and flutter. Crunchy lo-fi. 16-second buffer.

All buffer lengths double when the input is mono (only one input patched): 8, 16, 21 and 32 seconds respectively. Going between stereo (both inputs patched) and mono, or changing the Quality setting, re-formats the recording buffer, clearing recorded audio and muting the output for a quarter of a second. While Freeze is engaged, the Quality button refuses to cycle. (On MetaModule, where Quality is a four-position switch, the switch still changes the selection, but the reformat waits for Freeze to release.)

## Context-menu options

Open the context menu on the panel in VCV Rack, or open Options on MetaModule:

- **Auto gain** — Particules normally sets its own input gain (0 to +32 dB), as Beads does: for a few seconds after you patch or unpatch it learns the level of the source and boosts quiet material, then keeps adapting slowly. It never turns a source down: a source hotter than ±5 V goes into the engine at full level, where Beads' limiters hold it back, and the recording itself tops out at full scale. Selecting this re-runs the learning; the menu shows the current gain.
- **Manual gain** — turns auto gain off and lets you set a fixed input gain (0–32 dB). Useful when the source is silent or intermittent and auto gain would otherwise crank up the noise floor. In Sunny tape and Scorched cassette, a hot manual gain drives the input into soft saturation rather than hard clipping.
- **Input** — a live readout of the input level in dB, shown next to the gain options; reads "silent" below −60 dB. On MetaModule it's captured when the menu opens rather than updating live.
- **Seed CV mode** — how the module responds to CV at the Seed input. In **Triggers** mode (default), each incoming trigger spawns a grain — or several, or the probability of one — depending on Density. In **Gates** mode, grains fire only while the gate is high, with **Density** setting their repetition rate during the gate; at 12 o'clock exactly one grain fires per gate. With nothing patched into Seed, both modes behave identically.
- **Lock pitch** — quantize each grain's pitch (after CV and randomization): **Off**, **Octaves**, **Octaves + 5ths**, **Chromatic**, **Major**, **Minor**, **Major pentatonic**, or **Minor pentatonic**. The five scale modes quantize grain pitch to a 12-tone scale; a **Root** submenu (C through B, default C) sets the scale root, and applies only to those modes.
- **Grain trigger on R output** — replaces the right output with a 10 V gate for every grain, while the left output carries a mono sum of the audio. Use this to clock other modules from the grain rate.
- **Clear buffer** — empties the recording buffer.

---

## Patch ideas

- **Scrubbing / timestretch:** patch a slow ramp-down LFO or a decaying envelope into **Time** CV to sweep through recorded time at whatever speed you like.
- **Playable grains:** set the **Pitch** attenurandomizer fully clockwise and sequence **Pitch CV** (V/oct) to play melodies of grains — or feed a fast arpeggio in to build shimmering chords, each grain landing on a random note of the arp.
- **Speech / phoneme sequencing:** patch a sequencer's CV into **Time** and its gate into **Seed** to step through slices of a recorded phrase.
- **Living drone:** hold a sustained note, freeze the buffer, then add a little **Size** and **Pitch** attenurandomizer for a texture that never quite repeats.
- **Rhythmic slicer:** turn **Size** fully clockwise, clock **Seed** from your sequencer, and use **Density** to pick the subdivision.

___
_Particules runs on the source code of [Mutable Instruments Beads](https://github.com/pichenettes/eurorack/tree/master/beads), designed and written by Émilie Gillet and released under the MIT licence._

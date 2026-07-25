# Particules

**Particules** is a granular texture processor for [VCV Rack](https://vcvrack.com) and the [4ms MetaModule](https://4mscompany.com/metamodule). It continuously records your incoming audio into a buffer, then plays back a swarm of short overlapping fragments — *grains* — each with its own position, pitch, length, and envelope. The results can range from lush self-generating clouds to tightly clocked, rhythmic slicing. It's inspired by Mutable Instruments Beads.

<img src="screenshots/Particules.png" alt="Particules module" height="500">

---

## How it works

Picture a tape loop onto which your input is being recorded over and over. Every time a grain is requested, a little playback head drops onto the tape, reads a short slice, and lifts off again. Move that head closer to or further from the record head and the slice plays back a different section of the recording; retune it and it plays at a different pitch. Run dozens of these heads at once and you get a texture.

You control **how often** grains are born (Density), **where** on the tape they read from (Time), **how long** and in **which direction** they play (Size), the **amplitude envelope** on each one (Shape), and their **pitch.** Four of those controls have their own *attenurandomizer* for adding CV modulation or randomness. Feedback sends some of the processed output back into the recording buffer. A reverb is available at the end of the chain.

__NOTE:__ Grain parameters are sampled **once, when each grain is born,** and held for that grain's whole life. So turning the Pitch knob doesn't bend the notes that are already sounding — it affects subsequent grains. This is why Particules sounds like a cloud rather than a single voice.

---

## Quick start

1. **Patch audio** into **In L** (bottom left). Patch **In R** too for stereo; leave it out and the left channel feeds both sides. Particules recalibrates its input gain automatically whenever you patch or unpatch.
2. **Turn Density** away from center. At 12 o'clock no grains are created. Turn it clockwise for a cloud with randomly distributed grains, or counter-clockwise for a steady pulse of grains. The further you go from center, the more grains are created.
3. **Take your output** from **Out L / Out R** (bottom right). If you only patch Out L, you get a mono sum of both channels.
4. **Turn up Dry/Wet** toward wet to hear more of the grains and less of your dry input.

---

## Controls

### Grain generation

*Density:* How often grains are born. **Straight up (12 o'clock) = silence.** Turning clockwise generates grains at a *randomly modulated* rate; counter-clockwise generates them at a *constant* rate. The further from center, the shorter the gap between grains, up to a continuous stream. (When a clock is patched into **Seed**, Density works differently — see [Seed](#seed-clocking-grains).)

- The white LED under the **Density** CV input **flashes on every grain**, brightening with the number of grains active.

*Seed input:* The clock/trigger/gate input for grains (top right, labeled SEED). When a clock or trigger is patched into Seed, **Density** is repurposed as a divider/probability control: At 12 o'clock no grains fire. Turn clockwise to raise the *probability* (0–100%) that each incoming trigger spawns a grain; turn counter-clockwise to *divide* the clock (from 1/16 up to 1/1). 

*Freeze:* Stops recording into the buffer so the grains gather sound from a frozen snapshot instead of the live input. The button latches; the **Freeze** gate input (top left) does the same when incoming CV goes above 1V.

*Time:* *Where in the recorded buffer* grains read from. Fully counter-clockwise plays the most recent audio; fully clockwise reaches back to the oldest material in the buffer. Sweep it to scrub through recorded time.

*Pitch:* Transposition of each grain, roughly −24 to +24 semitones, with gentle notches at useful intervals so it settles onto pitches.

*Size:* Grain duration and direction. Near the 11 o'clock area a very short (\~30 ms) grain plays. Turn clockwise for grains (up to several seconds); turn counter-clockwise for *reversed* grains up to several seconds.

*Shape:* The amplitude envelope of each grain. Fully counter-clockwise gives clicky, near-rectangular envelopes; fully clockwise gives slow, soft attacks (reversed-sounding swells). The envelope shape is independent of playback direction.

### The attenurandomizers (Time · Size · Shape · Pitch)

Below **Time, Size, Shape,** and **Pitch** sit the *attenurandomizers.* These do one of two things depending on whether a cable is in that parameter's CV input:

- **With a CV cable patched:** it's an attenuator for that CV. From center, turn **clockwise for more external modulation**; turn **counter-clockwise to instead spread the value randomly** around the knob position, scaled by the CV.
- **With nothing patched:** it sets how much that parameter is randomized from an internal random source. Center is no randomness. Counter-clockwise gives a *peaky* distribution (values clustered near the knob setting, extremes rare); clockwise gives a *uniform* spread (any value equally likely).

This is a great tool for turning a static drone into a living, evolving one — a touch of Size and Pitch randomness goes a long way.

**NOTE:** the **Pitch CV input is scaled by its attenurandomizer** like the others. **Fully clockwise gives 1 V/octave** tracking of the grain's root note; at center the CV is ignored; counter-clockwise turns the CV into pitch *randomization* instead. To sequence or play melodies into Pitch CV, set the attenurandomizer fully clockwise.

### Mixing

*Feedback:* Feeds the processed output back into the recording chain. 

*Dry/Wet:* Balance between your input (dry) and the granular output (wet). (Because the dry path shares the input soft limiter, at very hot input levels "dry" is not perfectly bit-clean.)

*Reverb:* Amount of built-in reverb, applied to the dry/wet mix at the end of the chain.

Each of these has its own **CV input** and a small **CV-amount trimpot** beside it. These trimpots are **attenuverters**: at center the CV is ignored, clockwise scales it normally, and counter-clockwise scales and inverts it. (Unlike the four grain controls' attenurandomizers, they don't add randomization.)

### Quality

The **Quality** button cycles through four recording characters. Each changes the recording sample rate, bit depth, and buffer length (and Cold and Sunny also color the feedback limiter). The recording rate is a fixed decimation factor of your engine's sample rate, not an absolute number — the figures below are what that factor works out to at a 48 kHz sample rate:

*Bright digital* (white LED): full rate (48 kHz at 48 kHz), 16-bit or better — cleanest and brightest. 4-second buffer.

*Cold digital* (cyan LED): rate ÷ 2 (24 kHz at 48 kHz), 12-bit — the classic Mutable *Clouds* grain. 8-second buffer.

*Sunny tape* (amber LED): rate ÷ 2 (24 kHz at 48 kHz), 12-bit, gentle (half-depth) wow and flutter — warm tape. 16-second buffer.

*Scorched cassette* (magenta LED): rate ÷ 2 (24 kHz at 48 kHz), true 8-bit µ-law, tape hiss, wow and flutter — crunchy lo-fi. 32-second buffer.

All buffer lengths double when the input is mono (nothing patched into IN R): 8, 16, 32, and 64 seconds respectively. Patching or unpatching IN R, or changing the Quality setting, re-formats the recording buffer, briefly muting the wet signal and clearing recorded audio. While Freeze is engaged, the Quality button refuses to cycle; choosing a quality from the context menu does change the selection, but the reformat waits until you release Freeze.

## Context-menu options

Right-click the panel in VCV Rack, or open Options on MetaModule:

- **Auto gain** — Particules normally sets its own input gain (anywhere from −60 to +32 dB) whenever you patch or unpatch, leaving headroom — quiet sources are boosted and hot ones attenuated (a full-scale ±5 V input lands around −8 dB). Selecting this re-runs calibration; the menu shows the current gain.
- **Manual gain** — turns auto gain off and lets you set a fixed input gain (0–32 dB). Useful when the source is silent or intermittent and auto gain would otherwise crank up the noise floor.
- **Input** — a live readout of the input level in dB, shown next to the gain options; reads "silent" below −60 dB. On MetaModule the value is captured at the moment the menu opens rather than updating live.
- **Seed CV mode** — determines how the module responds to CV at the Seed input. 
	- In Triggers mode (default), each incoming trigger spawns a grain, or multiple grains, or the probability of one, depending on Density settings. 
	- In Gates mode, grains are generated only while a gate at the Seed input is high. **Density** then sets the repetition rate of grains during the gate; at 12 o'clock exactly one grain fires per gate.
- **Lock pitch** — quantize the Pitch control: **Off**, **Octaves**, **Octaves + 5ths**, **Chromatic**, **Major**, **Minor**, **Major pentatonic**, or **Minor pentatonic**. The five scale modes quantize grain pitch to a 12-tone scale; a **Root** submenu (C through B, default C) sets the scale root and applies only to the scale modes.
- **Grain trigger on R output** — replaces the right output with a trigger pulse on every grain, while the left output carries a mono sum of the audio. Use this to clock other modules from the grain rate.
- **Clear buffer** — empties the recording buffer.

---

## Patch ideas

- **Scrubbing / timestretch:** patch a slow ramp-down LFO or a decaying envelope into **Time** CV to sweep through recorded time at whatever speed you like.
- **Playable grains:** set the **Pitch** attenurandomizer fully clockwise and sequence **Pitch CV** (V/oct) to play melodies of grains — or feed a fast arpeggio in to build shimmering chords, each grain landing on a random note of the arp.
- **Speech / phoneme sequencing:** patch a sequencer's CV into **Time** and its gate into **Seed** to step through slices of a recorded phrase.
- **Living drone:** hold a sustained note, freeze the buffer, then add a little **Size** and **Pitch** attenurandomizer for a texture that never quite repeats.
- **Rhythmic slicer:** turn **Size** fully clockwise, clock **Seed** from your sequencer, and use **Density** to pick the subdivision.

___
_Particules is inspired by Mutable Instruments Beads, designed by Émilie Gillet. It uses some code from [No Such Texture](https://github.com/thorinside/nosuch_texture) by Neal Sanche._


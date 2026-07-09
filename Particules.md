# Particules

**Particules** is a granular texture processor for [VCV Rack](https://vcvrack.com) and the [4ms MetaModule](https://4mscompany.com/metamodule). It continuously records your incoming audio into a buffer, then plays back a swarm of short overlapping fragments — *grains* — each with its own position, pitch, length, and envelope. The result ranges from lush self-generating clouds to tightly clocked, rhythmic slicing. It's inspired by Mutable Instruments Beads (by way of [No Such Texture](https://github.com/thorinside/nosuch_texture)); credit to Émilie Gillet for the design and Neal Sanche for the core DSP.

<img src="screenshots/Particules.png" alt="Particules module" height="500">

---

## How it works

Picture a tape loop onto which your input is being recorded over and over. Every time a grain is requested, a little playback head drops onto the tape, reads a short slice, applies an envelope, and lifts off again. Move that head closer to or further from the record head and the slice plays back at a different point in time; retune it and it plays at a different pitch. Run dozens of these heads at once and you get a texture.

You control **how often** grains are born (Density), **where** on the tape they read from (Time), **how long** and in **which direction** they play (Size), the **envelope** on each one (Shape), and their **pitch**. Four of those controls have their own *attenurandomizer* for adding CV modulation or randomness. Feedback sends some of the processed output back into the recording buffer. A reverb is available at the end of the chain.

> Grain parameters are sampled **once, when each grain is born**, and held for that grain's whole life. So turning the Pitch knob doesn't bend the notes that are already sounding — it affects subsequent grains. This is why Particules sounds like a cloud rather than a single voice.

---

## Quick start

1. **Patch audio** into **In L** (bottom left). Patch **In R** too for stereo; leave it out and the left channel feeds both sides. Particules recalibrates its input gain automatically whenever you patch or unpatch.
2. **Turn Density** away from center. At 12 o'clock no grains are created. Turn it clockwise for a cloud with randomly distributed grains, or counter-clockwise for a steady pulse of grains. The further you go from center, the more grains are created.
3. **Take your output** from **Out L / Out R** (bottom right). If you only patch Out L, you get a mono sum of both channels.
4. **Turn up Dry/Wet** toward wet to hear more of the grains and less of your dry input.

---

## Controls

### Grain generation

*Density:* How often grains are born. **Straight up (12 o'clock) = silence.** Clockwise generates grains at a *randomly modulated* rate; counter-clockwise generates them at a *constant* rate. The further from center, the shorter the gap between grains, up to a continuous stream. (When a clock is patched into **Seed**, Density works differently — see [Seed](#seed-clocking-grains).)

*Seed input:* The clock/trigger/gate input for grains (top right, labeled SEED). Its behavior depends on whether a cable is patched and on the **Seed CV mode** menu setting — see below.

*Freeze:* Stops recording into the buffer so the grains gather sound from a frozen snapshot instead of the live input. The button latches; the **Freeze** gate input (top left) does the same from CV (high above 1 V).

### Grain playback

*Time:* *Where in the recorded buffer* grains read from. Fully counter-clockwise plays the most recent audio; fully clockwise reaches back to the oldest material in the buffer. Sweep it to scrub through recorded time.

*Pitch:* Transposition of each grain, roughly −24 to +24 semitones, with gentle notches at useful intervals so it settles onto pitches.

*Size:* Grain duration and direction. Near the 11 o'clock area a very short (\~30 ms) grain plays. Turn clockwise to lengthen grains up to several seconds; turn counter-clockwise for *reversed* grains up to several seconds.

*Shape:* The amplitude envelope of each grain. Fully counter-clockwise gives clicky, near-rectangular envelopes; fully clockwise gives slow, soft attacks (reversed-sounding swells). The envelope shape is independent of playback direction.

### The attenurandomizers (Time · Size · Shape · Pitch)

Below **Time, Size, Shape,** and **Pitch** sits a small trimpot — the *attenurandomizer*. It does one of two things depending on whether a cable is in that parameter's CV input:

- **With a CV cable patched:** it's an attenuverter for that CV. From center, turn **clockwise for more external modulation**; turn **counter-clockwise to instead spread the value randomly** around the knob position, scaled by the CV.
- **With nothing patched:** it sets how much that parameter is randomized from an internal random source. Center is no randomness. Counter-clockwise gives a *peaky* distribution (values clustered near the knob setting, extremes rare); clockwise gives a *uniform* spread (any value equally likely).

This is the main tool for turning a static drone into a living, evolving one — a touch of Size and Pitch randomness goes a long way.

> **Pitch note:** the **Pitch CV input always tracks 1 V/octave** for the grain's root note, regardless of the attenurandomizer. The Pitch attenurandomizer then adds pitch *randomization* on top. So you can sequence or play a melody into Pitch CV and still dial in some pitch scatter.

### Mixing

*Feedback:* Feeds the processed output back into the recording chain. Each quality setting (see below) limits feedback differently — from a clean brickwall to grungy tape saturation.

*Dry/Wet:* Balance between your untouched input (dry) and the granular output (wet).

*Reverb:* Amount of built-in reverb on the wet signal.

Each of these has its own **CV input** and a small **CV-amount trimpot** beside it (unlike the four grain controls, these are plain attenuators, not attenurandomizers).

### Quality

The **Quality** button (top, with the multicolor LED) cycles through four recording characters. Each changes the recording sample rate, bit depth, buffer length, and the flavor of the feedback limiter. The recording rate is a fixed decimation factor of your engine's sample rate, not an absolute number — the figures below are what that factor works out to at a 48 kHz sample rate:

*Bright digital* (white LED): full rate (48 kHz at 48 kHz), 16-bit — cleanest and brightest.

*Cold digital* (cyan LED): rate ÷ 2 (24 kHz at 48 kHz), 12-bit — the classic Mutable *Clouds* grain.

*Sunny tape* (amber LED): rate ÷ 8 (6 kHz at 48 kHz), 12-bit — warm.

*Scorched cassette* (magenta LED): rate ÷ 4 (12 kHz at 48 kHz), 8-bit — lo-fi, with wow and flutter.

Lower-quality settings also give you a **longer recording buffer** to gather grains from. Quality can't be changed while Freeze is engaged.

### Lights

- The **Freeze** button lights when freeze is active.
- The white LED under the **Density** CV input **flashes on every grain**, so you can see the grain rate at a glance.

---

## I/O

*In L / In R:* Audio in. Patch just L for mono (it feeds both sides); patch both for stereo.

*Freeze:* Gate input; high (> 1 V) freezes the buffer, same as the button.

*Seed:* Clock / trigger / gate for grain generation (see below).

*Time / Size / Shape CV:* Modulation for those controls, scaled by their attenurandomizers.

*Pitch CV:* 1 V/octave pitch input for the grain root note (always V/oct; the attenurandomizer adds randomization).

*Density CV:* Modulates grain rate / probability.

*Feedback / Dry-Wet / Reverb CV:* Modulation for the mix controls, scaled by their CV-amount trimpots.

*Out L / Out R:* Stereo output. If **Out R** is left unpatched, both channels are summed to **Out L** (mono).

---

## Seed: clocking grains

The **Seed** input, together with the **Seed CV mode** menu option, decides how grains are triggered.

- **Seed CV mode = Triggers** (default):
  - **Nothing patched into Seed** — *free-running.* Grains are generated continuously at the rate set by the **Density** knob (as described above).
  - **A clock or trigger patched into Seed** — *clocked.* **Density** is repurposed as a divider/probability control. At 12 o'clock no grains fire. Turn clockwise to raise the *probability* (0–100%) that each incoming trigger spawns a grain; turn counter-clockwise to *divide* the clock (from 1/16 up to 1/1).
- **Seed CV mode = Gates:** grains are generated only while a gate at the Seed input is high. **Density** then sets the repetition rate of grains during the gate; at 12 o'clock exactly one grain fires per gate.

---

## Right-click options

Right-click the panel in VCV Rack, or open Options on MetaModule:

- **Auto gain** — Particules normally sets its own input gain (0 to +32 dB) whenever you patch or unpatch, leaving headroom. Selecting this re-runs calibration; the menu shows the current gain.
- **Manual gain** — turns auto gain off and lets you set a fixed input gain (0–32 dB). Useful when the source is silent or intermittent and auto gain would otherwise crank up the noise floor.
- **Seed CV mode** — **Triggers** (default) or **Gates**; see [Seed](#seed-clocking-grains).
- **Lock pitch** — quantize the Pitch control: **Off**, **Octaves**, or **Octaves + 5ths**.
- **Grain trigger on R output** — replaces the right output with a trigger pulse on every grain (a 1 ms gate), while the left output carries a mono sum of the audio. Handy for clocking other modules from the grain rate. Patch Out R for the triggers and take audio from Out L.
- **Clear buffer** — empties the recording buffer immediately.

---

## Patch ideas

- **Scrubbing / timestretch:** patch a slow ramp-down LFO or a decaying envelope into **Time** CV to sweep through recorded time at whatever speed you like.
- **Playable grains:** set the **Pitch** attenurandomizer fully clockwise and sequence **Pitch CV** (V/oct) to play melodies of grains — or feed a fast arpeggio in to build shimmering chords, each grain landing on a random note of the arp.
- **Speech / phoneme sequencing:** patch a sequencer's CV into **Time** and its gate into **Seed** to step through slices of a recorded phrase.
- **Living drone:** hold a sustained note, freeze the buffer, then add a little **Size** and **Pitch** attenurandomizer for a texture that never quite repeats.
- **Rhythmic slicer:** turn **Size** fully clockwise, clock **Seed** from your sequencer, and use **Density** to pick the subdivision.

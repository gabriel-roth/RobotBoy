# Brume

**Brume** is a 29 HP stereo reverb for [VCV Rack](https://vcvrack.com) and the [4ms MetaModule](https://4mscompany.com/metamodule), with every parameter of the reverb on its own knob. It runs either of two reverbs, the one from Mutable Instruments Beads or the one from Mutable Instruments Clouds, and adds a smear and a filter on the way in, and an envelope follower that lets the reverb respond to your playing without a single cable. Brume runs on the source code of Beads and Clouds.

<img src="screenshots/Brume.png" alt="Brume module" height="500">

---

## How it works

Your input goes through a **Send** level, a **Filter**, a **Smear** (a diffuser that blurs transients) and a **Pre-delay** into the reverb tank, whose **Size** you can scale. The tank is fully wet; **Dry/Wet** at the end mixes it with your untouched input. **Width** narrows or widens the reverb's stereo image.

**Wow** adds tape-style pitch wobble.

The **follower** (the sand-coloured column on the left) listens to your input, the reverb itself, or a sidechain signal, and turns it into an envelope. Any knob whose CV jack is empty is moved by that envelope by the amount of its trimpot, so a reverb that swells, ducks or opens up as you play needs no patching at all.

Like Particules and Retours, Brume runs its reverb at one of three **Quality** sample rates and converts to and from your engine's rate, so the sound is the same whatever rate VCV or MetaModule runs at.

---

## Quick start

1. **Patch audio** into **In** (bottom left). Patch both jacks for stereo; patch just the left and it feeds both sides.
2. **Take your output** from **Out** (bottom right). If you only patch the left output, you get a mono sum of both channels.
3. **Set Dry/Wet** to taste, and **Decay** for the length of the tail.
4. **Try the follower.** Turn the trimpot under Decay or Dry/Wet clockwise with nothing patched into its CV jack. The reverb now grows and shrinks with the loudness of your playing.

---

## Controls

Each of the 13 plate controls (Send through Dry/Wet) has a **CV input** below it and a small trimpot below that (see The trimpots below).

Size, Damp, Diffuse, Speed and Depth start at noon, which is the original reverb's setting in either Mode: double-click one of these knobs to return to the original sound. Their ranges go well beyond the original in both directions.

### Input

*Send:* How much signal goes into the reverb, from nothing up to full (the default, the original Beads and Clouds level). The dry signal is not affected.

*Filter:* A filter on the reverb's input, a bit like the Texture filter in Clouds. Noon is flat. Turn it **counter-clockwise** for a low-pass filter that darkens what goes in, down to a rumble at the end stop; turn it **clockwise** for a high-pass filter that thins it.

*Smear:* Blurs the input into the reverb with a short diffuser, softening transients before the tank sees them. Off at fully counter-clockwise. Over the first 70% of its travel it fades in the diffuser of Clouds; beyond that the diffuser itself gets denser.

*Pre-delay:* How long the input waits before it reaches the tank, from 0 to 500 ms. The knob gives more of its travel to short times. Fully counter-clockwise is off, and leaves the sound exactly as it was without it. Changes glide over about half a second, so turning it bends the pitch of what is passing through for a moment instead of clicking.

*Wow:* Tape-style wow and flutter (the pitch wobble from Scorched cassette on Beads). Off at fully counter-clockwise, where it is bypassed entirely. The Beads depth is a little past halfway (1x in the tooltip); fully clockwise is three times as deep. By default it wobbles the whole input, dry signal included; the **Wow & flutter** context menu item can limit it to the reverb.

### Reverb

*Size:* The size of the room. Scales every delay inside the tank, from half to twice the original, with the original at noon. A bigger room spaces its echoes further apart and takes longer to decay; a smaller one is tighter and shorter. Changes glide over about half a second, so sweeping Size bends the pitch of the tail for a moment instead of jumping.

*Decay:* The length of the tail. Over the first 95% of the knob's travel it sets the decay time. The last 5% (the infinity mark) is a **hold zone**: the tail stops decaying. In Beads mode it rings almost endlessly. In Clouds mode it still fades slowly, and faster for quiet or noisy material, because Clouds stores its reverb at 12 bits and a little is lost on every pass, as on the original module. Hold keeps whatever is in the tank ringing; with the input still coming in, it builds up (the limiter keeps it bounded).

*Damp:* How quickly high frequencies die out inside the tank. Counter-clockwise leaves the tail bright; clockwise makes it dark, darker than Beads can go.

*Diffuse:* The tank's diffusion. Counter-clockwise gives separate, decaying echoes; clockwise gives one smooth wash. Noon is each Mode's original setting, so the same knob position is slightly denser in Clouds mode.

*Speed:* The speed of the slow modulation inside the tank, from a tenth of the original rate to 16 times it.

*Depth:* The amount of that modulation, from none (a static tank) to four times the original. Noon is the original.

### Output

The Output column on the right edge holds **Clear**, **Width** and **Dry/Wet**, top to bottom.

*Clear:* A button and a trigger input that empty the reverb immediately, with a quick fade so there is no click. The input keeps flowing.

*Width:* The stereo width of the reverb. Noon is the reverb's own stereo image; counter-clockwise narrows it to mono, clockwise exaggerates it.

*Dry/Wet:* Balance between your untouched input (dry) and the reverb (wet), using the same laws as Beads.

### Mode and Quality

*Mode* (bottom, with the multicolor LED): Picks the reverb tank. **Beads** (green) and **Clouds** (blue) differ in their character and decay range. Brume matches their levels, so switching doesn't change the volume. When you switch, the old tank doesn't cut off: it fades out over about 2 seconds under the new one, so tails overlap. While Decay is in the hold zone those two tails add together for a moment, up to about 3 dB louder.

*Quality* (bottom, with the multicolor LED): The sample rate the reverb runs at, which changes how bright and clean it is:

- ***Bright*** (white LED): 48 kHz. The cleanest sound; Clouds at Bright is new to Brume.
- ***Cold*** (cyan LED): 32 kHz. This is the Clouds original's rate; Clouds at Cold is exactly Clouds.
- ***Scorched*** (magenta LED): 24 kHz. Darker and dirtier.

Changing Quality clears the reverb's tail.

---

## The follower

The follower column sits at the left edge.

*Rise* and *Fall:* How fast the envelope follows your signal up and down, from 10 ms to 5 s. Fast Rise with slow Fall gives a swell that lingers; the response is quick at first and slower toward the top.

*In / Out switch:* What the follower listens to. **In** (up, the default) follows the input. **Out** (down) follows the reverb itself, before Dry/Wet mixes in the dry signal. Because the envelope then moves the very reverb it is listening to, Out makes a feedback loop: a tail can open up Decay and keep itself going, or duck its own Send and pump.

*Side:* A sidechain input. When something is patched here, the follower listens to it whichever way the switch is set (5 V is full scale), so another signal such as a kick drum can drive the reverb.

*Env (output and light):* The envelope itself, 0 to 10 V. Use it to modulate other modules. The light shows it too.

By default the follower has **auto-gain**, which learns the level of its source over the first few seconds and scales the envelope so a steady signal reaches about full scale whatever its volume. Turn it off in the context menu for the raw envelope, where a 5 V peak is full scale.

### The trimpots

Below each CV jack is a trimpot, and it does one of two things:

- **With a CV cable patched:** it is an attenuverter for that CV: at center the CV is ignored, clockwise adds it, counter-clockwise inverts it. A ±5 V signal at full trimpot sweeps the whole knob range.
- **With nothing patched:** it sets how much the follower's envelope moves that control. At center nothing moves. Clockwise pushes the control up with the envelope; counter-clockwise pushes it down.

With every trimpot at center (the default), nothing moves. Patching a cable disconnects the envelope from that control.

## I/O

- **In (L / R):** audio in. Patch just L for mono; patch both for stereo.
- **Out (L / R):** stereo output. Leave **Out R** unpatched and both channels sum to **Out L**.
- **Side:** sidechain input for the follower.
- **Env:** the follower's envelope, 0 to 10 V.
- **Clear:** trigger input (top of the Output column); same as the button.
- **CV inputs:** one for each of Send, Filter, Smear, Pre-delay, Wow, Size, Decay, Damp, Diffuse, Speed, Depth, Width and Dry/Wet.

---

## Context menu

Open the context menu on the panel in VCV Rack, or open Options on MetaModule:

- **Mode** and **Quality** (VCV Rack only; on MetaModule, use the Mode and Quality switches) — the same choices as the front-panel buttons.
- **Wow & flutter** — **Input** (the default) applies Wow to the whole input, dry signal included, as Beads does; **Reverb only** leaves the dry signal alone. Changing it while Wow is up may glitch briefly.
- **Follower auto-gain** — on by default; see The follower.

Initialize resets these along with the knobs.

---

## MetaModule

Brume's Mode and Quality are labeled switches on MetaModule. A patch saved on desktop opens on MetaModule in **Beads** and **Bright**, whatever Mode and Quality it was saved with, just as Particules' and Retours' Quality does.

---

## Patch ideas

- **Ducking reverb:** patch a kick drum into **Side**, turn the **Dry/Wet** trimpot counter-clockwise with its CV jack empty, and set Rise fast and Fall slow: the reverb pulls back on each kick and blooms between them.
- **Swelling reverb:** with nothing in Side, turn the **Decay** trimpot clockwise and set Fall long. Long tails open up on loud passages and tighten when you stop.
- **Self-swelling tail:** set the follower switch to **Out**, turn the **Decay** trimpot clockwise, and play short notes. Each tail lifts Decay as it grows, so it blooms after the note instead of with it.
- **Frozen pad:** in Beads mode, push **Decay** into the hold zone and play a chord; it sustains until you pull Decay back or hit Clear. (In Clouds mode the pad slowly fades away instead.)
- **Reverb as an instrument:** use Mode to jump between the two tanks while tails overlap, and Quality to change the colour.

___
_Brume runs on the source code of [Mutable Instruments Beads](https://github.com/pichenettes/eurorack/tree/master/beads) and [Mutable Instruments Clouds](https://github.com/pichenettes/eurorack/tree/master/clouds), designed and written by Émilie Gillet and released under the MIT licence._

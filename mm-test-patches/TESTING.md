# Robot Boy MetaModule test patches

Nineteen patches covering every feature of the eight Robot Boy modules on MetaModule hardware, one Robot Boy module per patch. Copy this whole directory onto the MetaModule's SD card — the patches that use the drum loop expect `drum-loop.wav` in the **same directory** as the `.yml` files.

**Plugins required on the MetaModule:** Robot Boy (the release candidate build), Fundamental, Bogaudio, Count Modula. (The 4ms modules are built in.)

## Conventions

- **Buttons live on knobs.** The Hub has no mappable buttons, so button params (Record, Clear, Freeze, Slice, Tap tempo, WAV restart) are mapped to small knobs: **twist up past center = press, twist back down = release.** One press = one full up-down twist. Latching params (Freeze, Slice) simply hold while the knob is up.
- **Drum-loop patches start themselves.** A Count Modula StartupDelay fires the 4ms WAV player about 2 seconds after the patch loads. If the loop isn't playing, twist the knob mapped as *Loop restart / Reload loop* (usually knob z).
- **Never open a module's menu.** Every alternate context-menu setting under test is baked into its own patch via saved module state (that's what the -2/-3 patches are). If a test seems to need a menu, that's a bug in the patch, not the procedure.
- **Bring four external signals** across the session: an LFO (any shape, slow to audio-rate), a pitch CV source (keyboard/sequencer, 1 V/oct), a clock/gate/trigger source, and an audio-rate oscillator. Also bring a **passive mult or stackcable** — a couple of tracking tests feed the same pitch CV into two panel inputs at once. Panel jack assignments are aliased in each patch (visible on the jack roller).
- **Some patches have a second knob set** (extra mix/utility controls that didn't fit on the twelve knobs). A step that needs it says so by name, e.g. "switch to Knob Set 2 (*Head Levels*)". Switching sets never changes a param by itself — the set you leave holds its values.
- **Before you start, set knob catchup to "Track when equal."** It's in the device preferences (not a module menu). The factory default is "Track if knob moves," which means the first nudge of a physical knob *snaps* its param to wherever that knob is physically sitting. Because both knob sets share the same twelve physical knobs, that default turns every set switch into a trap: zero the head Levels on Knob Set 2, come back to Set 1, touch knob C, and Yellow Speed jumps to full CCW instead of resuming where the patch left it. "Track when equal" makes the knob pick up its param only when the two match, so switching sets is safe. If you'd rather leave the default alone, treat every knob you touched on Set 2 as scrambled on Set 1 and re-set it deliberately.
- **Main audio is always Out 1 / Out 2.** Extra taps (individual playheads, filter responses) sit on Outs 3-5 where noted.
- A few behaviors key off whether a **physical cable** is present in a mapped input (Particules/Retours attenurandomizers switch between randomizer and CV-attenuator; Particules auto-gain recalibrates on patch/unpatch; Seed/Clock detection). Steps that depend on this say so — if something behaves as if a cable were patched when the jack is empty, note it as a finding.

All 19 patches load clean in the MetaModule headless simulator with the current Robot Boy build (module counts verified, states applied); patches that make sound without external input were additionally verified non-silent (Ondes drone, Vespid German self-oscillation — note the German *hardware-pitch* build-up takes a few seconds to bloom).

---

## RB-Onbetap-2 — Vintage stereo pad (Onbetap, Vintage drift + hard mode switching)

**Setup:** Two saw VCOs — C3 left, C3 +7 cents right — feed the Onbetap's L/R inputs (true stereo through one filter). Vintage mode is baked in (drift ON, 1×). A slow sine LFO (0.1 Hz, ±1.5 V) is wired to Cutoff CV internally with Cutoff CV Amt at +30%. Cutoff \~750 Hz, Q 50%, Drive 30%, LP mode. Output on Out 1/Out 2 — listen in stereo.
**Panel:**

| Control | Maps to |
|---|---|
| Knob A | Cutoff |
| Knob B | Q |
| Knob C | Drive |
| Knob D | Mode |
| Knob E | Cutoff CV Amt |
| Knob F | Q CV Amt |
| In 1 | Pad Pitch (1V/oct, both VCOs) |
| In 2 | Q CV |

**Try:**
1. Load the patch and just listen in stereo for \~30 s — **Expect:** the image comes alive: L and R cutoffs drift independently (Vintage drift), a seasick color-detune on top of the 7-cent oscillator beat. On top rides the slow internal LFO sweep (\~10 s period, subtle at +30%).
2. Step D between modes while audio plays — **Expect:** mode changes CLICK hard — unfaded, authentic panel-switch behavior (contrast with RB-Onbetap-1, where the same move is smooth).
3. Yank A fast across a wide arc — **Expect:** a DC thump, like slamming the real hardware's cutoff — Vintage mode does not soften it.
4. Reload the patch (re-open it from the patch list) and listen to the first \~30 s again — **Expect:** the drift evolves IDENTICALLY to the first load — it is seeded, not random per boot.
5. Raise B to 85% in LP mode — **Expect:** bass stays planted even at high Q (Polivoks trait carries into Vintage mode).
6. Sequence In 1 — **Expect:** both saws transpose together in tune (one CV, two VCOs); the detune interval and stereo drift character hold at every pitch.
7. Flag for surprises: In 2 (Q CV) is mapped but may be empty — **Expect:** with nothing patched it behaves exactly as if unmapped; note anything odd here.

## RB-Onbetap-3 — 4× oversampling (Onbetap, aliasing A/B against RB-Onbetap-1)

**Setup:** Same layout as RB-Onbetap-1 (saw VCO at C2, Tamed mode) but baked at 4× oversampling with Drive at 80% and Cutoff high (\~70%, several kHz) — a deliberately hot, bright setting. RB-Onbetap-1 runs the MetaModule default 1×; to A/B, set patch 1's knobs C to 80% and A to 70% to match. Output on Out 1/Out 2.
**Panel:** identical to RB-Onbetap-1 (A Cutoff · B Q · C Drive · D Mode · E Cutoff CV Amt · F Q CV Amt · u Drive CV Amt · v Input level · In 1 Cutoff CV · In 2 Q CV · In 3 Drive CV · In 4 VCO Pitch).

**Try:**
1. Play a rising line (keyboard/sequencer into In 4) over 2-3 octaves in RB-Onbetap-1 with its knobs matched to this patch (C 80%, A 70%) — **Expect:** at 1×, inharmonic aliasing "birdies" under the distortion — faint whistles that sweep DOWN as you play UP.
2. Play the same rising line in THIS patch, knobs untouched — **Expect:** clean at 4× — no counter-sweeping birdies; the distortion harmonics all move up with the notes.
3. Push it harder: C to max, sweep A through the top of its range while holding a high note — **Expect:** still no birdies; the harshest available setting stays harmonically well-behaved.
4. Watch the device CPU meter while this patch runs with knobs at their loaded positions — **Expect:** headroom stays acceptable at 4×. **Write down the exact CPU figure here and the figure for RB-Onbetap-1 at 1×** — we're considering locking Onbetap to 4× the way Vespid is, and this pair of numbers is the deciding data point. While you're at it, wiggle A and B and modulate In 1 to catch the worst case, not just the idle figure.

## RB-Vespid-1 — British: the CMOS rasp (Vespid, dirty-Wasp character + Blend morph + output taps)

**Setup:** Fundamental VCO square (C2) → VCA → Vespid audio in L. Vespid in British mode (oversampling is fixed at 4× in all Vespid builds). Freq \~300 Hz, Res 0.6, Drive 0.4, Blend centered (notch). Mix out on Out 1/2; individual taps: BP on Out 3, HP on Out 4, LP on Out 5.

**Panel:**

| Control | Alias | Function |
|---|---|---|
| Knob A | Freq | Vespid cutoff |
| Knob B | Res | Resonance |
| Knob C | Drive | Drive |
| Knob D | Blend | Mix output LP→notch→HP |
| Knob E | Freq CV amt | Attenuvert In 1 |
| Knob F | Res CV amt | Attenuvert In 3 |
| Knob u | Drive CV amt | Drive CV amount (no jack mapped) |
| Knob v | Input level | VCA level feeding the filter (0 = silence) |
| In 1 | Freq CV | Cutoff CV |
| In 2 | Blend CV | Blend CV |
| In 3 | Res CV | Resonance CV |
| In 4 | VCO pitch | Square osc V/Oct |
| Out 1/2 | Mix L/R | Blended output |
| Out 3 | BP L | Bandpass tap |
| Out 4 | HP L | Highpass tap |
| Out 5 | LP L | Lowpass tap |

**Try:**
1. Listen to Out 3 (BP) at the loaded settings, sweeping A slowly through the mids — **Expect:** the Wasp fingerprint: buzzy, nasal, fizzy CMOS tone even at these moderate settings, not a polite clean bandpass.
2. Turn C (Drive) fully down and keep listening — **Expect:** the sound is STILL lightly overdriven — the classic dirty-Wasp rasp is present at Drive 0, not clean.
3. Sweep C from 0 toward max — **Expect:** the grit shifts from ragged/asymmetric fuzz toward a harder, more square, odd-harmonic distortion; louder and nastier but continuous, no dropouts.
4. Patch a slow LFO (\~0.2 Hz, ±5 V) into In 2 and listen to Out 1 — **Expect:** the Mix output morphs LP → notch → HP and back. At the notch point (center) you hear a hollow mid-scoop, NOT silence.
5. With no LFO, park D at noon, then fully CCW, then fully CW — **Expect:** noon = hollow notch, CCW = lowpass (dull), CW = highpass (thin); Outs 3/4/5 keep their own characters the whole time.
6. Compare Outs 1, 3, 4, 5 (move one cable, same knob settings) — **Expect:** all four responses are live simultaneously: BP nasal, HP thin/fizzy, LP round, Mix = whatever D says.
7. Turn B (Res) to max and play notes on In 4 (keyboard/sequencer into VCO pitch) — **Expect:** British verge-of-self-osc: whistles and chirps riding along WITH the notes, but it never runs away into a standalone scream.
8. Still at Res max, turn v (Input level) to 0 — **Expect:** the whistle DIES with the input. British mode cannot self-oscillate without signal; if it keeps ringing, that's a bug.
9. Patch a snappy envelope into In 1 and use E to set depth (try full CW, then partially CCW to invert) — **Expect:** classic percussive filter sweeps; E at CCW flips the sweep direction.
10. Patch an LFO into In 3 and attenuvert with F — **Expect:** resonance animates from smooth to chirpy in time with the LFO; no clicks at the modulation extremes.

## RB-Vespid-2 — German corrected: playable self-oscillation (Vespid, self-osc voice with in-tune 1V/oct tracking)

**Setup:** Vespid alone, German mode (+12 V rails), Corrected pitch tracking. Res pinned at 1.0, Freq \~mid, Drive 0.2, Blend fully LP. No source module — the filter itself is the voice. Audio out on Out 1/2. It should be singing the moment the patch loads.

**Panel:**

| Control | Alias | Function |
|---|---|---|
| Knob A | Freq | Cutoff = oscillation pitch |
| Knob B | Res | Resonance (starts at max) |
| Knob C | Drive | Drive |
| Knob D | Blend | Mix output LP→notch→HP (starts LP) |
| Knob E | Freq CV amt | Attenuvert In 1 |
| In 1 | Pitch (1V/oct) | Cutoff/pitch CV |
| In 2 | Res CV | Resonance CV |
| In 3 | Audio in (optional) | External audio into the filter |
| Out 1/2 | Mix L/R | Output |

**Try:**
1. Load the patch and just listen, nothing patched — **Expect:** the filter sings on its own: a hollow, slightly gritty sine voice. Loud but stable — bounded by the rails, never a runaway blast.
2. Sweep A across its range — **Expect:** the sung pitch follows the knob smoothly across the audible range, tone staying sine-like with a touch of grit.
3. Sequence a melody into In 1 (keyboard or sequencer, E fully CW) — **Expect:** the melody plays IN TUNE over 3-4 octaves. Corrected tracking: octaves are real octaves.
4. Check tuning against a reference: send the same pitch CV (via the mult/stackcable) to a reference oscillator and to In 1, compare by ear at 1, 2, and 3 octaves up — **Expect:** unisons/octaves beat slowly or not at all across the range; no progressive flattening as you go up (that flattening lives in RB-Vespid-3).
5. Slowly back off B from max and find the point where oscillation stops; nudge back up — **Expect:** a clear, repeatable threshold where the voice dies/restarts; near threshold it may chirp before locking into steady tone.
6. Restore B to max and feed audio (any oscillator or a drum loop) into In 3 — **Expect:** filter and oscillation interact — the input pulls, beats against, and colors the sung tone; combined output stays loud-but-bounded, no runaway.
7. Patch an LFO into In 2 with B around the threshold from step 5 — **Expect:** oscillation gates on and off with the LFO — a crude but playable tremolo/chirp effect, no instability.

## RB-Vespid-3 — German hardware drift (Vespid, uncorrected vintage tuning sag)

**Setup:** Same layout as RB-Vespid-2 (Vespid alone, German mode, Res 1.0, Blend LP, Out 1/2), but tracking is UNCORRECTED (authentic circuit sag). Drive is higher (0.6). Note: after load the drone may take a couple of seconds to bloom to full level — that's the uncorrected oscillator settling, not a fault.

**Panel:**

| Control | Alias | Function |
|---|---|---|
| Knob A | Freq | Cutoff = oscillation pitch |
| Knob B | Res | Resonance (starts at max) |
| Knob C | Drive | Drive (starts at 0.6) |
| Knob D | Blend | Mix output LP→notch→HP (starts LP) |
| Knob E | Freq CV amt | Attenuvert In 1 |
| In 1 | Pitch (1V/oct) | Cutoff/pitch CV |
| In 2 | Res CV | Resonance CV |
| In 3 | Audio in (optional) | External audio into the filter |
| Out 1/2 | Mix L/R | Output |

**Try:**
1. Play the SAME melody you used in RB-Vespid-2 step 3 into In 1 — **Expect:** it now drifts FLAT the way the real circuit sags at high resonance: intervals compress/stretch, "vintage out-of-tune." Musical, not broken.
2. Push the melody to the extremes (very low and very high notes) — **Expect:** the sag is worst at the extremes of the range; the middle stays closest to true.
3. A/B the same two-octave jump in RB-Vespid-2 and here — **Expect:** patch 2 lands a true octave; this patch lands audibly flat of it.
4. Aliasing check: drive a bright, hot audio-rate signal (raw saw or square, high pitch) into In 3 with C high and A high — **Expect:** clean: Vespid is fixed at 4× oversampling in every build, so no inharmonic "birdies" sweeping the wrong way as you play up.
5. While the patch runs, check the device CPU meter — **Expect:** acceptable headroom (no audio dropouts while turning knobs); Vespid always pays the 4× cost, so this is its worst-case CPU figure.

## RB-Particules-1 — Free-running texture & qualities (Particules, granular texture from a baked-in drum loop)

**Setup:** A drum loop (drum-loop.wav, looping) auto-starts about 2 s after the patch loads and feeds Particules. All twelve knobs are spoken for, so the loop-restart control lives on Knob Set 2 (*Utility*): if the loop isn't playing, switch there, twist z up then down, and switch back. Particules output on Out 1 (L) / Out 2 (R). Baked state: Triggers seed mode, auto-gain on, pitch quantizer off, grain trigger out off. Starting texture: Density 65%, Time 30%, Size 55%, Shape 45%, Reverb 25%, Dry/Wet 85%, Time attenuverter fixed at +75%. Seed input is deliberately unmapped — Density free-runs.

**Panel:**

| Control | Alias | Function |
|---|---|---|
| Knob A | Density | Grain density/probability |
| Knob B | Time | Playback position in buffer |
| Knob C | Size | Grain length (CCW = reversed) |
| Knob D | Shape | Grain envelope |
| Knob E | Pitch | Grain pitch (semitone notches) |
| Knob F | Dry/Wet | Mix |
| Knob u | Freeze (up=on) | Freeze latch |
| Knob v | Quality | Bright / Cold / Sunny / Scorched |
| Knob w | Feedback | Feedback amount |
| Knob x | Reverb | Reverb amount |
| Knob y | Size AR | Size attenurandomizer |
| Knob z | Pitch AR | Pitch attenurandomizer |
| Knob z (Set 2: *Utility*) | Loop restart | BWAVP Play (twist up then down) |
| In 1 | Time CV | Time CV (attenuverted +75%) |
| Gate In 1 | Freeze gate | Freeze while high |

**Try:**
1. Sweep A (Density) through its whole range — **Expect:** exactly 12 o'clock is silence; clockwise gives random grain clouds that thicken as you go; counter-clockwise gives a metronomic pulse, denser the further you turn.
2. Twist u up past mid (Freeze on), then sweep B (Time) — **Expect:** you scrub through the frozen snapshot of the loop; new incoming audio is ignored until u comes back down. A gate into Gate In 1 should do the same freeze.
3. Sweep C (Size) from about 9 o'clock to full — **Expect:** grains lengthen from short ticks to long overlapping swells; take C counter-clockwise of center and grains play reversed.
4. Sweep D (Shape) — **Expect:** grain attacks go from clicky/percussive at one end to soft swells at the other.
5. Turn E (Pitch) — **Expect:** grain pitch steps through semitone notches; center = original pitch, roughly 3 o'clock = +12 st.
6. With NOTHING patched into any CV jack, move y (Size AR) and z (Pitch AR): center = every grain identical; counter-clockwise = values spread with a peak near the knob setting; clockwise = uniform random spread — **Expect:** a touch of both makes the texture breathe; back to center makes it static again. (These jacks are unmapped, so this is the pure randomize behavior.)
7. Patch a slow LFO into In 1 — **Expect:** the read position scrubs back and forth through the recorded loop (Time attenuverter is fixed at +75%, so the sweep is wide but not full-range). With the cable removed, In 1 should have no effect; flag it if the empty jack changes anything.
8. Step v (Quality) through its four positions — **Expect:** Bright = clean; Cold = 12-bit Clouds-style grit; Sunny = darker with a GENTLE cassette waver (not seasick); Scorched = 8-bit crunch plus obvious warble. Each change re-formats the buffer: brief mute and the recorded audio clears, then the loop refills.
9. Freeze with u, then try to move v — **Expect:** quality does NOT change while Freeze is engaged; release u and the change takes.
10. Set w (Feedback) to about 60% on Scorched, let it run — **Expect:** repeats degrade into tape mush. Same w on Bright — **Expect:** stays clean and just builds, held in check by the per-quality limiter.
11. Turn x (Reverb) up past noon — **Expect:** a smooth reverb tail blooms around the grains; back to 0 kills it.
12. Turn F (Dry/Wet) down — **Expect:** the untouched drum loop fades in under the grains.

## RB-Particules-2 — Clocked melodic grains + grain trigger out (Particules, Seed-clocked quantized grain voice)

**Setup:** A C3 saw drone feeds Particules continuously. Baked state: Triggers seed mode, pitch quantizer locked to A minor, grain trigger out ON — so Out 1 is a MONO audio sum and Out 2 fires a 1 ms trigger per grain. Dry/Wet 100%, Time 10%, Density at 12 o'clock, Pitch attenuverter full clockwise (clean 1 V/oct tracking), Reverb 15%.

**Panel:**

| Control | Alias | Function |
|---|---|---|
| Knob A | Density | Grain probability/division vs clock |
| Knob B | Time | Playback position |
| Knob C | Size | Grain length |
| Knob D | Shape | Grain envelope |
| Knob E | Pitch | Grain pitch |
| Knob F | Dry/Wet | Mix |
| Knob u | Pitch AR | Pitch attenuverter/randomizer |
| Knob v | Quality | Bright / Cold / Sunny / Scorched |
| Gate In 1 | Seed clock | Clock for grain spawning |
| In 1 | Pitch CV (V/oct) | Melody input, quantized |
| In 2 | Density CV | Density CV |

**Try:**
1. Patch a steady clock (e.g. 4-8 Hz) into Gate In 1 with A (Density) at exactly 12 o'clock — **Expect:** SILENCE — probability is 0 at center when clocked.
2. Turn A clockwise from center — **Expect:** grains start appearing on clock ticks, more of the ticks spawning grains the further you turn, until nearly every tick fires; everything stays locked to the clock, never between ticks.
3. Turn A counter-clockwise from center — **Expect:** clean clock divisions (every 2nd tick, every 4th, ...) — a regular sub-rhythm, not random thinning.
4. Sequence In 1 with a melody (1 V/oct) — **Expect:** the grains play the melody, with every pitch snapped to A minor. Detune the sequence slightly — the quantizer pulls notes back to the scale.
5. Turn u (Pitch AR) from full clockwise back toward center — **Expect:** at full CW the CV tracks cleanly 1 V/oct; toward center and below, the pitch CV increasingly becomes per-grain randomization instead of tracking.
6. Listen to Out 2 (or scope it) — **Expect:** a 1 ms trigger per grain — audible as clicks; on the rig, drive an envelope/voice from it for a lockstep second voice, with distinct triggers even for back-to-back grains.
7. Confirm Out 1 alone carries the full audio — **Expect:** mono sum (both stereo sides mixed) while grain triggers are enabled.
8. Unplug the clock from Gate In 1 — **Expect:** Particules free-runs on Density again (clock detection follows the physical cable). Flag it if the empty mapped jack still acts clocked.

## RB-Particules-3 — Gates seed mode (Particules, grains gated by an external gate)

**Setup:** A C3 saw drone feeds Particules continuously. Baked state: Seed CV mode = GATES (grains spawn only while the gate is high), quantizer off, grain trigger out off, auto-gain on. Dry/Wet 100%, Time 10%, Size 50%, Shape 30%, Reverb 15%, Density at 12 o'clock. Stereo out on Out 1/2.

**Panel:**

| Control | Alias | Function |
|---|---|---|
| Knob A | Density | Repetition rate inside each gate |
| Knob B | Size | Grain length |
| Knob C | Shape | Grain envelope |
| Knob D | Time | Playback position |
| Knob E | Dry/Wet | Mix |
| Knob F | Quality | Bright / Cold / Sunny / Scorched |
| Gate In 1 | Seed gate | Grains fire while high |
| In 1 | Time CV | Time CV |
| In 2 | Pitch CV | Pitch CV (V/oct) |

**Try:**
1. Patch a slow gate (long steps, e.g. 1 s high / 1 s low) into Gate In 1 — **Expect:** grains fire ONLY while the gate is high — rhythmic puffs of texture that stop dead when the gate falls, not a continuous stream.
2. With the gate running, turn A (Density) clockwise from center — **Expect:** the repetition rate INSIDE each gate rises — each puff gets busier; counter-clockwise thins it out.
3. Set A to exactly 12 o'clock — **Expect:** exactly one grain per gate — a single hit on each rising edge.
4. Unplug the cable from Gate In 1 — **Expect:** behaves identically to Triggers mode: free-runs on Density (silent at 12 o'clock, clouds CW, pulse CCW). Gate detection follows the physical cable, so the empty mapped jack should read as unpatched — flag any surprise here.
5. While gated puffs play, shape them with B (Size) and C (Shape) — **Expect:** longer grains overlap into small swells; Shape moves attacks from clicky to soft.

## RB-Ondes-1 — Morphing wavetable voice (Ondes, drone/bank/position tour)

**Setup:** Ondes alone, droning from the moment the patch loads. Mono output on Out 1. Baked state: Pitch center (0 st), Position center, Position CV amount .75, Bank fully CCW, Bank CV amount center (off).

**Panel:**

| Control | Maps to |
|---|---|
| Knob A | Pitch |
| Knob B | Bank |
| Knob C | Position |
| Knob D | Pos CV amt |
| Knob E | Bank CV amt |
| In 1 | Pitch (V/oct) |
| In 2 | Position CV |
| In 3 | Bank CV |

**Try:**
1. Load the patch — **Expect:** a steady drone on Out 1 immediately, no input needed.
2. Sweep B (Bank) slowly from 0 to max — **Expect:** three families in order: soft sines/triangles/drawbar organ tones (low third), buzzy vocal formants (middle), then the Braids imports — choir/metal/drone flavors (top). Every in-between position should be a usable hybrid, not garbage.
3. Sweep C (Position) slowly at several Bank settings — **Expect:** continuous timbre blends — no steps, zipper noise, or clicks anywhere in the travel.
4. Patch a slow LFO (\~0.1-0.5 Hz) into In 2 — **Expect:** the timbre breathes smoothly through the table; D scales the sweep depth (at 12 o'clock the LFO does nothing; the baked \~.75 gives a moderate positive sweep).
5. Turn A (Pitch) slowly across its range — **Expect:** the knob settles into notches at musical intervals — octaves, fifths, unison — rather than free-gliding, over a ±24 st span.
6. Play a keyboard or sequencer into In 1 — **Expect:** accurate V/oct tracking; octaves on the keyboard are octaves out, over at least 2-3 octaves.
7. Patch an audio-rate oscillator into In 2 and set D to about 2 o'clock (\~.65) — **Expect:** an FM-like clangorous growl — pitched and playable, not noise hash. Back D toward 12 o'clock — **Expect:** the growl cleans up smoothly back to the plain wavetable tone.
8. Patch an LFO or sequencer into In 3 and raise E (Bank CV amt) above center — **Expect:** the CV walks the drone through the bank families, same character as turning B by hand; at E center the input does nothing.

## RB-Retours-1 — Tape delay, slicer, shimmer (Retours, full feature tour in Tape/doppler time-change mode)

**Setup:** Drum loop (BWAVP, looping, auto-started \~2 s after load) feeds Retours in stereo; delay mix on Out 1/2. Baked state: Tape (doppler) time-change, Bright quality. Feedback starts at 40%, Dry/Wet 50%, Interval at its default .35, Time at 1×. If the loop ever stops, twist knob z up and back down to restart it.

**Panel:**

| Control | Alias | What it does |
|---|---|---|
| Knob A | Interval | Base delay time (12 o'clock = longest; far CCW/CW = audio rate) |
| Knob B | Time | Time multiplier 1-16× |
| Knob C | Feedback | Regeneration (arrow at 90% = unity) |
| Knob D | Pitch | Shimmer pitch shift (center = bypass, notched) |
| Knob E | Shape | Repeat envelope: flat / gated / swell / ramp |
| Knob F | Dry/Wet | Mix |
| Knob u | Slice (up=hold) | Latch slice/beat-repeat |
| Knob v | Quality | Bright / Cold / Sunny / Scorched |
| Knob w | Tap (twist up) | Tap tempo button |
| Knob x | Time AR | Time attenurandomizer |
| Knob y | Pitch AR | Pitch attenurandomizer |
| Knob z | Loop restart | BWAVP Play (twist up then down to replay) |
| In 1 | Time CV | Time CV |
| In 2 | Interval CV | Interval CV |
| Gate In 1 | Clock | Clock input (patching it switches Interval to divider mode) |
| Gate In 2 | Slice gate | Momentary slice |

**Try:**

*Manual time & doppler*

1. With nothing patched, sweep A from 12 o'clock toward CCW — **Expect:** a single clean echo tap whose delay shortens as you turn; 12 o'clock is the longest delay.
2. Sweep A to the CW side — **Expect:** a second, uneven tap joins in: a galloping two-tap pattern, not an even doubling.
3. Set A near 12 o'clock, then turn B up through its range — **Expect:** the delay time multiplies 1-16×; repeats get much longer.
4. While repeats are audibly regenerating (C \~60%), grab A or B and turn — **Expect:** the echoes pitch-bend like varispeed tape: swooping DOWN while you lengthen, UP while you shorten, with no clicks.
5. Patch a small slow sine LFO into In 1 (Time CV) — **Expect:** a tape-warble chorus on the repeats, gentle cyclic detune.
6. Unpatch In 1 and turn x (Time AR) toward CCW — **Expect:** a slow tape-mechanism wander creeps into the repeat timing — irregular drift, not a regular vibrato. (This relies on the jack sensing no cable; flag anything odd if a mapped-but-empty jack misbehaves.)

*Clocked mode*

7. Patch a steady clock into Gate In 1 — **Expect:** Interval (A) becomes a divider: CCW side steps 1/2, 1/4, 1/8, 1/16; CW side gives triplet divisions. Repeats lock to the clock.
8. Nudge the clock tempo up and down while it runs — **Expect:** repeats stay locked to the new tempo, no free-run drift.
9. With the clock still patched, turn B — **Expect:** Time snaps to musical multiples instead of a continuous 1-16× sweep.
10. Unpatch the clock, then twist w up/down four times in rhythm — **Expect:** tap tempo takes over; the light blinks the tapped beat and the tempo holds. To change it, just re-tap (clearing a saved tempo is menu-only, so it persists otherwise).
11. Turn E (Shape) up in stages while clocked — **Expect:** repeat envelope steps flat → gated → swell → slow ramp, phase-locked to the beat.

*Slice / beat-repeat*

12. Twist u up (Slice latch) while the drum loop plays — **Expect:** recording stops instantly and a slice repeats — an immediate beat-repeat hold.
13. While sliced, turn B — **Expect:** B chooses WHICH slice of the held audio repeats.
14. While sliced, turn A — **Expect:** A sets the slice length. Twist u back down to release.
15. Send a gate into Gate In 2 instead — **Expect:** same slice behavior, held only while the gate is high.

*Shimmer / pitch*

16. Turn D to the +12 st notch and set C \~65% — **Expect:** each repeat climbs an octave — a rising shimmer ladder.
17. Return D exactly to center — **Expect:** the shifter is truly bypassed: repeats are clean copies, no chorus blur or detune haze.
18. With nothing in a Pitch CV jack, turn y (Pitch AR) off-center — **Expect:** random per-repeat pitch spread; back to center = exact.

*Quality & feedback*

19. Set C just below the 90% arrow — **Expect:** repeats decay away. At the arrow: they hold at unity. Above it: they grow each pass.
20. With C above unity on Bright (v at 0) — **Expect:** the stack builds clean and eventually brickwall-limits without distortion of tone.
21. Step v to Sunny, then Scorched, and repeat the over-unity build — **Expect:** each pass gets darker and more saturated; Scorched adds obvious warble on top. Each Quality limits differently — none should run away into harsh digital clipping.
22. Step v through all four positions while repeats sound — **Expect:** a brief mute/re-format at each change is acceptable; character steps Bright (clean) → Cold → Sunny → Scorched.

## RB-Retours-2 — Crossfade time-change (Retours, same rig as RB-Retours-1 with digital-clean time jumps)

**Setup:** Identical to RB-Retours-1 (drum loop → Retours, delay mix on Out 1/2) except the baked time-change mode is Crossfade instead of Tape. Same knob/jack layout and aliases as RB-Retours-1; knob z restarts the loop.

**Panel:** Same as RB-Retours-1: A=Interval, B=Time, C=Feedback, D=Pitch, E=Shape, F=Dry/Wet, u=Slice (up=hold), v=Quality, w=Tap (twist up), x=Time AR, y=Pitch AR, z=Loop restart; In 1=Time CV, In 2=Interval CV, Gate In 1=Clock, Gate In 2=Slice gate.

**Try:**

1. With repeats regenerating (C \~60%), grab A or B and turn — **Expect:** the delay JUMPS cleanly to the new time: pitch-neutral, click-free, no tape swoop. Each jump should land waveform-aligned (no tick at the splice).
2. Sweep A fast across a wide range — **Expect:** the repeats stay clean and intelligible rather than garbling or smearing.
3. Do the same moves back-to-back against RB-Retours-1 — **Expect:** the two patches feel like a tape machine (1, pitch-bending) vs a digital delay (2, stepping cleanly); if they sound alike, that's a bug.
4. Spot-check the rest: clock into Gate In 1 (divider mode + locked repeats), slice via u or Gate In 2, shimmer with D at +12 and C \~65% — **Expect:** all behave exactly as in RB-Retours-1; only manual/CV time changes differ.

## RB-Retours-3 — Karplus-Strong string (Retours, audio-rate delay as a plucked-string voice)

**Setup:** Retours alone, no source module — you pluck it. Baked state: Tape (doppler) time-change, Bright quality, Interval at .05 (audio-rate short delay), Feedback 85%, Dry/Wet 80%. String voice on Out 1/2.

**Panel:**

| Control | Alias | What it does |
|---|---|---|
| Knob A | Interval | String pitch (delay time at audio rate) |
| Knob B | Feedback | Ring/sustain length |
| Knob C | Dry/Wet | Mix (mostly wet by default) |
| Knob D | Pitch | Per-repeat pitch shift (center = off) |
| Knob v | Quality | Bright / Cold / Sunny / Scorched |
| In 1 | Pluck in (bursts/clicks) | Excitation input |
| In 2 | Interval CV (V/oct) | Pitch CV |

**Try:**

1. Feed short clicks, gates, or noise bursts into In 1 — **Expect:** each one rings out as a plucked string with a clear pitch, not a discrete echo.
2. Sequence In 2 with V/oct pitches while plucking — **Expect:** a playable Karplus voice; tuning stays stable and in tune over roughly 2 octaves.
3. Turn B up toward max while plucking — **Expect:** notes sustain longer and longer but stay bounded — no runaway blow-up even near the top.
4. Grab A and turn it by hand while a note rings — **Expect:** a tape-doppler glissando between pitches, a continuous swoop rather than a step.
5. Move D off-center and pluck — **Expect:** each ring spirals upward or downward in pitch as it decays — strange, metallic, but pitched and controllable; back at the center notch the spiral stops dead.



== DONE ==
## RB-Loooop-1 — Ensemble & overdub modes (Loooop, four-head playback + overdub mode tour)

**Setup:** A drum loop WAV player feeds Loooop's stereo input and auto-starts ~2 s after the patch loads (you still have to record it into Loooop yourself). A slow sine LFO (0.1 Hz, ±2.5 V) is wired to Purple's Pan CV. Baked in: Dry/Wet 50%, Yellow at 2× speed panned left, Blue at −1× (reverse) panned right, Purple at 1.5× center, all head Levels at default, all options at defaults (crossfade ON, trigger record mode). Listen on Out 1/2.

**Panel:**

| Control | Maps to |
|---|---|
| Knob A | Red Size |
| Knob B | Red Position |
| Knob C | Yellow Speed |
| Knob D | Dry/Wet |
| Knob E | Grid |
| Knob F | Overdub |
| Knob u | Record (twist up) |
| Knob v | Clear |
| Knob w | Red Level |
| Knob x | Purple Level |
| Knob y | Red Jitter |
| Knob z | Loop restart (WAV player Play) |
| In 1 | Red Pos CV |
| Gate In 1 | Record trig |
| Gate In 2 | Clear trig |

**Knob Set 2 (*Head Levels*)** — for isolating one head at a time:

| Control | Maps to |
|---|---|
| Knob A | Red Level |
| Knob B | Yellow Level |
| Knob C | Blue Level |
| Knob D | Purple Level |
| Knob E | Dry/Wet (same param as D in Set 1) |

All four Levels load at 25%; after soloing a head, return them there and switch back to Knob Set 1.

**Try:**
1. Wait ~2 s after load until the drum loop is audible, then twist u fully up to start recording; after 4-8 bars twist u back down — **Expect:** loop plays back immediately through all four heads; twisting u up = button press, down = release.
2. Instead of u, send a trigger into Gate In 1 to start, another to stop — **Expect:** identical record start/stop behavior from the jack.
3. Listen to the ensemble with all heads up — **Expect:** four simultaneous voices: Red at unison, Yellow an octave up panned left, Blue playing in reverse panned right, Purple at 1.5× in the center. To check any single voice, switch to Knob Set 2 (*Head Levels*), pull the other three Levels to 0 and push E toward full wet, then restore (Levels 25%, E noon) and switch back.
4. Sit on the loop for a minute and listen to Purple — **Expect:** Purple's voice drifts slowly left-right (\~10 s per cycle) from the baked-in LFO; the other heads stay put. If the drift is hard to pick out of the ensemble, solo Purple on Knob Set 2 first.
5. Solo Yellow on Knob Set 2 (Red/Blue/Purple Levels to 0), switch back to Knob Set 1, then turn C (Yellow Speed): 3 o'clock = 1×, full CW = 2×, 9 o'clock = −1×, noon = stopped region — **Expect:** Yellow re-pitches smoothly; below noon it runs backward. Restore the levels when done. (This is the step that punishes the factory catchup setting: knob C is Blue Level on Set 2 and Yellow Speed on Set 1. With "Track when equal" set as advised above, Yellow Speed stays put until the knob reaches it.)
6. Loop seam check: with the loop playing, listen across the wrap point several times — **Expect:** no click or tick at the seam (crossfade is ON by default in this patch; contrast with RB-Loooop-3, which has it off).
7. Overdub modes on F, full CCW = Layer: while the loop plays, twist u up for a pass, then down (the still-running drum loop re-records over itself at a new offset — that IS the new material); repeat several passes — **Expect:** each pass ducks the older material by roughly 1 dB; old layers slowly recede but never dull.
8. F at \~10 o'clock = Decay: overdub several passes — **Expect:** old material both loses level AND loses high end, getting duller each pass.
9. F at noon = Add: overdub several hot passes — **Expect:** layers pile up at full level and eventually clip/distort — that is the mode working, not a bug.
10. F at \~2 o'clock = Replace: twist u up mid-loop, then down after a beat — **Expect:** a clean punch-in — new audio replaces the old only where you held record.
11. F full CW = Lock: twist u up and try to record — **Expect:** nothing records; the loop is protected and Record is ignored.
12. Turn A (Red Size) down to \~25%, then sweep B (Red Position) — **Expect:** Red scrubs a short window around the loop; at full Size (A max), B and y do nothing — that's by design. (If the other heads mask the scrubbing, solo Red on Knob Set 2 for steps 12-14.)
13. With A partway down, raise y (Red Jitter) — **Expect:** Red's window hops to random positions, from subtle shuffle (low y) to full scatter (high y).
14. Turn E (Grid) up one step at a time (Off/4/8/16/32/64) while scrubbing A and B — **Expect:** Red's Size and Position snap to grid divisions; changes land musically on the beat instead of free-sliding.
15. Sweep D (Dry/Wet) full CCW then full CW — **Expect:** CCW = only the live drum-loop input, CW = only the heads; noon = the baked-in 50/50 blend.
16. Twist v up (or send a trigger into Gate In 2) — **Expect:** the loop erases; heads go silent until you record again.
17. If the source WAV ever stops (it shouldn't with Loop on), twist z up then down — **Expect:** the drum loop restarts from the top.

## RB-Loooop-2 — Self-loading gate record + grid slicer (Loooop, one-shot slice heads on a 16-grid)

**Setup:** The WAV player is set to play the drum file ONCE; its Play Gate is cabled to Loooop's Record jack, and Loooop's Record jack is in Gate mode — so ~2 s after the patch loads, the file plays through once and records itself into Loooop with no help from you. Baked in: Grid = 16, Red and Yellow are one-shot slice heads (Size 10%, Levels 40%, Positions 20%/60%), Blue plays the whole loop at full Size, Purple is grid-excluded with Jitter 50% at Level 20%, Dry/Wet 100%. Mix on Out 1/2, Red's own left output on Out 3.

**Panel:**

| Control | Maps to |
|---|---|
| Knob A | Red Size |
| Knob B | Red Position |
| Knob C | Yellow Size |
| Knob D | Yellow Position |
| Knob E | Grid |
| Knob F | Dry/Wet |
| Knob u | Purple Jitter |
| Knob v | Purple Level |
| Knob w | Record |
| Knob x | Clear |
| Knob y | Blue Speed |
| Knob z | Reload loop (WAV player Play) |
| In 1 | Blue Jump (0-10V) |
| In 2 | Red Pos CV |
| Gate In 1 | Red one-shot trig |
| Gate In 2 | Yellow one-shot trig |

**Knob Set 2 (*Head Levels*)** — for isolating one head at a time:

| Control | Maps to |
|---|---|
| Knob A | Red Level |
| Knob B | Yellow Level |
| Knob C | Blue Level |
| Knob D | Purple Level (same param as v in Set 1) |
| Knob E | Dry/Wet (same param as F in Set 1) |

Loaded Levels: Red 40%, Yellow 40%, Blue 25%, Purple 20% — return them there after soloing.

**Try:**
1. Load the patch and touch nothing — **Expect:** ~2 s of silence, then the drum file plays once while recording itself; when it ends, Blue (full loop) and Purple (jittery fragments) start playing back on their own.
2. Check the display lanes for Red and Yellow — **Expect:** both lanes dim/inactive: one-shot heads stay silent until triggered.
3. Send a trigger into Gate In 1 — **Expect:** Red fires one clean slice and stops; each trigger = exactly one slice, ending with a short fade, not a click. (Blue and Purple keep playing underneath; for a clean listen, pull their Levels to 0 on Knob Set 2 (*Head Levels*) for steps 3-7, then restore.)
4. Same into Gate In 2 — **Expect:** Yellow fires its own one-shot slice from a different part of the loop (Position 60% vs Red's 20%).
5. While triggering Red, step B (Red Position) to different positions, then A (Red Size) — **Expect:** every slice start and length snaps to 1/16 divisions of the loop; the knobs move in audible whole-segment jumps, never landing mid-segment.
6. Do the same with C/D for Yellow — **Expect:** identical grid-snapped behavior on the second slice head.
7. Turn E (Grid) down to Off, retrigger Red while moving B — **Expect:** slices now start anywhere (free, un-snapped); return E to \~3 o'clock (16) to restore snapping.
8. Listen to Purple while the grid is on — **Expect:** Purple ignores the grid entirely (grid-exclude option): its fragments wander to un-quantized positions.
9. Sweep u (Purple Jitter) from 0 to max — **Expect:** at 0 Purple sits still; up high it scatters across the whole loop. Set v to taste or 0 to mute it.
10. Patch a square LFO (\~1-4 Hz, 0-10 V) into In 1 — **Expect:** Blue hard-stutters, jumping between two loop points in time with the square; a beat-repeat chop, no glide between them.
11. Patch a slow LFO or sequencer CV into In 2 — **Expect:** Red's slice position moves under CV, still snapping to the 16-grid.
12. Compare Out 3 against Out 1/2 — **Expect:** Out 3 carries only Red's slices (left channel), isolated from the mix.
13. Turn y (Blue Speed) to 9 o'clock — **Expect:** Blue plays the whole loop in reverse; 3 o'clock returns it to 1× (solo Blue on Knob Set 2 if Purple's fragments get in the way).
14. Twist z up then down — **Expect:** the drum file plays through once more and re-records itself (Play Gate drives Record again), replacing the loop content.
15. Twist x up — **Expect:** the loop clears; then twist z to reload it as in step 14.

## RB-Loooop-3 — Speed V/Oct + raw seams + keeps-overdubbing (Loooop, chromatic loop transposition)

**Setup:** A triangle VCO at C3 is Loooop's only input; you play it from a keyboard via In 1. Baked in: Red Speed is in V/Oct mode (In 2 transposes chromatically), Crossfade is OFF (raw seams), "when recording ends" = Keeps overdubbing, only Red is audible (Yellow/Blue/Purple Levels = 0, Red Level 50%), Dry/Wet 50%. Listen on Out 1/2.

**Panel:**

| Control | Maps to |
|---|---|
| Knob A | Red Speed |
| Knob B | Red Size |
| Knob C | Dry/Wet |
| Knob D | Overdub |
| Knob u | Record (twist up) |
| Knob v | Clear |
| Knob w | Red Position |
| In 1 | VCO pitch (V/Oct) |
| In 2 | Red Speed V/Oct |
| Gate In 1 | Record trig |

**Try:**
1. Plug a keyboard into In 1 and play — **Expect:** you hear the triangle VCO live (Dry/Wet is at 50%, so dry input passes).
2. Twist u up and play a short phrase, then twist u down — **Expect:** recording starts on the FIRST upward twist (the button and the Gate In 1 jack fire independently — don't double-arm with both or you'll toggle it twice); the phrase loops on Red.
3. Keep listening after you stop playing, without touching u — **Expect:** the loop KEEPS OVERDUBBING (option baked in): anything you play on the keyboard keeps stacking into the loop until you twist u up/down again to stop recording.
4. Twist u again to close recording, then patch the keyboard (or a sequencer) into In 2 instead — **Expect:** the whole loop transposes CHROMATICALLY with the CV — semitone steps, in tune like an oscillator.
5. Play In 2 across at least 2 octaves against a reference pitch — **Expect:** octaves are exact (V/Oct tracking on Red Speed); +1 V doubles playback speed/pitch, −1 V halves it.
6. While In 2 holds a note, turn A (Red Speed) — **Expect:** the knob still works ON TOP of the CV, shifting the transposed pitch further.
7. Let the loop wrap several times and listen closely to the seam — **Expect:** a hard tick/click at every wrap — Crossfade is OFF here on purpose; compare with RB-Loooop-1, where the same seam is silent.
8. Confirm only one voice sounds — **Expect:** only Red is audible; Yellow, Blue, and Purple levels are baked to zero.
9. Turn B (Red Size) down and sweep w (Red Position) — **Expect:** Red scrubs a shorter window of the recorded phrase; each wrap of the short window also seams with a raw tick.
10. Set D to \~2 o'clock (Replace) and punch in a new note with u — **Expect:** the new note replaces that stretch of the phrase cleanly; return D full CCW (Layer) for normal stacking.
11. Use a trigger into Gate In 1 instead of u for one record cycle — **Expect:** same start/stop behavior from the jack.
12. Twist v up — **Expect:** loop erases; you're back to the live VCO only.

## RB-Lop-1 — Single head parity (Löp, one Loooop head's full feature set)

**Setup:** BWAVP loops `drum-loop.wav` (keep the wav next to the .yml) into Löp; a StartupDelay auto-starts the player \~2 s after load. Löp output on Out 1/2. Baked state: Dry/Wet .6, Size full, Position center, Speed 1×, Jitter 0, Grid Off, all option switches at defaults (Record jack = Trigger, Crossfade On, loop-start trig mode, linear Speed CV). Nothing is recorded yet — you'll hear the dry drum loop until you record.

**Panel:**

| Control | Maps to |
|---|---|
| Knob A | Size |
| Knob B | Position |
| Knob C | Speed |
| Knob D | Jitter |
| Knob E | Grid |
| Knob F | Dry/Wet |
| Knob u | Record (twist up) |
| Knob v | Clear (twist up) |
| Knob w | Overdub mode |
| Knob z | Loop restart (BWAVP Play) |
| In 1 | Jump (0-10V) |
| In 2 | Speed CV |
| Gate In 1 | Record trig |
| Gate In 2 | Retrig head |

**Try:**
1. Wait \~2 s after load — **Expect:** the drum loop plays on Out 1/2.
2. Twist u up past mid to start recording, wait one pass of the loop (\~14 s of file, record as much as you like), twist u back down — **Expect:** recording stops and the head plays back your loop seamlessly; F at .6 mixes it over the still-running dry loop.
3. Clear (twist v up/down), then record again but fire a trigger into Gate In 1 to start and again to stop — **Expect:** identical record behavior to the knob (Trigger mode), start/stop on each pulse.
4. Turn A (Size) down to around 9 o'clock — **Expect:** the head plays a short chunk of the loop, repeating faster.
5. With A still low, sweep B (Position) — **Expect:** the chunk scrubs to different spots in the recording. Then return A to max and move B — **Expect:** no audible change (Position does nothing at full Size — that's correct).
6. Sweep C (Speed): from its default \~3-o'clock (1×) up to max — **Expect:** double speed, octave up. Down through mid — **Expect:** slows, momentarily stops at 12 o'clock, then plays backward below it; fully CCW is −2× (reverse, octave up).
7. With A partway down, raise D (Jitter) — **Expect:** the playback point scatters randomly around Position; at 0 it locks steady again. (Like Position, Jitter does nothing at full Size.)
8. Turn E (Grid) up one step at a time (Off/4/8/16/32/64) with A and B moving — **Expect:** Size and Position snap to clean rhythmic divisions of the loop; Off restores continuous control.
9. Send a clock or button gate into Gate In 2 — **Expect:** each pulse retrigs the head from its loop start — rhythmic restarts in time with your clock.
10. Send a stepped 0-10 V sequence (or square LFO) into In 1 (Jump) — **Expect:** playback jumps hard between points in the recording — a stutter effect keyed to the voltage.
11. Patch a slow LFO into In 2 (Speed CV) — **Expect:** speed glides up and down around the C knob's setting — linear varispeed (this patch has V/Oct off), pitch bending smoothly through the LFO cycle.
12. Set w (Overdub) fully CCW (Layer) and record several passes over an existing loop — **Expect:** old material ducks about 1 dB per pass — level fades slowly, tone stays bright.
13. Now set w to the second position (\~10 o'clock, Decay) and overdub several passes — **Expect:** old layers lose level AND high end — each pass audibly duller than Layer mode at the same pass count. Spend time here; this is the key A/B.
14. Try the remaining w positions: center (Add) — layers stack at full level and eventually clip/saturate; \~2 o'clock (Replace) — new audio punches out the old where you play; full CW (Lock) — twisting u does nothing, loop is protected.
15. Listen across the loop seam with default settings — **Expect:** no click at the wrap (Crossfade is On).
16. Twist z up/down anytime — **Expect:** the source drum loop restarts from the top.

## RB-MF20-1 — OTA acid & vocal band (MF-20, series HP→LP character test)

**Setup:** A saw VCO at C2 runs into the MF-20 in OTA mode (baked state) through a unity-gain VCA (knob w = Input level, loaded at full). LP starts \~150 Hz with Peak at 70%, HP is parked at the bottom of its range, all CV amounts full. Filter output on Out 1/Out 2.
**Panel:**

| Control | Maps to |
|---|---|
| Knob A | LP Cutoff |
| Knob B | LP Peak |
| Knob C | HP Cutoff |
| Knob D | HP Peak |
| Knob E | Drive |
| Knob F | LP CV Amt |
| Knob u | Total CV Amt |
| Knob v | HP CV Amt |
| Knob w | Input level (saw into the filter; 0 = silence) |
| In 1 | LP Cutoff CV |
| In 2 | Total Cutoff CV |
| In 3 | VCO Pitch (1V/oct) |
| In 4 | HP Cutoff CV |

**Try:**
1. Patch an envelope (or decaying LFO) into In 1 and play notes — **Expect:** classic acid squelch, cutoff riding the envelope; knob F attenuverts it, and below center the sweep inverts (closes on attack).
2. Sweep Drive E from 0 to max with A around 10 o'clock — **Expect:** smooth, creamy OTA thickening; it warms up rather than turning buzzy.
3. Turn B (LP Peak) to 100%, then A (LP Cutoff) well below the bass note — **Expect:** a clean sine whistle rides above the bass note; both filters self-oscillate cleanly at full Peak (repeat with C/D for the HP). To judge the whistle's purity, turn w (Input level) to 0 — the whistle alone should remain, saw gone; restore w to full afterwards.
4. Set C (HP) near \~300 Hz (about 10-11 o'clock) and A (LP) near \~1.5 kHz (about 2 o'clock), then patch a slow LFO into In 2 (Total) — **Expect:** the whole HP→LP passband slides as ONE unit — a vowel-like formant sweep, band width constant.
5. Swap the band: set C above A (HP cutoff higher than LP cutoff) — **Expect:** the sound thins into a notch/phaser-like residue instead of a band; still audible, not silence.
6. V/Oct check: with the LP self-oscillating (step 3), patch the same pitch CV into In 1 (F full CW) and In 3 — use the mult/stackcable — **Expect:** the whistle tracks the VCO exactly; a 1 V step moves the whistle precisely one octave, staying in tune with the saw.
7. Trim u (Total CV Amt) toward 0 while the In 2 LFO runs — **Expect:** the formant sweep from step 4 shrinks to nothing; u scales only the Total CV input.

## RB-MF20-2 — Korg35 mode (MF-20, direct A/B against RB-MF20-1)

**Setup:** Byte-for-byte the same patch as RB-MF20-1 — saw VCO at C2, LP \~150 Hz, Peak 70%, HP parked low, CV amounts full — except the filter core is Korg35 instead of OTA. Output on Out 1/Out 2. Load the two patches back to back with the SAME knob positions for every test.
**Panel:** identical to RB-MF20-1 (A LP Cutoff · B LP Peak · C HP Cutoff · D HP Peak · E Drive · F LP CV Amt · u Total CV Amt · v HP CV Amt · w Input level · In 1 LP CV · In 2 Total CV · In 3 VCO Pitch · In 4 HP CV).

**Try:**
1. With knobs untouched (loaded defaults), just listen and flip between the two patches — **Expect:** Korg35 resonance is edgier and raspier than the OTA's smooth ring at the same Peak setting.
2. Push Drive E past noon with B (LP Peak) above 70% — **Expect:** a harder, buzzier, slightly hollow/gravelly bite — asymmetric clipping with audible even harmonics; the OTA patch at the same settings stays creamier.
3. Set Drive E to minimum and play the bass at full level — **Expect:** even at zero Drive the full-scale input already grazes the Korg35 clipper — a faint hair of grit; RB-MF20-1 at the same setting stays clean.
4. Turn B to 100% with A low — **Expect:** self-oscillation is still a clean whistle (turn w to 0 to hear it without the saw, then restore); both cores self-osc cleanly at full Peak, the difference lives in the driven/resonant midrange, not the pure tone.
5. Overall A/B under drive — **Expect:** the two patches are clearly distinguishable once Drive is up; if they sound identical under drive, that is a bug — report it.

## RB-Onbetap-1 — Tamed: the Polivoks behaviors (Onbetap, signature-behavior tour)

**Setup:** Saw VCO at C2 into the Onbetap through a unity-gain VCA (knob v = Input level, loaded at full), Tamed mode at 1× oversampling (baked state). Cutoff starts \~400 Hz, Q 60%, Drive 20%, LP mode, all CV amounts full. Filter output on Out 1/Out 2.
**Panel:**

| Control | Maps to |
|---|---|
| Knob A | Cutoff |
| Knob B | Q |
| Knob C | Drive |
| Knob D | Mode (LP/BP/HP/Notch/Peak) |
| Knob E | Cutoff CV Amt |
| Knob F | Q CV Amt |
| Knob u | Drive CV Amt |
| Knob v | Input level (saw into the filter; 0 = silence) |
| In 1 | Cutoff CV |
| In 2 | Q CV |
| In 3 | Drive CV |
| In 4 | VCO Pitch (1V/oct) |

**Try:**
1. Drive fights resonance: hold B (Q) around 70%, then sweep C (Drive) slowly from 20% to 80% — **Expect:** the sound gets louder and dirtier but RINGS LESS — the resonant peak audibly recedes as drive rises.
2. Self-osc onset moves with cutoff: turn v (Input level) to 0 so the saw doesn't mask the onset. With A low (\~9 o'clock), creep B up until oscillation just barely starts (should be in the top fifth of the knob). Now raise A without touching B — **Expect:** the filter sings earlier/is already singing at higher cutoffs; the oscillation threshold depends on cutoff. Restore v to full before moving on.
3. Relaxation harshness: B at max, C past 70%, A in the low-mid range (\~10 o'clock) — **Expect:** the tone drops into a LOWER, buzzier relaxation-oscillation growl — alarming but bounded; back off any one knob and it returns to normal.
4. No bass loss: LP mode (D full CCW), B at 85%, play the C2 bass — **Expect:** the low end stays planted at high resonance; no classic thinning of the fundamental.
5. Step D through all five positions (LP, BP at \~10 o'clock, HP at noon, Notch at \~2 o'clock, Peak full CW) — **Expect:** five distinct responses; BP is audibly WIDE (gentle 6 dB skirts, not a narrow chirp); each mode change is click-free (\~5 ms fade, this is the Tamed behavior).
6. Patch an envelope into In 2 and play notes — **Expect:** resonance blooms per note — each hit rings up and settles as the envelope falls; F attenuverts the depth.
7. Envelope or LFO into In 1 — **Expect:** normal cutoff sweeps, depth set by E; 1 V into In 1 with E full moves cutoff one octave.


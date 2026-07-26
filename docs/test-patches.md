# Robot Boy test patches

A set of VCV Rack patches for testing every Robot Boy module in a musical context, designed to transfer cleanly to the MetaModule. Each patch exercises a specific cluster of features and tells you what you should hear — musically and technically — if the module is behaving. Patches that involve a tunable context-menu parameter include a **Find a value** step: dial the slider by ear in VCV, note the number, and we'll bake it in as the default (several of these sliders are desktop-only and don't exist on MM, so the value you find *is* the MM behavior).

**Companion plugins** (available on both platforms): VCV Fundamental, 4ms, Bogaudio, Audible Instruments (Cloned Instruments on MM), Count Modula. Companion modules are kept as simple as possible so anything interesting you hear is ours.

**General setup notes**

- Run VCV at 48 kHz to match the MetaModule.
- Keep source levels around ±5 V. Remember that headless VCV WAVs are volts/5 while MM records 1:1 — irrelevant for listening, but normalize before comparing renders.
- On MM, context-menu items live in the **Options** list below the jacks.
- Audible Instruments "Macro Oscillator 2" is the Plaits clone; on MM use the Cloned Instruments equivalent.

---

## 1. Loooop — "Instant ensemble" (harmonizer / texture layers)

**Tests:** record, Layer overdub, per-head Speed (incl. reverse), Pan, Level, Size, Position, Crossfade, **Speed CV = V/Oct** menu option.

**Modules:** Macro Oscillator 2 (Plaits) → Loooop. Fundamental LFO ×1. A keyboard/MIDI-CV or SEQ3 for the V/Oct test.

**Patch:**

1. Plaits in a mellow model (e.g. the wavetable or string model), output → Loooop **In L** only (mono; confirm it feeds both sides).
2. Click **Record**, play/hold a short phrase — a sustained note then a small melodic figure, 4–8 seconds — click Record again.
3. Set the four heads:
   - Head 1: Speed 1.0, Pan center, full Size.
   - Head 2: Speed 2.0 (octave up), Pan left, Size \~75%.
   - Head 3: Speed 1.5 (a fifth up — set by ear), Pan right.
   - Head 4: Speed −1.0 (reverse), Pan center, Level slightly down.
4. Slow LFO (\~0.05 Hz triangle) → Head 2 **Pan CV** for a drifting stereo field.
5. Take **Mix L/R** out, Dry/Wet fully wet.

**What to hear:** one recorded phrase becomes a self-harmonizing ensemble — root, octave, fifth, and a reverse shadow of itself. The fifth (1.5×) should sit at a stable, in-tune interval. The reverse head should sound like the phrase inhaled. No clicks at the loop seam with **Crossfade** on (default); toggle Crossfade **off** and listen for the hard seam tick returning — that's the option working, not a bug.

**Then:** enable **Speed CV = V/Oct** on Head 1 and patch a keyboard/SEQ3 pitch CV into Head 1's Speed CV. Playing a scale should transpose the loop chromatically and stay in tune with itself across at least two octaves. Without the option, the same CV just pushes linear speed — audibly out of tune. That contrast is the test.

**Overdub check:** with Overdub on **Layer** (blue), record two more passes over the loop. Each pass should push older material down gently (\~1 dB/pass) — the loop gets denser but never clips.

---

## 2. Loooop — "Beat re-slicer" (grid, one-shot, jump, jitter)

**Tests:** Grid snapping, **One-shot on trigger**, **Exclude from Grid**, Jump input, Jitter, Replace overdub punch-in, Record/Clear triggers.

**Modules:** 4ms Djembe (drum voice) + Fundamental SEQ3 (clock + triggers) → Loooop. Fundamental LFO for the Jump test.

**Patch:**

1. SEQ3 internal clock \~120 BPM; its gate outputs trigger Djembe to make a simple one-bar drum pattern. Djembe out → Loooop **In L**.
2. Patch a spare SEQ3 trigger into Loooop's **Record trigger** to punch recording in/out on the beat; record exactly one or two bars.
3. Set **Grid = 16**. The display should show 16 vertical bars.
4. Heads 1–3: **One-shot on trigger** checked, Size \~1–2 grid segments each, Positions at different segments. Their display lanes should dim while waiting. Patch three SEQ3 gates into their **Trig** inputs.
5. Head 4: **Exclude from Grid** checked, Size \~30%, Jitter \~50%, Level low — a free-floating texture layer behind the sliced beat.

**What to hear:** heads 1–3 fire clean slices of the drum loop on the sequencer's rhythm — a rearranged beat where every slice lands exactly on a grid division (no flams, no half-segment smears; that's Grid quantizing Size and Position). Head 4 wanders loosely underneath, ignoring the grid — audibly "off the rails" in a good way. Slices should end with a short fade, not a click.

**Jump test:** un-check one head's one-shot, give it full Size, and patch a square LFO (\~4 Hz) into its **Jump** input — you should get a hard rhythmic stutter between two points of the loop.

**Punch-in test:** set Overdub to **Replace** (red), hit Record for one bar while Djembe plays a different pattern — the new bar should cleanly overwrite the old audio only where the record head passed. Then set **Lock** (flashing magenta) and confirm Record does nothing at all. Finish with a trigger into **Clear** and confirm silence.

---

## 3. Löp — "Frippertronics" (single head, Decay overdub)

**Tests:** Löp's single-head feature parity, **Decay** overdub degradation, Dry/Wet, reverse playback.

**Modules:** Macro Oscillator 2 (or Fundamental VCO through an ADSR/VCA if you want it simpler) → Löp.

**Patch:**

1. Record \~8 seconds of sparse melodic playing.
2. Overdub mode **Decay** (orange). Keep recording enabled and layer new notes every pass or two, tape-loop style.
3. Dry/Wet \~70% wet so you hear yourself plus the accumulating loop.

**What to hear:** a slowly thickening bed where **older layers sink in level *and* dull in the high end** every pass — the tape-degradation half of Decay mode is the thing to verify against Layer mode (repeat the exercise on Layer: layers should get quieter but stay *bright*). After many passes the oldest material should be a warm blur, not a distorted mess (contrast with **Add** mode, which should audibly clip after a few hot passes — worth 30 seconds to confirm).

**Then:** flip Speed to −1 and keep overdubbing — recording forward over a backward-playing loop is instant ambient. Confirm the display, window shading, and seam crossfade all behave identically to a single Loooop head.

---

## 4. MF-20 — "Acid line" (LP sweep, Peak, Drive, OTA vs Korg35)

**Tests:** LP cutoff/Peak/Drive musical behavior, attenuverters, the **Filter revision** menu (the module's core A/B), polyphony if convenient.

**Modules:** Fundamental SEQ3 → Fundamental VCO (saw) → MF-20. Fundamental ADSR for the filter envelope.

**Patch:**

1. SEQ3 sequences a one-bar bassline into VCO's V/Oct; VCO saw → MF-20 input.
2. SEQ3 gate → ADSR; ADSR out → **LP cutoff CV**, attenuverter \~+60%. LP cutoff knob \~150 Hz, HP cutoff fully down (out of the way).
3. LP **Peak** \~70%. Drive at minimum to start.

**What to hear:** a classic squelchy acid line — each note gets a resonant "wah" from the envelope. Now the character test, the reason this module exists:

- **OTA mode:** smooth and open. Resonance rings sweetly; turning Drive up adds a rounded, creamy saturation.
- **Korg35 mode:** noticeably edgier. The resonant peak should sound slightly raspier/more aggressive, and under Drive it should develop a harder, buzzier bite — the asymmetric clipping adds even-order harmonics, so listen for a subtle "hollow/gravelly" quality OTA doesn't have, especially with Peak above 70% and Drive past noon.

A/B the revisions repeatedly on the same running sequence — the difference should be obvious at high Peak + Drive and subtle when clean. If the two modes sound identical under drive, something's wrong. Also verify Peak at 100% self-oscillates cleanly in *both* modes (kill the VCO and listen for a stable sine whistle).

---

## 5. MF-20 — "Vocal band" (HP→LP series topology, Total input, self-osc ping)

**Tests:** the HP-then-LP series structure, the shared **Total** cutoff bus, band/notch behavior, self-oscillation ping.

**Modules:** Fundamental Noise (pink) → MF-20. Fundamental LFO. Count Modula Clock Divider + any trigger source for the ping test.

**Patch:**

1. Pink noise → MF-20. Set HP cutoff \~300 Hz, LP cutoff \~1.5 kHz — a mid band. Both Peaks \~50%.
2. Slow triangle LFO (\~0.1 Hz) → **Total cutoff CV**, attenuverter \~+50%.

**What to hear:** the whole band slides up and down the spectrum as one unit — a breathy, vowel-like "aaah–oooh" formant sweep. This is the MS-20's shared-cutoff trick: both filters move together, preserving the band's width. Then swap the knobs (HP *above* LP, e.g. HP 2 kHz / LP 800 Hz) — because the filters are in series you should get a notch/phasey scoop rather than a band; sweeping Total now sounds like a slow phaser.

**Ping test:** unpatch the noise. Both Peaks to 100%. Send a bare trigger (SEQ3 gate through a VCA, or just tap a cable) into the audio input — each hit should ring the filter like a damped sine drum. Korg35 mode should ring with a slightly grittier tail than OTA.

---

## 6. Onbetap — "Feral lead" (drive-vs-resonance, moving self-osc onset)

**Tests:** the Polivoks signature behaviors — **drive suppresses resonance**, **self-osc onset moves with cutoff**, relaxation-regime harshness.

**Modules:** Fundamental SEQ3 → Fundamental VCO (saw) → Onbetap (mono). Fundamental ADSR → Q CV.

**Patch:**

1. Sequence a slow, brooding line (minor, low register) into the VCO; saw → Onbetap **In L**. Mode **Lowpass**, Character **Tamed**.
2. Cutoff \~400 Hz, Q \~60%, Drive \~20%. ADSR → **Q CV**, attenuverter full (a 0–5 V envelope sweeps the whole Q range).

**What to hear:** a growling lead where each note blooms with resonance as the envelope opens. Now the three character checks — these distinguish Onbetap from every generic SVF:

- **Drive suppresses resonance.** Hold a note, fix Q at \~70%, then sweep Drive from 20% → 80%. The tone should get *louder and dirtier but ring less* — the resonant whistle audibly ducks as saturation takes over. On a normal filter, drive and resonance both pile up; here they fight. This is the headline behavior.
- **Self-osc onset moves with cutoff.** Input silent, Drive low. Find the Q position where self-oscillation just starts at cutoff \~200 Hz (should be around 3/4 travel). Now raise cutoff to \~5 kHz without touching Q — the filter should already be singing, because onset comes *earlier* at high cutoff. Sweep cutoff up and down and listen to oscillation appearing/disappearing at fixed Q.
- **Relaxation regime.** Q maxed, Drive high, cutoff low-mid: the self-oscillation may drop into a lower-pitched, harsher, buzzier mode than the filter's ringing frequency — the "suddenly harsh" Polivoks misbehavior. It should sound alarming but stay bounded (no runaway level).

**Menu check:** the Tuning sliders this patch originally value-found (Drive span, Core headroom, Self-osc onset trim, Output trim) have since been baked in and removed (onset trim shipped as a 0.045 constant). The context menu now holds only **Character** (Tamed/Vintage), **Resonance limiting** (Soft default / Hard), and **Oversampling** (1×/2×/4×, default 2× in VCV Rack and 1× on MetaModule) — confirm those three are all that's there, and that the baked values still pass the ear tests above (Drive knob's useful range, "clean with hair" at \~30% Drive, onset in the top fifth of Q travel at typical cutoffs (earlier at high cutoff), roughly bypass-matched loudness).

---

## 7. Onbetap — "Haunted stereo pad" (modes, Vintage character, Hard/Soft limiting, oversampling)

**Tests:** true stereo, all five filter modes, **Character: Vintage**, **Resonance limiting**, **Oversampling**, no-bass-loss at high Q.

**Modules:** two Fundamental VCOs (detuned saws, one per channel) → Onbetap L and R. Bogaudio LFO (or two Fundamental LFOs at slightly different rates) → Cutoff CV.

**Patch:**

1. VCO 1 saw → In L, VCO 2 saw (detuned \~7 cents, same pitch CV) → In R. Patching In R engages true stereo.
2. Cutoff \~800 Hz, Q \~50%, Drive \~30%. Slow LFO → Cutoff CV, attenuverter \~30%.
3. Step the **Mode** knob through all five positions while the pad plays.

**What to hear, per mode:** LP = warm pad; BP = thin, hollow, vocal (native Polivoks BP has gentle 6 dB skirts — it should sound *wide* for a bandpass); HP = airy shimmer with the body removed; Notch = subtle phasey hollow; Peak = the pad with a resonant emphasis riding the LFO. In Tamed, mode changes should be click-free (5 ms crossfade). 

- **No bass loss check (LP):** crank Q to 85% on the LP mode — the pad's low end should stay planted under the resonance rather than thinning out. This is a Polivoks trait; most SVFs lose bass here.
- **Vintage character:** switch Character → **Vintage**. Within \~30 seconds you should hear the stereo image come alive — L and R cutoffs drifting independently (slow, seasick detune of the filter color), and mode changes now *click* hard (unfaded, authentic). Sweep cutoff fast (grab the knob and yank) and listen for the DC **thump** — like the real panel switch. Reload the patch: the drift should evolve *identically* (it's seeded). Switch back to Tamed and the image should freeze solid again.
- **Resonance limiting (Hard vs Soft):** silence the VCOs, Q to max, let it self-oscillate. A/B Hard vs Soft: pitch should differ audibly (Soft oscillates noticeably higher — roughly 310 vs 360 Hz territory under like conditions), and behavior right at onset should feel slightly different; the *timbre* difference is intentionally subtle. **Soft** shipped as the default — confirm it still sounds like the right call.
- **Oversampling:** Drive to max, cutoff \~8 kHz, high-pitched saw input. A/B 1× / 2× / 4×: at 1× listen for inharmonic aliasing "birdies" under the distortion (frequencies that sweep *down* when you play *up*); 2× should mostly clean them; 4× should be clean. Confirm 2× is an acceptable default on musical material in VCV Rack — and, since MetaModule now defaults to 1×, that 1× is tolerable on musical material there (the birdie test above is the worst case, not the typical one).

---

## 8. Vespid — "Wasp sting" (the CMOS rasp, Mix morph, British vs German)

**Tests:** the Wasp's signature nasal aggression, LP/BP/HP + **Mix** crossfade output with CV, Drive CV, **Character: British vs German**.

**Modules:** Fundamental SEQ3 → Fundamental VCO (square) → Vespid. Fundamental ADSR → Freq CV; Fundamental LFO → Mix CV.

**Patch:**

1. Sequence a mid-tempo bass/lead line; square wave → Vespid In L. Freq \~300 Hz, Res \~60%, Drive \~40%.
2. ADSR (snappy) → Freq CV, attenuverter \~+50%.
3. Take the **BP output** first.

**What to hear:** this is the character test. The Wasp doesn't sound like a Moog or an SVF-textbook filter — the CMOS inverter core adds a **buzzy, nasal, slightly fizzy rasp** even at moderate settings, most obvious on the BP output: hollow and aggressive at once, like the filter is spitting. Compare directly against Fundamental VCF (same input, same cutoff/res, BP out) — the Fundamental should sound polite and glassy; Vespid should sound like it's working an attitude. If they sound alike, the nonlinearity isn't doing its job.

- **Mix output morph:** switch to the **Mix** output and patch a slow LFO into **Mix CV**. The tone should sweep continuously LP → notch → HP — at the midpoint, a hollow scooped sound (notch), not silence and not a bandpass. Musically: a slow, phasery "opening up" gesture over the sequence.
- **British vs German:** Res to max, input still running. **British** (+5 V rails, the 1978 board): the filter should sit *at the verge* — whistling and chirping along with the notes, resonance singing on transients but never taking off on its own. Kill the input: the whistle should die. **German** (+12 V rails, the Doepfer A-124 mod): same settings, the filter should cross into genuine self-oscillation and keep singing after the input stops, loud but bounded by the rails. This A/B is the module's party trick — verify both halves.
- **Find a value (Input trim / Output level, ±12 dB):** with a ±5 V square in, Drive at minimum, the filter should be clean; with Drive up it should snarl. If it's already snarling at zero Drive, pull Input trim down. Match bypass loudness with Output level. Note both.

---

## 9. Vespid — "Singing voice" (self-osc tracking, oversampling)

**Tests:** playing the self-oscillation as a voice, **Self-oscillation pitch** (hardware drift vs corrected 1 V/oct), **Oversampling**.

**Modules:** Fundamental SEQ3 (or MIDI-CV) → Vespid Freq CV. No audio input at all.

**Patch:**

1. Character **German**, Res max, no input. The filter is now an oscillator.
2. Sequence a simple melody into **Freq CV** (attenuverter full / 1 V/oct).

**What to hear:**

- **Self-oscillation pitch = corrected 1 V/oct:** the melody should play in tune across 3–4 octaves — a hollow, slightly gritty sine lead. Check octaves against a reference VCO.
- **Hardware-accurate mode:** the same melody should now drift and detune the way the real circuit does — intervals compress/stretch, especially at the extremes. Musically it should sound "vintage out-of-tune," not broken. This A/B tells us the option is worth its menu space.
- **Inverter bandwidth:** resolved — the menu slider is gone; the value is baked per character (British 60 kHz, German 50 kHz), chosen exactly so **British just barely doesn't self-oscillate at max Res** and **German sings confidently**. Confirm both halves of that promise here.
- **Oversampling:** as with Onbetap — high Drive, high Freq, listen for aliasing birdies at 1×. In VCV Rack (Auto/1×/2×/4×) confirm Auto picks something clean. On MetaModule the menu is 1×/2×/4× with no Auto and 1× is the default, so confirm 1× is tolerable there and that 2×/4× are reachable if a patch needs them.

---

## 10. Particules — "Living drone" (grain cloud, freeze, attenurandomizers, quality modes)

**Tests:** free-running Density, Freeze, the four attenurandomizers, Reverb, Feedback, and all four **Quality** modes — including the new half-depth wow/flutter on Sunny tape.

**Modules:** Macro Oscillator 2 (Plaits, a chord or string model) → Particules. Nothing else — this patch is about the module itself.

**Patch:**

1. Plaits holding a sustained chord/drone → **In L**. Dry/Wet fully wet, Reverb \~20%.
2. Density \~2 o'clock (random clouds). Size \~12 o'clock, Time \~9 o'clock (recent audio), Shape \~11 o'clock, Pitch centered.
3. Press **Freeze**. The drone is now a snapshot.
4. Attenurandomizers: Size CCW a touch (\~10 o'clock), Pitch CCW a touch. No CV cables — these are pure randomizers now.

**What to hear:** a frozen chord that shimmers and breathes — grains landing at slightly different lengths and pitches so the texture never repeats exactly. The white LED under Density CV should flash with every grain. Turn the Pitch attenurandomizer further CCW and the cloud should get wilder (peaky distribution — mostly near the root with occasional outliers); with it centered the drone should go static and exact.

- **Quality tour** (unfreeze first — Quality is locked while frozen; then re-freeze after each switch if you like):
  - **Bright digital** (white): clean, full-bandwidth, essentially hi-fi.
  - **Cold digital** (cyan): the classic Clouds sound — slightly gray, aliased top end, 12-bit grit.
  - **Sunny tape** (amber): warm and dark (6 kHz at 48 k), and — new behavior to verify — a **gentle** wow/flutter: the drone should waver subtly, like a good cassette deck, *not* seasick. If it warbles as hard as Scorched, the half-depth change didn't take.
  - **Scorched cassette** (magenta): aggressively lo-fi, 8-bit crunch, obvious wow/flutter warble.
- **Feedback:** unfreeze, turn Feedback to \~60% on Scorched — the regeneration should degrade *musically* into tape-saturated mush rather than exploding. On Bright digital the same feedback should stay clean and just build density (brickwall limiting). That per-quality difference is the test.
- **Menu:** check the **Input** readout shows a sane dB figure and that **Auto gain** re-calibrates when you unpatch/repatch the input (the drone shouldn't get quieter or clip after repatch).

---

## 11. Particules — "Clocked melodic grains" (Seed clocking, Lock pitch, grain trigger out)

**Tests:** clocked Seed (Density as divider/probability), **Seed CV mode: Gates**, Pitch CV at 1 V/oct, **Lock pitch** scale quantization, **Grain trigger on R output**.

**Modules:** Fundamental SEQ3 (clock + pitch CV) → Particules. Fundamental VCO (saw, held note) as audio source. 4ms EnvVCA (or Fundamental ADSR+VCA) driven from Particules' grain trigger for the trigger-out test.

**Patch:**

1. VCO saw (steady note, \~C3) → **In L**. Dry/Wet fully wet.
2. SEQ3 clock → **Seed**. Density at 12 o'clock: **silence** (probability 0). Turn CW to \~3 o'clock: most clock ticks should now spawn a grain, locked to the sequencer's rhythm. Turn CCW instead and you should hear clean clock *divisions* (1/2, 1/4 …) — grains on every 2nd, 4th tick.
3. SEQ3 pitch CV → **Pitch CV**; Pitch attenurandomizer fully CW (pure CV, no randomness). Sequence a melody.
4. Menu: **Lock pitch → Minor**, Root → A.

**What to hear:** a melody played *by grains* — each clock tick births a grain transposed to the sequenced pitch, snapped to A minor. Detune the sequence slightly; the quantizer should pull every grain onto the scale (no sour notes). Switch Lock pitch to **Octaves** and the same sequence should collapse to octave jumps only.

- **Gates mode:** menu **Seed CV mode → Gates**, and send a slow gate (SEQ3 gate, long steps). Grains should fire *only while the gate is high*, in bursts whose internal rate follows Density; at 12 o'clock exactly one grain per gate. Musically: rhythmic puffs of texture instead of a stream.
- **Grain trigger on R output:** enable it. Out L is now mono audio; **Out R fires a 1 ms trigger per grain.** Patch Out R → EnvVCA trigger with some other sound behind it — the second voice should play in perfect lockstep with the grains, even at high densities (back-to-back triggers stay distinct). This confirms the one-sample gap behavior.

---

## 12. Ondes — "Morphing wavetable voice" (both axes, interpolation, bank families)

**Tests:** Bank and Position sweeps with interpolation, the three bank families, pitch notches, V/Oct tracking, audio-rate Position modulation.

**Modules:** Fundamental SEQ3 → Ondes V/Oct; Fundamental ADSR + VCA on the output; Fundamental LFO → Position CV; second Fundamental VCO for the audio-rate test.

**Patch:**

1. Sequence a mid-register melody into **V/Oct**; Ondes **Out** → VCA → out, ADSR shaping notes.
2. Slow triangle LFO (\~0.2 Hz) → **Position CV**, attenuverter \~50%. Bank knob \~0.2 (mild/additive family).

**What to hear:** a melody whose timbre is constantly, *smoothly* evolving — the LFO slides through the waveforms inside the bank. The critical listen: transitions must be continuous blends with **no steps, zippering, or clicks** anywhere in the Position travel. Then grab the **Bank** knob and sweep it slowly 0 → 1 by hand: you should pass through three audibly distinct neighborhoods — soft sines/triangles/organ drawbars (low), buzzy vocal formants (middle), and the gnarly Braids imports — choir, metallic, drone (top) — again with every in-between position a usable hybrid, not a crossfade artifact.

- **Pitch knob notches:** sweep the Pitch knob slowly — it should "settle" at octaves, fifths, and unison rather than sliding past them.
- **Audio-rate test:** patch a second VCO's output into **Position CV** (attenuverter \~30%) and tune it near Ondes' pitch. You should get FM-like sidebands/growl — clangorous and pitched, not white-noise hash. Back the attenuverter down and the growl should smoothly clean up.

---

## 13. Retours — "Tape doppler delay" (manual time, Time-change response, quality, find slew value)

**Tests:** manual Interval→time mapping, TIME multiplier, **doppler pitch bends**, **Time change response (Tape vs Crossfade)**, **Doppler slew** value-finding, per-quality feedback, single vs multi-tap.

**Modules:** Macro Oscillator 2 (Plaits) or Fundamental VCO+ADSR+VCA (sparse plucky notes) → Retours. Fundamental LFO for Time modulation.

**Patch:**

1. Sparse melodic plucks → **In L**. Dry/Wet \~50%. Feedback \~40%. Shape fully CCW (plain repeats). Pitch at noon (bypass). No clock patched — manual mode.
2. Interval \~10 o'clock (CCW side = single tap): medium delay time. TIME \~9 o'clock (1× the base time).

**What to hear:** a straightforward, clean delay. Now the tests:

- **Doppler (the headline):** grab the TIME knob and turn it while repeats are sounding. In **Tape (doppler)** mode the echoes should *pitch-bend* like varispeeding a tape machine — swooping down when you lengthen, up when you shorten — with no clicks. Patch a slow sine LFO into **Time CV** (small amount) for continuous chorus/tape-warble on the repeats. Then switch the menu to **Crossfade** and repeat: time changes should now be pitch-neutral and click-free but "jump" rather than swoop. Both modes must be artifact-free; they should just *feel* completely different (tape machine vs. digital delay).
- **Find a value (Doppler slew, 0.01–1 s):** in Tape mode, wiggle TIME hard. Small slew = fast, chirpy bends; large = long lazy swoops. Find the value where a hand-turned knob sounds like a real tape echo's varispeed (musical swoop, not a comedy pitch drop). Note it — this slider doesn't exist on MM, so this becomes the baked-in feel.
- **Multi-tap:** move Interval to the **CW** side of noon at a similar distance. A second, unevenly spaced tap should appear — repeats go from "ping… ping…" to a galloping, syncopated pattern. Confirm the CCW side stays single-tap.
- **Quality + feedback:** Feedback to \~75%. On **Bright digital** the regeneration should stack up clean and finally brickwall gracefully. On **Sunny tape / Scorched cassette** each repeat should get progressively darker and more saturated — a self-degrading echo that fades into warm murk (Scorched adds wow/flutter warble to the tail). This per-quality decay character is the point of the Quality switch on a *delay*.
- **Random LFO rate:** resolved — the menu slider is gone; the rate is baked at 0.1 Hz. Turn the TIME attenurandomizer CCW with nothing in Time CV — the delay time should wander slowly (tape-mechanism instability) and read as "old tape deck," not vibrato. (Also check **Input trim** ±12 dB if hot sources distort the input stage — that slider and **Doppler slew** are the only two menu sliders left.)

---

## 14. Retours — "Clocked slicer & shimmer" (clock sync, Shape, Slice, pitch feedback, Karplus-Strong)

**Tests:** clocked mode + subdivisions, tap tempo, **Shape** tempo-synced envelope, **Slice** beat-slicer with TIME as slice selector, **Pitch** shifter in the feedback path (shimmer), **Envelope feedback tap**, audio-rate Karplus-Strong.

**Modules:** Fundamental SEQ3 (clock + melody) → 4ms Djembe (rhythmic source) → Retours. For Karplus: any trigger + SEQ3 pitch CV.

**Patch (rhythmic half):**

1. SEQ3 clock → Djembe trigger *and* → Retours **Clock input**. Djembe → In L. Dry/Wet \~50%, Feedback \~50%.
2. Interval at noon = repeats at exactly 1/1 with the clock; turn CCW for locked binary subdivisions (1/2, 1/4 — echoes in perfect eighth/sixteenth time), CW for the wider ratio set (triplety 1/3, 1/6 feels). The delay should stay locked if you nudge the SEQ3 tempo. Tap the **Tap tempo button** in time instead of patching the clock and confirm tap tempo works (light blinks the base time).
3. **Shape:** sweep it up from CCW. The flat echo tail should become *pumping*, tempo-synced repeats — rectangular gating first, then soft breathing swells fully CW. This should stay phase-locked to the beat.
4. **Beat slicer:** engage **Slice** (button or gate). Recording stops; now turn **TIME** — it should step between *slices* of the recent audio, each one beat long, looping cleanly. Musically: an instant beat-repeat/roll effect you can play with one knob. Interval subdivision stays live while Slice is engaged.
5. **Shimmer:** disengage Slice. Set Pitch to **+12 st** (it should click into the notch) with Feedback \~65%. Each repeat should climb an octave — the classic shimmer staircase, rising into sparkle before the limiter tames it. At noon, confirm the shifter is *truly bypassed* (repeats bit-identical in tone, no chorus blur). Try −12 for descending darkness. Then A/B **Envelope feedback tap** (post vs pre): with Shape up and high feedback, "post" should feed the *shaped* repeats back (rhythm reinforces itself), "pre" should keep the envelope cosmetic (tails stay full under the pumping).

**Patch (Karplus-Strong coda):**

6. Remove the clock cable (back to manual). Interval fully CCW-ish to reach audio-rate base times, Feedback \~85%, quality Bright. Feed a single short click/trigger into In L (a bare SEQ3 gate works). You should hear a *plucked string*. Patch SEQ3 pitch CV → **Interval CV** (it's V/oct at audio rates) and sequence it — a playable Karplus-Strong voice. Check tuning stability over a couple of octaves and that high feedback sustains without blowing up.

---

## 15. Appendix — Filter shoot-out (character comparison)

Run one identical signal into all three Robot Boy filters plus a reference, switch between them with a Fundamental **Mutes** or a mixer, and confirm each has a *recognizable* identity. Source: Fundamental VCO saw playing a slow two-note sequence, hot-ish level. All filters: cutoff \~500 Hz, resonance \~70%, comparable drive.

| Filter | Should sound like |
|---|---|
| Fundamental VCF (reference) | Polite, neutral, textbook — the control group |
| **MF-20 (OTA)** | Smooth, open, creamy resonance; drive rounds it off |
| **MF-20 (Korg35)** | Same family but edgier, asymmetric rasp under drive |
| **Onbetap** | Growly and organic; resonance *ducks* as you push level; bass stays planted at high Q |
| **Vespid** | Nasal, buzzy, fizzy CMOS attitude even at moderate settings; BP output is the fingerprint |

If any two of these are hard to tell apart in a blind A/B at high resonance + drive, that's a finding worth reporting.

---

## Value-finding checklist

Fill these in during patches 8 and 13; they become shipping defaults (several sliders are VCV-desktop-only, so MM users get exactly these numbers). Rows marked **resolved** were found in earlier passes and baked into the code — their sliders no longer exist; they're kept here as the record of what shipped.

| Module | Menu parameter | Range (default) | Found value |
|---|---|---|---|
| Onbetap | Drive span | 24–48 dB (36) | Resolved — baked, slider removed |
| Onbetap | Core headroom | 0.5–2× (1×) | Resolved — baked, slider removed |
| Onbetap | Self-osc onset trim | ±0.1 (0) | Resolved — baked at 0.045, slider removed |
| Onbetap | Output trim | ±12 dB (0) | Resolved — baked, slider removed |
| Onbetap | Resonance limiting | Hard/Soft (Soft) | Resolved — Soft shipped as default |
| Onbetap | Oversampling default | 1×/2×/4× (2×) | Resolved — 2× on desktop, 1× on MetaModule |
| Vespid | Input trim | ±12 dB | Resolved — baked at 0 dB (unity), menu item removed |
| Vespid | Output level | ±12 dB | Resolved — baked at 0 dB (unity), menu item removed |
| Vespid | Inverter bandwidth | 60–300 kHz | Resolved — baked per character (British 60 / German 50 kHz), slider removed |
| Vespid | Accuracy default | Standard/High | Resolved — the High-accuracy Newton path was removed entirely; every build runs Standard accuracy, no Accuracy menu anywhere |
| Vespid | Oversampling default | Auto/1×/2×/4× | Resolved — Auto on desktop; MetaModule defaults to 1× with no Auto entry |
| Retours | Input trim | ±12 dB (0) | Resolved — baked at 0 dB (unity), slider removed |
| Retours | Doppler slew | 0.01–1 s | Resolved — baked at 0.285 s, slider removed |
| Retours | Random LFO rate | 0.02–2 Hz | Resolved — baked at 0.1 Hz, slider removed |

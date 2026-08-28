# Robot Boy MetaModule test patches

Nineteen patches covering every feature of the eight Robot Boy modules on MetaModule hardware, one Robot Boy module per patch. Copy this whole directory onto the MetaModule's SD card — the patches that use a WAV loop expect `drum-loop.wav`, `melody-loop.wav` and `delay-test-loop.wav` in the **same directory** as the `.yml` files.

**Plugins required on the MetaModule:** Robot Boy (the release candidate build), Fundamental, Bogaudio, Count Modula. (The 4ms modules are built in.)

## Conventions

- **Buttons live on knobs.** The Hub has no mappable buttons, so button params (Record, Clear, Freeze, Slice, Tap tempo, WAV restart) are mapped to small knobs: **twist up past center = press, twist back down = release.** One press = one full up-down twist. Latching params (Freeze, Slice) simply hold while the knob is up.
- **WAV-loop patches start themselves.** A Count Modula StartupDelay fires the 4ms WAV player about 2 seconds after the patch loads. If the loop isn't playing, twist the knob mapped as *Loop restart / Reload loop* (usually knob z). The Loooop patches use `drum-loop.wav`; RB-Particules-1 uses `melody-loop.wav`, a 4-second loop of eight plucked notes ascending an A-minor arpeggio followed by a steady held tone (A3) — built so pitch shifts, buffer position, and pitch-wobble artifacts are all audible. RB-Retours-1 and -2 use `delay-test-loop.wav`, described in their Setup: a *deliberately sparse* 8-second loop, because an echo that lands on top of fresh source material can't be heard as an echo.
- **Never open a module's menu.** Every alternate context-menu setting under test is baked into its own patch via saved module state (that's what the -2/-3 patches are). If a test seems to need a menu, that's a bug in the patch, not the procedure.
- **Bring four external signals** across the session: an LFO (any shape, slow to audio-rate), a pitch CV source (keyboard/sequencer, 1 V/oct), a clock/gate/trigger source, and an audio-rate oscillator. Also bring a **passive mult or stackcable** — a couple of tracking tests feed the same pitch CV into two panel inputs at once. Panel jack assignments are aliased in each patch (visible on the jack roller).
- **Some patches have a second knob set** (extra mix/utility controls that didn't fit on the twelve knobs). A step that needs it says so by name, e.g. "switch to Knob Set 2 (*Head Levels*)". Switching sets never changes a param by itself — the set you leave holds its values.
- **Before you start, set knob catchup to "Track when equal."** It's in the device preferences (not a module menu). The factory default is "Track if knob moves," which means the first nudge of a physical knob *snaps* its param to wherever that knob is physically sitting. Because both knob sets share the same twelve physical knobs, that default turns every set switch into a trap: zero the head Levels on Knob Set 2, come back to Set 1, touch knob C, and Yellow Speed jumps to full CCW instead of resuming where the patch left it. "Track when equal" makes the knob pick up its param only when the two match, so switching sets is safe. If you'd rather leave the default alone, treat every knob you touched on Set 2 as scrambled on Set 1 and re-set it deliberately.
- **Main audio is always Out 1 / Out 2.** Extra taps (individual playheads, filter responses) sit on Outs 3-5 where noted.
- A few behaviors key off whether a **physical cable** is present in a mapped input (Particules/Retours attenurandomizers switch between randomizer and CV-attenuator; Particules auto-gain recalibrates on patch/unpatch; Seed/Clock detection). Steps that depend on this say so — if something behaves as if a cable were patched when the jack is empty, note it as a finding.

All 19 patches load clean in the MetaModule headless simulator with the current Robot Boy build (module counts verified, states applied); patches that make sound without external input were additionally verified non-silent (Ondes drone, Vespid German self-oscillation — note the German *hardware-pitch* build-up takes a few seconds to bloom — and Particules-1's grain bursts from the melody loop).

---

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

All four Levels load at 25%.

**Recording the loop** — several blocks below open with this, so it gets a name. *Record the loop:* wait \~2 s after load until the drum loop is audible, twist u fully up, wait 4-8 bars, twist u back down. Loooop now has \~4-8 bars of drums in it and all four heads are playing.

**Try:**

*Block 1 — record start/stop · from a freshly loaded patch*

1. Record the loop as described above — **Expect:** playback through all four heads the instant you twist u down; twisting u up = press, down = release.
2. Twist v up to clear, then record again using a trigger into Gate In 1 to start and a second trigger to stop — **Expect:** identical start/stop from the jack. If knob and jack interfere (one arms and the other won't disarm), that's a finding.

**Reset:** reload the patch.

*Block 2 — the four-head ensemble · from a freshly loaded patch, then record the loop*

3. Listen to the ensemble with all Levels at their loaded 25% — **Expect:** four voices at once: Red at unison, Yellow an octave up panned left, Blue reversed panned right, Purple at 1.5× in the center.
4. Sit on it for a minute and listen for Purple's pan — **Expect:** Purple drifts slowly left-right (\~10 s per cycle) from the baked-in LFO; the other three stay put.
5. Solo each head in turn: switch to Knob Set 2, pull the other three Levels to 0, listen, then pull the soloed one down and bring up the next — **Expect:** each head's character matches step 3 heard alone. Stay on Set 2 for the whole sweep; there's no need to switch back between heads.

**Reset:** reload the patch. Don't hand-restore the Levels — after working on Set 2 the physical knobs no longer line up with Set 1's params, and reloading is one button.

*Block 3 — Yellow's speed knob · from a freshly loaded patch, then record the loop*

6. Optionally solo Yellow on Knob Set 2 first (Red/Blue/Purple to 0), then return to Knob Set 1 and turn C (Yellow Speed): 3 o'clock = 1×, full CW = 2×, 9 o'clock = −1×, noon = stopped region — **Expect:** Yellow re-pitches smoothly; below noon it runs backward.
7. Loop seam check: listen across the wrap point several times — **Expect:** no click or tick at the seam (crossfade is ON here; contrast RB-Loooop-3, which has it off).

**Reset:** reload the patch.

*Block 4 — overdub modes · from a freshly loaded patch, then record the loop. Each mode is cumulative across its own passes — do NOT reload between passes within a mode, or you'll restart the effect you're trying to hear. DO reload between modes.*

8. F full CCW = Layer. Overdub four or five passes: twist u up for a pass, then down (the still-running drum loop re-records over itself at a new offset — that IS the new material) — **Expect:** each pass ducks the older material by roughly 1 dB; old layers recede but never dull. **Then reload and re-record.**
9. F at \~10 o'clock = Decay. Same four or five passes — **Expect:** old material loses level AND high end, audibly duller than Layer at the same pass count. This A/B against step 8 is the point of the block, so match the pass counts. **Then reload and re-record.**
10. F at noon = Add. Several hot passes — **Expect:** layers pile up at full level and eventually clip/distort — the mode working, not a bug. **Then reload and re-record.**
11. F at \~2 o'clock = Replace: twist u up mid-loop, then down after a beat — **Expect:** a clean punch-in; new audio replaces the old only where you held record.
12. F full CW = Lock: twist u up and try to record — **Expect:** nothing records; the loop is protected and Record is ignored.

**Reset:** reload the patch.

*Block 5 — Red's window: Size, Position, Jitter, Grid · from a freshly loaded patch, then record the loop. Runs continuously — each step leaves the state the next one wants.*

13. Optionally solo Red on Knob Set 2 (others to 0) and stay there for the block, then return to Set 1. Turn A (Red Size) down to \~25% and sweep B (Red Position) — **Expect:** Red scrubs a short window around the loop.
14. Return A to full and move B again — **Expect:** nothing; Position and Jitter do nothing at full Size, by design. Put A back to \~25% before continuing.
15. With A still at \~25%, raise y (Red Jitter) — **Expect:** Red's window hops to random positions, subtle shuffle at low y through full scatter at high y. Leave y up.
16. Turn E (Grid) up one step at a time (Off/4/8/16/32/64) while scrubbing A and B — **Expect:** Size and Position snap to grid divisions; changes land on the beat instead of free-sliding.

**Reset:** reload the patch.

*Block 6 — mix and erase · from a freshly loaded patch, then record the loop*

17. Sweep D (Dry/Wet) full CCW then full CW — **Expect:** CCW = only the live drum-loop input, CW = only the heads; noon = the baked-in 50/50 blend.
18. Twist v up (or send a trigger into Gate In 2) — **Expect:** the loop erases; heads go silent until you record again.
19. Any time the source WAV stops (it shouldn't, with Loop on), twist z up then down — **Expect:** the drum loop restarts from the top. This one needs no reset; it's a utility, not a test.

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

Loaded Levels: Red 40%, Yellow 40%, Blue 25%, Purple 20%.

This patch self-loads: every block below starts from a fresh reload and needs no manual recording — just wait \~2 s for the file to play through and record itself.

**Try:**

*Block 1 — the self-loading trick · from a freshly loaded patch*

1. Load the patch and touch nothing — **Expect:** \~2 s of silence, then the drum file plays once while recording itself; when it ends, Blue (full loop) and Purple (jittery fragments) play back on their own. If you have to touch anything to get audio, that's a finding.
2. Check the display lanes for Red and Yellow — **Expect:** both dim/inactive; one-shot heads stay silent until triggered.

**Reset:** none needed — nothing has been changed. Continue straight into Block 2.

*Block 2 — the two one-shot slice heads · continues from Block 1*

3. Solo the slice heads: switch to Knob Set 2 and pull Blue and Purple Levels to 0, then return to Knob Set 1 — **Expect:** silence between triggers, since Red and Yellow only sound when fired.
4. Send a trigger into Gate In 1 — **Expect:** Red fires one clean slice and stops; each trigger = exactly one slice, ending with a short fade, not a click.
5. Same into Gate In 2 — **Expect:** Yellow fires its own slice from a different part of the loop (Position 60% vs Red's 20%).
6. While triggering Red, step B (Red Position) around, then A (Red Size) — **Expect:** every slice start and length snaps to 1/16 divisions; the knobs move in audible whole-segment jumps, never landing mid-segment.
7. Do the same with C/D for Yellow — **Expect:** identical grid-snapped behavior on the second slice head.
8. Turn E (Grid) down to Off and retrigger Red while moving B — **Expect:** slices now start anywhere, free and un-snapped.
9. Compare Out 3 against Out 1/2 — **Expect:** Out 3 carries only Red's slices (left channel), isolated from the mix.

**Reset:** reload the patch and let it re-record itself. Don't try to put Grid and the Levels back by hand.

*Block 3 — Purple's grid exclusion and jitter · from a freshly loaded patch (self-records)*

10. Listen to Purple with the grid on — **Expect:** Purple ignores the grid entirely (grid-exclude is baked in): its fragments land on un-quantized positions while Red and Yellow snap.
11. Sweep u (Purple Jitter) from 0 to max — **Expect:** at 0 Purple sits still; high up it scatters across the whole loop.

**Reset:** reload the patch.

*Block 4 — Blue: jump stutter and reverse · from a freshly loaded patch (self-records)*

12. Optionally solo Blue on Knob Set 2 (Red/Yellow/Purple to 0), then return to Set 1. Patch a square LFO (\~1-4 Hz, 0-10 V) into In 1 — **Expect:** Blue hard-stutters between two loop points in time with the square; a beat-repeat chop, no glide between them.
13. Unpatch In 1 and turn y (Blue Speed) to 9 o'clock — **Expect:** Blue plays the whole loop in reverse; 3 o'clock returns it to 1×.

**Reset:** reload the patch.

*Block 5 — CV on the slice position · from a freshly loaded patch (self-records)*

14. Patch a slow LFO or sequencer CV into In 2 and trigger Red repeatedly — **Expect:** Red's slice position moves under CV, still snapping to the 16-grid.

**Reset:** reload the patch.

*Block 6 — reload and clear · from a freshly loaded patch (self-records). Runs in order; step 16 depends on step 15.*

15. Twist z up then down — **Expect:** the drum file plays through once more and re-records itself (Play Gate drives Record again), replacing the loop content.
16. Twist x up to clear — **Expect:** the loop erases and the heads go silent. Twist z again to reload it as in step 15 — **Expect:** the file re-records and playback returns.

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

**Recording a phrase** — most blocks below open with this. *Record a phrase:* twist u up, play a short phrase on the keyboard into In 1, twist u down. Recording starts on the FIRST upward twist; the knob and the Gate In 1 jack fire independently, so don't arm with both or you'll toggle it twice.

**Try:**

*Block 1 — recording and keeps-overdubbing · from a freshly loaded patch*

1. Plug a keyboard into In 1 and play — **Expect:** you hear the triangle VCO live (Dry/Wet is 50%, so dry input passes).
2. Record a phrase as described above — **Expect:** the phrase loops on Red.
3. Now play more notes WITHOUT touching u — **Expect:** the loop KEEPS OVERDUBBING (baked-in option): anything you play keeps stacking in. This is the whole point of the block, so give it several passes.
4. Twist u up/down to close recording, then play again — **Expect:** the new notes no longer stack; the loop is fixed.

**Reset:** reload the patch.

*Block 2 — chromatic transposition on Red Speed · from a freshly loaded patch, then record a phrase and close recording with u*

5. Move the keyboard (or a sequencer) from In 1 to In 2 and play — **Expect:** the whole loop transposes CHROMATICALLY with the CV — semitone steps, in tune like an oscillator.
6. Play In 2 across at least 2 octaves against a reference pitch — **Expect:** octaves are exact (V/Oct tracking on Red Speed); +1 V doubles playback speed and pitch, −1 V halves it.
7. While In 2 holds a note, turn A (Red Speed) — **Expect:** the knob works ON TOP of the CV, shifting the transposed pitch further. Leave A wherever you like; the next step doesn't care.

**Reset:** reload the patch.

*Block 3 — raw seams and the single voice · from a freshly loaded patch, then record a phrase and close recording*

8. Let the loop wrap several times and listen closely to the seam — **Expect:** a hard tick/click at every wrap. Crossfade is OFF here on purpose; compare against RB-Loooop-1, where the same seam is silent.
9. Confirm only one voice sounds — **Expect:** only Red; Yellow, Blue and Purple Levels are baked to zero. (Nothing to do here but listen — this patch has no Head Levels set.)
10. Turn B (Red Size) down and sweep w (Red Position) — **Expect:** Red scrubs a shorter window of the phrase, and each wrap of that short window seams with its own raw tick.

**Reset:** reload the patch.

*Block 4 — Replace punch-in and the record jack · from a freshly loaded patch, then record a phrase and close recording*

11. Set D to \~2 o'clock (Replace) and punch in a new note with u — **Expect:** the new note replaces that stretch of the phrase cleanly, leaving the rest intact.
12. Return D full CCW (Layer), then run one record cycle using a trigger into Gate In 1 instead of u — **Expect:** same start/stop behavior from the jack as from the knob.
13. Twist v up — **Expect:** the loop erases; you're back to the live VCO only.

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

**Recording the loop** — most blocks below open with this. *Record the loop:* wait \~2 s after load until the drum loop is audible, twist u up past mid, let it run one pass (\~14 s of file, or as much as you like), twist u back down. F at .6 then mixes your recording over the still-running dry loop.

**Try:**

*Block 1 — recording, from knob and from jack · from a freshly loaded patch*

1. Wait \~2 s after load — **Expect:** the drum loop plays on Out 1/2.
2. Record the loop as described above — **Expect:** recording stops on the downward twist and the head plays your loop back seamlessly.
3. Clear (twist v up then down), then record again using a trigger into Gate In 1 to start and a second trigger to stop — **Expect:** identical behavior to the knob (Record jack is in Trigger mode), start/stop on each pulse.

**Reset:** reload the patch.

*Block 2 — Size, Position, Jitter · from a freshly loaded patch, then record the loop. Runs continuously — each step leaves the state the next wants.*

4. Turn A (Size) down to around 9 o'clock — **Expect:** the head plays a short chunk of the recording, repeating faster.
5. With A still low, sweep B (Position) — **Expect:** the chunk scrubs to different spots in the recording.
6. Return A to max and move B again — **Expect:** no audible change; Position does nothing at full Size, and that's correct. Put A back to \~9 o'clock before continuing.
7. With A still low, raise D (Jitter) — **Expect:** the playback point scatters randomly around Position; back at 0 it locks steady. (Jitter is also inert at full Size, for the same reason as Position.)
8. Leaving A low, turn E (Grid) up one step at a time (Off/4/8/16/32/64) while moving A and B — **Expect:** Size and Position snap to clean rhythmic divisions; Off restores continuous control.

**Reset:** reload the patch.

*Block 3 — Speed and the CV inputs · from a freshly loaded patch, then record the loop*

9. Sweep C (Speed) from its default \~3 o'clock (1×) up to max — **Expect:** double speed, octave up. Down through mid — **Expect:** slows, stops momentarily at 12 o'clock, then plays backward below it; fully CCW is −2× (reverse, octave up). Return C to \~3 o'clock.
10. Patch a slow LFO into In 2 (Speed CV) — **Expect:** speed glides up and down around C's setting — linear varispeed (V/Oct is off in this patch), pitch bending smoothly through the LFO cycle. Unpatch it.
11. Send a stepped 0-10 V sequence (or square LFO) into In 1 (Jump) — **Expect:** playback jumps hard between points in the recording, a stutter keyed to the voltage. Unpatch it.
12. Send a clock or button gate into Gate In 2 — **Expect:** each pulse retrigs the head from its loop start, rhythmic restarts in time with your clock.
13. Listen across the loop seam — **Expect:** no click at the wrap (Crossfade is On here).

**Reset:** reload the patch.

*Block 4 — overdub modes · from a freshly loaded patch, then record the loop. Cumulative within each mode — do NOT reload between passes, or you restart the effect under test. DO reload between modes; the Layer-vs-Decay A/B is only meaningful at matched pass counts.*

14. w fully CCW (Layer). Overdub four or five passes — **Expect:** old material ducks about 1 dB per pass; level fades slowly, tone stays bright. **Then reload and re-record.**
15. w to the second position (\~10 o'clock, Decay). Same number of passes — **Expect:** old layers lose level AND high end, audibly duller than Layer at the same pass count. This is the key A/B; spend time here. **Then reload and re-record.**
16. Work through the remaining w positions, reloading and re-recording between each: center (Add) — layers stack at full level and eventually clip/saturate; \~2 o'clock (Replace) — new audio punches out the old only where you play; full CW (Lock) — twisting u does nothing and the loop is protected.

*Utility, any time:* twist z up/down to restart the source drum loop from the top. Needs no reset — it's a convenience, not a test.

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

*Block 1 — acid squelch and OTA drive · from a freshly loaded patch*

1. Patch an envelope (or decaying LFO) into In 1 and play notes — **Expect:** classic acid squelch, cutoff riding the envelope; F attenuverts it, and below center the sweep inverts (closes on attack). Unpatch In 1.
2. Leaving B (LP Peak) at its loaded 70%, sweep E (Drive) from 0 up past noon to max — **Expect:** smooth, creamy OTA thickening; it warms rather than turning buzzy. Fix this in your ear: RB-MF20-2's Block 2 step 2 is this exact move on the Korg35 core, and the comparison only works if both patches are at the same settings.

**Reset:** reload the patch.

*Block 2 — self-oscillation and V/Oct tracking · from a freshly loaded patch. Runs in order; step 5 needs the filter singing from step 3.*

3. Turn B (LP Peak) to 100%, then A (LP Cutoff) well below the bass note — **Expect:** a clean sine whistle rides above the bass note.
4. Turn w (Input level) to 0 — **Expect:** the whistle remains, saw gone, and it's a clean tone rather than a buzz. Leave w at 0 for the next step.
5. V/Oct check: send the same pitch CV to In 1 and In 3 using the mult or stackcable, with F full CW — **Expect:** the whistle tracks exactly; a 1 V step moves it precisely one octave. Bring w back up to hear it against the saw — **Expect:** whistle and saw stay in tune with each other.
6. Repeat steps 3-4 for the HP using C and D — **Expect:** the HP self-oscillates just as cleanly at full Peak.

**Reset:** reload the patch.

*Block 3 — the HP→LP band · from a freshly loaded patch. Runs in order; step 8 needs the band set up in step 7.*

7. Set C (HP) near \~300 Hz (about 10-11 o'clock) and A (LP) near \~1.5 kHz (about 2 o'clock) — **Expect:** a vocal-sounding midrange band.
8. Patch a slow LFO into In 2 (Total) — **Expect:** the whole HP→LP passband slides as ONE unit, a vowel-like formant sweep with the band width constant.
9. With the LFO still running, trim u (Total CV Amt) toward 0 — **Expect:** the sweep shrinks to nothing; u scales only the Total CV input. Return u to full.
10. Unpatch the LFO and swap the band: set C above A (HP cutoff higher than LP cutoff) — **Expect:** the sound thins into a notch/phaser-like residue instead of a band — still audible, not silence.

## RB-MF20-2 — Korg35 mode (MF-20, direct A/B against RB-MF20-1)

**Setup:** Byte-for-byte the same patch as RB-MF20-1 — saw VCO at C2, LP \~150 Hz, Peak 70%, HP parked low, CV amounts full — except the filter core is Korg35 instead of OTA. Output on Out 1/Out 2. Load the two patches back to back with the SAME knob positions for every test.
**Panel:** identical to RB-MF20-1 (A LP Cutoff · B LP Peak · C HP Cutoff · D HP Peak · E Drive · F LP CV Amt · u Total CV Amt · v HP CV Amt · w Input level · In 1 LP CV · In 2 Total CV · In 3 VCO Pitch · In 4 HP CV).

**This patch is one long A/B.** Every step compares against the same move in RB-MF20-1, so *both* patches must be at their loaded defaults when you switch between them — a knob left somewhere in one patch invalidates the comparison. Reload each patch as you switch to it, and the two are guaranteed to match.

**Try:**

*Block 1 — the resonance A/B · both patches freshly loaded, knobs untouched*

1. Listen to this patch, then reload RB-MF20-1 and listen, and flip back and forth — **Expect:** Korg35 resonance is edgier and raspier than the OTA's smooth ring at the same Peak setting.

**Reset:** reload this patch.

*Block 2 — the drive A/B · from a freshly loaded patch. Make the same move in both patches, reloading each as you switch.*

2. Leaving B (LP Peak) at its loaded 70%, sweep E (Drive) from 0 up past noon to max — **Expect:** a harder, buzzier, slightly hollow and gravelly bite: asymmetric clipping with audible even harmonics. The identical move in RB-MF20-1 (its Block 1 step 2) stays creamier.
3. **Reset both patches**, then set E to minimum in each and play the bass at full level — **Expect:** even at zero Drive the full-scale input already grazes the Korg35 clipper, a faint hair of grit; RB-MF20-1 at the same setting stays clean.
4. Overall: with Drive up in both — **Expect:** the two are clearly distinguishable. If they sound identical under drive, that's a bug worth reporting.

**Reset:** reload the patch.

*Block 3 — self-oscillation purity · from a freshly loaded patch*

5. Turn B to 100% with A low, then w (Input level) to 0 — **Expect:** a clean whistle, not a buzz. Both cores self-oscillate cleanly at full Peak; the difference between them lives in the driven and resonant midrange, not in the pure tone. If this whistle is dirtier than RB-MF20-1's, that's a finding.

## RB-Onbetap-1 — Tamed: the Polivoks behaviors (Onbetap, signature-behavior tour)

**Setup:** Saw VCO at C2 into the Onbetap through a unity-gain VCA (knob v = Input level, loaded at full), Tamed mode at 2× oversampling ("CPU efficient", the baked default on both hosts). Cutoff starts \~400 Hz, Q 60%, Drive 20%, LP mode, all CV amounts full. Filter output on Out 1/Out 2.
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

*Block 1 — drive fights resonance · from a freshly loaded patch*

1. Hold B (Q) around 70%, then sweep C (Drive) slowly from 20% to 80% — **Expect:** the sound gets louder and dirtier but RINGS LESS; the resonant peak audibly recedes as drive rises. This is the signature Polivoks behavior, so take your time. If resonance grows with drive, that's a bug.

**Reset:** reload the patch.

*Block 2 — self-oscillation onset · from a freshly loaded patch. Runs in order; the saw stays muted throughout.*

2. Turn v (Input level) to 0 so the saw can't mask the onset — **Expect:** silence.
3. With A low (\~9 o'clock), creep B up until oscillation just barely starts — **Expect:** onset lands in the top fifth of the knob.
4. Leave B exactly there and raise A — **Expect:** the filter is already singing, or starts singing earlier, at higher cutoffs; the threshold depends on cutoff.
5. Still with v at 0: B to max, C past 70%, A in the low-mid range (\~10 o'clock) — **Expect:** the tone drops into a LOWER, buzzier relaxation-oscillation growl — alarming but bounded. Back off any one knob and it returns to normal.

**Reset:** reload the patch (this restores v to full along with everything else).

*Block 3 — bass retention and the five modes · from a freshly loaded patch*

6. LP mode (D full CCW), B to 85%, listen to the C2 bass — **Expect:** the low end stays planted at high resonance; no classic thinning of the fundamental.
7. Leaving B at 85%, step D through all five positions (LP, BP \~10 o'clock, HP noon, Notch \~2 o'clock, Peak full CW) — **Expect:** five distinct responses; BP is audibly WIDE (gentle 6 dB skirts, not a narrow chirp); every mode change is click-free (\~5 ms fade — the Tamed behavior, and the thing RB-Onbetap-2 deliberately does not do).

**Reset:** reload the patch.

*Block 4 — CV inputs · from a freshly loaded patch*

8. Patch an envelope into In 2 and play notes — **Expect:** resonance blooms per note, each hit ringing up and settling as the envelope falls; F attenuverts the depth. Unpatch In 2.
9. Patch an envelope or LFO into In 1 — **Expect:** normal cutoff sweeps, depth set by E; 1 V in with E full moves cutoff one octave.

*If you are also running RB-Onbetap-3:* its CPU comparison wants this patch at C 80% and A 70%. Do that as a separate pass from a fresh reload, not tacked onto Block 4.

## RB-Onbetap-2 — Vintage stereo pad (Onbetap, Vintage drift + hard mode switching)

**Setup:** Two saw VCOs — C3 left, C3 +7 cents right — feed the Onbetap's L/R inputs (true stereo through one filter). Vintage mode is baked in (drift ON, 2× "CPU efficient"). A slow sine LFO (0.1 Hz, ±1.5 V) is wired to Cutoff CV internally with Cutoff CV Amt at +30%. Cutoff \~750 Hz, Q 50%, Drive 30%, LP mode. Output on Out 1/Out 2 — listen in stereo.
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

*Block 1 — the drift, and proving it's seeded · from a freshly loaded patch, twice. Touch nothing during either listen — the whole test is that two identical loads evolve identically.*

1. Load the patch and just listen in stereo for \~30 s — **Expect:** the image comes alive: L and R cutoffs drift independently (Vintage drift), a seasick colour-detune over the 7-cent oscillator beat, with the slow internal LFO sweep riding on top (\~10 s period, subtle at +30%).
2. Reload the patch and listen to the first \~30 s again — **Expect:** the drift evolves IDENTICALLY to step 1. It's seeded, not random per boot. If the two listens differ, that's a finding.

**Reset:** reload the patch.

*Block 2 — Vintage's rough edges · from a freshly loaded patch. These are the deliberately un-smoothed behaviors; each is an A/B against RB-Onbetap-1, which fades them.*

3. Step D between modes while audio plays — **Expect:** mode changes CLICK hard — unfaded, authentic panel-switch behavior. RB-Onbetap-1's Block 3 step 7 is the same move, click-free.
4. Yank A fast across a wide arc — **Expect:** a DC thump, like slamming the real hardware's cutoff. Vintage does not soften it.

**Reset:** reload the patch.

*Block 3 — pitch and resonance · from a freshly loaded patch*

5. Raise B to 85% in LP mode — **Expect:** bass stays planted even at high Q; the Polivoks trait carries into Vintage mode. Return B to its loaded 50%.
6. Sequence In 1 — **Expect:** both saws transpose together in tune (one CV, two VCOs); the detune interval and the stereo drift character hold at every pitch.
7. In 2 (Q CV) is mapped but you may leave it empty — **Expect:** with nothing patched it behaves exactly as if unmapped. Note anything odd; this is one of the cable-detection cases from the conventions.

## RB-Onbetap-3 — 4× oversampling / high quality (Onbetap, aliasing A/B against RB-Onbetap-1)

**Setup:** Same layout as RB-Onbetap-1 (saw VCO at C2, Tamed mode) but baked at 4× oversampling ("high quality") with Drive at 80% and Cutoff high (\~70%, several kHz) — a deliberately hot, bright setting. RB-Onbetap-1 runs the default 2× ("CPU efficient"); to A/B, set patch 1's knobs C to 80% and A to 70% to match. Output on Out 1/Out 2.
**Panel:** identical to RB-Onbetap-1 (A Cutoff · B Q · C Drive · D Mode · E Cutoff CV Amt · F Q CV Amt · u Drive CV Amt · v Input level · In 1 Cutoff CV · In 2 Q CV · In 3 Drive CV · In 4 VCO Pitch).

**This patch is an A/B against RB-Onbetap-1**, which needs its knobs moved to match (C 80%, A 70%) since it loads at gentler settings. Do that from a fresh reload of patch 1 each time, so the only difference between the two is oversampling.

**Try:**

*Block 1 — the aliasing A/B · this patch freshly loaded and untouched; RB-Onbetap-1 freshly loaded with C to 80% and A to 70%*

1. In RB-Onbetap-1 (2× "CPU efficient", knobs matched as above), play a rising line into In 4 over 2-3 octaves — **Expect:** inharmonic aliasing "birdies" under the distortion, faint whistles that sweep DOWN as you play UP.
2. Reload THIS patch and play the same rising line, knobs untouched — **Expect:** clean at 4× "high quality", no counter-sweeping birdies; the distortion harmonics all move up with the notes.
3. Push it harder here: C to max, sweep A through the top of its range while holding a high note — **Expect:** still no birdies; the harshest setting available stays harmonically well-behaved.

**Reset:** reload the patch — the CPU figures below must come from the loaded settings, not from step 3's maxed Drive.

*Block 2 — the CPU numbers · from a freshly loaded patch, and a freshly loaded RB-Onbetap-1 with C 80% / A 70%*

4. Watch the device CPU meter with this patch at its loaded positions, then do the same in RB-Onbetap-1 — **Expect:** headroom acceptable at 4×, and better still at 2× — the 1x option that used to trade audio quality for headroom is gone, since 2× alone now has enough headroom on MetaModule. **Write down both figures** as the current CPU-efficient/high-quality baseline.
5. For each patch, catch the worst case rather than the idle figure: wiggle A and B and modulate In 1 while watching the meter — **Expect:** the peak reading is higher than the resting one; record the peak.

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

*Block 1 — the CMOS rasp · from a freshly loaded patch, listening on Out 3 (BP). Runs in order.*

1. Sweep A slowly through the mids — **Expect:** the Wasp fingerprint: buzzy, nasal, fizzy CMOS tone even at these moderate settings, not a polite clean bandpass.
2. Turn C (Drive) fully down and keep listening — **Expect:** the sound is STILL lightly overdriven. The dirty-Wasp rasp is present at Drive 0, not clean. If it goes clean, that's a finding.
3. Sweep C back up from 0 toward max — **Expect:** the grit shifts from ragged asymmetric fuzz toward a harder, more square, odd-harmonic distortion; louder and nastier but continuous, no dropouts.

**Reset:** reload the patch.

*Block 2 — Blend and the four simultaneous outputs · from a freshly loaded patch*

4. Park D at noon, then fully CCW, then fully CW, listening on Out 1 — **Expect:** noon = hollow notch, CCW = lowpass (dull), CW = highpass (thin). The notch is a mid-scoop, NOT silence.
5. Leave D at noon and move your cable across Outs 1, 3, 4 and 5 — **Expect:** all four responses live simultaneously: BP nasal, HP thin and fizzy, LP round, Mix = whatever D says.
6. Back on Out 1, patch a slow LFO (\~0.2 Hz, ±5 V) into In 2 — **Expect:** the Mix output morphs LP → notch → HP and back, passing through the hollow scoop rather than a gap. Unpatch the LFO.

**Reset:** reload the patch.

*Block 3 — the resonance limit · from a freshly loaded patch. Runs in order; step 8 is the payoff and needs step 7's state.*

7. Turn B (Res) to max and play notes into In 4 — **Expect:** British verge-of-self-osc: whistles and chirps riding along WITH the notes, never running away into a standalone scream.
8. Leaving B at max, turn v (Input level) to 0 — **Expect:** the whistle DIES with the input. British mode cannot self-oscillate without signal; if it keeps ringing, that's a bug. (RB-Vespid-2 is the same knob in German mode, where it *should* keep ringing.)

**Reset:** reload the patch.

*Block 4 — CV inputs · from a freshly loaded patch*

9. Patch a snappy envelope into In 1 and set depth with E — full CW, then partially CCW to invert — **Expect:** classic percussive filter sweeps; E below centre flips the sweep direction. Unpatch In 1.
10. Patch an LFO into In 3 and attenuvert with F — **Expect:** resonance animates from smooth to chirpy in time with the LFO, no clicks at the modulation extremes.

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

*Block 1 — the voice, and its tuning · from a freshly loaded patch. Steps 3-4 are the reference melody for RB-Vespid-3, so settle on a melody you can play again there.*

1. Load the patch and just listen, nothing patched — **Expect:** the filter sings on its own: a hollow, slightly gritty sine voice, loud but stable, bounded by the rails and never a runaway blast.
2. Sweep A across its range — **Expect:** the sung pitch follows the knob smoothly across the audible range, tone staying sine-like with a touch of grit. Return A to its loaded mid position.
3. Sequence a melody into In 1 with E fully CW — **Expect:** the melody plays IN TUNE over 3-4 octaves; octaves are real octaves (Corrected tracking). **Write down which melody you used** — RB-Vespid-3 replays it.
4. Send that same pitch CV through the mult or stackcable to both In 1 and a reference oscillator, and compare by ear at 1, 2 and 3 octaves up — **Expect:** unisons and octaves beat slowly or not at all across the range, with no progressive flattening as you climb. (That flattening is exactly what RB-Vespid-3 should show.)

**Reset:** reload the patch.

*Block 2 — the oscillation threshold · from a freshly loaded patch. Runs in order; step 6 uses the threshold you find in step 5.*

5. Slowly back B off from max until oscillation stops, then nudge it back up — **Expect:** a clear, repeatable threshold where the voice dies and restarts; right near it the filter may chirp before locking into steady tone. **Note where the threshold sits** on the knob.
6. Leave B just at that threshold and patch an LFO into In 2 — **Expect:** oscillation gates on and off with the LFO, a crude but playable tremolo/chirp, with no instability. Unpatch the LFO.

**Reset:** reload the patch.

*Block 3 — filter and oscillator interacting · from a freshly loaded patch*

7. With B at its loaded max, feed audio (any oscillator, or the drum loop) into In 3 — **Expect:** the input pulls at, beats against and colors the sung tone; combined output stays loud-but-bounded with no runaway. If it takes off or drops out, that's a finding.

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

*Block 1 — the tuning sag · from a freshly loaded patch. This is a direct A/B against RB-Vespid-2, so use the melody you noted there and reload each patch as you switch.*

1. Play the SAME melody you used in RB-Vespid-2 (its Block 1 step 3) into In 1 — **Expect:** it now drifts FLAT the way the real circuit sags at high resonance: intervals compress and stretch, "vintage out-of-tune." Musical, not broken.
2. Push the melody to the extremes, very low and very high — **Expect:** the sag is worst at the ends of the range; the middle stays closest to true.
3. A/B one two-octave jump: play it here, reload RB-Vespid-2 and play it there — **Expect:** patch 2 lands a true octave, this patch lands audibly flat of it. If they match, the uncorrected mode isn't engaging.

**Reset:** reload the patch.

*Block 2 — aliasing and CPU at the fixed 4× · from a freshly loaded patch*

4. Drive a bright, hot audio-rate signal (raw saw or square, high pitch) into In 3, with C high and A high — **Expect:** clean. Vespid is fixed at 4× oversampling in every build, so no inharmonic "birdies" sweeping the wrong way as you play up.
5. Leaving those hot settings from step 4 in place, watch the device CPU meter and turn knobs — **Expect:** acceptable headroom, no audio dropouts. Vespid always pays the 4× cost, so this is its worst-case figure; note it alongside the Onbetap numbers from RB-Onbetap-3.

## RB-Particules-1 — Free-running texture & qualities (Particules, granular texture from a baked-in melodic loop)

**Setup:** A melodic loop (melody-loop.wav, looping — eight plucked notes up an A-minor arpeggio, then \~1.5 s of steady held A3) auto-starts about 2 s after the patch loads and feeds Particules. The tonal source is the point: semitone Pitch steps, buffer position, and the Sunny/Scorched pitch waver are all audible against it in a way they never were against drums. All twelve knobs are spoken for, so the loop-restart control lives on Knob Set 2 (*Utility*): if the loop isn't playing, switch there, twist z up then down, and switch back. Particules output on Out 1 (L) / Out 2 (R). Baked state: Triggers seed mode, auto-gain on, pitch quantizer off, grain trigger out off. Starting texture: Density 65%, Time 30%, Size 55%, Shape 45%, Reverb 25%, Dry/Wet 100% (fully wet — no dry bleed under the grains), Time attenuverter fixed at +75%. Seed input is deliberately unmapped — Density free-runs. At the loaded Density the cloud is *sparse* — scattered grains with gaps, not a wash. That's the starting point, not a fault; step 1 thickens it.

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

*Block 1 — Density, and the shape of a grain · from a freshly loaded patch. Runs in order; each step leaves the state the next wants.*

1. Sweep A (Density) through its whole range — **Expect:** exactly 12 o'clock is silence; clockwise gives random grain clouds thickening as you go; counter-clockwise gives a metronomic pulse, denser the further you turn. Leave A somewhere clockwise with a comfortable cloud.
2. Sweep C (Size) from about 9 o'clock to full — **Expect:** grains lengthen from single plucks to long overlapping phrases of the arpeggio. Take C counter-clockwise of centre — **Expect:** grains play reversed: each pluck becomes a swell that ends in its attack, unmistakably backwards. Return C to \~55%.
3. Sweep D (Shape) — **Expect:** grain attacks move from clicky and percussive at one end to soft swells at the other (clearest on grains that land on the held-tone part of the loop, where the envelope is the only transient).
4. Turn E (Pitch) — **Expect:** grain pitch steps through semitone notches — each notch a clearly musical half-step against the source notes; centre = original pitch, roughly 3 o'clock = +12 st (grains land an exact octave up — if the interval at 3 o'clock is audibly not an octave, that's a finding).

**Reset:** reload the patch.

*Block 2 — Freeze · from a freshly loaded patch. Runs in order; step 7 needs Freeze still engaged from step 5.*

5. Twist u up past mid (Freeze on), then sweep B (Time) — **Expect:** you scrub through a frozen snapshot of the loop: different knob positions land on different, identifiable arp notes (low notes one way, high notes and the held tone the other), and holding B still repeats the same pitch indefinitely. New incoming audio is ignored — if the pitches under a parked knob keep changing as the loop plays on, the freeze isn't freezing.
6. Release u, then do the same with a gate into Gate In 1 — **Expect:** identical freeze behavior from the jack.
7. With Freeze still engaged, try to move v (Quality) — **Expect:** quality does NOT change while frozen. Release the freeze — **Expect:** the pending change now takes.

**Reset:** reload the patch.

*Block 3 — the attenurandomizers · from a freshly loaded patch, with NOTHING patched into any CV jack (that's the condition under test)*

8. Move y (Size AR) and z (Pitch AR) away from centre: counter-clockwise spreads values with a peak near the knob setting, clockwise gives a uniform random spread — **Expect:** centre = every grain identical; a touch of either makes the texture breathe; back to centre makes it static. These jacks are unmapped, so this is the pure randomize behavior.

**Reset:** reload the patch.

*Block 4 — Time CV and cable detection · from a freshly loaded patch*

9. Patch a slow LFO into In 1 — **Expect:** the read position scrubs back and forth through the loop — you hear it walk up and down the arpeggio (the Time attenuverter is fixed at +75%, so the sweep is wide but not full-range).
10. Remove the cable from In 1 — **Expect:** no effect at all from the empty jack. Flag anything that behaves as if a cable were still present.

**Reset:** reload the patch.

*Block 5 — the four Qualities · from a freshly loaded patch. Each Quality change re-formats the buffer, so expect a brief mute and a refill each time; that's normal, not a dropout.*

11. Step v (Quality) through its four positions, giving the buffer a couple of seconds to refill at each — **Expect:** Bright = clean; Cold = 12-bit Clouds-style grit; Sunny = darker with a GENTLE cassette waver, not seasick; Scorched = 8-bit crunch plus obvious warble. Judge the waver on grains from the held-tone part of the loop — a steady tone is where pitch instability shows; on Bright and Cold that tone should hold rock-steady.
12. Park v on Scorched and set w (Feedback) to about 60%, then let it run — **Expect:** repeats degrade into tape mush.
13. Leaving w where it is, step v back to Bright — **Expect:** the same feedback setting stays clean and simply builds, held in check by the per-quality limiter. That contrast is the point of the block.

**Reset:** reload the patch.

*Block 6 — reverb and mix · from a freshly loaded patch*

14. Turn x (Reverb) up past noon — **Expect:** a smooth reverb tail blooms around the grains; back at 0 it's gone.
15. Turn F (Dry/Wet) down from full wet — **Expect:** the untouched melodic loop fades in under the grains; at full counter-clockwise the grains are gone entirely.

*Utility, any time:* if the melody loop stops, switch to Knob Set 2 (*Utility*), twist z up then down, and switch back.

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

*Block 1 — Density against the clock · from a freshly loaded patch, with a steady 4-8 Hz clock in Gate In 1 throughout. Runs in order.*

1. With the clock running and A (Density) at exactly 12 o'clock — **Expect:** SILENCE. Probability is 0 at centre when clocked.
2. Turn A clockwise from centre — **Expect:** grains appear on clock ticks, more ticks spawning grains the further you turn until nearly every tick fires. Everything stays locked to the clock, never landing between ticks.
3. Turn A counter-clockwise from centre — **Expect:** clean clock divisions (every 2nd tick, every 4th, …) — a regular sub-rhythm, not random thinning.

**Reset:** reload the patch (leave the clock patched; the next block needs it).

*Block 2 — the quantized melody · from a freshly loaded patch, clock still in Gate In 1, A turned clockwise so grains are firing*

4. Sequence In 1 with a melody (1 V/oct) — **Expect:** the grains play the melody with every pitch snapped to A minor. Detune the sequence slightly — **Expect:** the quantizer pulls the notes back into the scale.
5. Turn u (Pitch AR) from full clockwise back toward centre — **Expect:** at full CW the CV tracks cleanly at 1 V/oct; as you approach centre the pitch CV increasingly becomes per-grain randomization instead of tracking.

**Reset:** reload the patch.

*Block 3 — the grain trigger output · from a freshly loaded patch, clock in Gate In 1, A clockwise so grains are firing*

6. Listen to (or scope) Out 2 — **Expect:** a 1 ms trigger per grain, audible as clicks. Drive an envelope or voice from it — **Expect:** a second voice locked to the grains, with distinct triggers even for back-to-back grains.
7. Confirm Out 1 alone carries the full audio — **Expect:** a mono sum of both stereo sides, since enabling grain triggers costs you the right channel.

**Reset:** reload the patch.

*Block 4 — clock cable detection · from a freshly loaded patch*

8. Start with the clock patched into Gate In 1 and grains firing, then unplug it — **Expect:** Particules free-runs on Density again (silent at 12 o'clock, clouds CW, pulse CCW). Clock detection follows the physical cable, so flag it if the empty mapped jack still behaves as if clocked.

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

*Block 1 — gated grain bursts · from a freshly loaded patch, with a slow gate (e.g. 1 s high / 1 s low) in Gate In 1 throughout. Runs in order.*

1. With the gate running — **Expect:** grains fire ONLY while the gate is high: rhythmic puffs of texture that stop dead when the gate falls, not a continuous stream.
2. Turn A (Density) clockwise from centre — **Expect:** the repetition rate INSIDE each gate rises and each puff gets busier; counter-clockwise thins it out.
3. Set A to exactly 12 o'clock — **Expect:** exactly one grain per gate, a single hit on each rising edge. (Note the contrast with RB-Particules-2, where centre means silence — Gates mode reads the centre position differently.)
4. Leaving the gate running, shape the puffs with B (Size) and C (Shape) — **Expect:** longer grains overlap into small swells; Shape moves attacks from clicky to soft.

**Reset:** reload the patch.

*Block 2 — gate cable detection · from a freshly loaded patch*

5. Patch the gate into Gate In 1 and confirm puffs, then unplug it — **Expect:** behavior identical to Triggers mode: free-runs on Density, silent at 12 o'clock, clouds CW, pulse CCW. Gate detection follows the physical cable, so an empty mapped jack should read as unpatched — flag any surprise here.

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

*Block 1 — the wavetable by hand · from a freshly loaded patch. Runs in order.*

1. Load the patch — **Expect:** a steady drone on Out 1 immediately, no input needed.
2. Sweep B (Bank) slowly from 0 to max — **Expect:** three families in order: soft sines, triangles and drawbar-organ tones in the low third; buzzy vocal formants in the middle; the Braids imports — choir, metal, drone flavors — up top. Every in-between position should be a usable hybrid, not garbage.
3. Pick three or four Bank settings across that range and sweep C (Position) slowly at each — **Expect:** continuous timbre blends, with no steps, zipper noise or clicks anywhere in the travel.
4. Turn A (Pitch) slowly across its range — **Expect:** the knob settles into notches at musical intervals — octaves, fifths, unison — rather than free-gliding, across a ±24 st span.

**Reset:** reload the patch.

*Block 2 — Position CV, from slow to audio rate · from a freshly loaded patch. Runs in order; step 6 is the same jack as step 5 at a different rate.*

5. Patch a slow LFO (\~0.1-0.5 Hz) into In 2 — **Expect:** the timbre breathes smoothly through the table. Move D — **Expect:** it scales the sweep depth, doing nothing at 12 o'clock and giving a moderate positive sweep at the baked \~.75. Return D to \~.75.
6. Swap the LFO for an audio-rate oscillator in the same jack and set D to about 2 o'clock (\~.65) — **Expect:** an FM-like clangorous growl, pitched and playable rather than noise hash. Back D toward 12 o'clock — **Expect:** the growl cleans up smoothly to the plain wavetable tone. Unpatch In 2.

**Reset:** reload the patch.

*Block 3 — pitch and bank CV · from a freshly loaded patch*

7. Play a keyboard or sequencer into In 1 — **Expect:** accurate V/oct tracking; octaves on the keyboard are octaves out, over at least 2-3 octaves.
8. Patch an LFO or sequencer into In 3 and raise E (Bank CV amt) above centre — **Expect:** the CV walks the drone through the bank families with the same character as turning B by hand. At E centre — **Expect:** the input does nothing.

## RB-Retours-1 — Tape delay, slicer, shimmer (Retours, full feature tour in Tape/doppler time-change mode)

**Setup:** `delay-test-loop.wav` (BWAVP, looping, auto-started \~2 s after load) feeds Retours through an attenuverter acting as an input level; delay mix on Out 1/2. Baked state: Tape (doppler) time-change, Bright quality. Feedback starts at 40%, Dry/Wet 50%, Interval at its default .35 (\~0.4 s delay), Time at 1×. The source is mono into both inputs — nothing here tests stereo source content.

**The loop is sparse on purpose, and its four sections each do a job.** Over 8 seconds: a single bright **blip**, then \~2.9 s of **silence** (long enough to hear roughly seven repeats at the loaded delay time), then **three quick plucks** in rhythm, a second gap, then a **2-second steady held tone**, then silence. Use the gaps to hear individual repeats and count them, the plucks for the slice tests, and the held tone for anything involving pitch — doppler bend, shimmer, and quality waver all read on a sustained tone and are close to inaudible on a transient.

**Knob z is Input level, and it is the most useful knob in this patch.** Turn it fully down to mute the source and leave the delay running on its own tail — that's the only way to judge decay, unity feedback, and how repeats degrade, since a source that keeps feeding the line hides all three. It loads fully up (unity). **Knob Set 2 (*Utility*)** holds z = Loop restart, for the rare case where the source stops.

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
| Knob z | Input level | Source level into Retours: fully up = unity, fully down = silence (loads up) |
| Knob z (Set 2: *Utility*) | Loop restart | BWAVP Play (twist up then down to replay) |
| In 1 | Time CV | Time CV |
| In 2 | Interval CV | Interval CV |
| Gate In 1 | Clock | Clock input (patching it switches Interval to divider mode) |
| Gate In 2 | Slice gate | Momentary slice |

**Try:**

*Block 1 — manual time and the doppler swoop · from a freshly loaded patch, nothing patched. Runs in order.*

1. Listen through a full loop, watching the gap after the blip — **Expect:** blip, then a single clean echo repeating into the silence. Sweep A from 12 o'clock toward CCW — **Expect:** the spacing between those echoes shortens as you turn, and more of them fit in the gap; 12 o'clock is the longest delay (\~4 s, longer than the gap, so there you get one echo landing well after the blip).
2. Sweep A to the CW side — **Expect:** a second, uneven tap joins: in the gap you hear a galloping two-tap pattern — *ta-tak … ta-tak* — not an even doubling.
3. Set A near 12 o'clock, then turn B up through its range — **Expect:** the delay time multiplies 1-16× and repeats stretch out until they run past the gap into the next section of the loop.
4. Raise C to \~60% so repeats are audibly regenerating. Wait for the **held tone** to arrive, turn z (Input level) fully down as it ends so the tail rings on alone, then grab A or B and turn — **Expect:** the ringing tone pitch-bends like varispeed tape, swooping DOWN as you lengthen and UP as you shorten, with no clicks. Muting first is what makes this obvious: you're bending a sustained pitch with nothing else in the way. This is the behavior RB-Retours-2 replaces, so fix it in your ear before moving on.

**Reset:** reload the patch.

*Block 2 — Time CV and the tape wander · from a freshly loaded patch. Step 6 requires In 1 to be EMPTY, so the order matters.*

5. Patch a small slow sine LFO into In 1 (Time CV) — **Expect:** a tape-warble chorus on the repeats, a gentle cyclic detune. Judge it on the held tone, where a cyclic detune is unmistakable; on the plucks it only sounds like slightly loose timing.
6. Unpatch In 1, then turn x (Time AR) toward CCW — **Expect:** a slow tape-mechanism wander creeps into the repeat timing: irregular drift, not the regular vibrato of step 5. Easiest to hear as uneven spacing between the echoes of the blip in the long gap. This depends on the jack sensing no cable, so flag anything odd if the mapped-but-empty jack misbehaves.

**Reset:** reload the patch.

*Block 3 — clocked mode · from a freshly loaded patch, with a steady clock in Gate In 1. Runs in order; step 10 needs the clock REMOVED, so do it last.*

7. Patch the clock into Gate In 1 — **Expect:** Interval (A) becomes a divider: the CCW side steps 1/2, 1/4, 1/8, 1/16 and the CW side gives triplet divisions. Repeats lock to the clock — count them in the gap after the blip, where the division is countable rather than merely felt.
8. Nudge the clock tempo up and down while it runs — **Expect:** repeats stay locked to the new tempo with no free-run drift.
9. Still clocked, turn B — **Expect:** Time snaps to musical multiples instead of sweeping continuously. Then turn E (Shape) up in stages — **Expect:** the repeat envelope steps flat → gated → swell → slow ramp, phase-locked to the beat.
10. Unpatch the clock, then twist w up/down four times in rhythm — **Expect:** tap tempo takes over, the light blinks the tapped beat, and the tempo holds. Re-tap to change it. **Note:** a tapped tempo persists — clearing it is menu-only — so reload the patch before the next block rather than leaving a tempo saved.

**Reset:** reload the patch.

*Block 4 — slice / beat-repeat · from a freshly loaded patch. Runs in order; steps 12-13 need the slice still latched from step 11.*

11. Twist u up (Slice latch) during the three-pluck figure — **Expect:** capture stops instantly and a slice repeats: an immediate beat-repeat hold, stuttering on whichever plucks were caught.
12. While still sliced, turn B — **Expect:** B chooses WHICH slice of the held audio repeats — you should be able to land on a different pluck, or on the silence between them, and hear the held fragment change.
13. While still sliced, turn A — **Expect:** A sets the slice length. Twist u back down to release.
14. Send a gate into Gate In 2 instead — **Expect:** the same slice behavior, held only while the gate is high.

**Reset:** reload the patch. Slice leaves Interval and Time somewhere unpredictable, and the shimmer block below needs them at their loaded values.

*Block 5 — shimmer and pitch · from a freshly loaded patch. Step 17 needs the Pitch CV jack EMPTY.*

15. Turn D to the +12 st notch and set C to \~65% — **Expect:** each repeat climbs an octave, a rising shimmer ladder. In the gap after the blip you should be able to hear it as a ladder of discrete octave steps, not a general brightening.
16. Return D exactly to centre and listen to the held tone's repeats — **Expect:** the shifter is truly bypassed: repeats are clean copies of the tone, holding one steady pitch with no chorus blur or detune haze. If centre still smears, that's a finding.
17. With nothing patched to a Pitch CV jack, turn y (Pitch AR) off-centre — **Expect:** a random per-repeat pitch spread; back at centre, exact repeats.

**Reset:** reload the patch.

*Block 6 — feedback and the four Qualities · from a freshly loaded patch. Deliberately cumulative: each over-unity build needs to run for a while, so don't reload mid-build. DO reload between Qualities so each starts from the same place.*

**Every step in this block uses the same move: let the loop play into the delay, then turn z (Input level) fully down and judge what the delay does on its own.** With the source still running you are listening to new material, not to the tail, and none of these three results can be told apart.

18. Set C just below the 90% arrow, let a pass or two in, then mute with z — **Expect:** the repeats decay away to silence. At the arrow, muted — **Expect:** they hold at a steady level indefinitely instead of fading. Above it, muted — **Expect:** they grow louder each pass. The three are only distinguishable with the source muted.
19. With C above unity on Bright (v at 0), feed in the held tone, mute, and let the stack build — **Expect:** it builds clean and eventually brickwall-limits without distorting the tone. **Then reload.**
20. Repeat the over-unity build on Sunny, then again on Scorched, reloading between each and muting each time — **Expect:** each is darker and more saturated than Bright; Scorched adds obvious warble, audible as pitch instability in the held tone. Each Quality limits differently, and none should run away into harsh digital clipping.
21. Finally, with repeats sounding and the source muted, step v through all four positions in one pass — **Expect:** a brief mute and re-format at each change is acceptable; character steps Bright (clean) → Cold → Sunny → Scorched on the same ringing tail.

*Utility, any time:* if the source loop stops, switch to Knob Set 2 (*Utility*), twist z up then down, and switch back. (On the main set, z is Input level — if you get silence unexpectedly, check that first.)

## RB-Retours-2 — Crossfade time-change (Retours, same rig as RB-Retours-1 with digital-clean time jumps)

**Setup:** Identical to RB-Retours-1 (`delay-test-loop.wav` → input level → Retours, delay mix on Out 1/2) except the baked time-change mode is Crossfade instead of Tape. Same knob/jack layout and aliases as RB-Retours-1, same sparse loop, and the same **Input level on knob z** — mute the source and work on the tail exactly as there.

**Panel:** Same as RB-Retours-1: A=Interval, B=Time, C=Feedback, D=Pitch, E=Shape, F=Dry/Wet, u=Slice (up=hold), v=Quality, w=Tap (twist up), x=Time AR, y=Pitch AR, z=Input level; Knob Set 2 (*Utility*) z=Loop restart; In 1=Time CV, In 2=Interval CV, Gate In 1=Clock, Gate In 2=Slice gate.

**This patch exists to be compared with RB-Retours-1**, so the two must be in matching states when you switch. Reload whichever patch you're moving to, then make the same move in both.

**Try:**

*Block 1 — clean time jumps · from a freshly loaded patch. Runs in order.*

1. Raise C to \~60% so repeats are audibly regenerating. Wait for the **held tone**, mute with z as it ends so the tail rings alone, then grab A or B and turn — **Expect:** the delay JUMPS cleanly to the new time: the ringing pitch stays put, pitch-neutral and click-free with no tape swoop, each jump landing waveform-aligned with no tick at the splice. A sustained tone is the only material that shows this — on a transient, "no pitch bend" and "pitch bend" look alike.
2. Sweep A fast across a wide range, still on the muted tail — **Expect:** repeats stay clean and intelligible rather than garbling or smearing.

**Reset:** reload this patch, and reload RB-Retours-1 before switching to it.

*Block 2 — the A/B against Tape mode · both patches freshly loaded, same move in each*

3. Make the step 1 move here — same wait for the held tone, same mute, same turn — then reload RB-Retours-1 and make the identical move there — **Expect:** this patch steps cleanly like a digital delay; patch 1 pitch-bends like a tape machine. Both patches load the same loop and the same knob map, so the ringing tone is the one variable: if they sound alike, that's a bug worth reporting.

**Reset:** reload the patch.

*Block 3 — spot-check the shared features · from a freshly loaded patch, reloading between each item below*

4. Clock into Gate In 1 — **Expect:** divider mode and locked repeats, exactly as in RB-Retours-1. **Reload.**
5. Slice via u or Gate In 2 during the three-pluck figure — **Expect:** identical beat-repeat hold behavior. **Reload.**
6. Shimmer with D at the +12 notch and C \~65% — **Expect:** the rising octave ladder in the gap after the blip, same as patch 1. Only manual and CV time changes should differ between the two patches; anything else that diverges is a finding.

*Utility, any time:* if the source loop stops, switch to Knob Set 2 (*Utility*), twist z up then down, and switch back. (On the main set, z is Input level — if you get silence unexpectedly, check that first.)

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

**Plucking it** — every block needs an excitation source. *Pluck it:* feed short clicks, gates, or noise bursts into In 1. Nothing sounds until you do.

**Try:**

*Block 1 — the string voice · from a freshly loaded patch. Runs in order.*

1. Pluck it — **Expect:** each burst rings out as a plucked string with a clear pitch, not a discrete echo. If you hear separate echoes rather than a pitched ring, Interval isn't in its audio-rate range.
2. Sequence In 2 with V/oct pitches while plucking — **Expect:** a playable Karplus voice, tuning stable and in tune over roughly 2 octaves.
3. Turn B (Feedback) up toward max while plucking — **Expect:** notes sustain longer and longer but stay bounded, with no runaway blow-up even near the top.

**Reset:** reload the patch — step 3 leaves Feedback high, and the glissando below is easier to hear at the loaded 85%.

*Block 2 — doppler glissando and the pitch spiral · from a freshly loaded patch, plucking throughout*

4. Grab A and turn it by hand while a note rings — **Expect:** a tape-doppler glissando between pitches, a continuous swoop rather than a step (this patch is in Tape mode, like RB-Retours-1).
5. Move D off-centre and pluck again — **Expect:** each ring spirals upward or downward in pitch as it decays: strange and metallic, but pitched and controllable. Back at the centre notch — **Expect:** the spiral stops dead.

**Reset:** reload the patch.

*Block 3 — Quality on a resonant voice · from a freshly loaded patch, reloading between each Quality so the string starts identical each time*

6. Step v through Bright, Cold, Sunny and Scorched, plucking at each and reloading in between — **Expect:** the string's decay character changes with each — brighter and cleaner at Bright, progressively grittier and more filtered toward Scorched — while the pitch stays put. Feedback is high here, so listen for any Quality that lets the ring run away rather than decay.

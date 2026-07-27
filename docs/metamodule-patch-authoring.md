# Hand-writing MetaModule patches for Robot Boy

Reference for authoring MetaModule patch files (.yml) by hand, compiled 2026-07-26 from the firmware serializers (`Dev/metamodule/firmware/lib/patch-serial/`), the hub panel definitions, and the Robot Boy sources. Written to drive the `mm-test-patches/` generation; the ID tables and normalization rules are general-purpose.

## 1. Patch YAML format

Root map is `PatchData:`. Required keys: `patch_name` (non-empty), `module_slugs`, `int_cables`, `mapped_ins`, `mapped_outs` (use `[]` when empty). Optional: `description`, `static_knobs`, `mapped_knobs`, `vcvModuleStates`. Do NOT write `suggested_samplerate`/`suggested_blocksize` (omit = don't override device setting), `midi_*`, `mapped_lights`, `bypassed_modules`, `module_aliases`.

```yaml
PatchData:
  patch_name: RB-Vespid-1
  description: 'British wasp: square in, Mix+BP out'   # <=255 chars
  module_slugs:
    0: '4msCompany:HubMedium'      # module 0 is ALWAYS the hub
    1: 'RobotBoy:Vespid'
    2: 'Fundamental:VCO'
  int_cables:                      # internal cables: one entry per SOURCE jack
    - out: {module_id: 2, jack_id: 0}
      ins:
        - {module_id: 1, jack_id: 0}
  mapped_ins:                      # panel jacks -> module inputs
    - panel_jack_id: 0             # In 1
      alias_name: Freq CV          # optional, <=31 chars — ALWAYS set it
      ins:
        - {module_id: 1, jack_id: 2}
  mapped_outs:                     # module outputs -> panel jacks
    - panel_jack_id: 0             # Out 1
      alias_name: Mix L
      out: {module_id: 1, jack_id: 0}
  static_knobs:
    - {module_id: 1, param_id: 0, value: 0.524678}
    ...
  mapped_knobs:
    - name: Knob Set 1
      set:
        - {panel_knob_id: 0, module_id: 1, param_id: 0, curve_type: 0, min: 0, max: 1, alias_name: Freq}
  vcvModuleStates:                 # adapter-module menu options (omit if all defaults)
    - module_id: 1
      data: |-
        {"german":true,"panelTheme":0,"oscPitchCorrected":true}
```

Rules:
- `module_slugs` keys `0:`, `1:`… are decorative but must be in order; ids elsewhere refer to these positions.
- `jack_id` = 0-based index of the module's input (for cable `ins` / `mapped_ins`) or output (for cable `out` / `mapped_outs`) per the tables below. `param_id` likewise.
- One `int_cables` entry per source output; several `ins` = fan-out. Omit `color`.
- Inline flow maps (`{module_id: 1, jack_id: 0}`) and block style are both fine.
- `static_knobs` `value` is NORMALIZED 0..1: `value = (raw - min)/(max - min)`. Snapped params (switches/buttons) round to nearest position; an N-position switch takes `i/(N-1)` for position i.
- **CRITICAL: a param omitted from `static_knobs` loads as 0.0, NOT its default.** For EVERY module in the patch (except the hub), write a static_knobs entry for EVERY param, even ones at default. Use the tables' normalized defaults for params the sheet doesn't override.
- `mapped_knobs`: one knob set named `Knob Set 1`. `curve_type: 0` always (knob follows position; for button params, turning the knob up past mid = press, back down = release). `min: 0, max: 1` unless the sheet says otherwise. Always give `alias_name`.
- A module input can be fed by EITHER an internal cable OR a panel mapping, not both.
- `vcvModuleStates`: `data` is the module's dataFromJson JSON, compact, on one line, as a `|-` literal block indented two spaces past `data:`. Only include modules whose sheet specifies non-default state. Include ALL keys from that module's state template (below), not just the changed one.

## 2. MetaModule panel (HubMedium) IDs

`panel_knob_id`: 0-5 = big knobs **A B C D E F**; 6-11 = small knobs **u v w x y z**.
`mapped_ins` `panel_jack_id`: 0-5 = **In 1…In 6**; 6 = **Gate In 1**; 7 = **Gate In 2**.
`mapped_outs` `panel_jack_id`: 0-7 = **Out 1…Out 8**. (Outs 1-6 are line/audio outs; user listens on Out 1/2 by default.)

Conventions for these patches:
- Main stereo audio → Out 1 (`panel_jack_id: 0`) and Out 2 (1). Extra outputs (e.g. Vespid BP tap, Loooop head out) → Out 3/4.
- Performance params → big knobs A-F; buttons/secondary params → small knobs u-z.
- CV the user will patch (LFO/pitch/audio-rate) → In 1…In 6; clocks/gates/triggers → Gate In 1/2 first, then spare In jacks.

## 3. Robot Boy modules — brand slug `RobotBoy`

All static values below are NORMALIZED. "def" = normalized default — use it unless the sheet overrides.

### RobotBoy:Loooop
Params: 0 Record (button 0/1, def 0) · 1 Overdub (Layer 0, Decay .25, Add .5, Replace .75, Lock 1; def 0) · 2 Clear (button, 0) · 3 Grid (Off 0, 4=.2, 8=.4, 16=.6, 32=.8, 64=1; def 0) · 4 Dry/Wet (def **1.0**) · heads Red/Yellow/Blue/Purple: Size 5/11/17/23 (def **1.0**) · Position 6/12/18/24 (def .5) · Speed 7/13/19/25 (0..1 ↔ −2..+2×; 1×=.75, 2×=1.0, −1×=.25, 1.5×=.875, .5×=.625; def .75) · Jitter 8/14/20/26 (def 0) · Pan 9/15/21/27 (def .5) · Level 10/16/22/28 (def **.25**).
Options (0 or 1): 29 Record jack (0 Trigger/1 Gate) · 30 When rec ends (0 Plays back/1 Keeps overdubbing) · 31 Crossfade (**0 = ON**/1 = Off) · 32-35 Trig mode R/Y/B/P (0 loop-start/1 one-shot) · 36-39 Speed V/Oct R/Y/B/P (0/1) · 40-43 Grid exclude R/Y/B/P (0/1).
Inputs: 0 In L · 1 In R · 2 Record Trig · 3 Clear Trig · 4 Dry/Wet CV · then per head R/Y/B/P base 5/13/21/29: +0 Size CV, +1 Pos CV, +2 Speed CV, +3 Jitter CV, +4 Pan CV, +5 Level CV, +6 Trig, +7 Jump.
Outputs: 0 Mix L · 1 Mix R · 2/3 Red L/R · 4/5 Yellow L/R · 6/7 Blue L/R · 8/9 Purple L/R.
No vcvModuleStates entry (native module).

### RobotBoy:Lop
Params: 0 Size (def **1.0**) · 1 Position (.5) · 2 Speed (encoding as Loooop; def .75) · 3 Jitter (0) · 4 Dry/Wet (**1.0**) · 5 Record (0) · 6 Clear (0) · 7 Overdub (5-pos as Loooop; 0) · 8 Grid (6-pos; 0) · 9 Record jack (0/1) · 10 When rec ends (0/1) · 11 Crossfade (**0=On**/1=Off) · 12 Trig mode (0/1 one-shot) · 13 Speed V/Oct (0/1).
Inputs: 0 In L · 1 In R · 2 Size CV · 3 Position CV · 4 Speed CV · 5 Jitter CV · 6 Trig · 7 Jump · 8 Dry/Wet CV · 9 Record Trig · 10 Clear Trig. Outputs: 0 Out L · 1 Out R.
No vcvModuleStates.

### RobotBoy:MF20Filter
Params: 0 LP Cutoff (log2 Hz 4.32193..14.28771; norm = log2(Hz/20)/9.965784; def .524678 = 750 Hz; 150 Hz ≈ .2915, 1.5 kHz ≈ .625, 300 Hz ≈ .392, 120 Hz ≈ .2594) · 1 LP Peak (def .25) · 2 HP Cutoff (same scale, def .2594 = 120 Hz) · 3 HP Peak (def .25) · 4 Drive (raw 1..8×, def 0) · 5 LP Cutoff CV amt (−1..1, def **1.0**) · 6 HP Cutoff CV amt (def **1.0**) · 7 Total CV amt (def **1.0**).
Inputs: 0 Audio L · 1 Audio R · 2 LP Cutoff CV · 3 HP Cutoff CV · 4 Total Cutoff CV. Outputs: 0 Audio L · 1 Audio R.
State template: `{"_filterMode":0}` — 0 OTA · 1 Korg35.

### RobotBoy:Onbetap
Params: 0 Cutoff (log2 scale as MF-20; def .524678 = 750 Hz; 400 Hz ≈ .4337, 200 Hz ≈ .3333) · 1 Q (def 0) · 2 Drive (def 0) · 3 Mode (LP 0, BP .25, HP .5, Notch .75, Peak 1; def 0) · 4 Cutoff CV amt (def **1.0**) · 5 Q CV amt (def **1.0**) · 6 Drive CV amt (def **1.0**).
Inputs: 0 Audio L · 1 Audio R · 2 Cutoff CV · 3 Q CV · 4 Drive CV. Outputs: 0 Audio L · 1 Audio R.
State template: `{"vintageDrift":false,"oversample":1}` — vintageDrift true = Vintage; oversample 1/2/4 (MM default 1).

### RobotBoy:Particules
Params: 0 Freeze (0/1 latch, def 0) · 1 Quality (Bright 0, Cold .333333, Sunny .666667, Scorched 1; def 0) · 2 Time (def .5) · 3 Density (def .5 = silence at 12 o'clock) · 4 Pitch (def .5 = 0 st; +12 st ≈ .758, −12 st ≈ .242) · 5 Size (def .5) · 6 Shape (def .5) · 7 Feedback (def 0) · 8 Reverb (def 0) · 9 Dry/Wet (def .5) · 10 Time AR (def .5 = center) · 11 Pitch AR (.5) · 12 Size AR (.5) · 13 Shape AR (.5) · 14 Feedback CV amt (.5) · 15 Reverb CV amt (.5) · 16 Dry/wet CV amt (.5).
Inputs: 0 In L · 1 In R · 2 Freeze gate · 3 Seed/clock · 4 Time CV · 5 Density CV · 6 Pitch CV (V/oct) · 7 Size CV · 8 Shape CV · 9 Feedback CV · 10 Reverb CV · 11 Dry/wet CV. Outputs: 0 Out L · 1 Out R.
State template (include all 7 keys): `{"qualityState":0,"seedState":0,"autoGain":true,"manualGainDb":0.0,"pitchScale":0,"pitchRoot":0,"grainTriggerOut":false}`
seedState 0 Triggers/1 Gates; pitchScale 0 Off,1 Octaves,2 Oct+5ths,3 Chromatic,4 Major,5 Minor,6 MajPent,7 MinPent; pitchRoot 0-11 = C..B; grainTriggerOut true = R out fires 1 ms/grain, L = mono mix. qualityState is overridden by param 1 on MM — set param 1, keep key at matching value.

### RobotBoy:Ondes
Params: 0 Pitch (def .5 = 0 st, notch map ±24 st) · 1 Position (def .5) · 2 Position CV amt (−1..1, def .5 = off; full = 1.0) · 3 Bank (def 0) · 4 Bank CV amt (def .5; full = 1.0).
Inputs: 0 Pitch V/Oct · 1 Bank CV · 2 Position CV. Output: 0 Out.
No vcvModuleStates.

### RobotBoy:Vespid
Params: 0 Freq (log2 scale as MF-20; def .524678 = 750 Hz; 300 Hz ≈ .392) · 1 Res (def 0) · 2 Drive (def 0) · 3 Blend (0 LP, .5 notch, 1 HP; def .5) · 4 Freq CV amt (def **1.0**) · 5 Res CV amt (def **1.0**) · 6 Drive CV amt (def **1.0**).
Inputs: 0 Audio L · 1 Audio R · 2 Freq CV · 3 Res CV · 4 Drive CV · 5 Blend CV. Outputs: 0 Mix L · 1 Mix R · 2 HP L · 3 HP R · 4 BP L · 5 BP R · 6 LP L · 7 LP R.
State template (all 3 keys): `{"german":false,"panelTheme":0,"oscPitchCorrected":false}` — german true = German (self-osc); oscPitchCorrected true = Corrected tracking. (`osMenu` removed 2026-07-26 — oversampling is fixed at 4× in every build; the key is read-and-ignored in old states.)

### RobotBoy:Retours
Params: 0 Slice (0/1 latch, def 0) · 1 Quality (Bright 0, Cold .333333, Sunny .666667, Scorched 1; def 0) · 2 Tap tempo (momentary button, def 0) · 3 Time (def 0 = 1×) · 4 Interval (def .35; 12 o'clock = .5 = longest delay; far CCW/CW = audio rate) · 5 Pitch (def .5 = 0 st; +12 ≈ .758) · 6 Shape (def 0 = off) · 7 Feedback (def 0; unity at .9) · 8 Dry/Wet (def .5) · 9 Time AR (def .5) · 10 Pitch AR (.5) · 11 Shape AR (.5) · 12 Feedback CV amt (.5) · 13 Dry/wet CV amt (.5).
Inputs: 0 In L · 1 In R · 2 Slice gate · 3 Clock · 4 Time CV · 5 Interval CV · 6 Pitch CV · 7 Shape CV · 8 Feedback CV · 9 Dry/wet CV. Outputs: 0 Out L · 1 Out R.
State template: `{"qualityState":0,"timeChangeMode":0}` — timeChangeMode 0 Tape (doppler) / 1 Crossfade.

## 4. Supporting modules

Slugs verified against the shipped MetaModule plugin manifests. Only write static_knobs for the params listed here; some modules have unconfigured param ids (noted) — never write those.

### 4msCompany:BWAVP (Basic WAV Player)
Params: 0 Play (momentary button, 0) · 1 Loop (latch 0/1, 0) · 2 Retrig mode (0 Retrigger/.5 Stop/1 Pause; 0) · 3 Waveform zoom (def .1015625) · 4 Buffer threshold (def .25) · 5 Max buffer size (12-pos, idx/11; def 3/11 = .272727 = 8 MB ≈ 43 s @48k — fine for our 14 s file) · 6 Buffer strategy (0) · 7 Startup delay (21-pos idx/20; 0). NEVER write param 8 (Load Sample — opens a file browser).
Inputs: 0 Play Trig · 1 Loop Gate. Outputs: 0 Out L · 1 Out R (±5 V) · 2 Play Gate (5 V while playing) · 3 End Out (10 ms pulse at file end).
Sample file: via vcvModuleStates — the data is a BARE PATH STRING, not JSON:
```yaml
    - module_id: 2
      data: |-
        drum-loop.wav
```
A bare filename resolves to the patch's own directory on hardware (subdirs are stripped — keep the wav next to the .yml). The file `drum-loop.wav` ships in the patches dir.
IMPORTANT: after loading a sample the player sits PAUSED — playback needs a rising edge on Play (param past 0.5, or Play Trig input). Patches auto-start it via CountModula:StartupDelay (below) AND map Play to a small knob as a manual restart.

### CountModula:StartupDelay
Param: 0 Delay 1..30 s (norm = (sec−1)/29; 2 s = .034483). No inputs. Outputs: 0 gate high DURING delay · 1 gate high at END of delay · 2 trigger at end of delay. Use output 2 → BWAVP Play Trig.

### Fundamental:VCO
Params (write ONLY these; ids 0 and 3 are unconfigured — never write them): 1 Sync mode (def 1.0 = hard) · 2 Frequency (−54..+54 st around C4; norm = (st+54)/108; C1 .16667, C2 .27778, C3 .38889, C4 .5, C5 .61111) · 4 FM amount (def .5 = 0) · 5 Pulse width (def .5) · 6 PW CV amt (def .5) · 7 FM mode (def 0 = 1V/oct).
Inputs: 0 V/Oct · 1 FM · 2 Sync · 3 PW. Outputs: 0 Sin · 1 Tri · 2 Saw · 3 Sqr (±5 V).

### Fundamental:VCA-1
Params: 0 Level (0..1, def 1.0) · 1 Response (0 Exp/1 Linear, def 1.0). Inputs: 0 CV · 1 In. Output: 0 Out.

### Fundamental:LFO (if needed; prefer Bogaudio LLFO for single-wave uses)
Params (id 4 unconfigured — skip): 0 Offset (1 = unipolar 0-10 V, 0 = bipolar ±5 V; def 1.0) · 1 Invert (0) · 2 Frequency (f = 2^v Hz, v raw −8..10; norm = (log2(f)+8)/18; 0.1 Hz .25989, 0.5 Hz .38889, 2 Hz .5, 5 Hz .57344) · 3 FM amt (.5) · 5 PW (.5) · 6 PWM amt (.5).
Inputs: 0 FM · 1 (skip) · 2 Reset · 3 PW · 4 Clock. Outputs: 0 Sin · 1 Tri · 2 Saw · 3 Sqr.

### Bogaudio:Bogaudio-LLFO
Params: 0 Frequency (norm = (v+5)/13, f = 261.626·2^(v−7) Hz normal / 2^(v−11) slow; fast: 0.5 Hz .22836, 1 Hz .30528, 2 Hz .38220, 5 Hz .48389; slow(param 1 = 1): 0.05 Hz .28052, 0.1 Hz .35744, 0.5 Hz .53605) · 1 Slow (0/1) · 2 Waveform (idx/6: Sine 0, Tri .16667, RampUp .33333, RampDown .5, Square .66667, Pulse .83333, Stepped 1) · 3 Offset (def .5 = 0 V; ±5 V full) · 4 Scale (def 1.0 = ±5 V; .5 = ±2.5 V).
Inputs: 0 Pitch (V/oct) · 1 Reset. Output: 0 Out.

### Fundamental:Noise (if needed)
No params. Outputs: 0 White · 1 Pink · 2 Red · 3 Violet · 4 Blue · 5 Gray · 6 Black.

### Caveat for checklist writers
Behaviors keyed to "a cable is patched into this CV input" (Particules/Retours attenurandomizer mode, Particules auto-gain recalibration, Retours clock detection, Particules Seed detection) follow the PHYSICAL cable in the mapped panel jack (the MetaModule senses plug insertion). A mapped-but-empty panel jack should behave as unpatched — but flag any test that depends on this so the user can watch for surprises. Never map a panel jack to Particules' Seed input in a patch whose tests assume free-running Density.

## 5. Validation

After writing each patch:
```bash
cd /Users/gabrielroth/Dev/metamodule/simulator && ./build-headless/simulator -p /Users/gabrielroth/Dev/RobotBoy/mm-test-patches/RB-X-n.yml -n 48000 --out /tmp/rb-test.wav
```
Expect `Patch loaded: N modules` with N = your module count (hub excluded? report what it prints), and NO `Module ... not found` lines. If the patch should make sound without user input, check the wav is non-silent (`afinfo` or python soundfile RMS). Fix and re-run until clean. Note in your report what the simulator printed and whether the wav had signal.

## 6. Checklist section

Alongside each .yml, write `RB-X-n.checklist.md`: a section for the user's testing doc. Format:

```markdown
## RB-X-n — Title (module, one-line purpose)

**Setup:** what's in the patch, where audio comes out (Out 1/2…), what state is baked in.
**Panel:** table of Knob A-F/u-z and In/Gate jacks → what they control (use the aliases).
**Try:**
1. <action> — **Expect:** <specific audible/visible outcome>
2. …
```

"Try" items must trace to the features listed in the patch sheet; keep them concrete (knob positions like "turn D past 3 o'clock", specific pitches/rates for external signals) and expectations falsifiable ("repeats pitch-bend downward while you turn, no clicks"), not vague ("sounds good"). The user cannot open MetaModule menus — never ask them to; alternate settings live in other patches. External signals available to the user: LFOs (any shape/rate), pitch CV/keyboard, clocks/gates/triggers, audio-rate oscillators.

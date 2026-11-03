# Lyrebird

*The lyrebird is a virtuoso mimic, reproducing the sounds around it in its own song.*

**Lyrebird** is a gesture and CV recorder for [VCV Rack](https://vcvrack.com) and the [4ms MetaModule](https://4mscompany.com/metamodule), for modulation that is easier to perform than to program. Turn its knob or input CV while recording and it plays the movement back as a loop locked to the bar or as a triggered one-shot. Takes can be edited, transformed and saved as files.

Lyrebird is one of Robot Boy’s three complex modulators, with [Skylark](Skylark.md) and [Weaver](Weaver.md).

<img src="screenshots/Lyrebird.png" alt="Lyrebird module" height="500">

---

## Controls

### Recording

- **INPUT**: the big knob is the recording source, ±5 V by default (see [**Settings**](#settings)). If a cable is plugged into the jack, incoming voltage replaces the knob as the input source.
- **RECORD**: starts recording a new take, replacing the current one; press again to stop. A Sync take stops by itself at LENGTH.
- **PUNCH**: the button or a gate starts overwriting the take at the playhead while it keeps playing; press again to stop. Punch works at any speed and in either direction, with a short crossfade at each end. With no take, PUNCH records a new one.
- **LENGTH**: the length of a new Sync take, 1/4 to 16 bars, default 1.
- **SYNC / FREE**: the kind of take RECORD creates.
    - **Sync** (default): RECORD waits for the next bar, or TRIG, with its light blinking, then records for LENGTH. With LENGTH under a bar it waits for the next quarter or half bar. The take is a whole number of bars, follows the tempo and plays from where it was recorded.
    - **Free**: recording runs from press to press, up to 3 minutes, and plays at its recorded duration.

### Playback

- **LOOP / ONCE**: **Loop** (default) plays the take over and over; **Once** plays once. Switching doesn’t change the take.
- **SPEED**: sets the playback rate, from 1/4 to x4, default x1. In Sync it snaps to powers of two to stay on the bar. SPEED CV is 1 V/octave.
- **SMOOTH**: smooths playback, up to a 1 s window, without lag. The recording itself is unchanged.
- **REVERSE**: the button or a gate plays the take backward while held (see [**Settings**](#settings)).

SPEED and SMOOTH have CV inputs.

### Timing

- **CLOCK**: sets the tempo, 20–300 BPM, default 120. A clock at the input overrides the knob.
- **TRIG**: the button or a trigger restarts the take and the bar count. In Once, it plays a pass. With a clock patched, it waits for the next clock pulse.

### Files

- **SAVE** (the center FILE button): saves the take to the current folder as `LYREBIRD-001.wav`, `LYREBIRD-002.wav` and so on. On MetaModule with no card or drive, it opens the Save as dialog.
- **‹ ›**: the buttons or triggers load the previous or next WAV file in the current folder.
- The current folder is that of the last file opened or saved. In VCV Rack it defaults to `RobotBoy/Lyrebird` in the Rack user folder.
- On MetaModule, the default folder is the first of `RobotBoy/Lyrebird`, `Lyrebird` or `RobotBoy` found on the SD card, then on a USB drive, then the root of either. MetaModule can’t create folders; make one from a computer.
- Takes are saved as mono 32-bit float WAV files at 1 kHz, with ±10 V as full scale. Other WAV files load as Free takes from their first channel, up to 3 minutes.
- The take is also stored in the patch, so the patch doesn’t depend on the file.

### Outputs

- **OUT**: the take. While recording, or with no take, OUT follows the source.
- **EOC**: a trigger at each loop wrap and at the end of each Once pass.

---

## Display

The display shows the whole take, raw in grey and smoothed in white, with a playhead; new recording is drawn in magenta. The header shows the take name (`NEW TAKE` if unsaved, `EMPTY` with no take, `*` if modified), the play mode, speed and SMOOTH. The footer shows the recording status or a file message, the source (`IN: KNOB` or `IN: CV`), and the take’s length and tempo, or the LENGTH setting just after it’s turned. On MetaModule, the header leaves out SMOOTH and file messages replace the take name.

---
<a id="settings"> </a>
## Settings

- **Knob range**: ±5 V (default) or 0–10 V.
- **Record buttons**: **Latch** (default) or **Press** (records only while held).
- **Reverse button**: **Press** (default) or **Latch** (each press toggles direction).
- **Start recording (Free)**: **On press** (default) or **On first movement** (arms, then starts when the source moves). Sync takes always start on the bar.
- **Idle output**: what OUT holds between Once passes: **Last value** (default), **First value** or **0 V**.
- **Clock pulses per cycle**: pulses per bar at the CLOCK input: 1, 2, 4, 8, 16, 24, 32, 48 or 96 (default 4).
- **Open…**, **Save as…**: file dialogs.
- **Undo last take**: swaps back to the take before the last record, punch or file load.

# Lyrebird

*The lyrebird is a virtuoso mimic: it copies the sounds around it, from other birds' calls to camera shutters, and reproduces them faithfully in its own song.*

**Lyrebird** is a gesture and CV recorder for [VCV Rack](https://vcvrack.com) and the [4ms MetaModule](https://4mscompany.com/metamodule). Turn its knob while recording, or record any CV, and it plays the movement back as a loop locked to the bar or as a triggered one-shot. It is for modulation that is easier to perform than to program, and takes can be edited, transformed and saved as files. It is one of Robot Boy's three complex modulators, with [Skylark](Skylark.md) and [Weaver](Weaver.md).

<img src="screenshots/Lyrebird.png" alt="Lyrebird module" height="500">

---

## Controls

### CLOCK and LENGTH

- **CLOCK**: tempo, 20–300 BPM, default 120. A clock at the input overrides the knob.
- **LENGTH**: the length of a new Sync take, 1/4 to 16 bars (default 1).

### RECORD and PUNCH

- **RECORD**: the button or a gate starts recording a new take, replacing the current one; press again to stop. A Sync take stops by itself at LENGTH.
- **PUNCH**: the button or a gate starts overwriting the take at the playhead while it keeps playing; press again to stop.

### INPUT

The big knob is the recording source: ±5 V by default, or 0–10 V (see **Knob range**). A cable at the jack beside it replaces the knob as the source.

### SPEED, SMOOTH, TRIG and REVERSE

- **SPEED**: playback rate, x1/4 to x4, default x1. In Sync it snaps to powers of two to stay on the bar. SPEED CV is 1 V/octave.
- **SMOOTH**: smooths playback, up to a 1 s window, without lag. The recording itself is unchanged.
- **TRIG**: the button or a trigger restarts the take and the bar count. In Once, it plays a pass.
- **REVERSE**: the button or a gate plays the take backward while held (see **Reverse button**).

SPEED and SMOOTH have CV inputs.

### Outputs

- **OUT**: the take. While recording, or with no take, OUT follows the source.
- **EOC**: a trigger at each loop wrap and at the end of each Once pass.

---

## Display

- **Header:** the take name (`NEW TAKE` if unsaved, `EMPTY` with no take, `*` if modified); play mode, speed and SMOOTH.
- **Trace:** the whole take, raw in grey and smoothed in white, with a playhead. New recording is drawn in magenta.
- **Footer:** recording status or a file message; the source (`IN: KNOB` or `IN: CV`); the take's length and tempo, or the LENGTH setting just after it's turned.

---

## Recording

- **Sync:** recording is placed on the bar grid, counted from the last TRIG. Starting partway through the take wraps around to fill it.
- **Free:** recording runs from press to press, up to 3 minutes.
- **Once:** a take starts where RECORD is pressed, so it plays from its beginning on the next TRIG.
- **Punch:** works at any speed and in either direction, with a short crossfade at each end. With no take, PUNCH records a new one.

---

## Files

- **SAVE** (the center FILE button): saves the take to the current folder as `LYREBIRD-001.wav`, `LYREBIRD-002.wav` and so on.
- **‹ ›**: the buttons or triggers load the previous or next WAV file in the current folder.
- The current folder is that of the last file opened or saved. In VCV Rack it defaults to `RobotBoy/Lyrebird` in the Rack user folder.
- Takes are saved as mono 32-bit float WAV files at 1 kHz, with ±10 V as full scale. Other WAV files load as Free takes from their first channel, up to 3 minutes.
- The take is also stored in the patch, so the patch doesn't depend on the file.
- File messages appear in the display footer.

---

## Context menu

- **Time**: **Sync** (default; a take is a whole number of bars and follows the tempo) or **Free** (a take plays at its recorded duration).
- **Play**: **Loop** (default) or **Once** (one pass per TRIG).
- **Knob range**: ±5 V (default) or 0–10 V.
- **Record buttons**: **Latch** (default) or **Press** (records only while held).
- **Reverse button**: **Press** (default) or **Latch** (each press toggles direction).
- **Start recording**: **On press** (default) or **On first movement** (arms, then starts when the source moves).
- **Idle output**: what OUT holds between Once passes: **Last value** (default), **First value** or **0 V**.
- **Clock pulses per cycle**: pulses per bar at the CLOCK input, 1–96 (default 4).
- **Open…**, **Save as…**: file dialogs.
- **Undo last take**: swaps back to the take before the last record, punch or file load.

---

## MetaModule

- The display leaves out SMOOTH, and file messages replace the take name.
- MetaModule can't create folders; make one on the SD card or USB drive from a computer. The default folder is the first of `RobotBoy/Lyrebird`, `Lyrebird` or `RobotBoy` found on the SD card, then on a USB drive, then the root of either.
- With no card or drive, SAVE opens the Save as dialog.

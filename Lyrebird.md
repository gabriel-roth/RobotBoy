# Lyrebird

*The lyrebird is a virtuoso mimic, reproducing the sounds around it in its own song.*

**Lyrebird** is a gesture and CV recorder for [VCV Rack](https://vcvrack.com) and the [4ms MetaModule](https://4mscompany.com/metamodule), for modulation that is easier to perform than to program. Turn its knob or input CV while recording and it plays the movement back as a loop locked to the bar or as a triggered one-shot. Takes can be edited, transformed, and saved as files.

Lyrebird is one of Robot Boy’s three complex modulators, with [Skylark](Skylark.md) and [Weaver](Weaver.md).

<img src="screenshots/Lyrebird.png" alt="Lyrebird module" height="500">

---

## Controls

Lyrebird’s controls are divided into four groups.

### IN

- The big knob is the recording source, ±5 V by default (see [**Settings**](#settings)). If a cable is plugged into the jack, incoming voltage replaces the knob as the input source.

### REC

- **RECORD:** starts recording a new take, replacing the current one; press again to stop. (If Sync is enabled, the recording will stop by itself after a period determined by LENGTH.)
- **PUNCH:** starts overwriting the take at the playhead; press again to stop.
- **CLOCK:** sets the tempo in BPM; default is 120. A clock signal at the input overrides the knob.
- **LENGTH:** the length of a new take, in bars. Default is 1. Only has an effect when Sync is enabled.
- **SYNC / FREE:** determines how RECORD works.
    - **Sync** (default)**:** RECORD waits for the next interval (set by LENGTH), or a signal at TRIG, with its light blinking, then records for a number of bars set by LENGTH. When played back, the take is synced to the grid position at which it was recorded.
    - **Free:** recording runs from press to press, up to 3 minutes, and plays back at its recorded duration.

### PLAY

- **LOOP / ONCE:** **Loop** (default) plays the take repeatedly; **Once** plays once.
- **SPEED:** sets the playback rate, from 1/4 to x4, default x1. In Sync it snaps to powers of two to stay on the bar. SPEED CV is 1 V/octave.
- **SMOOTH:** smooths playback, up to a 1 s window, without lag. The recording itself is unchanged.
- **TRIG:** the button or a trigger restarts the take and the bar count. In Once, it plays a pass. With a clock patched, it waits for the next clock pulse.
- **REVERSE:** the button or a gate plays the take backward while held (see [**Settings**](#settings)).
- **FILE:**
    - **SAVE** (the center FILE button)**:** saves the take to the current folder.
    - **‹ ›:** load the previous or next WAV file in the current folder.

### OUT

- **OUT:** the take. While recording, or with no take, OUT follows the source at IN.
- **EOC:** a trigger at each loop wrap and at the end of each Once pass.

---

## Files

The current folder is that of the last file opened or saved. In VCV Rack it defaults to `RobotBoy/Lyrebird` in the Rack user folder. On MetaModule, the default folder is the first of `RobotBoy/Lyrebird`, `Lyrebird`, or `RobotBoy` found on the SD card, then on a USB drive, then the root directory of the SD card, then the root of the USB drive. (MetaModule can’t create folders; you might want to make one on a computer.)

Takes are saved as mono 32-bit float WAV files at 1 kHz, with ±10 V as full scale. Files not created by Lyrebird will load as Free takes from their first channel, up to 3 minutes.

If you save a patch, the active take is also stored in the patch.


---
<a id="settings"> </a>
## Settings

- **Knob range:** ±5 V (default) or 0–10 V.
- **Record buttons:** **Latch** (default) or **Press** (records only while held).
- **Reverse button:** **Press** (default) or **Latch** (each press toggles direction).
- **Start recording (Free):** **On press** (default) or **On first movement** (arms, then starts when the source moves). When Sync is enabled, recording always waits for the grid (see **Sync**).
- **Idle output:** what OUT holds between Once passes: **Last value**, **First value**, or **0 V** (default).
- **Clock pulses per cycle:** pulses per bar at the CLOCK input: 1, 2, 4, 8, 16, 24, 32, 48, or 96 (default 4).
- **Open…**, **Save as…:** file dialogs.
- **Undo last take:** swaps back to the take before the last record, punch, or file load.

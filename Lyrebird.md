# Lyrebird

**Lyrebird** is a gesture and CV recorder for [VCV Rack](https://vcvrack.com) and the [4ms MetaModule](https://4mscompany.com/metamodule). Perform a movement once — on its own big knob, or with any CV you patch in — and Lyrebird plays it back in time with your patch, as a loop or as a triggered one-shot. You can punch in over part of a take, change its speed, reverse it and smooth it, and save takes as WAV files to step through later. It's one of Robot Boy's three complex modulators, with [Skylark](Skylark.md) and [Weaver](Weaver.md), and keeps time the same way they do.

<img src="screenshots/Lyrebird.png" alt="Lyrebird module" height="500">

---

## How it works

Lyrebird holds one *take*: a single lane of recorded voltage, up to 3 minutes long. With nothing recorded, **OUT** simply passes on whatever you're recording from, so Lyrebird works as a plain knob until you press **RECORD**.

A take is recorded and played back in one of two ways, chosen from **Time** in the context menu:

- **Sync** (the default) — the take is a whole number of bars long (set by **LENGTH**) and is locked to Lyrebird's clock. Change the tempo and the take stretches to fit; the loop always lands on the bar.
- **Free** — the take is exactly as long as you recorded it, and plays at the speed you performed it, whatever the clock does.

**Play** in the context menu chooses whether the take repeats (**Loop**, the default) or plays once each time it's triggered (**Once**), like an envelope.

Lyrebird keeps its own bar count, from its **CLOCK** knob or a clock patched beside it. One cycle is one bar of four beats, as on Skylark and Weaver, so all three stay in step from the same clock.

---

## Quick start

1. **Patch OUT** to whatever you want to move — a filter's cutoff, say.
2. **Press RECORD** and turn the big knob on the **INPUT** plate. With the default settings, Lyrebird records exactly one bar (2 seconds at 120 BPM) and stops by itself.
3. **Listen:** the gesture now repeats every bar. Turn **SMOOTH** to round it off, or **SPEED** to play it at double or half time.
4. **Patch a clock** into the **CLOCK** jack to follow the rest of your patch. A clock of quarter notes works by default.

To record a CV instead of the knob, patch it into the jack beside the big knob. To save the take, press the button between the chevrons under **FILE**.

---

## The display

- **Header, left:** the take's name — its file name in capitals, `NEW TAKE` if it has never been saved, or `EMPTY`. A `*` means the take has changed since it was saved or opened.
- **Header, right:** `LOOP` or `ONCE`, the speed it plays at (`x1`, `x1/2`, `x1.41`), and the **SMOOTH** width (`SMOOTH OFF`, `SMOOTH 40ms`).
- **Trace:** the whole take, left to right. The raw recording is dim grey; the output, after **SMOOTH**, is white. A take playing in Sync has faint lines at each bar. The vertical scale is the big knob's range, widened if the take goes beyond it.
- **Playhead:** a line and a dot show where the take is playing.
- **While writing:** a new recording draws in magenta as it's written; during a punch-in, the section being replaced turns magenta on a faint magenta band.
- **Footer, left:** `REC`, `PUNCH`, a blinking `ARMED`, or a file message for 3 seconds (see **Files** below).
- **Footer, middle:** the source the next recording or punch will capture — `IN: KNOB`, or `IN: CV` while the CV jack is patched.
- **Footer, right:** in Sync, the take's length as it plays and the current tempo (`4 BARS  120.00 BPM`). With no take, or for 2 seconds after you turn **LENGTH**, it shows the **LENGTH** setting instead. In Free, the take's length in seconds (`12.3 S`), or `FREE` with no take.

---

## Controls

### CLOCK and LENGTH

*CLOCK (knob):* The tempo when no clock is patched, 20 to 300 BPM (default 120). Right-click it in VCV to type a tempo, to 0.01 BPM.

*CLOCK (jack):* An external clock. By default Lyrebird expects four pulses per bar (quarter notes); change this with **Clock pulses per cycle** in the context menu. The tempo is read from the second pulse onward, averaged over a bar's worth of pulses so a swung clock doesn't wobble it, and Lyrebird's bar count is pulled gently into line with the pulses. If the clock pauses for more than about two pulses, the next pulse snaps the count back to the start of a bar. Unplug the clock to return to the knob's tempo.

*LENGTH:* The length of a new Sync take: 1/4, 1/2, 1, 2, 4, 8 or 16 bars (default 1 bar). It's read when a take starts, so turning it afterwards doesn't change the take you have. It has no effect on Free takes.

### RECORD and PUNCH

*RECORD:* Records a new take, replacing the whole of the old one (which you can get back with **Undo last take**). By default, press once to start and again to stop; a Sync take also stops by itself once it reaches **LENGTH**. The jack beside it does the same as the button. The magenta light is solid while recording and blinks while armed.

*PUNCH:* Replaces part of the current take while it keeps playing. Press to start writing over the take at the playhead, press again to stop. The take's length, timing and mode stay the same. The jack beside it does the same as the button.

The context menu changes how both buttons behave: **Record buttons** can make them record only while held, and **Start recording** can make them wait for the source to move. See **Recording** below.

### INPUT: knob and CV jack

The magenta **INPUT** plate holds the two sources you can record from.

*Knob:* The big knob you perform on. It covers −5 V to +5 V by default (centered at 0 V), or 0 to 10 V with **Knob range** in the context menu.

*CV jack (beside the knob):* Patch a cable here and Lyrebird records the CV instead of the knob, in volts, between −10 V and +10 V. Unplug it and the knob is the source again. The display's footer shows which one is in use.

Lyrebird records at 1,000 samples per second, which is plenty for hand movements and modulation; very sharp CV edges come out up to 1 ms soft.

### SPEED, SMOOTH, TRIG and REVERSE

*SPEED:* Playback rate, from ×1/4 to ×4 (default ×1, at the center). In Sync it snaps to ×1/4, ×1/2, ×1, ×2 or ×4, so the take stays on the bar: at ×2 it plays twice per **LENGTH**, at ×1/2 it takes twice as long. In Free it's continuous. The **SPEED** jack adds CV at 1 V/octave (+1 V doubles the speed), and with CV the range reaches ×1/16 to ×16. SPEED only changes playback; recording always happens in real time.

*SMOOTH:* Rounds off the take as it plays, from off (default) up to a 1-second window. Most of the knob's travel is given to short settings, where small changes matter. The smoothing looks ahead as well as behind, so it rounds the gesture without making it lag. It's measured in the take's own time, so at ×2 a 100 ms window covers 50 ms of real time. In Loop it wraps around the loop point; in Once it stops at the ends. It never changes the recording itself, and saved files hold the raw take. The **SMOOTH** jack adds CV: +10 V sweeps the whole range of the knob.

*TRIG:* Restarts the bar count and the take, from its start (or its end, when reversed). In Once, it fires a pass. With a clock patched, the next clock pulse after a TRIG counts as the start of the bar. The button and the jack below it do the same thing.

*REVERSE:* Plays the take backwards while the button is held or the jack below it is high (above 1 V); the white light shows when playback is reversed. In Sync, a reversed take stays locked to the bar. **Reverse button** in the context menu can make each press or rising gate flip it instead.

### EOC and OUT

*OUT:* The take as it plays, clamped to ±10 V. While recording or punching, OUT follows the live source (through **SMOOTH**, which here acts as a simple lag, since the future isn't known yet), so you hear what you're recording.

*EOC:* A 1 ms, 10 V trigger each time a loop wraps and at the end of each Once pass. TRIG doesn't fire it.

---

## Playback modes

**Sync + Loop:** The take loops in step with the bar count. Its position is worked out from the bar count, so it never drifts from the clock.

**Sync + Once:** Each TRIG plays the take once at the current tempo; a TRIG during a pass restarts it.

**Free + Loop:** The take loops at the speed it was recorded, times SPEED, ignoring the clock. TRIG restarts it.

**Free + Once:** As Sync + Once, at the take's own speed.

Between passes in Once — and after a take is loaded, before the first TRIG — OUT holds the take's last value. **Idle output** in the context menu can make it hold the first value, or 0 V, instead. Reversed, "last" and "first" follow the direction of play.

Switching **Time** changes how the current take plays as well as how the next one is recorded:

- A Sync take played in Free runs at the tempo it was recorded at, times SPEED.
- A Free take played in Sync is stretched to the nearest bar length in the **LENGTH** list (at the tempo it was recorded at), so it locks to the clock.

---

## Recording

### Sync takes

A Sync take is laid out by bar, not by when you press **RECORD**. Press it two bars into a four-bar loop, and recording starts two bars into the take and wraps around to fill the beginning, so the take stays on the grid. The bar count starts at the last **TRIG** (or when the patch started); send the same gate to **TRIG** and **RECORD** if you want a take to begin where you press.

Recording stops by itself after **LENGTH** bars of clock, even if the tempo changes along the way. If you stop it early, the rest of the take holds the last value you recorded.

In Once, the take starts where you press **RECORD**, wherever the bar is, so the gesture plays from its beginning on the next TRIG. The bar count carries on undisturbed.

Sync takes longer than 3 minutes (16 bars below about 21 BPM) are recorded at a lower resolution rather than refused.

### Free takes

A Free take starts when you press **RECORD** and stops when you press it again. It's exactly what you did, and loops from its start. At 3 minutes recording stops and the display shows `LIMIT 3 MIN`.

### Punch-in

**PUNCH** writes the source over the take wherever the playhead is, at any SPEED and in either direction, in Sync or Free. Playback carries on around it, and in Loop a punch can run on past the loop point. Each end of a punch crossfades over 10 ms, both in what you hear and in what's written, so there's no click.

- In Once, you can only punch during a pass, and the punch ends with the pass.
- With no take yet, **PUNCH** records a new take, just as **RECORD** does.
- Pressing **RECORD** during a punch ends the punch and starts a fresh take.

### Hold and first-movement recording

**Record buttons → Press** makes **RECORD** and **PUNCH** (buttons and jacks) write only while held high. A Sync take still stops at **LENGTH**, even if you're holding on.

**Start recording → On first movement** makes a press *arm* the take (its light and the display's `ARMED` blink); writing starts once the source moves by more than 0.05 V, so a take never begins with dead air. Press again (or release, in Press) to cancel. It applies to **PUNCH** too.

### Undo

**Undo last take** in the context menu brings back the take you had before the last **RECORD**, **PUNCH** or file load (including stepping with the **FILE** buttons). Choose it again to swap back. It's one level deep: only the previous take is kept. In VCV, each of those actions is also a step in Rack's undo history, so Ctrl+Z / Ctrl+Shift+Z work too.

Undoing a context-menu setting never changes the take, and **Initialize** resets the controls and the context-menu settings (Time back to Sync, Play to Loop) but keeps the take.

---

## Files

### SAVE, ‹ and ›

*SAVE (the button between the chevrons):* Saves the take straight into the current folder, under the next free name — `LYREBIRD-001.wav`, `LYREBIRD-002.wav` and so on — without asking. The display shows `SAVED LYREBIRD-001` and the `*` disappears from the header.

*‹ and ›:* Step to the previous or next WAV file in the current folder, in alphabetical order, wrapping around at the ends, and load it at once as the take. If the current take isn't one of those files, › loads the first and ‹ the last. The jacks below them do the same on a trigger, so a clock divider or sequencer can change takes for you. An accidental step can be undone with **Undo last take**. In VCV, if the current folder doesn't exist yet, the chevrons open the Open… browser instead.

**Open…** and **Save as…** in the context menu open a file browser so you can pick any file, folder or name. A name typed without `.wav` gets it added.

The *current folder* is the folder of the last file you opened or saved with **Open…**, **Save as…** or the chevrons; it's saved with the patch. Until you choose one, it's the default folder: in VCV, `RobotBoy/Lyrebird` inside your Rack user folder (**Help → Open user folder**), created the first time you save. On MetaModule, see below.

### Messages

The footer shows each of these for 3 seconds:

- `SAVED …` / `LOADED …` — the file's name.
- `NOTHING TO SAVE` — there's no take yet.
- `SAVE FAILED` — the file couldn't be written.
- `CANT OPEN`, `NOT A WAV`, `UNSUPPORTED`, `EMPTY FILE` — the file couldn't be loaded; the take is unchanged.
- `TRUNCATED TO 3 MIN` — the file was longer than 3 minutes, and only its first 3 minutes were loaded.
- `NO FILES` — there are no WAV files in the current folder.
- `BUSY` — the last file action hasn't finished yet; try again.

Loading a file while a recording or punch is in progress discards what was being written.

### The WAV format

Lyrebird saves an ordinary mono WAV file (32-bit float, 1,000 samples per second, or fewer for a Sync take over 3 minutes) that any audio editor can open. Full scale is ±10 V, so a take reaching +10 V shows as a full-height peak and nothing is clipped: 1 V is a tenth of full scale.

The file also carries a small extra chunk of information that audio software ignores: whether the take is Sync or Free, its length in bars or seconds, and the tempo it was recorded at. That's what lets Lyrebird load its own files back exactly. If you edit a file in other software and it drops that chunk, Lyrebird loads it as a Free take.

You can open other WAV files too — 8-, 16-, 24- or 32-bit integer, or 32- or 64-bit float, at any sample rate. Lyrebird uses the first channel only, converts it to its own 1,000 samples per second, maps full scale to ±10 V, and loads it as a Free take (counted at 120 BPM if you play it in Sync). Only the first 3 minutes are kept. This is a handy way to bring in automation or an envelope drawn elsewhere.

### Takes in the patch

The take is also stored inside the patch, so a patch never depends on its WAV file: move or delete the file and the take still loads. The copy in the patch is at 16-bit resolution (about 0.3 mV steps). A full 3-minute take adds about 480 KB to the patch file; a few bars add only a few KB.

---

## Context menu

Open the context menu on the panel in VCV Rack, or open Options on MetaModule:

- **Time** — **Sync** (the default) or **Free**: how the take plays, and how the next one is recorded. See **Playback modes** above.
- **Play** — **Loop** (the default) or **Once**.
- **Knob range** — **+-5 V** (the default) or **0-10 V**, for the big INPUT knob.
- **Record buttons** — **Latch** (the default: press to start, press again to stop) or **Press** (writes while held), for RECORD and PUNCH and their jacks. In Latch the jacks respond to rising edges; in Press, to the gate level.
- **Reverse button** — **Press** (the default: reversed while held or while the gate is high) or **Latch** (each press or rising gate flips direction; the direction is saved with the patch). Changing it starts playback forward.
- **Start recording** — **On press** (the default) or **On first movement**.
- **Idle output** — what OUT holds in Once when no pass is playing: **Last value** (the default), **First value** or **0 V**.
- **Clock pulses per cycle** — how many clock pulses make a bar: 1, 2, 4 (the default), 8, 16, 24, 32, 48 or 96.
- **Open…** — load a WAV file as the take.
- **Save as…** — save the take, choosing the folder and name.
- **Undo last take** — swap back to the previous take; again to swap forward.

---

## On MetaModule

Everything on the panel works on MetaModule, including recording with a mapped knob, saving, opening and stepping through files. The differences:

- **Display:** the header leaves out **SMOOTH** (the traces show it), file messages appear in place of the take's name, and `REC`, `PUNCH` or `ARMED` replaces the source in the footer.
- **Folders:** MetaModule can't create folders, so make one on your SD card or USB drive from a computer. Lyrebird's default folder is the first of these that exists: `RobotBoy/Lyrebird`, `Lyrebird` or `RobotBoy` on the SD card, then the same three on a USB drive, then the root of the SD card, then the root of the USB drive. It looks again each time, so a card inserted later is found. If a saved folder has gone (a different card, say), Lyrebird falls back to the default folder.
- **No card or drive:** **SAVE** opens the Save as browser instead. The chevrons and the FILE jacks show `NO FOLDER` and don't open a browser, since a trigger shouldn't pop one up while the module may be off screen.
- **Internal storage:** you can choose it in Save as…, but it's small and meant for patches; use a card or drive for takes.
- **Undo:** **Undo last take** works as in VCV, but there's no Ctrl+Z history.
- **Off screen:** saving, loading and file stepping carry on while Lyrebird isn't on screen.
- **Patches move both ways:** a patch made in VCV loads on MetaModule with its take and settings, and vice versa. A folder saved on the other platform is ignored in favor of the default.

---

## Patch ideas

- **Hand-played filter sweep:** Sync + Loop, **LENGTH** at 4 bars, **OUT** into a filter's cutoff. Record a sweep over four bars, then raise **SMOOTH** to iron out the wobbles.
- **Gestural envelope:** set **Play** to **Once** and patch a sequencer's gate into **TRIG**. Record a swell or a pluck shape on the knob; every note now gets that exact gesture, with **EOC** marking its end.
- **Freeze a random line:** patch a random or chaotic CV into the **INPUT** jack and record a bar of it. Now the randomness repeats, in time, until you record again.
- **Living automation:** gate **PUNCH** from a slow clock divider while a random source sits in the **INPUT** jack, so a slice of the loop is rewritten every few bars and it slowly evolves.
- **Half-time / double-time:** sequence **SPEED** CV in 1 V steps (in Sync, each volt doubles or halves the speed and the loop stays on the bar), and gate **REVERSE** for palindromes.
- **A library of gestures:** save a handful of takes, then patch a clock divider into the › jack to load a new one every 16 bars.
- **Bring in a curve from elsewhere:** draw an automation curve or bounce an audio envelope in a DAW, export it as a WAV, and load it with **Open…**. Switch **Time** to **Sync** to lock it to the bar.
- **Chained one-shots:** two Lyrebirds in Once, the first one's **EOC** into the second one's **TRIG**, for a two-part gesture that runs end to end.

# Loooop & Löp changes — user GUI checklist

Date: 2026-07-18 · branch `worktree-loooop-lop`

Automated coverage (all green): engine unit tests (`tests/loooop/test_loop_engine.cpp`
— continue-overdub on close, Lock override, armed one-shot live window),
`test_head_colors.py`, full `tests/run.sh`, VCV build+install, MetaModule
`.mmplugin` build ("All symbols found!"). The items below need eyes/hands in the
running host — they can't be checked by an agent.

**Restart VCV Rack** to load the freshly installed build before testing.

## Löp — Clear/Record swap (item 3)
- [ ] Bottom control row reads DRY/WET, **RECORD**, **CLEAR** left→right (Record is
      now left of Clear).
- [ ] The **RECORD** button *and* its trigger jack are the middle column; **CLEAR**
      button + trig are the right column. Labels sit above their controls.
- [ ] Record actually records/stops; Clear actually clears (jacks wired to the right
      controls, not swapped in function).

## Panels — "DUB MODE" label (item 5)
- [ ] Löp: the label under the overdub button reads **DUB MODE** (was "OVERDUB").
- [ ] Loooop: the label under the overdub button (bottom row) reads **DUB MODE**.

## Loooop — Purple head (item 4)
- [ ] Right-click Loooop → the head-4 submenus (Exclude from Grid / One-shot on
      trigger / Speed CV = V/Oct) list **"Purple playhead"** (was "Magenta playhead").
- [ ] Head-4 tint/lane color is unchanged (still the same pink-purple).

## Trigger when recording (item 1) — both modules
- [ ] Right-click → **"Trigger when recording"** submenu present with **Stops
      recording** (default, checked) and **Starts overdubbing**.
- [ ] With **Stops recording**: press Record to start, press again → recording stops,
      loop plays (unchanged behavior).
- [ ] With **Starts overdubbing** + Overdub mode = Layer/Add/Replace/Decay: press
      Record to start, press again → the loop closes AND the Record LED **stays lit**;
      new input keeps layering onto the loop in the selected write mode. Press Record
      once more → overdub stops.
- [ ] With **Starts overdubbing** + Overdub = **Lock**: the second Record press
      **stops** recording (setting is overridden by Lock).
- [ ] Same behavior driving the **Record trigger jack** instead of the button.

## One-shot Size/Position on display (item 2) — both modules
- [ ] Record a loop. Set a head (Loooop: any head via "One-shot on trigger"; Löp: the
      menu one-shot) to one-shot — the lane goes "asleep" (dim, silent, waiting).
- [ ] Turn **Size** on that armed head → the window bar on the display grows/shrinks
      live while the lane stays asleep (no audio until triggered).
- [ ] Turn **Position** → the window bar slides live; the parked head marker sits at
      the window start.
- [ ] Trigger the head (trig jack) → it plays one pass from that window, as before.

## MetaModule (if testing on MM / simulator)
- [ ] Löp faceplate shows the swapped Record/Clear and DUB MODE; Loooop shows DUB MODE.
- [ ] "Trigger when recording" appears in the module's Options roller with both
      choices, and behaves as above.

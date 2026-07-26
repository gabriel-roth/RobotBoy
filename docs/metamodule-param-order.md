# MetaModule module menu (per module)

On MetaModule a module exposes controls through **two** surfaces:

1. **The element roller** — the scrollable param/mapping list. Entries are the
   module's params (knobs, switches, and — on native modules — menu-only
   **AltParams**), ordered by **param index** (`element_sort.hh` in the plugin
   SDK sorts by `param_idx`, i.e. the module's `ParamId` order). Native modules
   group their AltParams under an **"Options:"** header here; these are real,
   mappable params.
2. **The module context menu** — opened from the module (right-click / menu
   button). The **current firmware renders the module's VCV `appendContextMenu`**
   here (`firmware/src/gui/module_menu/vcv_plugin_menu.hh` calls
   `mw->appendContextMenu(...)` and draws MenuItems, submenus, sliders, and
   separators). This is where the **VCV-adapter** modules' menu-only settings
   appear. They are ordinary menu items — **not** roller params, so they can't be
   mapped to knobs/CV, but they are present and adjustable, and persist via the
   module's own patch state.

So: native modules (Loooop, Löp) put menu-only settings in the roller as AltParam
"Options"; adapter modules (MF-20, Onbetap, Particules, Ondes, Vespid, Retours)
put them in the context menu. Both are reachable on the device. Verified in the
simulator (Particules' context menu shows its full option list).

> Sources: `metamodule/loooop/*_info.hh` (native element lists), each module's
> `ParamId` enum + `config*` labels, the SDK's `element_sort.hh` (param-index
> sort), and firmware `vcv_plugin_menu.hh` (context-menu rendering). Reflects the
> finalized prerelease param order/naming as of 2026-07-24 (branch
> `metamodule-param-order`).

---

## Loooop  *(native MM module)*

Roller params:

1. Record
2. Overdub mode
3. Clear
4. Grid
5. Dry/Wet
6. Red Size
7. Red Position
8. Red Speed
9. Red Jitter
10. Red Pan
11. Red Level
12. Yellow Size
13. Yellow Position
14. Yellow Speed
15. Yellow Jitter
16. Yellow Pan
17. Yellow Level
18. Blue Size
19. Blue Position
20. Blue Speed
21. Blue Jitter
22. Blue Pan
23. Blue Level
24. Purple Size
25. Purple Position
26. Purple Speed
27. Purple Jitter
28. Purple Pan
29. Purple Level

Options (roller AltParams under an "Options:" header):

30. Record jack
31. When recording ends
32. Crossfade
33. Red Trig mode
34. Yellow Trig mode
35. Blue Trig mode
36. Purple Trig mode
37. Red Speed V/Oct
38. Yellow Speed V/Oct
39. Blue Speed V/Oct
40. Purple Speed V/Oct
41. Red Grid exclude
42. Yellow Grid exclude
43. Blue Grid exclude
44. Purple Grid exclude

VCV context menu (same commands, playheads by colour inside each submenu):
Record jack, When recording ends, Crossfade, — divider —, One-shot on
trigger, Speed CV = V/Oct, Exclude from Grid.

## Löp  *(native MM module)*

Roller params:

1. Size
2. Position
3. Speed
4. Jitter
5. Dry/Wet
6. Record
7. Clear
8. Overdub mode
9. Grid

Options (roller AltParams under an "Options:" header):

10. Record jack
11. When recording ends
12. Crossfade
13. Trigger
14. Speed CV V/Oct

VCV context menu: Record jack, When recording ends, Crossfade, — divider —,
One-shot on trigger, Speed CV = V/Oct.

## MF-20  *(VCV-adapter module)*

Roller params:

1. LP Cutoff
2. LP Resonance
3. HP Cutoff
4. HP Resonance
5. Drive
6. LP Cutoff CV
7. HP Cutoff CV
8. Total Cutoff CV (both filters)

Context menu: Filter revision (OTA / Korg35).

## Onbetap  *(VCV-adapter module)*

Roller params:

1. Cutoff
2. Resonance
3. Drive
4. Mode
5. Cutoff CV
6. Resonance CV
7. Drive CV

Context menu: Character (Vintage / Tamed), Resonance limiting (Hard / Soft),
Oversampling (1x / 2x / 4x, default 1x on MetaModule and 2x on desktop).

## Particules  *(VCV-adapter module)*

Roller params:

1. Freeze
2. Quality
3. Time
4. Density
5. Pitch
6. Size
7. Shape
8. Feedback
9. Reverb
10. Dry/Wet
11. Time AR
12. Pitch AR
13. Size AR
14. Shape AR
15. Feedback CV amount
16. Reverb CV amount
17. Dry/wet CV amount

Context menu: Auto gain / Disable auto gain (+ Input manual-gain slider), SEED CV
mode, Lock pitch, Root, Grain trigger on R Out, Clear buffer.

## Ondes  *(VCV-adapter module)*

Roller params:

1. Pitch
2. Position
3. Position CV amount
4. Bank
5. Bank CV amount

Context menu: none (standard menu only).

## Vespid  *(VCV-adapter module)*

Roller params:

1. Frequency
2. Resonance
3. Drive
4. Blend: LP/notch/HP
5. Frequency CV
6. Resonance CV
7. Drive CV

Context menu: Character (British / German), Self-oscillation pitch — German
(Hardware / Corrected), Oversampling (1x / 2x / 4x, default 1x). Panel is
VCV-only — MetaModule is locked to the charcoal panel — and Oversampling has
no Auto entry here, unlike the desktop build.

## Retours  *(VCV-adapter module)*

Roller params:

1. Slice
2. Quality
3. Tap tempo
4. Time
5. Interval
6. Pitch
7. Shape
8. Feedback
9. Dry/wet
10. Time AR
11. Pitch AR
12. Shape AR
13. Feedback CV amount
14. Dry/wet CV amount

Context menu: Quality, Time change response, Clear saved tempo, Clear buffer.

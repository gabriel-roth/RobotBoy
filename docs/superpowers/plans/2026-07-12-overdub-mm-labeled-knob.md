# Overdub → Labeled Snapped Knob + LED (MetaModule) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** On MetaModule, make Loooop's and Löp's Overdub control show its state label ("Layer/Decay/Add/Replace/Lock") as you adjust it — like the Grid knob — while keeping the RGB colour feedback via a small LED overlaid on the knob. (Option "1b" from the research.)

**Architecture:** Both modules are **native MetaModule cores** (`LoooopCore.cc` / `LopCore.cc` + hand-maintained `*_info.hh`), so the on-screen control type is fully under our control. Today Overdub is a `MomentaryButtonRGB` and the core cycles an internal `od_` counter on each press — the firmware only knows `PRESSED`/`RELEASED`, so its "Adjust" popup shows no mode label. We switch the element to a `KnobSnapped` with five position names (the firmware shows the current name, exactly as it does for `GridKnob`), add a separate `RgbLight` element at the same spot for colour, and change the core to read the mode straight from the snapped param value instead of edge-cycling.

**Tech Stack:** C++20, MetaModule plugin SDK (`SmartCoreProcessor`), arm-none-eabi cross-build via CMake.

## Global Constraints

- **VCV Rack side is NOT changed.** The desktop Overdub stays a 5-state RGB light-bezel (`OverdubButton` in `src/loooop/*.cpp`), which already shows the mode in its tooltip. This work is MetaModule-only, confined to `metamodule/loooop/`.
- **Element array and `Elem` enum stay in the exact same order** in each `*_info.hh` — `SmartCoreProcessor` maps enum→element by index. Adding one element means adding it to BOTH the array and the enum at the same position.
- **Mode index → write-mode / colour tables are shared and unchanged:** `loooop::applyOverdub`, `loooop::kOverdubColors`, `loooop::overdubWriteMode` in `src/loooop/LooperModuleDSP.hpp`. Index order is Layer=0, Decay=1, Add=2, Replace=3, Lock=4.
- **Overdub knob keeps its current footprint** (Loooop 7×7 mm, Löp 5×5 mm) and position, so it matches the panel SVG and `mm_sync` stays a no-op (no `sync_info_positions.py` run required).
- **Colour LED** is driven from `kOverdubColors[od]` exactly as today; only the `setLED<...>` target element changes.
- Build/verify is headless (CMake build). All *visual* checks (label readout, LED colour, knob-over-faceplate look) are deferred to a user-run simulator checklist — never agent-driven GUI-sim tests.

---

## File Structure

- `metamodule/loooop/QlpElements.hh` — element-type definitions. Replace `QlpOverdubButton` (a `MomentaryButtonRGB`) with `QlpOverdubKnob` (a `KnobSnapped`, 5 labelled positions) and add `QlpOverdubLight` (an `RgbLight`).
- `metamodule/loooop/Loooop_info.hh` — Loooop element array + `Elem` enum: swap the Overdub element type, add the LED element, rename the enum entry, insert the LED enum entry.
- `metamodule/loooop/Lop_info.hh` — same for Löp.
- `metamodule/loooop/LoooopCore.cc` — read Overdub mode from the snapped param; retarget LED to the new light element; drop the momentary-cycle + `save_state`/`load_state` machinery.
- `metamodule/loooop/LopCore.cc` — same for Löp.
- `metamodule/loooop/sync-map-loooop.yaml` / `sync-map-lop.yaml` — rename `OverdubButton` key → `OverdubKnob`; add `OverdubLight: null` (hand-placed, no SVG source).

## Why no automated test

The change's observable effect — a mode **label** in the firmware's Adjust popup and an **LED colour** on the panel — is GUI-only and lives in the closed-source firmware renderer, not in our DSP. The DSP path (`applyOverdub`/`kOverdubColors`) is unchanged and already covered by `tests/loooop/`. So the headless gate is "the plugin compiles and links", and correctness of the visuals is a user simulator check. This mirrors the existing code, which also has no unit test for the momentary-cycle behaviour.

---

### Task 1: New element types in `QlpElements.hh`

**Files:**
- Modify: `metamodule/loooop/QlpElements.hh:32-35`

**Interfaces:**
- Produces: `MetaModule::QlpOverdubKnob` (a `KnobSnapped`, 5 positions, names `{"Layer","Decay","Add","Replace","Lock"}`, min 0 / max 4, knob image); `MetaModule::QlpOverdubLight` (an `RgbLight` using `4ms/comp/led_x.png`).
- Consumes: nothing new — `KnobSnapped` / `RgbLight` are SDK types already in scope via `element_info.hh`.

- [ ] **Step 1: Replace the `QlpOverdubButton` struct**

Replace lines 32-35 (the `QlpOverdubButton : MomentaryButtonRGB` definition and its comment above it, lines 32-35) with a snapped-knob type mirroring `QlpGridKnob` (a proven `KnobSnapped`) plus a dedicated RGB light. New text:

```cpp
// Overdub is a five-position snapped knob (Layer/Decay/Add/Replace/Lock) so the
// MetaModule firmware shows the mode name as you adjust it, exactly like Grid.
// The mode colour is drawn by a separate RGB LED (QlpOverdubLight) overlaid at
// the knob's centre — KnobSnapped itself carries no light.
struct QlpOverdubKnob : KnobSnapped {
    constexpr QlpOverdubKnob(BaseElement b)
        : KnobSnapped{{{{b, "4ms/comp/knob9mm_x.png"}, 0.f, 0.f, 4.f}}, 5,
                      {"Layer", "Decay", "Add", "Replace", "Lock"}} {}
};
struct QlpOverdubLight : RgbLight {
    constexpr QlpOverdubLight(BaseElement b)
        : RgbLight{{b, "4ms/comp/led_x.png"}} {}
};
```

- [ ] **Step 2: Verify it compiles as part of the full build in Task 6.** (No standalone compile — the header is only meaningful when included by the info files.)

---

### Task 2: Loooop info — swap element + add LED

**Files:**
- Modify: `metamodule/loooop/Loooop_info.hh:43` (array), `:151-153` (enum), `:40` (array size), `:31-32` (comment)

**Interfaces:**
- Consumes: `QlpOverdubKnob`, `QlpOverdubLight` from Task 1.
- Produces: enum members `LoooopInfo::Elem::OverdubKnob` and `LoooopInfo::Elem::OverdubLight` (used by `LoooopCore.cc` in Task 4).

- [ ] **Step 1: Bump the array size 90 → 91**

`Loooop_info.hh:40`: change `std::array<Element, 90> Elements{{` to `std::array<Element, 91> Elements{{`.

- [ ] **Step 2: Swap the Overdub array element and add the LED element**

`Loooop_info.hh:43`, replace:

```cpp
        QlpOverdubButton{{69.543f, 116.050f, Center, "Overdub", "", 7.f, 7.f}},
```

with (same position/size for the knob; LED centred on it, 3 mm):

```cpp
        QlpOverdubKnob{{69.543f, 116.050f, Center, "Overdub", "", 7.f, 7.f}},
        QlpOverdubLight{{69.543f, 116.050f, Center, "Overdub LED", "", 3.f, 3.f}},
```

- [ ] **Step 3: Add the enum entries in the same order**

`Loooop_info.hh:153`, replace:

```cpp
        RecordButton, OverdubButton, ClearButton, GridKnob, DryWetKnob,
```

with:

```cpp
        RecordButton, OverdubKnob, OverdubLight, ClearButton, GridKnob, DryWetKnob,
```

- [ ] **Step 4: Update the descriptive comment**

`Loooop_info.hh:31-32`, change the phrase "Overdub is a 5-state RGB button" to "Overdub is a 5-position snapped knob with an RGB LED overlay".

---

### Task 3: Löp info — swap element + add LED

**Files:**
- Modify: `metamodule/loooop/Lop_info.hh:44` (array), `:71` (enum), `:34` (array size), `:26-27` (comment)

**Interfaces:**
- Consumes: `QlpOverdubKnob`, `QlpOverdubLight` from Task 1.
- Produces: enum members `LopInfo::Elem::OverdubKnob` and `LopInfo::Elem::OverdubLight` (used by `LopCore.cc` in Task 5).

- [ ] **Step 1: Bump the array size 26 → 27**

`Lop_info.hh:34`: change `std::array<Element, 26> Elements{{` to `std::array<Element, 27> Elements{{`.

- [ ] **Step 2: Swap the Overdub array element and add the LED element**

`Lop_info.hh:44`, replace:

```cpp
        QlpOverdubButton{{37.968f, 102.150f, Center, "Overdub", "", 5.f, 5.f}},
```

with:

```cpp
        QlpOverdubKnob{{37.968f, 102.150f, Center, "Overdub", "", 5.f, 5.f}},
        QlpOverdubLight{{37.968f, 102.150f, Center, "Overdub LED", "", 3.f, 3.f}},
```

- [ ] **Step 3: Add the enum entries in the same order**

`Lop_info.hh:71`, replace:

```cpp
        DryWetKnob, RecordButton, ClearButton, OverdubButton, GridKnob,
```

with:

```cpp
        DryWetKnob, RecordButton, ClearButton, OverdubKnob, OverdubLight, GridKnob,
```

- [ ] **Step 4: Update the descriptive comment**

`Lop_info.hh:26-27`, change "Overdub is a 5-state RGB button" to "Overdub is a 5-position snapped knob with an RGB LED overlay".

---

### Task 4: LoooopCore — read param, retarget LED, drop momentary machinery

**Files:**
- Modify: `metamodule/loooop/LoooopCore.cc:41-46` (read mode), `:98-100` (LED), `:110-116` (remove save/load), `:219-220` (remove members)

**Interfaces:**
- Consumes: `Elem::OverdubKnob`, `Elem::OverdubLight` from Task 2.

- [ ] **Step 1: Replace the momentary-cycle block**

`LoooopCore.cc:41-46`, replace:

```cpp
        // Overdub: momentary button cycles the 5-state control
        // (Layer/Decay/Add/Replace/Lock), matching VCV; index persists via save_state.
        bool odPressed = getState<OverdubButton>() == MomentaryButton::State_t::PRESSED;
        if (odPressed && !odPrev_) od_ = (od_ + 1) % 5;
        odPrev_ = odPressed;
        loooop::applyOverdub(engine_, od_);
```

with (read the five-position snapped knob directly; the firmware persists the param, so no hand-rolled state):

```cpp
        // Overdub: five-position snapped knob (Layer/Decay/Add/Replace/Lock).
        // getState returns 0..1 for a KnobSnapped; * (num_pos-1) -> index.
        int od = std::clamp((int)std::lround(getState<OverdubKnob>() * 4.f), 0, 4);
        loooop::applyOverdub(engine_, od);
```

- [ ] **Step 2: Retarget the LED to the new light element**

`LoooopCore.cc:98-100`, replace `setLED<OverdubButton>(...)` with `setLED<OverdubLight>(...)` and use the local `od`:

```cpp
        setLED<OverdubLight>(std::array<float, 3>{
            loooop::kOverdubColors[od][0], loooop::kOverdubColors[od][1],
            loooop::kOverdubColors[od][2]});
```

- [ ] **Step 3: Remove the now-unused persistence overrides**

Delete `LoooopCore.cc:110-116` (the `save_state()` / `load_state()` overrides that persisted `od_`). The base-class defaults are inherited; the Overdub position now round-trips as an ordinary param value.

- [ ] **Step 4: Remove the now-unused members**

Delete `LoooopCore.cc:219-220`:

```cpp
    int od_ = 0;          // 0 = Layer (matches VCV Overdub default)
    bool odPrev_ = false;
```

- [ ] **Step 5: Verify `<charconv>` include is now unused but harmless — leave it** (removing it is out of scope; it does no harm and keeps the diff focused).

---

### Task 5: LopCore — read param, retarget LED, drop momentary machinery

**Files:**
- Modify: `metamodule/loooop/LopCore.cc:38-43` (read mode), `:102-105` (LED), `:113-119` (remove save/load), `:181-182` (remove members)

**Interfaces:**
- Consumes: `Elem::OverdubKnob`, `Elem::OverdubLight` from Task 3.

- [ ] **Step 1: Replace the momentary-cycle block**

`LopCore.cc:38-43`, replace:

```cpp
        // Overdub: momentary button cycles the 5-state control
        // (Layer/Decay/Add/Replace/Lock), matching VCV; index persists via save_state.
        bool odPressed = getState<OverdubButton>() == MomentaryButton::State_t::PRESSED;
        if (odPressed && !odPrev_) od_ = (od_ + 1) % 5;
        odPrev_ = odPressed;
        loooop::applyOverdub(engine_, od_);
```

with:

```cpp
        // Overdub: five-position snapped knob (Layer/Decay/Add/Replace/Lock).
        // getState returns 0..1 for a KnobSnapped; * (num_pos-1) -> index.
        int od = std::clamp((int)std::lround(getState<OverdubKnob>() * 4.f), 0, 4);
        loooop::applyOverdub(engine_, od);
```

- [ ] **Step 2: Retarget the LED**

`LopCore.cc:102-105`, replace `setLED<OverdubButton>(...)` with:

```cpp
        setLED<OverdubLight>(std::array<float, 3>{
            loooop::kOverdubColors[od][0], loooop::kOverdubColors[od][1],
            loooop::kOverdubColors[od][2]});
```

- [ ] **Step 3: Remove the persistence overrides**

Delete `LopCore.cc:113-119` (`save_state()` / `load_state()`).

- [ ] **Step 4: Remove the members**

Delete `LopCore.cc:181-182`:

```cpp
    int od_ = 0;          // 0 = Layer (matches VCV Overdub default)
    bool odPrev_ = false;
```

---

### Task 6: Sync-maps + build + verify

**Files:**
- Modify: `metamodule/loooop/sync-map-loooop.yaml:7`, `metamodule/loooop/sync-map-lop.yaml` (the `OverdubButton` line)

- [ ] **Step 1: Update `sync-map-loooop.yaml`**

Change `OverdubButton: OVERDUB_PARAM` to `OverdubKnob: OVERDUB_PARAM` and add on the next line `OverdubLight: null`.

- [ ] **Step 2: Update `sync-map-lop.yaml`**

Same edit: `OverdubButton: OVERDUB_PARAM` → `OverdubKnob: OVERDUB_PARAM`, add `OverdubLight: null`.

- [ ] **Step 3: Build the MetaModule plugin**

Run: `cmake --build metamodule/build -j8`
Expected: `[100%] Built target plugin` and `Creating plugin at .../RobotBoy.mmplugin`, exit 0. A compile error here almost always means the array/enum fell out of lockstep — recount Task 2/3.

- [ ] **Step 4: Confirm the VCV build is untouched but still clean**

Run: `make -C vcv -j8`
Expected: builds clean (this change edits no `vcv/`-compiled sources, but confirms nothing shared broke).

- [ ] **Step 5: Commit**

```bash
git add metamodule/loooop/
git commit -m "MetaModule: Overdub becomes a labeled snapped knob with an RGB LED overlay"
```

---

## User-run simulator checklist (visual — do last, not agent-run)

Load Loooop and Löp in the MetaModule simulator / hardware and confirm:

1. **Label readout** — mapping Overdub to a knob (or using Adjust) steps through **Layer → Decay → Add → Replace → Lock** with the name shown, just like Grid.
2. **LED colour** — the overlaid LED shows the per-mode colour (matches the desktop bezel colours) and tracks as you change modes.
3. **Faceplate look** — the MM faceplate PNG is still generated from the VCV panel, which keeps the *button* graphic under the new knob art. Confirm the knob + LED drawn on top looks acceptable. **If it looks wrong**, the follow-up is an MM-specific faceplate (out of scope here) — report back.
4. **LED size/placement** — the 3 mm LED sits centred on the knob; confirm it reads as an indicator and doesn't swamp the (small, 5 mm) Löp knob. Easy to tune if needed.
5. **Patch round-trip** — save a patch with Overdub set to e.g. Replace, reload, confirm it restores. (Note: patches saved with the *old* momentary build won't carry their mode — they reopen at Layer. Acceptable.)

## Self-Review notes

- Spec coverage: label readout (Tasks 1-3 KnobSnapped names), colour (Task 1 RgbLight + Tasks 4-5 setLED), both modules (Loooop Tasks 2/4, Löp Tasks 3/5), contract integrity (Task 6 sync-maps), build gate (Task 6). Covered.
- Type consistency: enum name `OverdubKnob` and light `OverdubLight` used identically across info files (2/3), cores (4/5), and sync-maps (6). `od` (local int) replaces the removed `od_` member consistently.

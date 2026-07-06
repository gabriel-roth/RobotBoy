# Task 2 Report — MetaModule registration spike (GATE)

**Status: DONE_WITH_CONCERNS** — the gate is met at the sanctioned fallback level (single `init`, all four modules registered, links + all firmware symbols resolve). Live sim load was not performed because it is impossible for a `.mmplugin` and out of scope to drive headlessly (see "Simulator" below).

## Result

One `.mmplugin` (`metamodule/metamodule-plugins/Foobar.mmplugin`) builds and links with a **single exported `init`** that registers **all four** modules:
- **Loooop / Löp** via the native `SmartCoreProcessor` cores (`register_loooop_modules()` / `register_lop_modules()` → `register_module<Core,Info>(...)`).
- **MF-20 / Particules** via the VCV adapter (`pluginInstance->addModel(...)`, which calls `register_module` internally).

## Exact build command

```bash
cd ~/Dev/Foobar/metamodule
cmake -B build -G "Unix Makefiles" -DMETAMODULE_SDK_DIR=$HOME/Dev/metamodule-plugin-sdk
cmake --build build
```

## Final build output (tail)

```
   text     data     bss     dec     hex  filename
 210759    32980      96  243835   3b87b  .../build/Foobar.so
Checking if symbols in .../build/Foobar.so would be resolved
All symbols found!
------------------
Creating plugin at .../metamodule/metamodule-plugins/Foobar.mmplugin
[100%] Built target plugin
```

`All symbols found!` is the SDK's own `check_syms.py` confirming every symbol the plugin imports exists in the firmware API table (`api-symbols.txt`).

## Verification method — nm / readelf evidence (fallback gate)

From `build/Foobar-debug.so.nm` / `.readelf`:

- **Exactly one defined global `init`:** `0000a60c T init`. In `.dynsym`: `FUNC GLOBAL DEFAULT ... init` at the same address 0xa60c (satisfies `-Wl,--require-defined=init`). No other defined `init`; `init_Loooop`/`init_Lop` are absent (correctly compiled out by the `FOOBAR_COMBINED` guard).
- **Single init → all four register paths:**
  - `init_Foobar(rack::plugin::Plugin*)` present, called by `init`.
  - `MetaModule::register_loooop_modules()` and `MetaModule::register_lop_modules()` present.
  - `register_module<MetaModule::LoooopCore, LoooopInfo>` and `register_module<MetaModule::LopCore, LopInfo>` template instantiations present (native cores linked in).
  - `modelMF20Filter`, `modelParticules` present; `rack::plugin::Plugin::addModel(...)` referenced (adapter path linked in; addModel resolved by firmware at load).
- **Package contents** (`tar tf Foobar.mmplugin`): `Foobar.so`, `plugin.json`, `plugin-mm.json`, faceplates `MF20Filter.png`, `Particules.png`, `Loooop/Loooop.png`, `Loooop/Lop.png`, `SDK-2.2`, `presets/`.

## How the mechanism actually works (verified against the SDK)

The brief's premise (native + adapter share one registry) is correct, but two details in the brief's literal `register.cc`/`plugin.cpp` were wrong and were corrected:

1. **`init` signature / plugin bootstrap.** The SDK declares the entry point as
   `extern "C" __attribute__((visibility("default"))) void init(rack::plugin::Plugin* plugin);`
   in `rack-interface/include/plugin/callbacks.hpp`. So the exported unmangled `init` symbol is the *`Plugin*`-taking* function, and the firmware calls it with a `Plugin` **already populated from `plugin.json`** (brand slug `Foobar`). The brief's no-arg `extern "C" void init()` that constructs its own `static rack::Plugin plugin;` would have registered the adapter modules under an **empty brand slug** (addModel derives the brand from the plugin's slug). Corrected to define `void init(rack::plugin::Plugin* p)`, matching the SDK declaration. Confirmed against the working standalone MF-20 build, whose `.so` exports exactly one `T init` for its `void init(Plugin*)`.

## Non-trivial fixes made to the brief's file contents (and why)

1. **`register.cc` uses `init(rack::plugin::Plugin* p)`**, not no-arg `init()` + self-constructed `Plugin` (reason above). It just calls `init_Foobar(p)`.
2. **`src/plugin.cpp` MM branch does NOT `addModel(modelLoooop/modelLop)`.** Those VCV `Model*` live in `Loooop.cpp`/`Lop.cpp`, which are **not** compiled into the MM build — adding them would be undefined symbols. The native cores register Loooop/Löp instead. (The brief's Step 4 note already anticipated that `modelLoooop/modelLop` are VCV-only.) All registration for the MM build now lives in `init_Foobar` (natives + adapters), so the same function serves both the `.mmplugin` `init()` and the simulator's built-in `init_Foobar` dispatch.
3. **`register_loooop_modules()` was `static`** in `LoooopCore.cc` (internal linkage) — `register.cc`/`plugin.cpp` in other TUs could not link against it. Removed `static`.
4. **Native register brand `"Loooop"` → `"Foobar"`** in `LoooopCore.cc` and `LopCore.cc`, so Loooop/Löp group under the same brand as the adapter modules (adapters get their brand from the plugin slug `Foobar`). See concern #1.
5. **`LoooopCore.cc` bottom `init` block guarded with `#ifndef FOOBAR_COMBINED`** (per brief Step 7) so it defines neither `extern "C" init()` nor `init_Loooop` in the combined build; `register.cc` owns `init`.
6. **`register.cc` guards its `init` and `pluginInstance` definitions with `#ifndef SIMULATOR`.** In the simulator built-in build the sim generates a direct `init_Foobar(&plugin)` call, provides `pluginInstance`, and links all built-in plugins together — a bare `init` or a second `pluginInstance` there would collide. This makes the source work in all three modes (VCV / `.mmplugin` / sim built-in) without further edits.
7. **Vendored include-path fixes (Task 1 left these broken for the new directory layout):**
   - `src/mf20/MF20Filter.cpp` and `src/mf20/engine.hpp`: `"../MF20Filter.hpp"` → `"MF20Filter.hpp"` (header is now a sibling under `src/mf20/`).
   - `metamodule/loooop/LoooopCore.cc` & `LopCore.cc`: `"../src/dsp/LoopEngine.hpp"` → `"dsp/LoopEngine.hpp"`, `"../src/display/LoopWaveformRenderer.hpp"` → `"display/LoopWaveformRenderer.hpp"` (resolved via `-I src/loooop`).
   - `src/particules/Particules.cpp`: `"../nosuch_texture/beads_dsp/include/beads/beads.h"` → `"beads/beads.h"` (via `-I .../beads_dsp/include`); `"../nosuch_texture/beads_dsp/src/util/control_conditioner.h"` → `"../vendor/beads_dsp/src/util/control_conditioner.h"`.
   - `src/particules/particules_block_runtime.h`: `"../nosuch_texture/beads_dsp/include/beads/types.h"` → `"beads/types.h"`.
8. **Assets:** created `metamodule/assets/Loooop/` containing `Loooop.png` and `Lop.png`, because the native cores' `png_filename` is `"Loooop/Loooop.png"` / `"Loooop/Lop.png"` and `create_plugin` copies `SOURCE_ASSETS` to the package root verbatim (no brand prefix).

## Simulator

The `build-simulator` skill is explicit: **the simulator cannot load `.mmplugin` files at runtime.** The only sim path is compiling a plugin *into* the simulator binary as a built-in, which:
- is native-compiled against a "fake SDK" and shares a single `pluginInstance` across built-in plugins (a Task-4 firmware-integration concern), and
- opens a GUI window that a human must navigate to instantiate modules — not headlessly verifiable.

This is the "simulator harness too involved to drive headlessly" case the brief's fallback explicitly allows, so I stopped at the nm/readelf gate. I did the `register.cc`/`plugin.cpp` restructure (fix #2, #6) specifically so the built-in path (`init_Foobar` registers all four; `SIMULATOR` guard) is ready for Task 4 to drive when a human is available.

## Concerns for later tasks

1. **Brand-slug unification (done here, verify downstream).** Native cores now register under `"Foobar"` to match the adapter modules and `plugin.json`/`plugin-mm.json` (`MetaModuleBrandSlug: "Foobar"`). If any later param/asset tooling keys off the old `"Loooop"` brand, update it. The looper faceplate paths remain `"Loooop/*.png"` (independent of brand); assets are laid out accordingly.
2. **Faceplate assets for the adapter modules.** MF-20/Particules widgets call `asset::plugin(pluginInstance, "res/*.svg")`, but the MM package ships PNGs (`MF20Filter.png`, `Particules.png`) at the root, mirroring the working standalone builds. This spike did not verify on-device panel rendering for the combined plugin; confirm in Task 3/4 (panel/asset work). Particules also historically shipped a `bogaudio/` asset subdir and its sibling modules (Ondes/Retours) — only `Particules` is vendored/registered here, which is intended.
3. **`Particules.cpp` uses `<malloc.h>`** (line ~12). Fine for the ARM `.mmplugin` build; it will break a *native macOS* simulator built-in build (`<malloc.h>` doesn't exist on macOS — the skill notes this). Task 4 will need a guard (e.g. `<malloc/malloc.h>` under `defined(__APPLE__)`) before the sim built-in build.
4. **`FOOBAR_COMBINED` + `METAMODULE_BUILTIN` are both defined by our CMakeLists**, while the sim also defines `METAMODULE_BUILTIN`/`SIMULATOR`. The three-mode guard scheme (VCV: neither; `.mmplugin`: `METAMODULE_BUILTIN`+`FOOBAR_COMBINED`, no `SIMULATOR`; sim built-in: `+SIMULATOR`) is what makes the shared source compile everywhere — keep it intact when editing these files.
5. **VCV build path (Task 4) is untested here.** `src/plugin.cpp`'s `#else` branch registers all four VCV models and expects `Loooop.cpp`/`Lop.cpp` to be compiled (they are not part of the MM target). That is Task 4's responsibility.

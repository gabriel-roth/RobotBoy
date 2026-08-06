# SDK v2.3 adoption — OOM resilience plan

Branch: `sdk-2.3-oom`. Started 2026-08-06.

## Why

SDK v2.3 lets exceptions cross the plugin↔host boundary (`plugin.cmake:118` compiles `unwind_exidx.c`, which routes `__gnu_Unwind_Find_exidx` to the firmware's `mm_host_find_exidx`). The firmware catches `std::bad_alloc` around module creation (`firmware/src/patch_play/patch_player.hh:1070`) and shows "Not enough memory to load module" (`firmware/src/gui/pages/module_list.hh:468`). 4ms has released firmware v2.3.0.

RobotBoy's shipped `.mmplugin` is SDK-2.2, so it gets none of this.

## What is actually at risk

Only the looper buffers. `LoopEngine::reset()` (`src/loooop/dsp/LoopEngine.cpp:22`) does `bufL_.assign(maxSamples_, 0.f)` with `maxSamples_ = sampleRate * 60 s` — **23 MB per instance at 48 kHz**, allocated from the core constructors (`metamodule/loooop/LoooopCore.cc:27`, `LopCore.cc:26`). Uncaught.

Particules (~1.6 MB) and Retours (~1.5 MB) use `memalign`/`posix_memalign` with null checks and a safe no-op DSP fallback — they never throw. MF-20, Onbetap, Vespid, Ondes allocate nothing. No changes needed for any of them.

Two distinct paths, needing opposite treatment:

1. **Construction.** Covered by the firmware's catch once rebuilt on 2.3. Letting `bad_alloc` propagate is the *desired* behaviour — the module refuses to load with a clear message, which beats a silently dead looper.
2. **Sample-rate change.** `LoopEngine::setSampleRate` re-calls `reset()` when the looper is empty (`LoopEngine.cpp:45-51`). 96 kHz is user-selectable (`firmware/src/user_settings/audio_settings.hh:11`), doubling the buffer to 46 MB, and `vector::assign` transiently holds old+new. This runs through `PatchPlayer::set_samplerate` (`firmware/src/patch_play/patch_player.hh:635-650`) from `audio.cc:523` — **not wrapped in any try/catch**. A throw there reaches `std::terminate`. This is the one genuinely fatal path.

## Changes

### A. `LoopEngine` — allocate-then-commit, and a non-throwing rate change

- Restructure `reset()` so the two buffers are built into locals and committed by `swap` *before* any member state is touched. `reset()` becomes all-or-nothing: on `bad_alloc` the engine is exactly as it was.
- `reset()` keeps propagating `bad_alloc` (construction path depends on it).
- `setSampleRate()` must never throw. Wrap its `reset()` call; on `bad_alloc`, fall through to the existing retune-only branch (recompute rate-derived scalars, keep the current buffer) and clamp `maxSamples_` to `bufL_.size()`. Degradation is a shorter maximum loop time, which is coherent and audible-free.
- Expose a sticky flag so the host layer can tell the user once.
- Audit the audio path for anything that assumed `maxSamples_ == sampleRate_ * maxSeconds_`.

### B. Host layer — tell the user

- `LoooopCore::set_samplerate` / `LopCore::set_samplerate`: on the shortfall flag, `MetaModule::Gui::notify_user(...)` (`core-interface/gui/notification.hh`). ASCII-only text — the MM font has no en/em-dashes.
- VCV `Loooop.cpp` / `Lop.cpp` `onSampleRateChange`: `WARN(...)`, matching the Particules/Retours precedent.

### C. Metadata + build

- `plugin.json` version 2.0.1 → 2.1.0 (new firmware floor + behaviour change; `docs/release.md:112` asks for a bump when the SDK changes).
- CHANGELOG entry noting the firmware v2.3 requirement.
- Rebuild `.mmplugin` against SDK 2.3; confirm the `SDK-2.3` marker and that `check_syms.py` passes.
- Rebuild the VCV plugin — the shared `LoopEngine` changes affect it too.

## Verification

1. **Lane 1** (`tests/run.sh`) — new `LoopEngine` regression tests. Real allocation failure is reachable on the host: `reset(48000.f, huge)` genuinely throws `bad_alloc`.
2. **ARM build** — `SDK-2.3` marker present, `check_syms.py` clean, `mm_host_find_exidx` undefined-and-expected in the `.so`.
3. **Headless simulator** — render a Loooop patch to WAV before and after; outputs must be bit-identical (nothing on the audio path changed).

Not verifiable here: real OOM on hardware and the exidx bridge itself. Those need the device — they go on the user checklist.

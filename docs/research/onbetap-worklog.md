# Onbetap DSP — worklog

Working notes for the autonomous DSP build session (2026-07-15). Newest entries at the bottom.

## 2026-07-15 morning — setup and research

- Working strictly on `worktree-polivoks` (`/Users/gabrielroth/Dev/RobotBoy/.worktrees/worktree-polivoks`). No changes to main or other worktrees.
- Copied `resources/polivoks-sources/` (schematics, Erica DIY docs, Shruthi Polivoks board PDF, IME mk2 quickstart) from the main checkout into this worktree per instructions. Left untracked (\~15 MB of binaries).
- Surveyed the scaffold (`src/Onbetap.cpp`): stereo in/out, Cutoff (log2 Hz knob 20–20k, default 750), Resonance, Drive (all three with CV input + attenuverter), MODE_PARAM 5-position snap knob (LP/BP/HP/Notch/Peak), `vintageDrift` bool persisted via JSON with a Tamed/Vintage context menu. Panel is final — not to be touched.
- House DSP style established by MF-20 (`src/mf20/`): header-only filter core (TPT/ZDF 2-pole, closed-form piecewise-linear nonlinear solve, no iteration), `engine.hpp` voice pool (16 voices by value), modulate() at 2.5 ms with per-sample OnePoleSmoother slews in the g-domain (no per-sample tan/exp2/sqrt), deterministic alternating-sign denormal dither, NaN sanitize per modulate block, `processVCVG` ±5 V ↔ ±1 normalised convention (kVCVScale 0.2), mono-input R-mirror optimization. Host-free unit tests in `tests/mf20/*.cpp` run by `tests/run.sh`.
- Test lanes available: (1) `tests/run.sh` g++ DSP tests; (2) vcv-headless host (`~/Dev/vcv-headless`) WAV-in/WAV-out on the built VCV plugin; (3) MetaModule headless simulator (`~/Dev/metamodule/simulator`, `cmake --preset headless`, plugin compiled in as built-in via `-Dext_builtin_brand_*` cache vars — never edit ext-plugins.cmake).
- Dispatched three research agents: local schematic/doc analysis, web literature (DAFx paper etc.), existing emulations (open-source code, hardware clones). Outputs land in `docs/research/`.

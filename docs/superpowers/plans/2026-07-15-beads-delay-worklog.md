# beads-delay worklog

Running notes for the autonomous beads-delay build (branch
`worktree-beads-delay`). Newest entries at the bottom.

## 2026-07-15 — research & spec

- Sources gathered: Beads manual (tracked at
  `resources/Mutable-Beads-manual.pdf`), transcripts of both YouTube
  videos (scratchpad), full survey of `~/Dev/4ms-vcv` MetaModule DSP
  techniques → `docs/superpowers/specs/2026-07-15-beads-delay-mm-optimization-notes.md`.
- Found large reusable infrastructure in `src/particules/dsp/`
  (RecordingBuffer with Hermite reads + freeze declick + write decimation,
  QualityProcessor with the 4 Beads quality characters, Saturation with
  per-quality feedback limiting, Attenurandomizer, Random, Svf). The spec
  builds the new core on these.
- Old Particules delay engine was removed 2026-07-07 as unreachable dead
  code; per instructions it is ignored entirely (not consulted).
  `tests/test_no_delay_mode.py` guards against its return — will be
  rescoped to Particules files only.
- Spec written: `docs/superpowers/specs/2026-07-15-beads-delay-design.md`.

### Decisions Gabriel should review on return

1. **Module name**: slug `Echos`, display **Échos** (family consistency
   with Particules/Ondes; avoids MI's "Beads" name). Rename is trivial
   until patches exist — say the word.
2. Dedicated FEEDBACK/BLEND CV jacks (no assignable-CV button).
3. Always-stereo buffer; quality→duration = 4/8/32/16 s (Particules
   mapping) instead of hardware mono column.
4. Reverb, AGC, buffer persistence: out of scope for v1.
5. Context-menu selectables where behavior was ambiguous: time-change
   response (tape doppler vs crossfade), envelope feedback tap point,
   sliders for slew rate / random-LFO rate / input trim.

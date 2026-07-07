# Remove Particules Delay Mode Design

## Goal

Remove the unreachable Particules delay-mode implementation and simplify the processor to its only active granular signal path.

## Scope

- Delete the delay engine implementation and its dedicated tests.
- Remove the delay-mode parameter, processor state, public queries, alternate wet buffer, mode-switch crossfade, and delay-specific processing branches.
- Remove delay-engine build references and stale test-document references.
- Preserve all granular processing behavior, including quality-mode pitch modulation, feedback limiting, dry/wet mixing, reverb, grain-trigger reporting, and buffer controls.
- Leave `Loooop.md`, `MF20.md`, and `Particules.md` untouched because they contain concurrent user edits.

## Processing Simplification

The processor will always:

1. apply auto-gain using the current granular path;
2. mix feedback using the current granular feedback limiter;
3. write the resulting input into the recording buffer unless frozen;
4. apply quality pitch modulation to `GrainEngine`;
5. render the wet block with `GrainEngine`;
6. continue through quality output processing, feedback capture, dry/wet mixing, reverb, and output unchanged.

The removal will not alter module parameters, panel controls, patch serialization, or module slugs because the wrapper never exposed or set delay mode.

## API and File Removal

- Remove `BeadsParameters::delay_mode`.
- Remove `BeadsProcessor::IsDelayMode()` and `DelayTriggeredThisBlock()`.
- Delete `src/vendor/beads_dsp/src/delay/delay_engine.h` and `.cpp`.
- Delete `tests/beads/test_delay.cpp`.
- Remove the delay source from `metamodule/CMakeLists.txt` and the test name from `tests/README.md`.

## Verification

- Add a source-contract test that fails while delay-mode symbols and files remain.
- Run the complete DSP regression suites.

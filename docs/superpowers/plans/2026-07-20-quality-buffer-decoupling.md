# Quality Buffer Decoupling Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Decouple recording-buffer sample rate from buffer length by packing samples at their true bit width and channel count, so all four quality modes match hardware Beads on rate, bit depth, and duration — including input-adaptive mono/stereo recording (mono input doubles every mode's buffer, up to 64 s Scorched).

**Architecture:** `RecordingBuffer` (shared by Particules and Retours) becomes format- and channel-aware: the same fixed byte pool can hold float32, 12-bit-in-int16, or 8-bit µ-law samples, in stereo or mono, with frame count derived from the config. Each `QualityMode` maps to `(decimation, format, max_bytes)`; channel count comes from input-jack connections. Because reconfiguring reinterprets the pool's bytes, both processors replace their V-shaped quality duck with a fade-out → reconfigure+clear → hold-muted-until-cleared → fade-in state machine. Grain read positions move to Q32.32 fixed point so interpolation precision is constant at any buffer size.

**Tech Stack:** C++17, Catch2 tests (`tests/particules_dsp/`, `tests/retours_delay_dsp/`), VCV Rack + MetaModule (Cortex-A7, `-mfpu=neon-vfpv4 -O3`) targets. No heap allocation in DSP code.

## Global Constraints

- **Memory budget unchanged:** `GetMemoryRequirements(...).total_bytes` must be byte-identical before/after for both processors (pool = `(192000 + kInterpolationTail) × 2 × sizeof(float)` = 1,536,032 bytes each).
- **No transcendentals (`logf`/`expf`/`powf`) on any per-sample audio path.** The new codec must be integer/LUT based. (This *removes* the \~144k transcendentals/s today's Scorched spends on µ-law companding.)
- **No `double` arithmetic in per-sample hot loops** (Cortex-A7 doubles are scalar VFP, not NEON). Doubles are allowed in per-grain-start / per-block setup code.
- **No allocation or lazy static init on the audio thread.** The µ-law decode table is primed inside `RecordingBuffer::Init()` (host thread).
- **Buffer durations at 48 kHz:** stereo 4/8/16/32 s, mono 8/16/32/64 s for Bright/Cold/Sunny/Scorched.
- Commit messages: short (≤ \~15 words), **no AI attribution lines**.
- Run the relevant test suite after every task: `cd tests/particules_dsp && ./run.sh` and/or `cd tests/retours_delay_dsp && ./run.sh` (inspect `run.sh` on first use; do not substitute a different build path).
- Work stays on the current worktree branch; commit at the end of every task.

## Mode configuration decision record

Hardware Beads packs one fixed \~768 KB pool at each mode's true width; its manual's buffer table (mono/stereo columns) confirms every cell as pure memory arithmetic. Our pool is exactly 2× hardware's. Channel count follows the input jacks (mono = IN R unpatched — the same rule the adapters already use to normalize R←L), doubling duration when mono:

| Mode | decimation | format | stereo @48 kHz | mono @48 kHz | hardware (stereo/mono) |
|---|---|---|---|---|---|
| Bright digital | ÷1 | `kFloat32` | 48 kHz, 4 s | 8 s | 48 kHz 16-bit, 4 s / 8 s |
| Cold digital | ÷2 | `kInt12`, **half pool** | 24 kHz, 8 s | 16 s | 32 kHz 12-bit, 8 s / 16 s |
| Sunny tape | ÷2 | `kInt12` | 24 kHz, 16 s | 32 s | 24 kHz 12-bit, 10 s / 20 s |
| Scorched cassette | ÷2 | `kMuLaw8` | 24 kHz, 32 s | 64 s | 24 kHz 8-bit, 16 s / 32 s |

Decided judgment calls (do not relitigate during execution):

- **Bright stays float32** (16-bit packing is inaudible at unity; float keeps frozen content bit-exact). Manual says "16-bit or better".
- **Cold is capped at half the pool** so it matches hardware's 8 s/16 s; its rate is 24 kHz, not hardware's 32 kHz (decimation must divide the host rate).
- **Sunny/Scorched exceed hardware's durations** (2× pool + int16 container overhead vs true 12-bit packing). Kept — today's manual already promises 16/32 s.
- **Scorched no longer force-mono-sums.** Mono-vs-stereo is an input property for all modes (user decision 2026-07-20). Scorched keeps hiss, wow/flutter, dark output LP, and 8-bit µ-law grit; with stereo input it records true stereo.
- **µ-law codec is classic G.711-style segment encoding (bias 33, 8 segments × 16 steps)** with a sign-magnitude byte layout where codes `0x00`/`0x80` decode to exactly 0.0. Chosen over a `logf`-formula codec for exactness (integer ops, idempotent re-encode) and speed (\~10 int ops + `clz`, no transcendentals).
- **12-bit quantization moves into the storage codec** (Cold + Sunny, per recorded sample) and out of `QualityProcessor::ProcessOutput`; feedback re-quantizes every pass, as on hardware.
- **Grain read positions become Q32.32 fixed point** (int64 accumulator). This gives constant interpolation precision at any buffer size (a float32 position quantizes to 1/8 sample at 1.5M frames) at float-like cost on the A7. Retours' `EchoEngine` keeps float positions: it re-syncs to the write head every block and 1/8-sample quantization of an echo *delay time* is inaudible, unlike grain playback-phase jitter.

## MetaModule performance analysis

Target: STM32MP15x Cortex-A7 (\~800 MHz), NEON+VFPv4 (doubles scalar-only), 32 KB L1 / 256 KB L2, buffers in DDR3. Impact of this plan vs. current implementation, per cost axis:

**Removed — companding transcendentals.** Today's Scorched calls `MuLawCompress` per input sample (48k `logf`/s, `quality_processor.cpp:107`) and `MuLawExpand` per output sample ×2 (96k `expf`/s, `:170-171`). At \~50–150 cycles per call (newlib on A7) that is roughly **1–2.5% of the core, deleted outright**.

**Added — storage codec.** Encode runs only at committed-write rate (≤ 24 kHz × 2 ch): G.711-style encode is \~10–12 integer ops + `__builtin_clz` → \~0.6M cycles/s ≈ **0.1% of the core**. int12 encode is a clamp + `lround`, similar. Decode in the read hot path: µ-law = 1 extra L1 load per tap (256-float LUT, 1 KB, L1-resident) ≈ +2 cycles/tap; int12 = int16 load + `vcvt` + `vmul` ≈ +3–4 cycles/tap. At 8 taps per stereo grain-read: **+16–30 cycles per grain-sample**, on an inner loop that costs \~80–150 cycles today → **+10–20% on the grain render loop** in lo-fi modes, worst case (30 active grains) \~3–4.5% of the core.

**Neutral — memory bandwidth (by design).** Bytes a grain stream touches per host-sample = `bytes_per_frame × pitch / decimation`: Scorched today 8 B ÷ 8 = 1·pitch B, new 2 B ÷ 2 = 1·pitch B — identical; Sunny identical (2·pitch B); Cold **halves** (4·pitch → 2·pitch); Bright unchanged. Mono halves everything again. Buffer bytes per second of recorded audio are likewise unchanged for Scorched/Sunny (48 KB/s), so L2/DDR pressure does not grow anywhere and shrinks for Cold and all mono use.

**Neutral-to-better — Q32.32 positions.** One 64-bit add + integer wrap compare replaces a float add + two float `while`-wrap loops; extract costs `vcvt`+`vmul` (\~3 cycles). Estimated **within ±1% of the current float path** — vs. an estimated +10–15% grain-loop cost had we used `double` positions (scalar VFP). This is why the precision fix is Q32.32, not double.

**Net estimate:** Particules module CPU on MetaModule within **−2% to +4%** of current for like-for-like patches; Scorched-heavy patches likely a small net *win* (transcendental removal + unchanged bandwidth vs. added decode). Retours (≤ \~6 buffer reads/sample, no grain loop): transcendental removal dominates → **expected small net win**. These are instruction-count estimates, not measurements — Task 10 mandates on-device before/after comparison.

**Escalation mitigations if device measurement disagrees** (deferred, not in scope unless needed): (a) format-specialized grain render loops (three inlined variants, dispatched once per block — removes the per-tap format switch entirely, est. recovers 1–2% core worst case); (b) reduce Scorched's effective grain cap via the existing duration-driven `cached_max_active_`.

## Gotcha register (why the plan is shaped this way)

1. **Reconfiguring turns the pool into garbage.** Old float bytes read as µ-law are bounded nonsense; old µ-law/int bytes read as float32 can be **NaN or huge** (FZ mode flushes denormals but not NaN). The old V-duck plays "old content" right after a switch — fine for stale audio, catastrophic for reinterpreted bytes. Hence fade-out → apply → hold-muted-until-`ClearPending()` → fade-in. `NaN × 0 == NaN`, so muting by multiplication is NOT sufficient where NaN can appear:
   - Particules grains: `Grain::ProcessBlock` discards non-finite grain output before accumulating — safe. Finite garbage × 0 gain = 0 — safe.
   - Retours: `ReadWet()` feeds the pitch shifter and feedback path *before* any gain. During `kClearing` the processor must **skip `ReadWet()` entirely** and feed the shifter `{0,0}`.
2. **Particules grain read positions are NOT resize-safe.** `Grain::Process` reads *before* wrapping (`grain.h:69-76`); after a shrink a stale position indexes past the allocation → OOB. Fix: `GrainEngine::KillAllGrains()` at the muted apply point. Retours' `EchoEngine` wraps every read via `WrapPosition(pos, current_size)` (`echo_engine.cpp:11-16`) — resize-safe by construction.
3. **Grain-engine caches key on decimation only.** `grain_engine.cpp:239` recomputes duration/max-active caches only when `df != cached_decimation_`; Cold/Sunny/Scorched all have df=2 but different sizes. `KillAllGrains()` must set `cached_decimation_ = -1`.
4. **Deferred clear must outrun the write head.** Byte-based `TickClear` chunk = `capacity_bytes()/128` (12,003 B ≈ today's 3,000 floats): cursor sweeps ≥ 6,000 frames/block vs write head ≤ 32 frames/block at ÷2 — invariant preserved in every format.
5. **The tail mirror must copy the *stored representation*** (bytes/int16s), not decoded floats.
6. **`memset(0)` must decode to silence in every format.** float32 ✓, int12 ✓; µ-law uses a sign-magnitude layout where `0x00` decodes to 0.0 (NOT G.711's inverted wire format).
7. **Encode must guard NaN** (feedback glitches): `std::min(fabs(NaN), 1.0f)` returns NaN; codecs use `!(x == x)` / `!(fabs(x) > 0)` guards.
8. **Retours defers quality changes while frozen and for one block after unfreeze** (`retours_processor.cpp:157-179`, regression-tested at `test_quality_modes.cpp:367`). The state machine keeps exactly those guards on *transition start*, now also covering channel-count changes. Particules gains the same deferral (desktop UI blocks quality-while-frozen, but **MetaModule's Quality is a 4-position switch, not freeze-gated** — `Particules.cpp:410-418` — and cable changes are never gated).
9. **`BaseTimeControl` capacity:** Retours' apply point must compute `effective_seconds` from the **live** `recording_buffer.size()`, not `kBufferFrames` (`retours_processor.cpp:176` becomes wrong; `SetBufferSeconds` deliberately skips the Init clamp, `base_time.cpp:221-233`).
10. **Several tests poke the pool through raw `float*`** (`test_buffer.cpp` freeze tests, `test_freeze_slicer.cpp:168`). They stay valid (default config is stereo float32); new-format tests go through the API.
11. **Strict aliasing:** the pool is accessed as float32/int16/uint8 at different times; same-function accesses always use one format branch, `memset` via `uint8_t*` is always legal, and cross-time reinterpretation only happens in the masked garbage window. Do not "clean up" with type-punned unions.
12. **Startup:** a patch saved with a non-Bright mode (or mono input) runs a full transition on the first blocks (\~250 ms wet mute). Today's duck+clear is the same order of magnitude. Accepted; no special case.
13. **Mid-transition re-request** (Quality flipped again, cable re-patched): the machine finishes the current transition; the `kIdle` check picks up the newest `(quality, mono)` pair next block. No queue.
14. **Cable gesture = buffer wipe.** Connecting/removing IN R clears recorded/frozen content and mutes wet \~¼ s (physically required — the frame layout changes). Precedent: connection edges already retrigger auto-gain calibration (`Particules.cpp:437-443`). Document in manuals.
15. **Magic statics:** the µ-law decode table uses a function-local static (C++11 thread-safe; VCV may Init two instances concurrently). MetaModule may compile `-fno-threadsafe-statics`, but its plugin init is single-threaded; verify no warning in the MM build.
16. **`-mno-unaligned-access` (MM):** element-typed loads (`uint8_t`/`int16_t`/`float`) at element-stride offsets from the 16-aligned pool are always naturally aligned — no packed-struct or misaligned casts allowed in the buffer code.
17. **Q32.32 conversion domain:** `position × 2^32` for positions up to 1.5M frames ≈ 6.6e15 < 2^53, so the one-time double conversion at grain start is exact. NaN/Inf must never reach that cast (`static_cast<int64_t>(NaN)` is UB) — the existing `isfinite` guards in `ComputeGrainParams` (`grain_engine.cpp:162,188`) stay mandatory.

## File structure

- **Create** `src/particules/dsp/src/buffer/sample_codec.h` — int12 + µ-law8 codecs, header-only.
- **Modify** `src/particules/dsp/include/particules_dsp/types.h` — `StorageFormat`, `QualityConfig`, `QualityConfigFor()`, `kRecordingPoolBytes`, re-based `DecimationFactorForQuality()`.
- **Modify** `src/particules/dsp/src/buffer/recording_buffer.{h,cpp}` — format+channel-aware storage, `Configure()`, byte-based clear, `ClearPending()`, `ReadHermiteStereoFrac()`.
- **Modify** `src/particules/dsp/src/grain/grain.{h,cpp}`, `grain_engine.{h,cpp}` — Q32.32 positions, `KillAllGrains()`.
- **Modify** `src/particules/dsp/src/quality/quality_processor.{h,cpp}` — drop companding/quantization/mono-sum, retune LPs.
- **Modify** `src/particules/dsp/src/fx/saturation.cpp` — stale comment.
- **Modify** `src/particules/dsp/include/particules_dsp/parameters.h` and Retours' parameter struct — `bool mono_input`.
- **Modify** `src/particules/dsp/src/particules_processor.{h,cpp}`, `src/retours_delay/dsp/src/retours_processor.{h,cpp}` — transition state machines.
- **Modify** `src/particules/Particules.cpp`, `src/retours_delay/Retours.cpp` — mono-input detection.
- **Tests:** new `test_sample_codec.cpp`, `test_quality_transition.cpp` (particules); modify `test_buffer.cpp`, both `test_quality_modes.cpp`, `test_hardening.cpp`, `test_grain.cpp` if it constructs positions directly; register new files in `tests/particules_dsp/CMakeLists.txt`.
- **Docs:** `Particules.md`, `Retours.md`, `docs/scorched-cassette-quality-analysis.md`, `CHANGELOG.md` (if present).

---

### Task 1: Sample codecs (int12 + G.711-style µ-law8)

**Files:**
- Create: `src/particules/dsp/src/buffer/sample_codec.h`
- Create: `tests/particules_dsp/test_sample_codec.cpp`
- Modify: `tests/particules_dsp/CMakeLists.txt`

**Interfaces:**
- Produces (namespace `particules_dsp`, header-only): `Int12Encode(float) -> int16_t`, `Int12Decode(int16_t) -> float`, `MuLaw8Encode(float) -> uint8_t`, `MuLaw8Decode(uint8_t) -> float`, `MuLaw8DecodeTable() -> const float*` (256 entries).

- [ ] **Step 1: Write the failing tests**

Create `tests/particules_dsp/test_sample_codec.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <cmath>
#include <limits>

#include "buffer/sample_codec.h"

using namespace particules_dsp;
using Catch::Approx;

TEST_CASE("Int12: zero, clamping, NaN", "[codec]") {
    REQUIRE(Int12Encode(0.0f) == 0);
    REQUIRE(Int12Decode(0) == 0.0f);
    REQUIRE(Int12Encode(1.0f) == 2047);
    REQUIRE(Int12Encode(-1.0f) == -2047);
    REQUIRE(Int12Encode(5.0f) == 2047);
    REQUIRE(Int12Encode(-5.0f) == -2047);
    REQUIRE(Int12Encode(std::numeric_limits<float>::quiet_NaN()) == 0);
}

TEST_CASE("Int12: roundtrip error within half a step", "[codec]") {
    for (float x = -1.0f; x <= 1.0f; x += 0.001f) {
        float y = Int12Decode(Int12Encode(x));
        REQUIRE(std::fabs(y - x) <= 0.5f / 2047.0f + 1e-6f);
    }
}

TEST_CASE("MuLaw8: zero codes decode to exact silence", "[codec]") {
    REQUIRE(MuLaw8Encode(0.0f) == 0);
    REQUIRE(MuLaw8Encode(-0.0f) == 0);
    REQUIRE(MuLaw8Decode(0x00) == 0.0f);
    REQUIRE(MuLaw8Decode(0x80) == 0.0f);     // negative zero code
    REQUIRE(MuLaw8Encode(std::numeric_limits<float>::quiet_NaN()) == 0);
}

TEST_CASE("MuLaw8: full scale and clamping", "[codec]") {
    REQUIRE(MuLaw8Decode(MuLaw8Encode(1.0f)) == Approx(1.0f).margin(0.02f));
    REQUIRE(MuLaw8Decode(MuLaw8Encode(-1.0f)) == Approx(-1.0f).margin(0.02f));
    REQUIRE(MuLaw8Encode(3.0f) == MuLaw8Encode(1.0f));
}

TEST_CASE("MuLaw8: sign symmetry and bounds over all codes", "[codec]") {
    for (int c = 0; c < 128; ++c) {
        REQUIRE(MuLaw8Decode(static_cast<uint8_t>(c | 0x80)) ==
                Approx(-MuLaw8Decode(static_cast<uint8_t>(c))).margin(1e-9f));
        REQUIRE(MuLaw8Decode(static_cast<uint8_t>(c)) >= 0.0f);
        REQUIRE(MuLaw8Decode(static_cast<uint8_t>(c)) <= 1.0f);
    }
}

TEST_CASE("MuLaw8: decode is monotonic in magnitude", "[codec]") {
    for (int c = 0; c < 127; ++c) {
        REQUIRE(MuLaw8Decode(static_cast<uint8_t>(c + 1)) >
                MuLaw8Decode(static_cast<uint8_t>(c)));
    }
}

TEST_CASE("MuLaw8: encode(decode(c)) is the identity on codes", "[codec]") {
    // Idempotent re-quantization: re-recording an already-quantized signal
    // (freeze crossfade, feedback loop) must not drift.
    for (int c = 0; c < 256; ++c) {
        uint8_t code = static_cast<uint8_t>(c);
        uint8_t rt = MuLaw8Encode(MuLaw8Decode(code));
        if (code == 0x80) { REQUIRE(rt == 0x00); }   // negative zero
        else              { REQUIRE(rt == code); }
    }
}

TEST_CASE("MuLaw8: roundtrip relative error within segment bound", "[codec]") {
    // Segment mu-law: max relative error ~1/32 inside segments, worse only
    // in the near-zero linear region.
    for (float x = 0.02f; x <= 1.0f; x += 0.005f) {
        float y = MuLaw8Decode(MuLaw8Encode(x));
        REQUIRE(std::fabs(y - x) / x < 0.07f);
    }
}
```

- [ ] **Step 2: Register the test file, run to verify failure**

Add `test_sample_codec.cpp` to the source list in `tests/particules_dsp/CMakeLists.txt`.

Run: `cd tests/particules_dsp && ./run.sh`
Expected: FAIL — `buffer/sample_codec.h` not found.

- [ ] **Step 3: Write the codec header**

Create `src/particules/dsp/src/buffer/sample_codec.h`:

```cpp
#pragma once

#include <cmath>
#include <cstdint>

namespace particules_dsp {

// Storage codecs for RecordingBuffer's packed formats. Both are designed so
// that all-zero memory decodes to exact silence (memset(0) == clear), and
// both guard NaN on encode (the feedback path can transiently glitch).
// No transcendentals: encode is integer ops, decode is a table lookup —
// audio-thread safe on MetaModule's Cortex-A7.

// --- 12-bit linear codec, stored in int16 ----------------------------------
// Symmetric +/-2047 steps (avoids asymmetric clip).
inline int16_t Int12Encode(float x) {
    if (!(x == x)) return 0;               // NaN
    if (x > 1.0f) x = 1.0f;
    if (x < -1.0f) x = -1.0f;
    return static_cast<int16_t>(std::lround(x * 2047.0f));
}

inline float Int12Decode(int16_t v) {
    return static_cast<float>(v) * (1.0f / 2047.0f);
}

// --- 8-bit mu-law codec (G.711-style segment encoding) ----------------------
// Classic bias-33 segment mu-law: 8 exponential segments x 16 mantissa steps
// per sign. Byte layout: bit 7 = sign (1 = negative), bits 4-6 = segment,
// bits 0-3 = mantissa. NOT G.711's inverted wire format: codes 0x00 and 0x80
// decode to exactly 0.0f, so a zeroed buffer reads as silence.
// Full scale maps to the 14-bit magnitude 8158 (bias makes 8158+33 < 2^13).
inline uint8_t MuLaw8Encode(float x) {
    if (!(std::fabs(x) > 0.0f)) return 0;  // catches +0, -0, NaN
    uint8_t sign = 0;
    if (x < 0.0f) { sign = 0x80; x = -x; }
    if (x > 1.0f) x = 1.0f;
    int mag = static_cast<int>(x * 8158.0f) + 33;               // 33..8191
    int seg = 31 - __builtin_clz(static_cast<unsigned>(mag)) - 5;  // 0..7
    int mantissa = (mag >> (seg + 1)) & 0x0F;
    return static_cast<uint8_t>(sign | (seg << 4) | mantissa);
}

// Closed-form inverse for one code (used to build the table; also the
// reference for tests). decode(0) == 0 by construction: (2*0+33)<<0 - 33.
inline float MuLaw8DecodeRef(uint8_t code) {
    int seg = (code >> 4) & 0x07;
    int man = code & 0x0F;
    float mag = static_cast<float>(((2 * man + 33) << seg) - 33) / 8158.0f;
    return (code & 0x80) ? -mag : mag;
}

// 256-entry decode table. Function-local magic static: thread-safe under
// C++11. MUST be primed from a non-audio context (RecordingBuffer::Init
// calls it) so the one-time construction never runs on the audio thread.
inline const float* MuLaw8DecodeTable() {
    struct Table {
        float v[256];
        Table() {
            for (int c = 0; c < 256; ++c) {
                v[c] = MuLaw8DecodeRef(static_cast<uint8_t>(c));
            }
        }
    };
    static const Table t;
    return t.v;
}

inline float MuLaw8Decode(uint8_t code) {
    return MuLaw8DecodeTable()[code];
}

} // namespace particules_dsp
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `cd tests/particules_dsp && ./run.sh`
Expected: PASS (all `[codec]`). If the idempotence case fails, the seg/mantissa extraction disagrees with `MuLaw8DecodeRef` — fix the encode, not the test.

- [ ] **Step 5: Commit**

```bash
git add src/particules/dsp/src/buffer/sample_codec.h tests/particules_dsp/test_sample_codec.cpp tests/particules_dsp/CMakeLists.txt
git commit -m "Particules: add int12 and segment mu-law storage codecs"
```

---

### Task 2: Format- and channel-aware RecordingBuffer

**Files:**
- Modify: `src/particules/dsp/include/particules_dsp/types.h`
- Modify: `src/particules/dsp/src/buffer/recording_buffer.h`, `recording_buffer.cpp`
- Test: `tests/particules_dsp/test_buffer.cpp` (append)

**Interfaces:**
- Consumes: Task 1 codecs.
- Produces:
  - `enum class StorageFormat : uint8_t { kFloat32 = 0, kInt12 = 1, kMuLaw8 = 2 };` (types.h)
  - `void RecordingBuffer::Configure(int decimation_factor, StorageFormat format, int channels, size_t max_bytes = 0)` — reinterprets the pool; caller contract: follow with `Clear()` and mute wet until `ClearPending()` false (Task 3).
  - `StorageFormat format() const`, `size_t capacity_bytes() const`, `int channels() const`
  - `static size_t FramesForConfig(size_t capacity_bytes, int channels, StorageFormat format, size_t max_bytes)`
  - `inline void ReadHermiteStereoFrac(size_t i0, float frac, float* out_l, float* out_r) const` — hot reader taking a pre-split position (Q32.32 consumers); precondition `i0 < size()`, `frac in [0,1)`. `ReadHermiteStereoFast(float position, ...)` remains and delegates to it.
  - Mono behavior: `Write(l, r)` stores `(l+r)*0.5f`; all readers return the mono sample on both outputs.
  - `Init(float*, size_t num_frames, int channels)` keeps its exact current signature (num_frames = stereo-float32 capacity) — all existing call sites stay valid; Init resets to stereo float32.

- [ ] **Step 1: Write the failing tests**

Append to `tests/particules_dsp/test_buffer.cpp`:

```cpp
// ============================================================================
// Storage format + channel-count tests
// ============================================================================

#include "buffer/sample_codec.h"

TEST_CASE("RecordingBuffer: FramesForConfig arithmetic", "[buffer][format]") {
    // Production pool: (192000 + tail) * 2ch * 4B = 1,536,032 bytes.
    size_t cap = (192000 + kInterpolationTail) * 2 * sizeof(float);
    using SF = StorageFormat;
    REQUIRE(RecordingBuffer::FramesForConfig(cap, 2, SF::kFloat32, 0) == 192000);
    REQUIRE(RecordingBuffer::FramesForConfig(cap, 2, SF::kInt12, 0) == 384004);
    REQUIRE(RecordingBuffer::FramesForConfig(cap, 2, SF::kMuLaw8, 0) == 768012);
    // Mono doubles frames for the same bytes.
    REQUIRE(RecordingBuffer::FramesForConfig(cap, 1, SF::kFloat32, 0) == 384004);
    REQUIRE(RecordingBuffer::FramesForConfig(cap, 1, SF::kInt12, 0) == 768012);
    REQUIRE(RecordingBuffer::FramesForConfig(cap, 1, SF::kMuLaw8, 0) == 1536028);
    // max_bytes caps the pool (Cold digital: half pool -> hardware 8s/16s).
    REQUIRE(RecordingBuffer::FramesForConfig(cap, 2, SF::kInt12, cap / 2) == 192000);
    REQUIRE(RecordingBuffer::FramesForConfig(cap, 1, SF::kInt12, cap / 2) == 384004);
    // A cap larger than capacity is ignored.
    REQUIRE(RecordingBuffer::FramesForConfig(cap, 2, SF::kFloat32, cap * 4) == 192000);
}

TEST_CASE("RecordingBuffer: Configure changes size/format/channels, resets head", "[buffer][format]") {
    size_t num_frames = 1000;
    size_t bytes = (num_frames + kInterpolationTail) * 2 * sizeof(float);
    std::vector<uint8_t> mem(bytes, 0);
    RecordingBuffer buf;
    buf.Init(reinterpret_cast<float*>(mem.data()), num_frames, 2);

    for (int i = 0; i < 10; ++i) buf.Write(0.5f, 0.5f);
    REQUIRE(buf.write_head() == 10);

    buf.Configure(2, StorageFormat::kMuLaw8, 2);
    REQUIRE(buf.format() == StorageFormat::kMuLaw8);
    REQUIRE(buf.channels() == 2);
    REQUIRE(buf.size() == (num_frames + kInterpolationTail) * 4 - kInterpolationTail);
    REQUIRE(buf.write_head() == 0);
    REQUIRE(buf.decimation_factor() == 2);

    buf.Configure(2, StorageFormat::kMuLaw8, 1);   // mono: frames double again
    REQUIRE(buf.size() == (num_frames + kInterpolationTail) * 8 - kInterpolationTail);

    buf.Configure(1, StorageFormat::kFloat32, 2);
    REQUIRE(buf.size() == num_frames);
}

TEST_CASE("RecordingBuffer: mu-law stereo write/read roundtrip", "[buffer][format]") {
    size_t num_frames = 1000;
    size_t bytes = (num_frames + kInterpolationTail) * 2 * sizeof(float);
    std::vector<uint8_t> mem(bytes, 0);
    RecordingBuffer buf;
    buf.Init(reinterpret_cast<float*>(mem.data()), num_frames, 2);
    buf.Configure(1, StorageFormat::kMuLaw8, 2);
    buf.ImmediateClear();

    REQUIRE(buf.ReadLinear(0, 100.0f) == 0.0f);   // cleared = exact silence

    size_t n = buf.size();
    for (size_t i = 0; i < n; ++i) {
        float v = 0.6f * std::sin(2.0 * M_PI * static_cast<double>(i) / 64.0);
        buf.Write(v, -v);
    }
    for (size_t i = 10; i < 200; ++i) {
        float expected = 0.6f * std::sin(2.0 * M_PI * static_cast<double>(i) / 64.0);
        REQUIRE(buf.ReadHermite(0, static_cast<float>(i)) == Approx(expected).margin(0.06f));
        REQUIRE(buf.ReadHermite(1, static_cast<float>(i)) == Approx(-expected).margin(0.06f));
    }
    // Fast path, frac path, and reference reader agree.
    float fl, fr, sl, sr, ql, qr;
    buf.ReadHermiteStereoFast(123.5f, &fl, &fr);
    buf.ReadHermiteStereo(123.5f, &sl, &sr);
    buf.ReadHermiteStereoFrac(123, 0.5f, &ql, &qr);
    REQUIRE(fl == Approx(sl).margin(1e-6f));
    REQUIRE(ql == Approx(sl).margin(1e-6f));
    REQUIRE(qr == Approx(sr).margin(1e-6f));
}

TEST_CASE("RecordingBuffer: int12 write/read roundtrip", "[buffer][format]") {
    size_t num_frames = 1000;
    size_t bytes = (num_frames + kInterpolationTail) * 2 * sizeof(float);
    std::vector<uint8_t> mem(bytes, 0);
    RecordingBuffer buf;
    buf.Init(reinterpret_cast<float*>(mem.data()), num_frames, 2);
    buf.Configure(1, StorageFormat::kInt12, 2);
    buf.ImmediateClear();

    size_t check = 500;
    for (size_t i = 0; i < check; ++i) {
        float v = static_cast<float>(i) / static_cast<float>(check) - 0.5f;
        buf.Write(v, v);
    }
    for (size_t i = 10; i < check; ++i) {
        float expected = static_cast<float>(i) / static_cast<float>(check) - 0.5f;
        REQUIRE(buf.ReadLinear(0, static_cast<float>(i)) ==
                Approx(expected).margin(1.0f / 2047.0f));
    }
}

TEST_CASE("RecordingBuffer: mono stores the average, reads duplicate", "[buffer][format][mono]") {
    size_t num_frames = 1000;
    size_t bytes = (num_frames + kInterpolationTail) * 2 * sizeof(float);
    std::vector<uint8_t> mem(bytes, 0);
    RecordingBuffer buf;
    buf.Init(reinterpret_cast<float*>(mem.data()), num_frames, 2);
    buf.Configure(1, StorageFormat::kFloat32, 1);
    buf.ImmediateClear();

    buf.Write(0.2f, 0.6f);   // stores (0.2+0.6)/2 = 0.4
    float l, r;
    buf.ReadHermiteStereoFrac(0, 0.0f, &l, &r);
    REQUIRE(l == Approx(0.4f).margin(1e-6f));
    REQUIRE(r == Approx(0.4f).margin(1e-6f));
    REQUIRE(buf.ReadLinear(0, 0.0f) == Approx(0.4f).margin(1e-6f));
    REQUIRE(buf.ReadLinear(1, 0.0f) == Approx(0.4f).margin(1e-6f));
}

TEST_CASE("RecordingBuffer: tail mirror works in mu-law format", "[buffer][format][tail]") {
    size_t num_frames = 64;
    size_t bytes = (num_frames + kInterpolationTail) * 2 * sizeof(float);
    std::vector<uint8_t> mem(bytes, 0);
    RecordingBuffer buf;
    buf.Init(reinterpret_cast<float*>(mem.data()), num_frames, 2);
    buf.Configure(1, StorageFormat::kMuLaw8, 2);
    buf.ImmediateClear();

    const size_t N = buf.size();
    for (size_t i = 0; i < N; ++i) buf.Write(0.1f, 0.1f);
    buf.Write(0.9f, 0.9f);   // frame 0 (post-wrap) + its tail mirror
    buf.Write(0.8f, 0.8f);   // frame 1 + mirror

    float l = 0.f, r = 0.f, l2 = 0.f, r2 = 0.f;
    buf.ReadHermiteStereoFast(static_cast<float>(N) - 0.5f, &l, &r);
    buf.ReadHermiteStereo(static_cast<float>(N) - 0.5f, &l2, &r2);
    REQUIRE(l == Approx(l2).margin(1e-6f));
    REQUIRE(r == Approx(r2).margin(1e-6f));
    REQUIRE(l > 0.3f);   // taps at N/N+1 see the post-wrap writes
}

TEST_CASE("RecordingBuffer: decimation still applies in packed formats", "[buffer][format][decimation]") {
    size_t num_frames = 1000;
    size_t bytes = (num_frames + kInterpolationTail) * 2 * sizeof(float);
    std::vector<uint8_t> mem(bytes, 0);
    RecordingBuffer buf;
    buf.Init(reinterpret_cast<float*>(mem.data()), num_frames, 2);
    buf.Configure(2, StorageFormat::kMuLaw8, 2);
    buf.ImmediateClear();
    for (int i = 0; i < 100; ++i) buf.Write(0.5f, 0.5f);
    REQUIRE(buf.write_head() == 50);
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cd tests/particules_dsp && ./run.sh`
Expected: FAIL — `StorageFormat`/`Configure`/`FramesForConfig`/`ReadHermiteStereoFrac`/`channels` undeclared.

- [ ] **Step 3: Add `StorageFormat` to types.h**

In `types.h`, after the `QualityMode` enum:

```cpp
// Recording-buffer storage formats. Packing samples at reduced width lets the
// same byte pool hold more frames, decoupling recording rate from buffer
// length (see docs/superpowers/plans/2026-07-20-quality-buffer-decoupling.md).
enum class StorageFormat : uint8_t {
    kFloat32 = 0,   // 4 bytes/sample
    kInt12 = 1,     // 12-bit signed in an int16 container, 2 bytes/sample
    kMuLaw8 = 2     // 8-bit segment mu-law, 1 byte/sample
};
```

- [ ] **Step 4: Implement the buffer changes**

`recording_buffer.h` — replace the `float* buffer_` member block; add API. Private storage plumbing:

```cpp
// includes: #include "sample_codec.h"

// Public:
    // Reconfigure decimation, storage format, and channel count (1 = mono,
    // 2 = stereo). Frame count is derived from the byte pool fixed at Init,
    // optionally capped by max_bytes (0 = full pool). Resets the write head
    // and decimation counter. If the layout changed, the pool's existing
    // bytes are garbage under the new interpretation: the caller MUST follow
    // with Clear() and keep wet output muted until the clear completes.
    void Configure(int decimation_factor, StorageFormat format, int channels,
                   size_t max_bytes = 0);

    StorageFormat format() const { return format_; }
    int channels() const { return channels_; }
    size_t capacity_bytes() const { return capacity_bytes_; }

    static size_t FramesForConfig(size_t capacity_bytes, int channels,
                                  StorageFormat format, size_t max_bytes);

// Private:
    float* f32_ = nullptr;      // all three alias the same pool
    int16_t* i16_ = nullptr;
    uint8_t* u8_ = nullptr;
    size_t capacity_bytes_ = 0;
    StorageFormat format_ = StorageFormat::kFloat32;
    const float* mulaw_lut_ = nullptr;

    size_t bytes_per_sample() const {
        switch (format_) {
            case StorageFormat::kFloat32: return 4;
            case StorageFormat::kInt12:   return 2;
            case StorageFormat::kMuLaw8:  return 1;
        }
        return 4;
    }
    // Format-dispatched single-sample access (non-hot paths). In mono,
    // channel is clamped to 0 by the callers below.
    float SampleAt(size_t frame, int ch) const {
        size_t idx = frame * static_cast<size_t>(channels_) + static_cast<size_t>(ch);
        switch (format_) {
            case StorageFormat::kFloat32: return f32_[idx];
            case StorageFormat::kInt12:   return Int12Decode(i16_[idx]);
            case StorageFormat::kMuLaw8:  return mulaw_lut_[u8_[idx]];
        }
        return 0.0f;
    }
    void StoreSample(size_t frame, int ch, float v) {
        size_t idx = frame * static_cast<size_t>(channels_) + static_cast<size_t>(ch);
        switch (format_) {
            case StorageFormat::kFloat32: f32_[idx] = v; break;
            case StorageFormat::kInt12:   i16_[idx] = Int12Encode(v); break;
            case StorageFormat::kMuLaw8:  u8_[idx] = MuLaw8Encode(v); break;
        }
    }
    // Copy one frame's STORED representation into the tail mirror.
    void CopyFrameToTail(size_t frame) {
        size_t row = static_cast<size_t>(channels_) * bytes_per_sample();
        std::memcpy(u8_ + (size_ + frame) * row, u8_ + frame * row, row);
    }
```

Hot readers in the header — `ReadHermiteStereoFrac` is the workhorse; the float-position variant splits and delegates:

```cpp
    // Fast interpolated read with a pre-split position (integer frame +
    // fraction). Preconditions: i0 < size(), 0 <= frac < 1. Q32.32 callers
    // (grains) use this directly so precision is independent of buffer size.
    inline void ReadHermiteStereoFrac(size_t i0, float frac,
                                      float* out_l, float* out_r) const {
        size_t i_1 = (i0 == 0) ? size_ - 1 : i0 - 1;
        size_t i1 = i0 + 1;   // tail guarantees valid data
        size_t i2 = i0 + 2;   // tail guarantees valid data
        if (channels_ == 2) {
            switch (format_) {
                case StorageFormat::kFloat32: {
                    const float* p_1 = &f32_[i_1 * 2];
                    const float* p0  = &f32_[i0  * 2];
                    const float* p1  = &f32_[i1  * 2];
                    const float* p2  = &f32_[i2  * 2];
                    *out_l = InterpolateHermite(p_1[0], p0[0], p1[0], p2[0], frac);
                    *out_r = InterpolateHermite(p_1[1], p0[1], p1[1], p2[1], frac);
                    return;
                }
                case StorageFormat::kInt12: {
                    const int16_t* p_1 = &i16_[i_1 * 2];
                    const int16_t* p0  = &i16_[i0  * 2];
                    const int16_t* p1  = &i16_[i1  * 2];
                    const int16_t* p2  = &i16_[i2  * 2];
                    constexpr float kS = 1.0f / 2047.0f;
                    *out_l = InterpolateHermite(p_1[0] * kS, p0[0] * kS,
                                                p1[0] * kS, p2[0] * kS, frac);
                    *out_r = InterpolateHermite(p_1[1] * kS, p0[1] * kS,
                                                p1[1] * kS, p2[1] * kS, frac);
                    return;
                }
                case StorageFormat::kMuLaw8: {
                    const uint8_t* p_1 = &u8_[i_1 * 2];
                    const uint8_t* p0  = &u8_[i0  * 2];
                    const uint8_t* p1  = &u8_[i1  * 2];
                    const uint8_t* p2  = &u8_[i2  * 2];
                    const float* lut = mulaw_lut_;
                    *out_l = InterpolateHermite(lut[p_1[0]], lut[p0[0]],
                                                lut[p1[0]], lut[p2[0]], frac);
                    *out_r = InterpolateHermite(lut[p_1[1]], lut[p0[1]],
                                                lut[p1[1]], lut[p2[1]], frac);
                    return;
                }
            }
        }
        // Mono: one interpolation, duplicated to both outputs (cheaper than
        // stereo — half the loads).
        float m;
        switch (format_) {
            case StorageFormat::kFloat32:
                m = InterpolateHermite(f32_[i_1], f32_[i0], f32_[i1], f32_[i2], frac);
                break;
            case StorageFormat::kInt12: {
                constexpr float kS = 1.0f / 2047.0f;
                m = InterpolateHermite(i16_[i_1] * kS, i16_[i0] * kS,
                                       i16_[i1] * kS, i16_[i2] * kS, frac);
                break;
            }
            case StorageFormat::kMuLaw8:
                m = InterpolateHermite(mulaw_lut_[u8_[i_1]], mulaw_lut_[u8_[i0]],
                                       mulaw_lut_[u8_[i1]], mulaw_lut_[u8_[i2]], frac);
                break;
            default: m = 0.0f; break;
        }
        *out_l = m;
        *out_r = m;
    }

    // Float-position variant (Retours' EchoEngine; positions re-sync per
    // block there, so float precision suffices). Precondition unchanged:
    // position finite and in [0, size_).
    inline void ReadHermiteStereoFast(float position, float* out_l, float* out_r) const {
        int pos_int = static_cast<int>(position);
        float frac = position - static_cast<float>(pos_int);
        ReadHermiteStereoFrac(static_cast<size_t>(pos_int), frac, out_l, out_r);
    }
```

`recording_buffer.cpp`:

```cpp
void RecordingBuffer::Init(float* buffer, size_t num_frames, int num_channels) {
    f32_ = buffer;
    i16_ = reinterpret_cast<int16_t*>(buffer);
    u8_  = reinterpret_cast<uint8_t*>(buffer);
    capacity_bytes_ = (num_frames + kInterpolationTail)
                      * static_cast<size_t>(num_channels) * sizeof(float);
    size_ = num_frames;
    channels_ = num_channels;
    format_ = StorageFormat::kFloat32;
    mulaw_lut_ = MuLaw8DecodeTable();   // prime the table off the audio thread
    write_head_ = 0;
    decimation_factor_ = 1;
    decimation_counter_ = 0;
    write_ramp_remaining_ = 0;

    std::memset(u8_, 0, capacity_bytes_);
}

size_t RecordingBuffer::FramesForConfig(size_t capacity_bytes, int channels,
                                        StorageFormat format, size_t max_bytes) {
    size_t bps = 4;
    if (format == StorageFormat::kInt12) bps = 2;
    if (format == StorageFormat::kMuLaw8) bps = 1;
    size_t pool = capacity_bytes;
    if (max_bytes > 0 && max_bytes < pool) pool = max_bytes;
    size_t frames = pool / (bps * static_cast<size_t>(channels));
    return (frames > static_cast<size_t>(kInterpolationTail))
               ? frames - kInterpolationTail : 0;
}

void RecordingBuffer::Configure(int decimation_factor, StorageFormat format,
                                int channels, size_t max_bytes) {
    if (!u8_ || capacity_bytes_ == 0) return;
    if (channels < 1) channels = 1;
    if (channels > 2) channels = 2;
    SetDecimationFactor(decimation_factor);
    size_t frames = FramesForConfig(capacity_bytes_, channels, format, max_bytes);
    if (format == format_ && channels == channels_ && frames == size_) return;
    format_ = format;
    channels_ = channels;
    size_ = frames;
    write_head_ = 0;
    write_ramp_remaining_ = 0;
    // Pool bytes are now garbage under the new interpretation; caller must
    // Clear() and mute until ClearPending() (see header contract).
}
```

`Write()` — replace the raw-float body (note the mono branch and that the `channels_ < 2` early-out guard must be REMOVED — mono is now legal; guard on `channels_ < 1` instead):

```cpp
void RecordingBuffer::Write(float left, float right) {
    if (size_ == 0 || !u8_ || channels_ < 1) return;

    // Sample-and-hold decimation: keep every Nth sample.
    decimation_counter_++;
    if (decimation_counter_ < decimation_factor_) return;
    decimation_counter_ = 0;

    if (channels_ == 1) {
        float mono = (left + right) * 0.5f;
        if (write_ramp_remaining_ > 0) {
            float g = 1.0f - static_cast<float>(write_ramp_remaining_)
                           / static_cast<float>(kCrossfadeSamples);
            float old_m = SampleAt(write_head_, 0);
            mono = old_m + (mono - old_m) * g;
            --write_ramp_remaining_;
        }
        StoreSample(write_head_, 0, mono);
    } else {
        if (write_ramp_remaining_ > 0) {
            float g = 1.0f - static_cast<float>(write_ramp_remaining_)
                           / static_cast<float>(kCrossfadeSamples);
            float old_l = SampleAt(write_head_, 0);
            float old_r = SampleAt(write_head_, 1);
            left  = old_l + (left  - old_l) * g;
            right = old_r + (right - old_r) * g;
            --write_ramp_remaining_;
        }
        StoreSample(write_head_, 0, left);
        StoreSample(write_head_, 1, right);
    }

    if (write_head_ < static_cast<size_t>(kInterpolationTail)) {
        CopyFrameToTail(write_head_);
    }
    write_head_++;
    if (write_head_ >= size_) write_head_ = 0;
}
```

Out-of-line readers `ReadHermite`, `ReadHermiteStereo`, `ReadLinear`: replace raw loads with `SampleAt(i, ch)`, clamping `ch` to 0 when `channels_ == 1` (so `ReadLinear(1, pos)` in mono returns the mono sample, and `ReadHermiteStereo` outputs duplicates). Remove `ReadHermiteStereo`'s `channels_ < 2` silence guard. `ImmediateClear` memsets `(size_ + tail) * channels_ * bytes_per_sample()` via `u8_`. `NotifyFreeze`'s raw accesses stay expressed via `f32_` this task (Task 4 converts them); its `channels_ < 2` guard changes to `channels_ < 1`.

- [ ] **Step 5: Run both suites**

Run: `cd tests/particules_dsp && ./run.sh && cd ../retours_delay_dsp && ./run.sh`
Expected: PASS — pre-existing float32-stereo tests must be numerically unchanged.

- [ ] **Step 6: Commit**

```bash
git add src/particules/dsp/include/particules_dsp/types.h src/particules/dsp/src/buffer/ tests/particules_dsp/test_buffer.cpp
git commit -m "RecordingBuffer: format- and channel-aware storage with Configure()"
```

---

### Task 3: Byte-based deferred clear + ClearPending

**Files:**
- Modify: `src/particules/dsp/src/buffer/recording_buffer.h`, `recording_buffer.cpp`
- Modify: `src/particules/dsp/src/particules_processor.cpp:163-166`, `src/retours_delay/dsp/src/retours_processor.cpp:150-155` (TickClear call sites)
- Test: `tests/particules_dsp/test_buffer.cpp`

**Interfaces:**
- Produces: `void TickClear(size_t max_bytes)` (unit change: was floats), `bool ClearPending() const`.

- [ ] **Step 1: Write the failing tests**

Append to `tests/particules_dsp/test_buffer.cpp`:

```cpp
TEST_CASE("RecordingBuffer: deferred clear drains in bytes and reports pending", "[buffer][clear]") {
    size_t num_frames = 1000;
    size_t bytes = (num_frames + kInterpolationTail) * 2 * sizeof(float);
    std::vector<uint8_t> mem(bytes, 0);
    RecordingBuffer buf;
    buf.Init(reinterpret_cast<float*>(mem.data()), num_frames, 2);

    for (size_t i = 0; i < num_frames; ++i) buf.Write(1.0f, 1.0f);
    REQUIRE_FALSE(buf.ClearPending());

    buf.Clear();
    REQUIRE(buf.ClearPending());
    // Total: (1000 + 4) frames * 2ch * 4B = 8032 bytes -> 9 ticks of 1000.
    int ticks = 0;
    while (buf.ClearPending()) { buf.TickClear(1000); ++ticks; REQUIRE(ticks < 100); }
    REQUIRE(ticks == 9);
    REQUIRE(buf.ReadLinear(0, 500.0f) == 0.0f);
}

TEST_CASE("RecordingBuffer: clear extent follows the active config", "[buffer][clear][format]") {
    size_t num_frames = 1000;
    size_t bytes = (num_frames + kInterpolationTail) * 2 * sizeof(float);
    std::vector<uint8_t> mem(bytes, 0);
    RecordingBuffer buf;
    buf.Init(reinterpret_cast<float*>(mem.data()), num_frames, 2);
    buf.Configure(2, StorageFormat::kMuLaw8, 1);   // mono mu-law: full pool
    buf.Clear();
    size_t expected = (buf.size() + kInterpolationTail) * 1 * 1;   // == pool bytes
    size_t drained = 0;
    while (buf.ClearPending()) { buf.TickClear(512); drained += 512; }
    REQUIRE(drained >= expected);
    REQUIRE(buf.ReadLinear(0, 2000.0f) == 0.0f);
}

TEST_CASE("RecordingBuffer: ImmediateClear cancels a pending deferred clear", "[buffer][clear]") {
    size_t num_frames = 1000;
    size_t bytes = (num_frames + kInterpolationTail) * 2 * sizeof(float);
    std::vector<uint8_t> mem(bytes, 0);
    RecordingBuffer buf;
    buf.Init(reinterpret_cast<float*>(mem.data()), num_frames, 2);
    for (size_t i = 0; i < num_frames; ++i) buf.Write(1.0f, 1.0f);

    buf.Clear();
    REQUIRE(buf.ClearPending());
    buf.ImmediateClear();
    REQUIRE_FALSE(buf.ClearPending());
    REQUIRE(buf.ReadLinear(0, 999.0f) == 0.0f);
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cd tests/particules_dsp && ./run.sh`
Expected: FAIL — `ClearPending` undeclared; tick count mismatches float-unit TickClear.

- [ ] **Step 3: Implement byte-based clear**

In `recording_buffer.h`:

```cpp
    // Zero up to max_bytes of the pending clear started by Clear(). Byte-
    // based so wall-clock drain time is identical in every storage config.
    void TickClear(size_t max_bytes);
    // True while a deferred Clear() has not finished draining.
    bool ClearPending() const { return clear_cursor_ < clear_total_; }

    // (private — replaces the float-indexed pair)
    size_t clear_cursor_ = 0;  // next byte to zero
    size_t clear_total_  = 0;  // total bytes to zero (0 = none pending)
```

In `recording_buffer.cpp`:

```cpp
void RecordingBuffer::Clear() {
    if (!u8_ || size_ == 0) return;
    write_head_ = 0;
    decimation_counter_ = 0;
    clear_cursor_ = 0;
    clear_total_ = (size_ + kInterpolationTail)
                   * static_cast<size_t>(channels_) * bytes_per_sample();
}

void RecordingBuffer::ImmediateClear() {
    if (!u8_ || size_ == 0) return;
    std::memset(u8_, 0, (size_ + kInterpolationTail)
                        * static_cast<size_t>(channels_) * bytes_per_sample());
    clear_cursor_ = clear_total_;
}

void RecordingBuffer::TickClear(size_t max_bytes) {
    if (clear_cursor_ >= clear_total_) return;
    size_t chunk = std::min(max_bytes, clear_total_ - clear_cursor_);
    std::memset(u8_ + clear_cursor_, 0, chunk);
    clear_cursor_ += chunk;
}
```

- [ ] **Step 4: Update the two processor call sites**

Both processors replace their `kClearChunkFloats` constant + call with:

```cpp
    // Drain any deferred buffer clear (post-quality-change) incrementally.
    // Byte-based chunk: capacity/128 keeps the drain at ~128 blocks (~170 ms
    // at 48 kHz / 64-sample blocks) in every storage config.
    s.recording_buffer.TickClear(s.recording_buffer.capacity_bytes() / 128);
```

- [ ] **Step 5: Run both suites, commit**

Run: `cd tests/particules_dsp && ./run.sh && cd ../retours_delay_dsp && ./run.sh` — expected PASS.

```bash
git add src/particules/dsp/src/buffer/ src/particules/dsp/src/particules_processor.cpp src/retours_delay/dsp/src/retours_processor.cpp tests/particules_dsp/test_buffer.cpp
git commit -m "RecordingBuffer: byte-based deferred clear with ClearPending()"
```

---

### Task 4: Freeze declicking through format-aware accessors

**Files:**
- Modify: `src/particules/dsp/src/buffer/recording_buffer.cpp` (`NotifyFreeze`)
- Test: `tests/particules_dsp/test_buffer.cpp`

- [ ] **Step 1: Write the failing test**

Append to `tests/particules_dsp/test_buffer.cpp`:

```cpp
TEST_CASE("RecordingBuffer: freeze seam fade works in mu-law format", "[buffer][freeze][format]") {
    size_t num_frames = 1000;
    size_t bytes = (num_frames + kInterpolationTail) * 2 * sizeof(float);
    std::vector<uint8_t> mem(bytes, 0);
    RecordingBuffer buf;
    buf.Init(reinterpret_cast<float*>(mem.data()), num_frames, 2);
    buf.Configure(1, StorageFormat::kMuLaw8, 2);
    buf.ImmediateClear();

    for (int i = 0; i < 500; ++i) buf.Write(0.8f, 0.8f);
    REQUIRE(buf.write_head() == 500);

    buf.NotifyFreeze(true);

    REQUIRE(std::fabs(buf.ReadLinear(0, 499.0f)) < 0.05f);            // seam ~0
    REQUIRE(buf.ReadLinear(0, 490.0f) > buf.ReadLinear(0, 498.0f));   // ramps up
    REQUIRE(buf.ReadLinear(0, 460.0f) == Approx(0.8f).margin(0.06f)); // untouched

    buf.NotifyFreeze(false);
    buf.Write(0.5f, 0.5f);   // write ramp starts from faded (~0) content
    REQUIRE(std::fabs(buf.ReadLinear(0, 500.0f)) < 0.1f);
}
```

- [ ] **Step 2: Run to verify failure**

Run: `cd tests/particules_dsp && ./run.sh`
Expected: FAIL — `NotifyFreeze` writes raw floats over µ-law bytes.

- [ ] **Step 3: Convert NotifyFreeze**

In the entering-freeze fade loop, replace raw scaling and tail-sync:

```cpp
    for (int j = 0; j < fade; ++j) {
        float gain = static_cast<float>(j) / static_cast<float>(fade);
        int newest = static_cast<int>(write_head_) - 1 - j;
        newest = ((newest % size_int) + size_int) % size_int;
        int oldest = (static_cast<int>(write_head_) + j) % size_int;
        const int frames[2] = {newest, oldest};
        for (int f = 0; f < 2; ++f) {
            size_t frame = static_cast<size_t>(frames[f]);
            for (int c = 0; c < channels_; ++c) {
                StoreSample(frame, c, SampleAt(frame, c) * gain);
            }
            if (frames[f] < static_cast<int>(kInterpolationTail)) {
                CopyFrameToTail(frame);
            }
        }
    }
```

Top-of-function guard: `!u8_` instead of `!buffer_`.

- [ ] **Step 4: Run both suites, commit**

Expected: PASS including the exact-gain float32 freeze tests.

```bash
git add src/particules/dsp/src/buffer/recording_buffer.cpp tests/particules_dsp/test_buffer.cpp
git commit -m "RecordingBuffer: freeze declicking via format-aware accessors"
```

---

### Task 5: Mode config table + QualityProcessor retune

**Files:**
- Modify: `src/particules/dsp/include/particules_dsp/types.h`
- Modify: `src/particules/dsp/src/quality/quality_processor.h`, `quality_processor.cpp`
- Modify: `src/particules/dsp/src/fx/saturation.cpp` (comment)
- Test: `tests/particules_dsp/test_quality_modes.cpp`, `tests/retours_delay_dsp/test_quality_modes.cpp` (if any case asserts decimation 4/8)

**Interfaces:**
- Produces (types.h):

```cpp
static constexpr size_t kRecordingPoolBytes =
    (kDefaultBufferFrames + kInterpolationTail) * 2 * sizeof(float);

struct QualityConfig {
    int decimation;
    StorageFormat format;
    size_t max_bytes;   // 0 = full pool
};
QualityConfig QualityConfigFor(QualityMode mode);
```

- `DecimationFactorForQuality(mode)` re-based on the table. **Behavior change:** Sunny 4→2, Scorched 8→2.
- `QualityProcessor`: drops Cold 12-bit output rounding, Scorched µ=64 companding, **and the Scorched mono sum** (mono-vs-stereo is now an input property). Retunes `kSunnyTapeInputLpHz` 5000→10000, `kScorchedInputLpHz` 2500→10000. Keeps hiss (now added independently per channel), wow/flutter, output LPs (Sunny 10 kHz, Scorched 5 kHz).

**Interim state note:** until Tasks 7–8, both processors run Sunny/Scorched at ÷2 with float32 stereo storage (8 s) and no quantization. Committed, tests green, sound transitional — acceptable on the branch.

- [ ] **Step 1: Update the quality tests**

In `tests/particules_dsp/test_quality_modes.cpp`:

1. **Delete** the Cold 12-bit `ProcessOutput` rounding cases (\~lines 37, 54) — quantization now lives in the storage codec (Tasks 1–2 cover it).
2. **Delete or rewrite** any case asserting Scorched mono summing (`ProcessInput` returning identical L/R). New expectation — stereo is preserved:

```cpp
TEST_CASE("QualityModes: Scorched preserves stereo", "[quality]") {
    QualityProcessor qp;
    qp.Init(48000.0f);
    float diff_peak = 0.0f;
    for (int i = 0; i < 4800; ++i) {
        float l = 0.5f * std::sin(2.0f * kPi * 500.0f * i / 48000.0f);
        float r = 0.5f * std::sin(2.0f * kPi * 700.0f * i / 48000.0f);
        StereoFrame out = qp.ProcessInput({l, r}, QualityMode::kScorchedCassette);
        if (i > 480) diff_peak = std::max(diff_peak, std::fabs(out.l - out.r));
    }
    REQUIRE(diff_peak > 0.1f);   // channels stay distinct (old code mono-summed)
}
```

3. **Rewrite** the Scorched input-LP cases (\~64-83, \~323-346) against the 10 kHz cutoff:

```cpp
TEST_CASE("QualityModes: Scorched input LP passes 4kHz", "[quality][decimation]") {
    QualityProcessor qp;
    qp.Init(48000.0f);
    float in_peak = 0.0f, out_peak = 0.0f;
    for (int i = 0; i < 4800; ++i) {
        float v = 0.5f * std::sin(2.0f * kPi * 4000.0f * i / 48000.0f);
        StereoFrame out = qp.ProcessInput({v, v}, QualityMode::kScorchedCassette);
        if (i > 480) {
            in_peak = std::max(in_peak, std::fabs(v));
            out_peak = std::max(out_peak, std::fabs(out.l));
        }
    }
    REQUIRE(out_peak > in_peak * 0.7f);
}

TEST_CASE("QualityModes: Scorched input LP attenuates 20kHz", "[quality][decimation]") {
    QualityProcessor qp;
    qp.Init(48000.0f);
    float in_peak = 0.0f, out_peak = 0.0f;
    for (int i = 0; i < 4800; ++i) {
        float v = 0.5f * std::sin(2.0f * kPi * 20000.0f * i / 48000.0f);
        StereoFrame out = qp.ProcessInput({v, v}, QualityMode::kScorchedCassette);
        if (i > 480) {
            in_peak = std::max(in_peak, std::fabs(v));
            out_peak = std::max(out_peak, std::fabs(out.l));
        }
    }
    REQUIRE(out_peak < in_peak * 0.4f);   // one octave above 10k, 2nd-order LP
}
```

4. **Rewrite** the Sunny 5 kHz case (\~298) the same way (pass 4 kHz / attenuate 20 kHz with `kSunnyTape`).
5. **Rewrite** the companding round-trip cases (\~183, \~236) as level-neutrality (no companding):

```cpp
TEST_CASE("QualityModes: Scorched input/output pair is level-neutral", "[quality]") {
    QualityProcessor qp;
    qp.Init(48000.0f);
    float in_peak = 0.0f, out_peak = 0.0f;
    for (int i = 0; i < 9600; ++i) {
        float v = 0.4f * std::sin(2.0f * kPi * 1000.0f * i / 48000.0f);
        StereoFrame mid = qp.ProcessInput({v, v}, QualityMode::kScorchedCassette);
        StereoFrame out = qp.ProcessOutput(mid, QualityMode::kScorchedCassette);
        if (i > 960) {
            in_peak = std::max(in_peak, std::fabs(v));
            out_peak = std::max(out_peak, std::fabs(out.l));
        }
    }
    REQUIRE(out_peak == Approx(in_peak).margin(0.15f));
}
```

6. **Adapt** the feedback-stability cases (\~287, \~376): keep the `ProcessInput → LimitFeedback → ProcessOutput` loop structure and bounded-output assertions; drop companding-gain-specific assertions. Wow/flutter cases (\~217, \~394) untouched.

In `tests/retours_delay_dsp/test_quality_modes.cpp`: update any literal decimation expectations for Sunny (4→2) / Scorched (8→2); the formula-based cases self-adjust.

- [ ] **Step 2: Run to verify the new expectations fail**

Run: `cd tests/particules_dsp && ./run.sh`
Expected: FAIL — stereo-preservation, 4 kHz passband, and level-neutral cases fail against current code.

- [ ] **Step 3: Add the config table to types.h**

Place `kDefaultBufferFrames` (comment updated: "Packed storage + decimation set effective duration — stereo 4/8/16/32 s, mono 8/16/32/64 s at 48 kHz") **above** the `QualityMode` enum, then after `StorageFormat`:

```cpp
static constexpr size_t kRecordingPoolBytes =
    (kDefaultBufferFrames + kInterpolationTail) * 2 * sizeof(float);

// Per-mode recording configuration. decimation divides the host rate; format
// is the storage packing; max_bytes caps the pool (0 = all of it). Channel
// count is NOT part of the mode — it follows the input jacks (mono input
// doubles duration for the same bytes).
struct QualityConfig {
    int decimation;
    StorageFormat format;
    size_t max_bytes;
};

inline QualityConfig QualityConfigFor(QualityMode mode) {
    switch (mode) {
        case QualityMode::kBrightDigital:
            return {1, StorageFormat::kFloat32, 0};
        case QualityMode::kColdDigital:
            // Half pool: Cold stays 8 s stereo / 16 s mono like hardware
            // (uncapped int12 would give 16/32 s).
            return {2, StorageFormat::kInt12, kRecordingPoolBytes / 2};
        case QualityMode::kSunnyTape:
            return {2, StorageFormat::kInt12, 0};
        case QualityMode::kScorchedCassette:
            return {2, StorageFormat::kMuLaw8, 0};
    }
    return {1, StorageFormat::kFloat32, 0};
}

inline int DecimationFactorForQuality(QualityMode mode) {
    return QualityConfigFor(mode).decimation;
}
```

Delete the old standalone `DecimationFactorForQuality` (lines 37-45).

- [ ] **Step 4: Retune QualityProcessor**

`quality_processor.h`: replace the stale class doc comment (lines 10-18 still name Clouds/CleanLoFi/Tape) with current behavior per mode; constants block becomes:

```cpp
    // Anti-alias input LPs must sit below each mode's decimated Nyquist
    // (all lo-fi modes are 2x -> Nyquist 12 kHz); output LPs are tonal
    // shaping at host rate. Bit-depth character lives in the recording
    // buffer's storage codec, not here.
    static constexpr float kColdDigitalInputLpHz = 10000.0f;
    static constexpr float kSunnyTapeInputLpHz   = 10000.0f;
    static constexpr float kSunnyTapeOutputLpHz  = 10000.0f;  // tone (bright tape)
    static constexpr float kScorchedInputLpHz    = 10000.0f;
    static constexpr float kScorchedOutputLpHz   = 5000.0f;   // cassette tone (dark)
```

Delete `kQuantScale`.

`quality_processor.cpp` — `ProcessInput` Scorched branch (stereo, per-channel hiss, no companding, no mono sum):

```cpp
        case QualityMode::kScorchedCassette: {
            // Dark cassette: filtered stereo + independent per-channel hiss.
            // No mono sum (channel count follows the input jacks) and no
            // companding (the recording buffer stores real 8-bit mu-law).
            float hiss_l = noise_gen_.NextBipolar() * kTapeHissLevel;
            float hiss_r = noise_gen_.NextBipolar() * kTapeHissLevel;
            result = { filtered_l + hiss_l, filtered_r + hiss_r };
            break;
        }
```

`ProcessOutput`: Cold branch → `result = input;` (comment: quantization lives in the storage codec); Scorched branch → `result = { lp_l, lp_r };` (comment: µ-law decode already happened in the buffer's read path). Update the Sunny `ProcessInput` comment ("anti-alias for 2x decimation").

`saturation.cpp` Scorched comment (behavior unchanged, comment stale):

```cpp
        case QualityMode::kScorchedCassette:
            // Hard clip at +/-1: the storage codec clamps on write anyway,
            // so clipping here just bounds the feedback sum pre-encoder.
            return HardClip(input, 1.0f);
```

- [ ] **Step 5: Run both suites, commit**

Expected: PASS both.

```bash
git add src/particules/dsp/include/particules_dsp/types.h src/particules/dsp/src/quality/ src/particules/dsp/src/fx/saturation.cpp tests/particules_dsp/test_quality_modes.cpp tests/retours_delay_dsp/test_quality_modes.cpp
git commit -m "Quality: per-mode storage config table, retune LPs, drop fake degradation"
```

---

### Task 6: Q32.32 grain positions + KillAllGrains

**Files:**
- Modify: `src/particules/dsp/src/grain/grain.h`, `grain.cpp`
- Modify: `src/particules/dsp/src/grain/grain_engine.h`, `grain_engine.cpp`
- Test: existing `tests/particules_dsp/test_grain.cpp` + `test_grain_kill.cpp` suites (must stay green); new precision case below.

**Interfaces:**
- Consumes: `ReadHermiteStereoFrac(size_t, float, float*, float*)` (Task 2).
- Produces: `void GrainEngine::KillAllGrains()`. `Grain::GrainParameters` unchanged (float `position`/`pitch_ratio`); conversion to Q32.32 happens once in `Grain::Start`.

- [ ] **Step 1: Write the failing precision test**

Append to `tests/particules_dsp/test_grain.cpp`:

```cpp
TEST_CASE("Grain: playback phase is exact at large buffer positions", "[grain][precision]") {
    // A float32 position at frame ~700k quantizes to 1/16 sample; the Q32.32
    // accumulator must not. Buffer sized like Scorched-stereo (768k frames):
    // use mu-law to keep the test's memory footprint at the production pool.
    size_t pool_frames = 192000;
    size_t bytes = (pool_frames + kInterpolationTail) * 2 * sizeof(float);
    std::vector<uint8_t> mem(bytes, 0);
    RecordingBuffer buf;
    buf.Init(reinterpret_cast<float*>(mem.data()), pool_frames, 2);
    buf.Configure(1, StorageFormat::kMuLaw8, 2);   // 768012 frames
    buf.ImmediateClear();

    // Write a slow ramp near the far end so interpolated reads are smooth
    // and any position quantization shows as repeated/stepped values.
    // (Fill via the write head: seek by writing zeros.)
    size_t target = 700000;
    for (size_t i = 0; i < target; ++i) buf.Write(0.0f, 0.0f);
    for (size_t i = 0; i < 2000; ++i) {
        float v = 0.9f * std::sin(2.0 * M_PI * static_cast<double>(i) / 400.0);
        buf.Write(v, v);
    }

    Grain g;
    g.Init();
    Grain::GrainParameters gp{};
    gp.position = static_cast<float>(target);
    gp.pitch_ratio = 0.30078125f;   // deliberately non-dyadic step
    gp.size = 1500.0f;
    gp.shape = 0.5f;
    gp.pan = 0.0f;
    gp.gain = 1.0f;
    gp.pre_delay = 0;
    g.Start(gp);

    // Advance ~1200 samples; consecutive read positions must differ by the
    // exact ratio: with float32 positions at 700k, steps collapse to
    // multiples of 1/16 and repeated samples appear. Detect via successive
    // output values: on a smooth ramp region, output must be strictly
    // advancing (no more than 2 consecutive identical samples).
    float buf_size_f = static_cast<float>(buf.size());
    int max_repeats = 0, repeats = 0;
    float prev = -2.0f;
    for (int i = 0; i < 1200; ++i) {
        float l, r;
        g.Process(buf, buf_size_f, &l, &r);
        if (l == prev) { repeats++; max_repeats = std::max(max_repeats, repeats); }
        else repeats = 0;
        prev = l;
    }
    REQUIRE(max_repeats <= 2);
}
```

(Adjust the `Process` call signature in this test to whatever Task 6 Step 3 lands on — the test is written against the post-change API below.)

- [ ] **Step 2: Run to verify it fails**

Run: `cd tests/particules_dsp && ./run.sh`
Expected: FAIL — with float positions at frame 700k, quantized steps produce runs of identical output samples (or the test fails to compile against the old API, which is the same signal).

- [ ] **Step 3: Convert Grain to Q32.32**

`grain.h` — replace `float read_position_; float phase_increment_;` with:

```cpp
    // Q32.32 fixed-point playback position/step (frames). Constant
    // interpolation precision at any buffer size, and integer wrap math —
    // unlike a float32 position, which quantizes to 1/16 sample at ~1M
    // frames, or doubles, which are scalar-only on Cortex-A7 VFP.
    int64_t position_q_ = 0;
    int64_t increment_q_ = 0;
    static constexpr double kQ32One = 4294967296.0;   // 2^32
```

`Grain::Start` (grain.cpp) — one-time conversion (positions ≤ 1.5M frames × 2^32 < 2^53, exact in double; inputs already `isfinite`-guarded in `ComputeGrainParams`):

```cpp
    position_q_ = static_cast<int64_t>(static_cast<double>(params.position) * kQ32One);
    increment_q_ = static_cast<int64_t>(static_cast<double>(params.pitch_ratio) * kQ32One);
```

`Grain::Process` hot loop (grain.h) — replace the float read/advance/wrap block; the function's `buf_size` parameter changes from `float` to `int64_t buf_size_q` (callers pass `static_cast<int64_t>(buffer.size()) << 32`):

```cpp
        // Read from recording buffer with Hermite interpolation.
        size_t i0 = static_cast<size_t>(position_q_ >> 32);
        float frac = static_cast<float>(static_cast<uint32_t>(position_q_))
                     * (1.0f / 4294967296.0f);
        float sample_l, sample_r;
        buffer.ReadHermiteStereoFrac(i0, frac, &sample_l, &sample_r);

        // Advance and wrap (integer compare/subtract; position stays in
        // [0, size) so the >>32 above is always non-negative).
        position_q_ += increment_q_;
        if (buf_size_q > 0) {
            if (position_q_ >= buf_size_q) position_q_ -= buf_size_q;
            if (position_q_ < 0) position_q_ += buf_size_q;
        }
```

`Grain::ProcessBlock` signature: `float buf_size_f` → `int64_t buf_size_q`, passed through. Update `GrainEngine::Process` (grain_engine.cpp:310-316):

```cpp
    int64_t buf_size_q = static_cast<int64_t>(buffer_->size()) << 32;
    for (int g = 0; g < kMaxGrains; ++g) {
        if (!grains_[g].active()) continue;
        ++active_count;
        grains_[g].ProcessBlock(*buffer_, buf_size_q, output, num_frames);
    }
```

Note: a single `increment_q_` step never exceeds the wrap window (pitch ratios are bounded far below buffer sizes), so the single-conditional wrap is sufficient — same assumption the old float while-loops relied on, now enforced by construction.

- [ ] **Step 4: Add KillAllGrains**

`grain_engine.h` (public):

```cpp
    // Deactivate every grain immediately (no fade) and invalidate the
    // grain-duration cache. Call at a config-change apply point, where the
    // wet output is muted (the hard cut is inaudible) and stale grain read
    // positions would be invalid after the buffer resize.
    void KillAllGrains();
```

`grain_engine.cpp`:

```cpp
void GrainEngine::KillAllGrains() {
    for (int i = 0; i < kMaxGrains; ++i) {
        grains_[i].Init();
    }
    overlap_count_lp_ = 0.0f;
    // Buffer size may change at the same decimation (Cold/Sunny/Scorched are
    // all /2): force the max-active/duration cache to recompute.
    cached_decimation_ = -1;
}
```

- [ ] **Step 5: Run the full particules suite**

Run: `cd tests/particules_dsp && ./run.sh`
Expected: PASS — all existing grain/kill/click/NaN tests green (the NaN-robustness suite exercises the `isfinite` fences that protect the Q32.32 casts), plus the new precision case.

- [ ] **Step 6: Commit**

```bash
git add src/particules/dsp/src/grain/ tests/particules_dsp/test_grain.cpp
git commit -m "Grain: Q32.32 playback positions, KillAllGrains for buffer reconfigs"
```

---

### Task 7: Particules transition state machine + mono detection

**Files:**
- Modify: `src/particules/dsp/include/particules_dsp/parameters.h` (`bool mono_input = false;`)
- Modify: `src/particules/dsp/src/particules_processor.h`, `particules_processor.cpp`
- Modify: `src/particules/Particules.cpp` (set `params_.mono_input`)
- Create: `tests/particules_dsp/test_quality_transition.cpp` (register in `CMakeLists.txt`)

**Interfaces:**
- Consumes: `Configure`, `ClearPending`, `QualityConfigFor`, `KillAllGrains` (Tasks 2, 3, 5, 6).
- Produces: no processor API change. New param: `mono_input` (true when the R input jack is unpatched — same rule the adapter already uses to normalize R←L).

- [ ] **Step 1: Write the failing tests**

Create `tests/particules_dsp/test_quality_transition.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <vector>

#include "particules_dsp/particules_dsp.h"
#include "particules_dsp/types.h"
#include "particules_dsp/parameters.h"

using namespace particules_dsp;

namespace {

constexpr float kSr = 48000.0f;

struct Proc {
    std::vector<uint8_t> mem;
    ParticulesProcessor p;
    Proc() {
        auto req = ParticulesProcessor::GetMemoryRequirements(kSr);
        mem.resize(req.total_bytes + req.alignment, 0);
        p.Init(mem.data(), mem.size(), kSr);
    }
};

struct RunResult { float peak; bool finite; };
RunResult Run(ParticulesProcessor& p, ParticulesParameters& params,
              int blocks, double& phase) {
    RunResult r{0.0f, true};
    StereoFrame in[64], out[64];
    for (int b = 0; b < blocks; ++b) {
        for (int i = 0; i < 64; ++i) {
            float v = 0.5f * static_cast<float>(std::sin(phase));
            phase += 2.0 * M_PI * 220.0 / kSr;
            in[i] = {v, v};
        }
        p.SetParameters(params);
        p.Process(in, out, 64);
        for (int i = 0; i < 64; ++i) {
            if (!std::isfinite(out[i].l) || !std::isfinite(out[i].r)) r.finite = false;
            r.peak = std::max(r.peak, std::max(std::fabs(out[i].l), std::fabs(out[i].r)));
        }
    }
    return r;
}

ParticulesParameters WetParams() {
    ParticulesParameters params{};
    params.dry_wet = 1.0f;
    params.density = 0.8f;
    params.size = 0.0f;
    params.quality_mode = QualityMode::kBrightDigital;
    return params;
}

}  // namespace

TEST_CASE("QualityTransition: Bright->Scorched stays finite and recovers", "[transition]") {
    Proc proc;
    auto params = WetParams();
    double phase = 0.0;

    auto settled = Run(proc.p, params, 800, phase);   // past grain startup ramp
    REQUIRE(settled.finite);
    REQUIRE(settled.peak > 0.01f);

    params.quality_mode = QualityMode::kScorchedCassette;
    auto transition = Run(proc.p, params, 300, phase);   // ~400 ms: full transition
    REQUIRE(transition.finite);

    auto after = Run(proc.p, params, 1500, phase);
    REQUIRE(after.finite);
    REQUIRE(after.peak > 0.01f);
}

TEST_CASE("QualityTransition: Scorched->Bright shrink is safe", "[transition]") {
    Proc proc;
    auto params = WetParams();
    params.quality_mode = QualityMode::kScorchedCassette;
    params.time = 0.9f;   // grains read deep into the long buffer
    double phase = 0.0;

    auto scorched = Run(proc.p, params, 1500, phase);
    REQUIRE(scorched.finite);

    params.quality_mode = QualityMode::kBrightDigital;   // 768k -> 192k frames
    auto back = Run(proc.p, params, 1500, phase);
    REQUIRE(back.finite);
    REQUIRE(back.peak > 0.01f);
}

TEST_CASE("QualityTransition: mono/stereo input change reconfigures safely", "[transition][mono]") {
    Proc proc;
    auto params = WetParams();
    params.quality_mode = QualityMode::kScorchedCassette;
    double phase = 0.0;
    Run(proc.p, params, 1200, phase);

    params.mono_input = true;    // "cable unplugged": 768k -> 1.5M mono frames
    auto mono = Run(proc.p, params, 1500, phase);
    REQUIRE(mono.finite);
    REQUIRE(mono.peak > 0.01f);

    params.mono_input = false;   // re-patched
    auto stereo = Run(proc.p, params, 1500, phase);
    REQUIRE(stereo.finite);
    REQUIRE(stereo.peak > 0.01f);
}

TEST_CASE("QualityTransition: rapid mode cycling stays finite", "[transition]") {
    Proc proc;
    auto params = WetParams();
    double phase = 0.0;
    Run(proc.p, params, 200, phase);
    const QualityMode cycle[] = {
        QualityMode::kColdDigital, QualityMode::kScorchedCassette,
        QualityMode::kSunnyTape, QualityMode::kBrightDigital,
        QualityMode::kScorchedCassette, QualityMode::kColdDigital};
    for (QualityMode m : cycle) {
        params.quality_mode = m;
        auto r = Run(proc.p, params, 20, phase);   // re-flip mid-transition
        REQUIRE(r.finite);
    }
    params.quality_mode = QualityMode::kBrightDigital;
    auto r = Run(proc.p, params, 2000, phase);
    REQUIRE(r.finite);
    REQUIRE(r.peak > 0.01f);
}

TEST_CASE("QualityTransition: change while frozen is deferred", "[transition][freeze]") {
    Proc proc;
    auto params = WetParams();
    double phase = 0.0;
    Run(proc.p, params, 800, phase);          // record content in Bright

    params.freeze = true;
    Run(proc.p, params, 100, phase);

    params.quality_mode = QualityMode::kScorchedCassette;
    auto frozen = Run(proc.p, params, 300, phase);
    REQUIRE(frozen.finite);
    REQUIRE(frozen.peak > 0.01f);             // no transition mute while frozen

    params.freeze = false;
    auto after = Run(proc.p, params, 2000, phase);
    REQUIRE(after.finite);
    REQUIRE(after.peak > 0.01f);
}
```

- [ ] **Step 2: Run to verify failure**

Run: `cd tests/particules_dsp && ./run.sh`
Expected: FAIL — `mono_input` undeclared; freeze-deferral fails against current immediate-apply behavior.

- [ ] **Step 3: Add the param and rework the Impl**

`parameters.h`: add to `ParticulesParameters`:

```cpp
    // True when the input is effectively mono (R jack unpatched). Drives the
    // recording buffer's channel count: mono doubles buffer duration.
    bool mono_input = false;
```

`particules_processor.h` — replace the quality-transition block (lines 46-51):

```cpp
    // Config transition (quality mode and/or input channel count): fade the
    // wet path out, reconfigure + clear the buffer (a layout change makes old
    // pool bytes garbage), hold muted until the deferred clear drains, then
    // fade back in. See docs/superpowers/plans/2026-07-20-quality-buffer-decoupling.md.
    enum class QualityTransition : uint8_t { kIdle, kFadeOut, kClearing, kFadeIn };
    QualityTransition qt_state = QualityTransition::kIdle;
    QualityMode active_quality = QualityMode::kBrightDigital;
    QualityMode pending_quality = QualityMode::kBrightDigital;
    bool active_mono = false;
    bool pending_mono = false;
    int qt_fade_counter = 0;
    static constexpr int kQualityFadeSamples = 2048;   // ~43 ms at 48 kHz
```

Delete `prev_quality_mode`, `kQualityXfadeSamples`, `quality_xfade_counter`.

- [ ] **Step 4: Rework the processor**

`particules_processor.cpp`:

1. `Init()` (lines 64-66): replace `SetDecimationFactor(...)` with:

```cpp
    auto cfg = QualityConfigFor(QualityMode::kBrightDigital);
    impl_->recording_buffer.Configure(cfg.decimation, cfg.format, 2, cfg.max_bytes);
```

2. `SetParameters()`: **delete** the quality-change block (lines 109-116); switch the reverb-LP `switch` (line 130) to `impl_->active_quality`.

3. `ProcessBlock()`: every sub-processor call taking `s.params.quality_mode` (`ProcessInput` :190, `LimitFeedback` :209, `GetPitchModulation` :223, `ProcessOutput` :266) now takes `s.active_quality`.

4. After `TickClear` + freeze handling, the transition driver:

```cpp
    // --- Config transition state machine (quality and/or channel count) ---
    if (s.qt_state == Impl::QualityTransition::kIdle &&
        (s.params.quality_mode != s.active_quality ||
         s.params.mono_input != s.active_mono) &&
        !s.params.freeze) {
        s.pending_quality = s.params.quality_mode;
        s.pending_mono = s.params.mono_input;
        s.qt_state = Impl::QualityTransition::kFadeOut;
        s.qt_fade_counter = Impl::kQualityFadeSamples;
    }
    if (s.qt_state == Impl::QualityTransition::kClearing &&
        !s.recording_buffer.ClearPending()) {
        s.qt_state = Impl::QualityTransition::kFadeIn;
        s.qt_fade_counter = Impl::kQualityFadeSamples;
    }
```

5. Replace the V-duck block in the output loop (lines 250-263):

```cpp
        // Config transition: wet gain per state.
        float qt_gain = 1.0f;
        switch (s.qt_state) {
            case Impl::QualityTransition::kIdle:
                break;
            case Impl::QualityTransition::kFadeOut:
                qt_gain = static_cast<float>(s.qt_fade_counter)
                        / static_cast<float>(Impl::kQualityFadeSamples);
                if (--s.qt_fade_counter <= 0) {
                    // Apply point: wet is fully muted here.
                    s.active_quality = s.pending_quality;
                    s.active_mono = s.pending_mono;
                    s.grain_engine.KillAllGrains();
                    auto cfg = QualityConfigFor(s.active_quality);
                    s.recording_buffer.Configure(cfg.decimation, cfg.format,
                                                 s.active_mono ? 1 : 2,
                                                 cfg.max_bytes);
                    s.recording_buffer.Clear();
                    s.qt_state = Impl::QualityTransition::kClearing;
                }
                break;
            case Impl::QualityTransition::kClearing:
                qt_gain = 0.0f;
                break;
            case Impl::QualityTransition::kFadeIn:
                qt_gain = 1.0f - static_cast<float>(s.qt_fade_counter)
                                / static_cast<float>(Impl::kQualityFadeSamples);
                if (--s.qt_fade_counter <= 0) {
                    s.qt_state = Impl::QualityTransition::kIdle;
                }
                break;
        }
        wet_frame *= qt_gain;
```

6. `src/particules/Particules.cpp`: where `params_` is populated (near the other `*_connected` assignments), add `params_.mono_input = !in_r_connected;` using the same `in_r_connected` the input stage already computes (line \~437). On both `#ifdef METAMODULE` and desktop paths.

- [ ] **Step 5: Run the full particules suite**

Run: `cd tests/particules_dsp && ./run.sh`
Expected: PASS. Watch: `test_silence.cpp` "skip quality duck" windows (transition now up to \~12.5k samples — widen skips to 20,000 samples if needed); `test_processor.cpp:203` rapid cycling must stay green.

- [ ] **Step 6: Commit**

```bash
git add src/particules/dsp/include/particules_dsp/parameters.h src/particules/dsp/src/particules_processor.h src/particules/dsp/src/particules_processor.cpp src/particules/Particules.cpp tests/particules_dsp/
git commit -m "Particules: fade-clear-fade transitions for quality and mono/stereo input"
```

---

### Task 8: Retours transition state machine + mono detection

**Files:**
- Modify: Retours' parameter struct (`bool mono_input = false;` — same comment as Particules')
- Modify: `src/retours_delay/dsp/src/retours_processor.h`, `retours_processor.cpp`
- Modify: `src/retours_delay/Retours.cpp` (set `params_.mono_input` from its existing `in_r_connected`, line \~353)
- Modify: `tests/retours_delay_dsp/test_hardening.cpp`, `test_quality_modes.cpp`

**Interfaces:**
- Consumes: `Configure`, `ClearPending`, `QualityConfigFor`, `FramesForConfig` (Tasks 2, 3, 5). `EchoEngine` untouched (reads are wrap-guarded).

- [ ] **Step 1: Update test helpers to the new capacity math**

`test_hardening.cpp` — replace `ExpectedBaseSeconds` (line \~52):

```cpp
float ExpectedBaseSeconds(float density, QualityMode quality, float sr) {
    auto cfg = particules_dsp::QualityConfigFor(quality);
    size_t capacity_bytes =
        (kBufferFrames + particules_dsp::kInterpolationTail) * 2 * sizeof(float);
    size_t frames = particules_dsp::RecordingBuffer::FramesForConfig(
        capacity_bytes, /*channels=*/2, cfg.format, cfg.max_bytes);
    float buffer_seconds = static_cast<float>(frames) *
                           static_cast<float>(cfg.decimation) / sr;
    float buffer_samples = sr * buffer_seconds;
    float d = std::clamp(std::fabs(density - 0.5f) * 2.f, 0.f, 1.f);
    float base = buffer_samples * std::exp2(-kManualOctaves * d);
    float min_samples = kMinDelaySeconds * sr;
    return std::clamp(base, min_samples, buffer_samples) / sr;
}
```

(Include `buffer/recording_buffer.h` if needed.)

`test_quality_modes.cpp`: widen the mid-stream-switch (\~195) and frozen-deferral (\~367) settling windows — the transition takes `2048 + ~8192 + 2048` samples after its start conditions; process ≥ 20,000 extra samples before post-switch assertions. Keep the deferral-*condition* assertions verbatim. Add:

```cpp
TEST_CASE("quality: Scorched delay capacity is ~32 s after transition") {
    // Copy the corner-stress harness from test_hardening.cpp (same processor
    // setup and observable). Request kScorchedCassette, process >= 30k
    // samples (full transition), then REQUIRE BaseSeconds() within 1% of
    // ExpectedBaseSeconds(0.5f, QualityMode::kScorchedCassette, 48000.f)
    // — which now evaluates to ~32 s (768012 frames * 2 / 48000).
}
```

(Fill the body from the existing harness; the observable and threshold are exact as stated.)

- [ ] **Step 2: Run to verify failure**

Run: `cd tests/retours_delay_dsp && ./run.sh`
Expected: FAIL — expected Scorched capacity is \~32 s; live processor still float32 ÷2 → 8 s.

- [ ] **Step 3: Rework the Retours Impl and processor**

`retours_processor.h` — replace `prev_quality`/`quality_xfade_counter`/`kQualityXfadeSamples` with the same state block as Particules Task 7 Step 3 (`QualityTransition`, `active_quality`, `pending_quality`, `active_mono`, `pending_mono`, `qt_fade_counter`, `kQualityFadeSamples = 2048`).

`retours_processor.cpp`:

1. Replace the quality-change block (lines 157-179), **keeping the freeze guards verbatim** (regression-tested):

```cpp
    // Config transition (quality and/or channel count): fade wet out,
    // reconfigure + clear the buffer, hold muted until the clear drains,
    // fade back in. Starting a transition is ignored while frozen
    // (clearing/reformatting under a frozen slice would corrupt it) and on
    // the freeze falling edge (EchoEngine's unfreeze continuity math needs
    // this block's pre-clear write head); the kIdle re-check picks the
    // change up one block later.
    bool freeze_falling_edge = s.prev_freeze && !s.params.freeze;
    if (s.qt_state == Impl::QualityTransition::kIdle &&
        (s.params.quality != s.active_quality ||
         s.params.mono_input != s.active_mono) &&
        !s.params.freeze && !freeze_falling_edge) {
        s.pending_quality = s.params.quality;
        s.pending_mono = s.params.mono_input;
        s.qt_state = Impl::QualityTransition::kFadeOut;
        s.qt_fade_counter = Impl::kQualityFadeSamples;
    }
    if (s.qt_state == Impl::QualityTransition::kClearing &&
        !s.recording_buffer.ClearPending()) {
        s.qt_state = Impl::QualityTransition::kFadeIn;
        s.qt_fade_counter = Impl::kQualityFadeSamples;
    }
```

2. All DSP uses of `s.params.quality` (`GetPitchModulation` :255, `ProcessOutput` :288, `LimitFeedback` :300, `ProcessInput` further down) → `s.active_quality`. Delete `prev_quality`.

3. Guard the buffer read during the garbage window (gotcha 1):

```cpp
        // During kClearing the pool bytes are garbage under the new layout
        // (float32 garbage can be NaN, and NaN survives a x0 duck), so skip
        // the read entirely and feed the shifter silence.
        StereoFrame raw_wet =
            (s.qt_state == Impl::QualityTransition::kClearing)
                ? StereoFrame{0.f, 0.f}
                : s.engine.ReadWet();
        StereoFrame wet = s.shifter.Process(raw_wet);
```

4. Replace the V-duck (lines \~273-283) with the same `qt_gain` switch as Particules Task 7 Step 4.5, with the Retours apply point:

```cpp
                if (--s.qt_fade_counter <= 0) {
                    s.active_quality = s.pending_quality;
                    s.active_mono = s.pending_mono;
                    auto cfg = particules_dsp::QualityConfigFor(s.active_quality);
                    s.recording_buffer.Configure(cfg.decimation, cfg.format,
                                                 s.active_mono ? 1 : 2,
                                                 cfg.max_bytes);
                    s.recording_buffer.Clear();
                    // DENSITY's manual-mode delay scales with the effective
                    // buffer duration — from the LIVE frame count, not the
                    // float32 constant (packed formats hold more).
                    float effective_seconds =
                        static_cast<float>(s.recording_buffer.size()) *
                        static_cast<float>(cfg.decimation) / s.sample_rate;
                    s.base_time.SetBufferSeconds(effective_seconds);
                    s.qt_state = Impl::QualityTransition::kClearing;
                }
```

5. `Init`: replace `SetDecimationFactor` (lines 59-63) with `Configure` from `QualityConfigFor(kBrightDigital)`, channels 2.

6. `src/retours_delay/Retours.cpp`: set `params_.mono_input = !in_r_connected;` from its existing connection check (line \~353), both build paths.

- [ ] **Step 4: Run both suites**

Run: `cd tests/retours_delay_dsp && ./run.sh && cd ../particules_dsp && ./run.sh`
Expected: PASS, including the frozen-deferral regression, the \~32 s capacity case, and `test_freeze_slicer.cpp` (float32-stereo path untouched).

- [ ] **Step 5: Commit**

```bash
git add src/retours_delay/ tests/retours_delay_dsp/
git commit -m "Retours: fade-clear-fade transitions, live buffer capacity, mono input"
```

---

### Task 9: Documentation

**Files:**
- Modify: `Particules.md` (lines 71-81), `Retours.md` (lines 57-67)
- Modify: `docs/scorched-cassette-quality-analysis.md`
- Modify: `CHANGELOG.md` (only if present at repo root)

- [ ] **Step 1: Particules manual**

Replace `Particules.md` lines 73-79 with:

```markdown
*Bright digital* (white LED): full rate (48 kHz at 48 kHz), 16-bit or better — cleanest and brightest. 4-second buffer.

*Cold digital* (cyan LED): rate ÷ 2 (24 kHz at 48 kHz), 12-bit — the classic Mutable *Clouds* grain. 8-second buffer.

*Sunny tape* (amber LED): rate ÷ 2 (24 kHz at 48 kHz), 12-bit, gentle wow — warm tape. 16-second buffer.

*Scorched cassette* (magenta LED): rate ÷ 2 (24 kHz at 48 kHz), true 8-bit µ-law, tape hiss, wow and flutter — crunchy lo-fi. 32-second buffer.
```

Update the intro paragraph (line 71): lower-fidelity modes store samples at reduced bit width, which is what buys the longer buffer (rate and length are independent, as on hardware Beads). After the mode list (near line 81), add:

```markdown
All buffer lengths double when the input is mono (nothing patched into IN R): 8, 16, 32, and 64 seconds respectively. Patching or unpatching IN R re-formats the recording buffer, briefly muting the wet signal and clearing recorded audio.
```

- [ ] **Step 2: Retours manual**

Replace `Retours.md` lines 59-65 with the same four modes (delay framing), and update line 67's buffer note with the same mono-doubling sentence (delay times reach \~64 s with mono input on Scorched). Line 33's "up to about 4 seconds in the cleanest Quality mode" stays true.

- [ ] **Step 3: Close out the analysis doc**

`docs/scorched-cassette-quality-analysis.md`: update `Status:`, append "Resolution (2026-07-20)": decoupling implemented for all four modes via packed storage formats + input-adaptive channel count; note the hardware manual's mono/stereo buffer table (mono doubles duration) which the fidelity-fix design doc's single "32 s" figure had elided; link this plan; Findings 1 and 3 resolved.

- [ ] **Step 4: Changelog (if present)**

Unreleased entry: "Particules/Retours: recording buffer now packs samples at true bit width and channel count — Sunny/Scorched run at 24 kHz with real 12-bit/8-bit µ-law storage; mono input doubles buffer length (up to 64 s)."

- [ ] **Step 5: Commit**

```bash
git add Particules.md Retours.md docs/ CHANGELOG.md
git commit -m "Docs: quality tables reflect packed-storage buffers and mono doubling"
```

---

### Task 10: Verification (tests, builds, on-device performance)

**Files:** none; build + measurement evidence only.

- [ ] **Step 1: Full test suites, clean**

Run: `cd tests/particules_dsp && ./run.sh && cd ../retours_delay_dsp && ./run.sh`
Expected: PASS, zero failures. Paste summary lines into the task report.

- [ ] **Step 2: VCV build**

Per `build-robotboy-plugin` skill (`make -C vcv` + manual copy). Expected: clean compile, no new warnings in `particules`/`retours_delay` sources.

- [ ] **Step 3: Headless audio sanity**

Per `test-vcv-module-headless` skill: render broadband input through Particules, Quality = Scorched, `dry_wet = 1`; confirm output spectrum has energy in the 3–5 kHz band (old build's 2.5 kHz input LP left none). This is the audible acceptance criterion for the original complaint.

- [ ] **Step 4: MetaModule build**

Per `build-robotboy-plugin` skill (cmake). Expected: compiles; no threadsafe-statics warnings around `MuLaw8DecodeTable`; no double-promotion warnings in grain hot paths (spot-check with `-Wdouble-promotion` locally if in doubt).

- [ ] **Step 5: On-device performance comparison (user-run)**

The perf estimates in this plan are instruction-count analysis, not measurements. Hand the user this A/B procedure with the two `.mmplugin` builds (current main vs this branch):

1. Same stress patch both runs: Particules, Quality = Scorched, Density max, Size small, Dry/Wet full wet, audio patched into both inputs; note the MetaModule CPU meter reading for the module.
2. Repeat with Sunny, and with mono input (one cable).
3. Retours: Scorched, high feedback (self-oscillating), multi-tap on; note CPU.
4. Acceptance: new build within \~+5% of old on each patch (expected: at or below old). If it regresses beyond that, file the measurement — the plan's escalation mitigations (format-specialized grain loops) are the next step.

- [ ] **Step 6: User listening checklist (user-run, with the report)**

1. Scorched: noticeably brighter than before (speech/hats intelligible), crunchy 8-bit texture, hiss + wow intact, true stereo with two inputs.
2. Scorched TIME sweep: grains reachable \~32 s back (stereo) / \~64 s (mono) after recording that long.
3. Sunny: brighter (24 kHz), subtler grit than Scorched.
4. Cold: 12-bit character now baked into the recording; feedback grows grittier per pass.
5. Mode switches and IN R patch/unpatch: brief fade-mute-fade (\~¼ s), no clicks, no garbage bursts, both modules.
6. Freeze during Scorched; flip Quality while frozen → defers until unfreeze.
7. Deep-buffer pitched grains (TIME far right, PITCH +12, 32/64 s buffer): no warble/graininess beyond the intended lo-fi (validates Q32.32 precision).
8. Retours: Scorched Interval reaches very long delays; runaway feedback degrades musically.

---

## Out of scope / follow-ups

- **True 12-bit packing** (two samples per 3 bytes) for Sunny/Cold — would match hardware's 10/20 s Sunny exactly but ruins simple hot-path indexing. Rejected.
- **Cold at 32 kHz** — impossible with integer decimation of a 48 kHz host.
- **Format-specialized grain render loops** — escalation mitigation only, pending device measurements (Task 10 Step 5).
- **VCV patch migration** — none needed: quality is an ordinary param; `mono_input` derives from cables; buffer contents are never serialized.
- **MM param_ranges.json** — no param-range changes.

## Self-review notes

- Spec coverage: analysis-doc Findings 1 (manual tables, Task 9), 3 (real 8-bit, Tasks 1-2), 4 (decoupling, Tasks 2-8); user directives — all four modes (Task 5 table), input-adaptive mono/stereo (Tasks 2, 7, 8), 8/16/32/64 s with precision fix (Tasks 2, 6), MetaModule performance analysis with mitigations and estimates (analysis section; codec + Q32.32 choices; Task 10 Step 5 measurement).
- Type consistency: `Configure(int, StorageFormat, int, size_t)`, `FramesForConfig(size_t, int, StorageFormat, size_t)`, `ReadHermiteStereoFrac(size_t, float, float*, float*)`, `ClearPending()`, `KillAllGrains()`, `QualityConfigFor(QualityMode) -> {decimation, format, max_bytes}` used identically across Tasks 2/3/5/6/7/8.
- Placeholder scan: Task 8 Step 1's new Retours case body is a copy-instruction from an existing harness in the same suite with the observable and threshold specified exactly; all other steps carry complete code.

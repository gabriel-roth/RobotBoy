# Scorched cassette quality mode — analysis

- **Date:** 2026-07-20
- **Module:** Particules (shared `particules_dsp` quality engine; Retours aliases the same enum)
- **Status:** Decoupling implemented (all modes). See Resolution section below.

## The complaint

On **Scorched cassette**, the sound comes out very dark/dull ("low-pitched"), hissy, and
not musical. This document explains what is actually happening and what it would take to
fix it while honoring the hardware Beads behavior.

*The findings below are historical — file/line references and filter figures reflect the code as it stood on 2026-07-20, before the Resolution. See the Resolution section for what shipped.*

## Finding 1 — the code and the Particules manual are inverted; the manual is stale

`Particules.md:77-79` documents:

- *Sunny tape* — rate ÷ 8 (6 kHz at 48 kHz), 12-bit
- *Scorched cassette* — rate ÷ 4 (12 kHz at 48 kHz), 8-bit

The code (`src/particules/dsp/include/particules_dsp/types.h:39-45`) does the opposite:

- Sunny tape — decimation ÷ 4 (12 kHz)
- Scorched cassette — decimation ÷ 8 (6 kHz)

They are swapped. The manual was last edited 2026-07-10 (commit `d79fcee`). A later
"fidelity fix" on 2026-07-16 (commit `8749048`) deliberately swapped Sunny and Scorched so
that degradation increases monotonically Bright → Cold → Sunny → Scorched — matching the
real Mutable Beads ladder, where Scorched cassette is the *most* degraded mode with the
longest (32 s) buffer. The manual was never updated to follow that change.

So the heavy, dark sound is what the current code *intends*. It is the manual that is out
of date, not the code that is buggy on this axis.

Reference: `docs/superpowers/specs/2026-07-16-quality-mode-fidelity-fix-design.md`.

## Finding 2 — it is not actually low-pitched, it is very dark

Decimation is pitch-compensated in the grain engine
(`src/particules/dsp/src/grain/grain_engine.cpp:148`, `pitch_ratio ... / df_f`), so Scorched
plays back at the correct pitch. What reads as "low-pitched" is aggressive filtering:

- input anti-alias low-pass at **2500 Hz**
- output tone low-pass at **5000 Hz**

(`src/particules/dsp/src/quality/quality_processor.h:71-72`), plus a mono sum and a 6 kHz
effective rate. Nothing above \~2.5 kHz survives, so the mode sounds muffled and dull — which
is easy to mistake for a drop in pitch.

## Finding 3 — the "8-bit" bit reduction is not actually implemented

Only **Cold digital** does real quantization: 12-bit, `std::round(x * 2048) / 2048`
(`src/particules/dsp/src/quality/quality_processor.cpp:151`).

**Scorched does not quantize at all.** It mu-law *compresses* on input (`:107`) and mu-law
*expands* on output (`:170`) with **nothing in between** — a companding pair with no
quantizer is essentially lossless (identity, minus the low-pass done in the companded domain
and float rounding). Real µ-law "8-bit" grit comes from quantizing the companded signal to
256 steps; that step is missing.

Consequently the "noisy" character is **tape hiss** (`kTapeHissLevel = 0.00025`, added every
sample at `:104`) plus the dark filtering — **not** bit reduction. The manual's promised
8-bit grit is entirely absent from the DSP.

## Finding 4 — why hardware Beads can do 24 kHz *and* 32 s, but this engine cannot

Per the fidelity-fix design doc's reading of the Beads manual (a secondary source; the
primary hardware manual was not consulted directly):

| Beads mode        | rate   | bits          | buffer |
|-------------------|--------|---------------|--------|
| Bright digital    | 48 kHz | 16-bit        | 4 s    |
| Cold digital      | 32 kHz | 12-bit        | 8 s    |
| Sunny tape        | 24 kHz | 12-bit        | \~11 s |
| Scorched cassette | 24 kHz | 8-bit µ-law   | 32 s   |

On hardware, **Sunny and Scorched are both 24 kHz** — they differ in bit depth and buffer
length, not sample rate.

### Hardware: two independent knobs

Hardware has a **fixed pool of memory (bytes)** and packs samples at their true bit width:

```
time = memory_bytes / (sample_rate × bytes_per_sample × channels)
```

Sample rate and bit depth are independent. Dropping 12-bit → 8-bit makes each sample
physically smaller, so more samples fit in the same memory → **longer recording time at the
same rate**. That is exactly how Scorched reaches 32 s while staying at 24 kHz.

### This engine: one knob

Our recording buffer is a **fixed count of 32-bit floats**, 2 channels
(`src/particules/dsp/src/particules_processor.cpp:24`, `kDefaultBufferFrames × 2 ×
sizeof(float)`). Bit depth is *faked*: the quantization modes only **round the float value**
to fewer levels — the rounded value is still stored in a full 32-bit float, so reducing bit
depth saves **zero** memory and buys **zero** extra time.

That leaves **decimation (rate)** as the only lever on recording time:

```
time = fixed_frame_count × decimation / host_rate
```

Storing 1 of every N input samples makes the same fixed float array span N× more real time —
but N× is also exactly the factor by which the rate drops. **Longer buffer and lower rate are
the same knob.** You cannot move one without the other. That is why 32 s forces ÷8 forces
6 kHz.

### What decoupling would take

To match hardware on both axes (24 kHz *and* 32 s), the buffer would have to **actually pack
samples at reduced bit width** — e.g. store Scorched as 8-bit integers instead of 32-bit
floats (encode-on-write, decode-on-read). Then the same *byte* budget would hold 4× more
samples, buying 4× the time at an unchanged rate. That is a real change to the buffer's
storage format, not a tuning tweak, and it is precisely the work the fidelity-fix design doc
declared out of scope.

The constraint is therefore not fundamental — it is a consequence of storing everything as
fixed-width float.

## The decision

"Follow the hardware Beads manual" cannot be taken literally, because the engine's storage
format cannot reproduce hardware's rate *and* buffer length at once. Two axes of fidelity are
available, and the architecture forces a choice:

- **Buffer fidelity (current):** ÷8, 6 kHz, 32 s buffer — matches hardware's *recording time*
  but is far darker than hardware's tone.
- **Rate fidelity:** ÷2, 24 kHz, 8 s buffer — matches hardware's *rate and character*
  (bright, like Sunny but 8-bit), gives up the 32 s buffer.

Independent of that choice, **adding real 8-bit µ-law quantization** ("finding 3" fix) is
unambiguously correct: it is the defining feature of Scorched on hardware and is currently
missing. It supplies the musical bit-crush the manual promises.

Note that the 8-bit fix makes the mode *crunchier*, not *brighter* — it does not address the
dull/dark "low-pitched" character. That character is driven by the ÷8 rate and the
2.5 kHz / 5 kHz low-passes, so only the rate-fidelity option relieves it.

### Suggested plan

1. **Add real 8-bit µ-law quantization** to Scorched (quantize the companded signal to 256
   steps between compress and record). Do this regardless of the rate decision.
2. **Decide the rate axis** — keep ÷8 / 6 kHz / 32 s (darkest, matches hardware *buffer*), or
   move to ÷2 / 24 kHz / 8 s (matches hardware *rate and tone*, fixes the dullness).
3. **Update whichever manual is wrong** to match the outcome. The honest doc must note which
   axis was *not* matched, since the engine cannot match both.

## Resolution (2026-07-20)

The buffer decoupling has been implemented for all four modes via **packed storage formats** (Bright: float32, Cold/Sunny: 12-bit integer, Scorched: 8-bit µ-law) and **input-adaptive channel count** (mono input doubles all buffer lengths). This follows the hardware manual's packing principle from the Finding 4 table — each mode stores samples at its true bit width, and mono recording doubles duration — with two deliberate deviations: Cold runs at 24 kHz rather than hardware's 32 kHz (decimation must divide the host rate integrally), and Sunny's buffer is 16 s rather than hardware's \~11 s (our pool is larger and 12-bit samples are stored in 16-bit containers):

| Mode              | Rate | Bits      | Buffer (stereo) | Buffer (mono) |
|-------------------|------|-----------|-----------------|---------------|
| Bright digital    | 48 kHz | 16-bit  | 4 s             | 8 s           |
| Cold digital      | 24 kHz | 12-bit  | 8 s             | 16 s          |
| Sunny tape        | 24 kHz | 12-bit  | 16 s            | 32 s          |
| Scorched cassette | 24 kHz | 8-bit µ-law | 32 s        | 64 s          |

**Findings resolved:**
- **Finding 1** (manual/code mismatch): Manuals updated to reflect the true packed-storage behavior.
- **Finding 2** (Scorched darkness): Fixed alongside the decoupling (commit `e06d2c0`) — the sample-rate ladder is now ÷1/2/2/2 (Scorched runs at 24 kHz, not 6 kHz) and its input low-pass was retuned from 2.5 kHz to 10 kHz, so the 2.5 kHz filtering described above no longer exists. Acceptance-tested in `7ac4c99` (Scorched brightness, 3–5 kHz passes).
- **Finding 3** (missing 8-bit quantization): Real µ-law quantization now implemented on Scorched.

Reference: `docs/superpowers/specs/2026-07-16-quality-mode-fidelity-fix-design.md`.

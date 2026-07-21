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
// Full scale maps to the 13-bit magnitude 8158 (bias makes 8158+33 < 2^13).
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

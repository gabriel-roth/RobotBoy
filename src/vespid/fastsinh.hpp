#pragma once
// fastsinh.hpp — divide-free, libm-free sinh for the Wasp diode term.
//
// Why: on the MetaModule's Cortex-A7, newlib sinhf is a full out-of-line
// call (order 10^2 cycles plus the register spills around it), and
// WaspFilter's standard-accuracy path evaluates the diode conduction term
// every sample. See cpu-optimization-2026-07-24.md §7.1/§9.4.
//
// sinh(x) = 0.5*(2^y - 2^-y), y = x*log2(e), with a degree-6 polynomial for
// 2^f on f in [0,1) and the 2^n exponent assembled by integer bit
// manipulation. |x| < 0.5 uses the odd Taylor series instead (avoids the
// E+ - E- cancellation). Valid over the caller's clamp range |x| <= 30.
//
// Measured accuracy (tests/vespid/test_fastsinh.cpp): max relative error
// 1.12e-5 over [-30, 30] — three orders of magnitude below the dB
// tolerances of the golden behavioral suites, and invisible next to the
// module's deliberate 1e-4 V dither floor (per-step impact measured in
// cpu-optimization-2026-07-24.md §9.2).
//
// Used on both platforms (the desktop-only high-accuracy Newton path that
// once kept exact std::sinh/cosh was removed — every build now runs this
// same standard-accuracy path, so the host test suite exercises what
// MetaModule ships).

#include <cstring>
#include <cstdint>

namespace wasp {

inline float exp2Fast(float y) {
    int n = (int)y;
    n -= ((float)n > y);          // floor for negative y
    float f = y - (float)n;       // f in [0,1)
    // 2^f: Taylor of e^(f ln2) through degree 6 (max rel err ~1.5e-5)
    float p = 1.f + f * (0.69314718056f + f * (0.24022650696f
              + f * (0.05550410866f + f * (0.00961812911f
              + f * (0.00133335581f + f * 0.00015403530f)))));
    uint32_t bits;
    std::memcpy(&bits, &p, 4);
    bits += (uint32_t)n << 23;    // scale by 2^n
    float r;
    std::memcpy(&r, &bits, 4);
    return r;
}

inline float sinhFast(float x) {
    float ax = (x < 0.f) ? -x : x;
    if (ax < 0.5f) {              // odd Taylor: exact at 0, no cancellation
        float x2 = x * x;
        return x * (1.f + x2 * (1.f / 6.f + x2 * (1.f / 120.f)));
    }
    float y = x * 1.44269504089f;
    return 0.5f * (exp2Fast(y) - exp2Fast(-y));
}

} // namespace wasp

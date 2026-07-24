#pragma once
#include <cstdint>
#include <cstring>

namespace particules_dsp {

// Divide-free, libm-free exp2. Max relative error ~1.5e-5. Same construction
// as src/vespid/fastsinh.hpp's exp2Fast (kept separate: this dsp tree is
// self-contained). Argument must be finite and within float exponent range.
inline float Exp2Fast(float y) {
    int n = (int)y;
    n -= ((float)n > y);          // floor for negative y
    float f = y - (float)n;       // f in [0,1)
    float p = 1.f + f * (0.69314718056f + f * (0.24022650696f
              + f * (0.05550410866f + f * (0.00961812911f
              + f * (0.00133335581f + f * 0.00015403530f)))));
    uint32_t bits;
    std::memcpy(&bits, &p, 4);
    bits += (uint32_t)n << 23;
    float r;
    std::memcpy(&r, &bits, 4);
    return r;
}

inline float SemitonesToRatioFast(float semitones) {
    return Exp2Fast(semitones * (1.0f / 12.0f));
}

} // namespace particules_dsp

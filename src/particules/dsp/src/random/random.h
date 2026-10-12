#pragma once

#include <cstdint>
#include <cmath>

namespace particules_dsp {

// Fast PRNG using xorshift128
class Random {
public:
    static float Uint32ToFloat(uint32_t value) {
        return static_cast<float>(value >> 8) * (1.0f / 16777216.0f);
    }

    void Init(uint32_t seed = 0x12345678) {
        state_[0] = seed;
        state_[1] = seed ^ 0xDEADBEEF;
        state_[2] = seed ^ 0xCAFEBABE;
        state_[3] = seed ^ 0x8BADF00D;
        // Warm up
        for (int i = 0; i < 16; ++i) NextUint32();
    }

    uint32_t NextUint32() {
        uint32_t t = state_[3];
        t ^= t << 11;
        t ^= t >> 8;
        state_[3] = state_[2];
        state_[2] = state_[1];
        state_[1] = state_[0];
        t ^= state_[0];
        t ^= state_[0] >> 19;
        state_[0] = t;
        return t;
    }

    // Uniform float in [0, 1)
    float NextFloat() {
        return Uint32ToFloat(NextUint32());
    }

    // Uniform float in [-1, 1)
    float NextBipolar() {
        return NextFloat() * 2.0f - 1.0f;
    }

    // Gaussian-like CLT approximation with variance 1/3.
    float NextGaussian() {
        // Central limit theorem: sum of 4 uniforms ≈ Gaussian
        float sum = 0.0f;
        for (int i = 0; i < 4; ++i) {
            sum += NextBipolar();
        }
        return sum * 0.5f;
    }

    // Peaked distribution (triangular, values clustered near 0)
    // Average of 2 uniform randoms
    float NextPeaked() {
        return (NextBipolar() + NextBipolar()) * 0.5f;
    }

    // Exponential distribution (for random grain timing)
    float NextExponential() {
        float u = NextFloat();
        // Clamp to avoid log(0)
        if (u < 1e-7f) u = 1e-7f;
        return -logf(u);
    }

private:
    uint32_t state_[4];
};

} // namespace particules_dsp

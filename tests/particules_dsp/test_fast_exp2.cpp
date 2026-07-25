#include "catch2/catch_amalgamated.hpp"
#include "util/fast_exp2.h"
#include <cmath>

TEST_CASE("Exp2Fast matches exp2f within 2e-5 over the pitch range") {
    float max_rel = 0.f;
    for (float y = -11.f; y <= 11.f; y += 1e-3f) {
        float ref = std::exp2(y);
        float got = particules_dsp::Exp2Fast(y);
        max_rel = std::max(max_rel, std::abs(got - ref) / ref);
    }
    REQUIRE(max_rel < 2e-5f);
}

TEST_CASE("Exp2Fast is exact at integer exponents") {
    for (int n = -10; n <= 10; ++n)
        REQUIRE(particules_dsp::Exp2Fast((float)n) == std::exp2((float)n));
}

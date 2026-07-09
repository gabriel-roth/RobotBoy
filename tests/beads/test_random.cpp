#include <catch2/catch_test_macros.hpp>

#include "random/random.h"

#include <cstdint>

using namespace beads;

TEST_CASE("Random: uint32 conversion is half-open", "[random]") {
    REQUIRE(Random::Uint32ToFloat(0u) == 0.0f);
    REQUIRE(Random::Uint32ToFloat(UINT32_MAX) < 1.0f);
    REQUIRE(Random::Uint32ToFloat(UINT32_MAX) >= 0.0f);
}

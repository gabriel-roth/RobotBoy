#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "particules_density_control.h"

using Catch::Approx;

TEST_CASE("ParticulesDensityControl: slow path keeps the +/-5V to +/-1 density mapping", "[particules_density]") {
    REQUIRE(particules::ComputeSlowDensityOffset(2.5f) == Approx(0.5f));
    REQUIRE(particules::ComputeSlowDensityOffset(-5.0f) == Approx(-1.0f));
    REQUIRE(particules::ComputeSlowDensityOffset(0.0f) == Approx(0.0f));
}

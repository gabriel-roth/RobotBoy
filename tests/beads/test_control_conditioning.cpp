#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "util/control_conditioner.h"

using namespace beads;
using Catch::Approx;

TEST_CASE("ControlConditioner: decimation holds value between refreshes", "[control_conditioning]") {
    ControlConditioner c;
    c.Init(4, 0.0f, 0.0f, 0.0f);

    REQUIRE(c.Process(0.10f) == Approx(0.10f));
    REQUIRE(c.Process(0.90f) == Approx(0.10f));
    REQUIRE(c.Process(0.80f) == Approx(0.10f));
    REQUIRE(c.Process(0.70f) == Approx(0.70f));
}

TEST_CASE("ControlConditioner: quantization suppresses tiny movement", "[control_conditioning]") {
    ControlConditioner c;
    c.Init(1, 0.0f, 0.05f, 0.0f);

    REQUIRE(c.Process(0.101f) == Approx(0.10f).margin(0.0001f));
    REQUIRE(c.Process(0.119f) == Approx(0.10f).margin(0.0001f));
    REQUIRE(c.Process(0.151f) == Approx(0.15f).margin(0.0001f));
}

TEST_CASE("ControlConditioner: one-pole smoothing moves gradually", "[control_conditioning]") {
    ControlConditioner c;
    c.Init(1, 0.5f, 0.0f, 0.0f);

    float first = c.Process(1.0f);
    float second = c.Process(1.0f);

    REQUIRE(first == Approx(0.5f));
    REQUIRE(second == Approx(0.75f));
}

TEST_CASE("ControlConditioner: reset seeds the held value immediately", "[control_conditioning]") {
    ControlConditioner c;
    c.Init(8, 0.5f, 0.1f, 0.0f);
    c.Reset(0.32f);

    REQUIRE(c.Process(0.32f) == Approx(0.30f).margin(0.0001f));
}

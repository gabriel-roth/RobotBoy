#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <cmath>

#include "random/attenurandomizer.h"
#include "random/random.h"

using namespace beads;
using Catch::Approx;

TEST_CASE("Attenurandomizer: ar_amount=0 returns base unchanged", "[attenurandomizer]") {
    Random rng;
    rng.Init(42);
    Attenurandomizer ar;
    ar.Init(&rng);

    // With ar_amount = 0, output should always equal base regardless of CV
    for (int i = 0; i < 100; ++i) {
        float base = static_cast<float>(i) / 100.0f;
        REQUIRE(ar.Process(base, 0.0f, 0.0f, false) == base);
        REQUIRE(ar.Process(base, 0.0f, 1.0f, true) == base);
        REQUIRE(ar.Process(base, 0.0f, -1.0f, true) == base);
    }
}

TEST_CASE("Attenurandomizer: CV connected with positive ar_amount acts as attenuator", "[attenurandomizer]") {
    Random rng;
    rng.Init(42);
    Attenurandomizer ar;
    ar.Init(&rng);

    float base = 0.5f;
    float ar_amount = 0.8f;  // Positive = CV attenuator
    float cv = 0.5f;

    // With CV connected and positive ar_amount, output = base + ar_amount * cv
    float result = ar.Process(base, ar_amount, cv, true);
    float expected = base + ar_amount * cv;
    REQUIRE(result == Approx(expected));
}

TEST_CASE("Attenurandomizer: CV connected with negative ar_amount produces varied output", "[attenurandomizer]") {
    Random rng;
    rng.Init(42);
    Attenurandomizer ar;
    ar.Init(&rng);

    float base = 0.5f;
    float ar_amount = -0.5f;  // Negative = CV-controlled randomization
    float cv = 1.0f;

    // Run multiple times and collect results
    float min_val = 1e10f, max_val = -1e10f;
    for (int i = 0; i < 100; ++i) {
        float result = ar.Process(base, ar_amount, cv, true);
        min_val = std::min(min_val, result);
        max_val = std::max(max_val, result);
    }

    // Should have spread (Gaussian random scaled by -ar_amount * |cv|)
    float spread = max_val - min_val;
    REQUIRE(spread > 0.01f);
}

TEST_CASE("Attenurandomizer: No CV, positive ar_amount adds uniform random spread", "[attenurandomizer]") {
    Random rng;
    rng.Init(42);
    Attenurandomizer ar;
    ar.Init(&rng);

    float base = 0.5f;
    float ar_amount = 0.5f;

    // Without CV connected, positive ar_amount should add uniform random
    float min_val = 1e10f, max_val = -1e10f;
    float sum = 0.0f;
    int n = 500;
    for (int i = 0; i < n; ++i) {
        float result = ar.Process(base, ar_amount, 0.0f, false);
        min_val = std::min(min_val, result);
        max_val = std::max(max_val, result);
        sum += result;
    }

    // Should have spread centered around base
    float spread = max_val - min_val;
    REQUIRE(spread > 0.1f);

    // Mean should be near base (uniform bipolar random averages to 0)
    float mean = sum / static_cast<float>(n);
    REQUIRE(mean == Approx(base).margin(0.1f));
}

TEST_CASE("Attenurandomizer: No CV, negative ar_amount adds peaked random", "[attenurandomizer]") {
    Random rng;
    rng.Init(42);
    Attenurandomizer ar;
    ar.Init(&rng);

    float base = 0.5f;
    float ar_amount = -0.5f;

    // Without CV connected, negative ar_amount should add peaked random
    float min_val = 1e10f, max_val = -1e10f;
    float sum = 0.0f;
    int n = 500;
    for (int i = 0; i < n; ++i) {
        float result = ar.Process(base, ar_amount, 0.0f, false);
        min_val = std::min(min_val, result);
        max_val = std::max(max_val, result);
        sum += result;
    }

    // Should have spread (peaked distribution is narrower than uniform but still spread)
    float spread = max_val - min_val;
    REQUIRE(spread > 0.05f);

    // Mean should be near base
    float mean = sum / static_cast<float>(n);
    REQUIRE(mean == Approx(base).margin(0.1f));
}

TEST_CASE("Attenurandomizer: Null random pointer returns base", "[attenurandomizer]") {
    Attenurandomizer ar;
    ar.Init(nullptr);

    // With null random, should return base regardless of ar_amount
    float result = ar.Process(0.5f, 1.0f, 0.5f, true);
    REQUIRE(result == 0.5f);

    result = ar.Process(0.3f, -0.8f, 1.0f, false);
    REQUIRE(result == 0.3f);
}

TEST_CASE("Attenurandomizer: Peaked random has less variance than uniform", "[attenurandomizer]") {
    Random rng1, rng2;
    rng1.Init(42);
    rng2.Init(42);
    Attenurandomizer ar_uniform, ar_peaked;
    ar_uniform.Init(&rng1);
    ar_peaked.Init(&rng2);

    float base = 0.5f;
    int n = 1000;

    // Collect variance for uniform random (positive ar_amount, no CV)
    float sum_sq_uniform = 0.0f;
    for (int i = 0; i < n; ++i) {
        float result = ar_uniform.Process(base, 0.5f, 0.0f, false);
        float diff = result - base;
        sum_sq_uniform += diff * diff;
    }

    // Collect variance for peaked random (negative ar_amount, no CV)
    float sum_sq_peaked = 0.0f;
    for (int i = 0; i < n; ++i) {
        float result = ar_peaked.Process(base, -0.5f, 0.0f, false);
        float diff = result - base;
        sum_sq_peaked += diff * diff;
    }

    float var_uniform = sum_sq_uniform / static_cast<float>(n);
    float var_peaked = sum_sq_peaked / static_cast<float>(n);

    // Peaked distribution (average of 2 uniforms) should have lower variance
    REQUIRE(var_peaked < var_uniform);
}

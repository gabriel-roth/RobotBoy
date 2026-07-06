#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "particules_block_runtime.h"

using Catch::Approx;

TEST_CASE("ParticulesBlockRuntime: outputs samples in processed-block order", "[particules_block_runtime]") {
    ParticulesBlockRuntime<4> runtime;

    REQUIRE(runtime.OutputIndex() == 0);
    REQUIRE(runtime.InputIndex() == 0);

    runtime.PushInputSample({1.0f, 10.0f});
    REQUIRE(runtime.BlockReady() == false);
    REQUIRE(runtime.OutputIndex() == 1);

    runtime.PushInputSample({2.0f, 20.0f});
    runtime.PushInputSample({3.0f, 30.0f});
    runtime.PushInputSample({4.0f, 40.0f});
    REQUIRE(runtime.BlockReady() == true);

    beads::StereoFrame processed[4] = {
        {0.1f, 1.1f},
        {0.2f, 1.2f},
        {0.3f, 1.3f},
        {0.4f, 1.4f},
    };
    runtime.CommitProcessedBlock(processed, 4);

    beads::StereoFrame out = runtime.ReadOutputSample();
    REQUIRE(out.l == Approx(0.1f));
    REQUIRE(out.r == Approx(1.1f));

    out = runtime.ReadOutputSample();
    REQUIRE(out.l == Approx(0.2f));
    REQUIRE(out.r == Approx(1.2f));

    out = runtime.ReadOutputSample();
    REQUIRE(out.l == Approx(0.3f));
    REQUIRE(out.r == Approx(1.3f));

    out = runtime.ReadOutputSample();
    REQUIRE(out.l == Approx(0.4f));
    REQUIRE(out.r == Approx(1.4f));
}

TEST_CASE("ParticulesBlockRuntime: block-ready goes true only on the final sample", "[particules_block_runtime]") {
    ParticulesBlockRuntime<4> runtime;

    REQUIRE(runtime.PushInputSample({0.0f, 0.0f}) == false);
    REQUIRE(runtime.PushInputSample({0.0f, 0.0f}) == false);
    REQUIRE(runtime.PushInputSample({0.0f, 0.0f}) == false);
    REQUIRE(runtime.PushInputSample({0.0f, 0.0f}) == true);
}

TEST_CASE("ParticulesBlockRuntime: trigger pulse countdown remains sample accurate across block boundaries", "[particules_block_runtime]") {
    ParticulesBlockRuntime<4> runtime;
    runtime.StartGrainTriggerPulse(6);

    REQUIRE(runtime.PushInputSample({0.0f, 0.0f}) == false);
    REQUIRE(runtime.ConsumeTriggerPulseSample() == true);

    REQUIRE(runtime.PushInputSample({0.0f, 0.0f}) == false);
    REQUIRE(runtime.ConsumeTriggerPulseSample() == true);

    REQUIRE(runtime.PushInputSample({0.0f, 0.0f}) == false);
    REQUIRE(runtime.ConsumeTriggerPulseSample() == true);

    REQUIRE(runtime.PushInputSample({0.0f, 0.0f}) == true);
    REQUIRE(runtime.BlockReady() == true);
    REQUIRE(runtime.ConsumeTriggerPulseSample() == true);

    REQUIRE(runtime.PushInputSample({0.0f, 0.0f}) == false);
    REQUIRE(runtime.ConsumeTriggerPulseSample() == true);

    REQUIRE(runtime.PushInputSample({0.0f, 0.0f}) == false);
    REQUIRE(runtime.ConsumeTriggerPulseSample() == true);

    REQUIRE(runtime.ConsumeTriggerPulseSample() == false);
}

TEST_CASE("ParticulesBlockRuntime: block-rate LED decay matches per-sample exponential", "[particules_block_runtime]") {
    ParticulesBlockRuntime<4> runtime;
    runtime.SetGrainLed(1.0f);

    float expected = 1.0f;
    for (int block = 0; block < 3; ++block) {
        runtime.DecayGrainLed();
        for (int i = 0; i < 4; ++i) {
            expected *= 0.9999f;
        }
    }

    REQUIRE(runtime.GrainLed() == Approx(expected).margin(0.000001f));
}

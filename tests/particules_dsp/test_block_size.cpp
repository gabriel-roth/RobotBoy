#include <catch2/catch_test_macros.hpp>
#include <vector>
#include <cmath>
#include <cstdint>

#include "particules_dsp/particules_dsp.h"

using namespace particules_dsp;

static constexpr float kSR = 48000.0f;

namespace {
struct Proc {
    std::vector<uint8_t> memory;
    ParticulesProcessor p;
    Proc() {
        auto req = ParticulesProcessor::GetMemoryRequirements(kSR);
        memory.resize(req.total_bytes, 0);
        p.Init(memory.data(), memory.size(), kSR);
    }
};

ParticulesParameters DryParams() {
    ParticulesParameters params{};
    params.dry_wet = 0.0f;          // full dry — routes through dry_input_buf
    params.auto_gain = false;
    params.manual_gain_db = 12.0f;  // makes recorded (gained) audio differ from dry
    params.feedback = 0.0f;
    params.reverb = 0.0f;
    return params;
}
}  // namespace

// Process(input, output, 256) must equal four consecutive Process(…, 64)
// calls on an identically-initialized processor fed the same stream.
// Before the chunking fix, the 256-frame call read dry_input_buf (a 64-frame
// array) out of bounds for frames 64-255, so the dry path diverged.
TEST_CASE("ParticulesProcessor: Process is block-size invariant (256 vs 4x64)",
          "[processor][blocksize]") {
    constexpr size_t kTotal = 2048;
    std::vector<StereoFrame> input(kTotal), outBig(kTotal), outSmall(kTotal);
    for (size_t i = 0; i < kTotal; ++i) {
        float v = 0.5f * std::sin(2.0 * M_PI * 330.0 * i / kSR)
                + 0.25f * std::sin(2.0 * M_PI * 917.0 * i / kSR);
        input[i] = {v, -v};
    }

    Proc a, b;
    auto params = DryParams();
    a.p.SetParameters(params);
    b.p.SetParameters(params);

    for (size_t off = 0; off < kTotal; off += 256)
        a.p.Process(input.data() + off, outBig.data() + off, 256);
    for (size_t off = 0; off < kTotal; off += 64)
        b.p.Process(input.data() + off, outSmall.data() + off, 64);

    for (size_t i = 0; i < kTotal; ++i) {
        REQUIRE(outBig[i].l == outSmall[i].l);
        REQUIRE(outBig[i].r == outSmall[i].r);
    }
}

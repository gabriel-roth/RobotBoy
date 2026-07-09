#include <catch2/catch_test_macros.hpp>
#include <vector>
#include <cmath>
#include <cstdint>

#include "beads/beads.h"

using namespace beads;

static constexpr float kSR = 48000.0f;
static constexpr size_t kBlock = 64;  // MetaModule's Process() cadence

namespace {
struct Proc {
    std::vector<uint8_t> memory;
    BeadsProcessor p;
    Proc() {
        auto req = BeadsProcessor::GetMemoryRequirements(kSR);
        memory.resize(req.total_bytes, 0);
        p.Init(memory.data(), memory.size(), kSR);
    }
};
}  // namespace

// A malformed host (or a momentarily-glitched CV chain upstream of the
// wrapper's own clamp()) can hand the engine NaN in any of the CV-ish
// BeadsParameters fields. The wrapper's `clamp()` does not sanitize NaN
// (std::min/std::max leave a NaN operand unclamped -- see rack::math::clamp
// and beads::Clamp, both built on std::min/std::max), so nothing upstream
// of BeadsProcessor is a reliable backstop. This test drives BeadsParameters
// directly, bypassing the wrapper entirely, to pin that the DSP engine
// itself never emits a non-finite sample no matter what garbage lands in
// its parameter struct.
//
// AR (attenurandomizer) knobs must be nonzero for the corresponding *_cv
// field to actually reach the grain engine -- Attenurandomizer::Process
// short-circuits to the unmodulated base value when ar_amount == 0.0f, so
// time_ar/size_ar/shape_ar/pitch_ar are all engaged here specifically to
// exercise the time_cv/size_cv/shape_cv/pitch_cv NaN paths.
TEST_CASE("BeadsProcessor: NaN in every CV-ish parameter field yields finite output",
          "[processor][nan]") {
    Proc proc;

    BeadsParameters params{};
    params.dry_wet = NAN;
    params.feedback = NAN;
    params.reverb = NAN;

    params.time_ar = 0.5f;
    params.size_ar = 0.5f;
    params.shape_ar = 0.5f;
    params.pitch_ar = 0.5f;

    params.time_cv = NAN;
    params.time_cv_connected = true;
    params.size_cv = NAN;
    params.size_cv_connected = true;
    params.shape_cv = NAN;
    params.shape_cv_connected = true;
    params.pitch_cv = NAN;
    params.pitch_cv_connected = true;
    params.density_cv = NAN;
    params.density_cv_connected = true;

    params.density = 0.2f;   // fast, regular grain triggers
    params.size = 0.5f;
    params.time = 0.3f;
    params.shape = 0.5f;
    params.pitch = 0.0f;
    params.manual_gain_db = 0.0f;  // bypass auto-gain ramping
    proc.p.SetParameters(params);

    StereoFrame in[kBlock], out[kBlock];
    bool all_finite = true;
    bool had_output = false;

    // Several blocks with real audio input: enough for grains to trigger,
    // read from the buffer, and recirculate through feedback/reverb.
    for (int block = 0; block < 200; ++block) {
        for (size_t i = 0; i < kBlock; ++i) {
            size_t n = block * kBlock + i;
            float v = 0.5f * std::sin(2.0 * M_PI * 440.0 * n / kSR);
            in[i] = {v, v};
        }
        proc.p.Process(in, out, kBlock);
        for (size_t i = 0; i < kBlock; ++i) {
            if (!std::isfinite(out[i].l) || !std::isfinite(out[i].r)) {
                all_finite = false;
            }
            if (std::abs(out[i].l) > 1e-6f || std::abs(out[i].r) > 1e-6f) {
                had_output = true;
            }
        }
    }

    REQUIRE(all_finite);
    // Confirms the engine kept doing real work (rather than, say, silently
    // short-circuiting to zero output for the whole run) while soaking up
    // the NaN inputs -- a fence that degenerated to "always output silence"
    // would pass the finiteness check above for the wrong reason.
    REQUIRE(had_output);
}

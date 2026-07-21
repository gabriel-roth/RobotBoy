#include <catch2/catch_amalgamated.hpp>
#include <cmath>
#include <vector>
#include "quality/quality_processor.h"
#include "util/dsp_utils.h"

using namespace particules_dsp;

namespace {
// Steady-state amplitude of a 4 kHz sine after ProcessOutput in the given
// mode. Skips the 64-sample mode crossfade plus filter settle, then
// measures peak over one second.
float SteadyAmp4k(QualityProcessor& qp, QualityMode mode) {
    const float sr = 48000.0f;
    float peak = 0.0f;
    for (int i = 0; i < 52800; ++i) {
        float s = std::sin(kTwoPi * 4000.0f * static_cast<float>(i) / sr);
        StereoFrame out = qp.ProcessOutput({s, s}, mode);
        if (i >= 4800) peak = std::max(peak, std::fabs(out.l));
    }
    return peak;
}
} // namespace

TEST_CASE("QualityProcessor: tape tone cutoff override darkens Scorched output") {
    QualityProcessor stock;
    stock.Init(48000.0f);
    float amp_stock = SteadyAmp4k(stock, QualityMode::kScorchedCassette);

    QualityProcessor voiced;
    voiced.Init(48000.0f);
    voiced.SetTapeToneCutoffs(6500.0f, 2800.0f);
    float amp_voiced = SteadyAmp4k(voiced, QualityMode::kScorchedCassette);

    // 2-pole Butterworth at 4 kHz: fc=5 kHz -> ~-1.5 dB, fc=2.8 kHz ->
    // ~-7.1 dB. Require at least 4 dB extra attenuation from the override.
    float extra_db = 20.0f * std::log10(amp_stock / amp_voiced);
    REQUIRE(extra_db > 4.0f);

    // Defaults unchanged: a second stock instance matches the first.
    QualityProcessor stock2;
    stock2.Init(48000.0f);
    REQUIRE(SteadyAmp4k(stock2, QualityMode::kScorchedCassette)
            == Catch::Approx(amp_stock).epsilon(0.01));
}

#include <catch2/catch_amalgamated.hpp>
#include <cmath>
#include <vector>
#include "quality/quality_processor.h"
#include "util/dsp_utils.h"

using namespace particules_dsp;

namespace {
// Steady-state amplitude of a freq_hz sine after ProcessOutput in the given
// mode. Skips the 64-sample mode crossfade plus filter settle, then
// measures peak over one second.
float SteadyAmp(QualityProcessor& qp, QualityMode mode, float freq_hz) {
    const float sr = 48000.0f;
    float peak = 0.0f;
    for (int i = 0; i < 52800; ++i) {
        float s = std::sin(kTwoPi * freq_hz * static_cast<float>(i) / sr);
        StereoFrame out = qp.ProcessOutput({s, s}, mode);
        if (i >= 4800) peak = std::max(peak, std::fabs(out.l));
    }
    return peak;
}

// Same settle-then-measure structure as SteadyAmp, but probes the INPUT-side
// anti-alias LP (ProcessInput) instead of the output tone LP.
float SteadyInputAmp(QualityProcessor& qp, QualityMode mode, float freq_hz) {
    const float sr = 48000.0f;
    float peak = 0.0f;
    for (int i = 0; i < 52800; ++i) {
        float s = std::sin(kTwoPi * freq_hz * static_cast<float>(i) / sr);
        StereoFrame out = qp.ProcessInput({s, s}, mode);
        if (i >= 4800) peak = std::max(peak, std::fabs(out.l));
    }
    return peak;
}
} // namespace

TEST_CASE("QualityProcessor: tape tone cutoff override darkens Scorched output") {
    QualityProcessor stock;
    stock.Init(48000.0f);
    float amp_stock = SteadyAmp(stock, QualityMode::kScorchedCassette, 4000.0f);

    QualityProcessor voiced;
    voiced.Init(48000.0f);
    voiced.SetTapeToneCutoffs(6500.0f, 2800.0f);
    float amp_voiced = SteadyAmp(voiced, QualityMode::kScorchedCassette, 4000.0f);

    // 2-pole Butterworth at 4 kHz: fc=5 kHz -> ~-1.5 dB, fc=2.8 kHz ->
    // ~-7.1 dB. Require at least 4 dB extra attenuation from the override.
    float extra_db = 20.0f * std::log10(amp_stock / amp_voiced);
    REQUIRE(extra_db > 4.0f);

    // Defaults unchanged: a second stock instance matches the first.
    QualityProcessor stock2;
    stock2.Init(48000.0f);
    REQUIRE(SteadyAmp(stock2, QualityMode::kScorchedCassette, 4000.0f)
            == Catch::Approx(amp_stock).epsilon(0.01));
}

TEST_CASE("QualityProcessor: Cold anti-alias LP is deliberately leaky near Nyquist") {
    QualityProcessor qp;
    qp.Init(48000.0f);
    // 13 kHz probe through Cold's input LP. The idealized 2-pole Butterworth
    // math predicted fc=10 kHz (old) -> |H| ~= 0.509, fc=11.5 kHz -> ~0.617,
    // but the actual StateVariableFilter response measures lower: RED
    // (fc=10 kHz) = 0.41451, GREEN (fc=11.5 kHz) = 0.56084 (~2.63 dB apart).
    // Recalibrated to the dB midpoint between the two measured values per
    // the plan's threshold rule (0.482); the two measurements are close
    // enough together that no placement clears the full 1.5 dB margin on
    // both sides (best achievable here is ~1.31 dB each way).
    float in_amp = 1.0f;
    float out_amp = SteadyInputAmp(qp, QualityMode::kColdDigital, 13000.0f);
    REQUIRE(out_amp / in_amp > 0.482f);
    REQUIRE(out_amp / in_amp < 0.68f);   // still a filter, not a bypass
}

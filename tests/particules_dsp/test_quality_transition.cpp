#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <vector>

#include "particules_dsp/particules_dsp.h"
#include "particules_dsp/types.h"
#include "particules_dsp/parameters.h"

using namespace particules_dsp;

namespace {

constexpr float kSr = 48000.0f;

struct Proc {
    std::vector<uint8_t> mem;
    ParticulesProcessor p;
    Proc() {
        auto req = ParticulesProcessor::GetMemoryRequirements(kSr);
        mem.resize(req.total_bytes + req.alignment, 0);
        p.Init(mem.data(), mem.size(), kSr);
    }
};

struct RunResult { float peak; bool finite; };
RunResult Run(ParticulesProcessor& p, ParticulesParameters& params,
              int blocks, double& phase) {
    RunResult r{0.0f, true};
    StereoFrame in[64], out[64];
    for (int b = 0; b < blocks; ++b) {
        for (int i = 0; i < 64; ++i) {
            float v = 0.5f * static_cast<float>(std::sin(phase));
            phase += 2.0 * M_PI * 220.0 / kSr;
            in[i] = {v, v};
        }
        p.SetParameters(params);
        p.Process(in, out, 64);
        for (int i = 0; i < 64; ++i) {
            if (!std::isfinite(out[i].l) || !std::isfinite(out[i].r)) r.finite = false;
            r.peak = std::max(r.peak, std::max(std::fabs(out[i].l), std::fabs(out[i].r)));
        }
    }
    return r;
}

ParticulesParameters WetParams() {
    ParticulesParameters params{};
    params.dry_wet = 1.0f;
    params.density = 0.8f;
    params.size = 0.0f;
    // TIME near the write head: peak checks below want to know "does wet
    // output resume/continue", not "has a (possibly 32 s) buffer refilled
    // to some arbitrary depth" -- the latter needs far more real time than
    // these runs cover and isn't what these tests are probing.
    params.time = 0.0f;
    params.quality_mode = QualityMode::kBrightDigital;
    return params;
}

// Same block-processing harness as Run(), but with a configurable input
// frequency (the other tests only care about "is wet audio present", so
// 220 Hz is fine there; the spectral test below needs a specific tone) and
// an optional capture buffer for spectral measurement of the output.
RunResult RunAtFreq(ParticulesProcessor& p, ParticulesParameters& params,
                     int blocks, double& phase, double freq_hz,
                     std::vector<float>* capture = nullptr) {
    RunResult r{0.0f, true};
    StereoFrame in[64], out[64];
    for (int b = 0; b < blocks; ++b) {
        for (int i = 0; i < 64; ++i) {
            float v = 0.5f * static_cast<float>(std::sin(phase));
            phase += 2.0 * M_PI * freq_hz / kSr;
            in[i] = {v, v};
        }
        p.SetParameters(params);
        p.Process(in, out, 64);
        for (int i = 0; i < 64; ++i) {
            if (!std::isfinite(out[i].l) || !std::isfinite(out[i].r)) r.finite = false;
            r.peak = std::max(r.peak, std::max(std::fabs(out[i].l), std::fabs(out[i].r)));
            if (capture) capture->push_back(0.5f * (out[i].l + out[i].r));
        }
    }
    return r;
}

// Single-bin Goertzel magnitude of `freq_hz` within `samples` -- an O(N)
// stand-in for a full FFT when only one frequency's amplitude is of
// interest. Normalized so a full-scale sine of `freq_hz` reads back ~1.0.
float GoertzelMagnitude(const std::vector<float>& samples, float freq_hz, float sample_rate) {
    const float omega = 2.0f * static_cast<float>(M_PI) * freq_hz / sample_rate;
    const float coeff = 2.0f * std::cos(omega);
    float s_prev = 0.0f, s_prev2 = 0.0f;
    for (float x : samples) {
        float s = x + coeff * s_prev - s_prev2;
        s_prev2 = s_prev;
        s_prev = s;
    }
    const float real = s_prev - s_prev2 * std::cos(omega);
    const float imag = s_prev2 * std::sin(omega);
    return std::sqrt(real * real + imag * imag) / (static_cast<float>(samples.size()) * 0.5f);
}

}  // namespace

TEST_CASE("QualityTransition: Bright->Scorched stays finite and recovers", "[transition]") {
    Proc proc;
    auto params = WetParams();
    double phase = 0.0;

    auto settled = Run(proc.p, params, 800, phase);   // past grain startup ramp
    REQUIRE(settled.finite);
    REQUIRE(settled.peak > 0.01f);

    params.quality_mode = QualityMode::kScorchedCassette;
    auto transition = Run(proc.p, params, 300, phase);   // ~400 ms: full transition
    REQUIRE(transition.finite);

    auto after = Run(proc.p, params, 1500, phase);
    REQUIRE(after.finite);
    REQUIRE(after.peak > 0.01f);
}

TEST_CASE("QualityTransition: Scorched->Bright shrink is safe", "[transition]") {
    Proc proc;
    auto params = WetParams();
    params.quality_mode = QualityMode::kScorchedCassette;
    params.time = 0.9f;   // grains read deep into the long buffer
    double phase = 0.0;

    auto scorched = Run(proc.p, params, 1500, phase);
    REQUIRE(scorched.finite);

    params.quality_mode = QualityMode::kBrightDigital;   // 768k -> 192k frames
    // Deep TIME served its purpose above (stress the shrink while reading
    // far into the long buffer); reset it so the recovery check below
    // reflects "wet resumes", not "the 4 s buffer has refilled to 90% depth".
    params.time = 0.0f;
    auto back = Run(proc.p, params, 1500, phase);
    REQUIRE(back.finite);
    REQUIRE(back.peak > 0.01f);
}

TEST_CASE("QualityTransition: mono/stereo input change reconfigures safely", "[transition][mono]") {
    Proc proc;
    auto params = WetParams();
    params.quality_mode = QualityMode::kScorchedCassette;
    double phase = 0.0;
    Run(proc.p, params, 1200, phase);

    params.mono_input = true;    // "cable unplugged": 768k -> 1.5M mono frames
    auto mono = Run(proc.p, params, 1500, phase);
    REQUIRE(mono.finite);
    REQUIRE(mono.peak > 0.01f);

    params.mono_input = false;   // re-patched
    auto stereo = Run(proc.p, params, 1500, phase);
    REQUIRE(stereo.finite);
    REQUIRE(stereo.peak > 0.01f);
}

TEST_CASE("QualityTransition: rapid mode cycling stays finite", "[transition]") {
    Proc proc;
    auto params = WetParams();
    double phase = 0.0;
    Run(proc.p, params, 200, phase);
    const QualityMode cycle[] = {
        QualityMode::kColdDigital, QualityMode::kScorchedCassette,
        QualityMode::kSunnyTape, QualityMode::kBrightDigital,
        QualityMode::kScorchedCassette, QualityMode::kColdDigital};
    for (QualityMode m : cycle) {
        params.quality_mode = m;
        auto r = Run(proc.p, params, 20, phase);   // re-flip mid-transition
        REQUIRE(r.finite);
    }
    params.quality_mode = QualityMode::kBrightDigital;
    auto r = Run(proc.p, params, 2000, phase);
    REQUIRE(r.finite);
    REQUIRE(r.peak > 0.01f);
}

TEST_CASE("QualityTransition: change while frozen is deferred", "[transition][freeze]") {
    Proc proc;
    auto params = WetParams();
    double phase = 0.0;
    Run(proc.p, params, 800, phase);          // record content in Bright

    params.freeze = true;
    Run(proc.p, params, 100, phase);

    params.quality_mode = QualityMode::kScorchedCassette;
    auto frozen = Run(proc.p, params, 300, phase);
    REQUIRE(frozen.finite);
    REQUIRE(frozen.peak > 0.01f);             // no transition mute while frozen

    params.freeze = false;
    auto after = Run(proc.p, params, 2000, phase);
    REQUIRE(after.finite);
    REQUIRE(after.peak > 0.01f);
}

TEST_CASE("QualityTransition: freeze re-engaged mid-fade-out aborts the apply", "[transition][freeze]") {
    Proc proc;
    auto params = WetParams();
    double phase = 0.0;
    Run(proc.p, params, 800, phase);          // record content in Bright, unfrozen

    // Start a transition while unfrozen, then run well inside the
    // 2048-sample kFadeOut window (10 * 64 = 640 samples).
    params.quality_mode = QualityMode::kScorchedCassette;
    Run(proc.p, params, 10, phase);

    // Re-engage freeze mid-fade-out, then run past where the apply
    // (KillAllGrains/Configure/Clear) would have fired had it not been
    // aborted. Without the fix, the frozen slice gets cleared out from
    // under it and playback goes silent.
    params.freeze = true;
    auto frozen = Run(proc.p, params, 300, phase);
    REQUIRE(frozen.finite);
    REQUIRE(frozen.peak > 0.01f);   // frozen content is still playing, not cleared

    // Unfreeze: the deferred transition re-arms via the kIdle re-check and
    // completes; wet should recover and remain finite/audible.
    params.freeze = false;
    auto after = Run(proc.p, params, 2000, phase);
    REQUIRE(after.finite);
    REQUIRE(after.peak > 0.01f);
}

TEST_CASE("QualityTransition: Scorched passes mid-high frequencies (brightness acceptance)",
          "[transition][spectral]") {
    // The point of the quality-buffer-decoupling branch: Scorched used to
    // run at /8 with a 2.5 kHz input LP (nothing above ~2.5 kHz survived --
    // the "dull/dark" complaint). It now runs at /2 (24 kHz) with a 10 kHz
    // input LP and a 5 kHz output LP, so a 4 kHz tone (comfortably inside
    // the new pass band, well outside the old one) must come through the
    // full grain engine materially brighter than the old config would have
    // produced.
    //
    // Grains resynthesize buffer content with envelopes/overlap, so a clean
    // tone never comes out the other end -- only a Bright-vs-Scorched
    // *ratio* is meaningful. Discriminator used: single-bin Goertzel
    // magnitude at 4 kHz, measured over the trailing 0.5 s (24000 samples)
    // of a long-settled run in each mode.
    Proc proc;
    auto params = WetParams();
    params.size = -0.2f;      // "small" grains: kSizeBoundary, 30 ms minimum
    params.density = 0.8f;    // "high" density
    double phase = 0.0;
    constexpr double kToneHz = 4000.0;
    constexpr int kCaptureBlocks = 375;   // 375*64 = 24000 samples = 0.5 s @ 48 kHz

    // 1. Bright: settle past the grain startup ramp, then capture.
    auto settled = RunAtFreq(proc.p, params, 800, phase, kToneHz);
    REQUIRE(settled.finite);
    REQUIRE(settled.peak > 0.01f);

    std::vector<float> bright_capture;
    bright_capture.reserve(kCaptureBlocks * 64);
    auto bright_run = RunAtFreq(proc.p, params, kCaptureBlocks, phase, kToneHz, &bright_capture);
    REQUIRE(bright_run.finite);
    const float mag_bright = GoertzelMagnitude(bright_capture, static_cast<float>(kToneHz), kSr);

    // 2. Switch to Scorched. Run well past the full transition (fadeout
    // 2048 + clear ~8192 + fadein 2048 ~= 12288 samples) plus enough for
    // grains to re-establish on the freshly-cleared buffer's new content
    // (>= 30000 samples total, per the plan), then capture the trailing
    // 0.5 s.
    params.quality_mode = QualityMode::kScorchedCassette;
    auto transition = RunAtFreq(proc.p, params, 500, phase, kToneHz);        // 32000 samples
    REQUIRE(transition.finite);
    auto scorched_settle = RunAtFreq(proc.p, params, 500, phase, kToneHz);   // grains on fresh content
    REQUIRE(scorched_settle.finite);

    std::vector<float> scorched_capture;
    scorched_capture.reserve(kCaptureBlocks * 64);
    auto scorched_run = RunAtFreq(proc.p, params, kCaptureBlocks, phase, kToneHz, &scorched_capture);
    REQUIRE(scorched_run.finite);
    REQUIRE(scorched_run.peak > 0.01f);
    const float mag_scorched = GoertzelMagnitude(scorched_capture, static_cast<float>(kToneHz), kSr);

    INFO("mag_bright=" << mag_bright << " mag_scorched=" << mag_scorched);

    // Calibration (measured 2026-07-21 on this branch; scratch-build
    // measurement of the OLD Scorched config used kScorchedInputLpHz and
    // kScorchedOutputLpHz temporarily set to 2500 Hz and QualityConfigFor
    // Scorched temporarily set to {8, kFloat32, 0} to emulate the
    // pre-branch /8 + 2.5 kHz behavior, then restored exactly (verified
    // with `git diff` -- see task-10-report.md for the full readout):
    //   Bright:            mag_bright       ~= 0.01372
    //   New Scorched (/2): mag_scorched     ~= 0.01875   (~1.37x Bright)
    //   Old Scorched (/8): mag_old_scorched ~= 0.00045   (~0.033x Bright)
    // The new config doesn't just clear the old one -- it clears Bright's
    // own 4 kHz level (grain envelope/overlap-add spectral spreading adds
    // energy the old, heavily low-passed Scorched had none of to spread).
    // Old Scorched sits at ~3% of Bright, i.e. correctly near the noise
    // floor. K=0.3 sits with enormous margin above the old ratio (~0.033x)
    // and below the new ratio (~1.37x), tolerating run-to-run
    // grain-synthesis variance many times over.
    constexpr float K = 0.3f;
    REQUIRE(mag_scorched > mag_bright * K);
}

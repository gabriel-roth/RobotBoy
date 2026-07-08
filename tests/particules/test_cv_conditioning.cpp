#include "../../src/particules/particules_cv_conditioning.h"
#include "../../src/vendor/beads_dsp/src/util/control_conditioner.h"
#include <cstdio>
#include <cmath>

static int g_failures = 0;
static void check(bool cond, const char* name) {
    if (!cond) { std::printf("FAIL: %s\n", name); ++g_failures; }
    else       { std::printf("ok:   %s\n", name); }
}

int main() {
    using namespace particules;

    // Decimation: one CV sample per ~8 audio samples at any block size.
    check(CvDecimationForBlock(1) == 8,  "decimation(block=1) == 8 (VCV)");
    check(CvDecimationForBlock(8) == 1,  "decimation(block=8) == 1");
    check(CvDecimationForBlock(64) == 1, "decimation(block=64) == 1 (MetaModule)");

    // Smoothing: per-block coefficient equals per-sample applied block times.
    check(std::fabs(CvSmoothingForBlock(0.5f, 1) - 0.5f) < 1e-6f,
          "smoothing(0.5, block=1) == 0.5 (VCV unchanged)");
    check(CvSmoothingForBlock(0.5f, 64) > 0.99f,
          "smoothing(0.5, block=64) ~ 1 (settles within one block)");
    check(CvSmoothingForBlock(0.35f, 64) > 0.99f,
          "smoothing(0.35, block=64) ~ 1");

    // Pitch CV must never be quantized (1 V/oct).
    check(kPitchCvQuantizeStep == 0.0f, "pitch quantize step is 0");

    // A one-semitone CV step survives a pitch-configured conditioner exactly.
    beads::ControlConditioner c;
    c.Init(1, 1.0f, kPitchCvQuantizeStep, 0.0f);
    float semitone = 1.f / 12.f;
    check(c.Process(semitone) == semitone,
          "1/12 V passes through unquantized (0.05 V step returned 0.10 V)");

    // MetaModule-cadence settling: a step input reaches >99% of target after
    // one conditioned block, vs 35% with the old per-sample coefficient.
    beads::ControlConditioner mm;
    mm.Init(CvDecimationForBlock(64), CvSmoothingForBlock(0.35f, 64),
            kPitchCvQuantizeStep, 0.0f);
    mm.Process(0.0f);
    float after_one_block = mm.Process(1.0f);
    check(after_one_block > 0.99f, "MM pitch conditioner settles in ~1 block");

    std::printf(g_failures ? "\n%d FAILURES\n" : "\nall passed\n", g_failures);
    return g_failures ? 1 : 0;
}

#include "../../src/loooop/LooperModuleDSP.hpp"

#include <cmath>
#include <cstdio>

static int failures = 0;
static void check(bool condition, const char* name) {
    if (!condition) { std::printf("FAIL: %s\n", name); ++failures; }
    else std::printf("ok:   %s\n", name);
}
static bool near(float a, float b, float epsilon = 1e-6f) {
    return std::fabs(a - b) <= epsilon;
}

int main() {
    check(near(loooop::speedFromControls(1.5f, -1.25f), 1.0f), "linear speed CV sum");
    check(near(loooop::speedFromControls(2.0f, 10.0f), 2.0f), "linear speed clamps");
    check(near(loooop::speedFromVOct(1.0f, -1.0f), 0.5f), "negative V/oct speed");
    check(near(loooop::speedFromVOct(2.0f, 10.0f), 16.0f), "V/oct input and result clamp");
    check(near(loooop::normalizedControl(0.5f, 2.5f), 0.75f), "normalized control CV");
    check(near(loooop::normalizedControl(0.9f, 5.0f), 1.0f), "normalized control clamps");
    check(near(loooop::panControl(0.0f, 2.5f), 0.5f), "pan CV scaling");

    // NaN CV must land on a clamp bound (rack::clamp semantics), never
    // propagate into the engine. fmax(lo, fmin(NaN, hi)) == hi.
    const float nan = std::nanf("");
    check(near(loooop::speedFromControls(1.0f, nan), 2.0f), "NaN linear speed CV clamps");
    check(near(loooop::speedFromVOct(1.0f, nan), 16.0f), "NaN V/oct speed CV clamps");
    check(near(loooop::normalizedControl(0.5f, nan), 1.0f), "NaN normalized CV clamps");
    check(near(loooop::panControl(0.0f, nan), 1.0f), "NaN pan CV clamps");

    auto leftOnly = loooop::normalledStereo(true, 0.25f, false, 0.75f);
    check(near(leftOnly.l, 0.25f) && near(leftOnly.r, 0.25f), "left input normals to right");
    auto rightOnly = loooop::normalledStereo(false, 0.25f, true, 0.75f);
    check(near(rightOnly.l, 0.75f) && near(rightOnly.r, 0.75f), "right input normals to left");
    auto stereo = loooop::normalledStereo(true, 0.25f, true, 0.75f);
    check(near(stereo.l, 0.25f) && near(stereo.r, 0.75f), "stereo inputs stay independent");

    check(near(loooop::dryWet(0.25f, 0.75f, 0.4f), 0.45f), "dry wet mix");
    check(near(loooop::panLeftGain(-0.5f), 1.0f) && near(loooop::panRightGain(-0.5f), 0.5f),
          "left pan balance");
    check(near(loooop::panLeftGain(0.5f), 0.5f) && near(loooop::panRightGain(0.5f), 1.0f),
          "right pan balance");

    // Grid menu index -> engine segment count; anything out of range is Off.
    check(loooop::gridSegments(0) == 0,  "grid choice 0 = off");
    check(loooop::gridSegments(1) == 4,  "grid choice 1 = 4");
    check(loooop::gridSegments(2) == 8,  "grid choice 2 = 8");
    check(loooop::gridSegments(3) == 16, "grid choice 3 = 16");
    check(loooop::gridSegments(-1) == 0 && loooop::gridSegments(4) == 0,
          "grid choice out of range = off");

    loooop::OnePoleSmoother s;
    s.reset(0.f);
    s.alpha = loooop::smootherAlpha(48000.f, 0.002f);
    float v1 = s.process(1.f);
    check(v1 > 0.f && v1 < 0.02f, "smoother: first step bounded by alpha");
    float v = 0.f;
    for (int i = 0; i < 48000; ++i) v = s.process(1.f);
    check(std::fabs(v - 1.f) < 1e-3f, "smoother: settles at target");
    check(std::fabs(loooop::smootherAlpha(10.f, 0.002f) - 1.f) < 1e-6f,
          "smoother: alpha saturates to 1 at low sample rates");

    return failures == 0 ? 0 : 1;
}

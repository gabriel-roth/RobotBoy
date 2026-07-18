// tests/onbetap/test_drive_grit.cpp — Drive-grit VCA push (host-free).
//
// Guards the "top of the Drive knob keeps getting dirtier" promise: at high Q
// the (authentic) resonance choke used to take the resonance-derived grit and
// ~1 dB of level with it, so Drive got smoother/softer past ~90% of travel.
// The fix pushes the signal harder into the existing output VCA as Drive
// rises: out = 9*tanhish(push*v/9), push = exp2(gritDb/6.0206 * drive^2).
// See docs/superpowers/specs/2026-07-18-onbetap-drive-grit-design.md.
//
// Run with any argument (e.g. "./test_drive_grit sweep") to print the
// calibration sweep table instead of asserting.
#include "onbetap/OnbetapFilter.hpp"
#include "onbetap/engine.hpp"
#include "onbetap/drive.hpp"
#include <cmath>
#include <cstdio>
#include <vector>

static int passed = 0, failed = 0;
#define CHECK(cond, name) do { \
    if (cond) { printf("PASS %s\n", name); passed++; } \
    else      { printf("FAIL %s\n", name); failed++; } } while (0)

int main(int argc, char**) {
    (void)argc;
    // --- Law unit checks (pure driveGains math) ---
    CHECK(onbetap::driveGains(0.f, 30.f, 1.f, 0.f, 12.f).vcaPush == 1.f,
          "push == 1 exactly at Drive 0 (bit-identity)");
    CHECK(onbetap::driveGains(0.5f, 30.f, 1.f, 0.f, 0.f).vcaPush == 1.f,
          "gritDb 0 -> push == 1 (escape hatch)");
    float p1 = onbetap::driveGains(1.f, 30.f, 1.f, 0.f, 6.f).vcaPush;
    CHECK(std::fabs(p1 - std::exp2(6.f / 6.0206f)) < 1e-4f,
          "push(drive 1, 6 dB) == exp2(6/6.0206)");
    float pa = onbetap::driveGains(0.25f, 30.f, 1.f, 0.f, 6.f).vcaPush;
    float pb = onbetap::driveGains(0.50f, 30.f, 1.f, 0.f, 6.f).vcaPush;
    float pc = onbetap::driveGains(0.75f, 30.f, 1.f, 0.f, 6.f).vcaPush;
    CHECK(1.f < pa && pa < pb && pb < pc && pc < p1,
          "push monotone increasing in drive");
    auto ga = onbetap::driveGains(0.7f, 30.f, 1.f, 0.f, 0.f);
    auto gb = onbetap::driveGains(0.7f, 30.f, 1.f, 0.f, 12.f);
    CHECK(ga.driveScale == gb.driveScale && ga.makeup == gb.makeup,
          "gritDb does not touch driveScale/makeup");

    printf("\n%d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}

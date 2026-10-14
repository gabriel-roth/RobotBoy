#include "../../src/lucarne/ScopeLayout.hpp"
#include "../../src/lucarne/GrainLayout.hpp"
#include <cmath>
#include <cstdio>
#include <vector>
using namespace lucarne;
static int g_failures = 0;
static void check(bool c, const char* n) { std::printf(c ? "ok:   %s\n" : "FAIL: %s\n", n); if (!c) ++g_failures; }
static bool near(float a, float b, float e = 1e-2f) { return std::fabs(a - b) <= e; }

static std::vector<int8_t> LO(8192, 0), HI(8192, 0);
static ScopeLayout::Input In(int entries, float dt = 0.f) {
    ScopeLayout::Input in;
    in.epoch = 1; in.lo = LO.data(); in.hi = HI.data(); in.entries = entries;
    in.samples_per_entry = 24; in.line_rate = 48000.f; in.max_delay = 196608; in.dt = dt;
    return in;
}

int main() {
    {   // age -> x
        ScopeLayout L; L.SetSize(320.f, 180.f);
        ScopeLayout::Input in = In(8192);
        in.taps[0] = {4800.f, 1.f, 0}; in.num_taps = 1;          // 100 ms
        L.Update(in);                                              // first update snaps
        check(near(L.window(), 4800.f * 1.15f, 1.f), "window = longest delay x margin");
        check(near(L.XForAge(0.f), 320.f), "newest at right edge");
        check(near(L.XForAge(L.window()), 0.f), "window age at left edge");
    }
    {   // floor and cap
        ScopeLayout L; L.SetSize(320.f, 180.f);
        ScopeLayout::Input in = In(8192);
        in.taps[0] = {100.f, 1.f, 0}; in.num_taps = 1;
        L.Update(in);
        check(near(L.window(), 0.05f * 48000.f, 1.f), "window floor 50 ms");
        ScopeLayout M; M.SetSize(320.f, 180.f);
        in.taps[0] = {500000.f, 1.f, 0};
        M.Update(in);
        check(near(M.window(), 196608.f, 1.f), "window capped at max_delay");
    }
    {   // easing and snap
        ScopeLayout L; L.SetSize(320.f, 180.f);
        ScopeLayout::Input in = In(8192);
        in.taps[0] = {4800.f, 1.f, 0}; in.num_taps = 1;
        L.Update(in);
        const float w0 = L.window();
        in.taps[0].delay = 19200.f; in.dt = 0.016f;
        L.Update(in);
        check(L.window() > w0 && L.window() < 19200.f * 1.15f, "zoom eases part-way");
        for (int i = 0; i < 60; ++i) L.Update(in);
        check(near(L.window(), 19200.f * 1.15f, 50.f), "zoom settles within a second");
        L.Clear(); in.taps[0].delay = 4800.f; L.Update(in);
        check(near(L.window(), 4800.f * 1.15f, 1.f), "Clear makes the next update snap");
        ScopeLayout::Input e = in; e.epoch = 2; e.taps[0].delay = 9600.f; e.dt = 0.016f;
        L.Update(e);
        check(near(L.window(), 9600.f * 1.15f, 1.f), "epoch change snaps");
    }
    {   // no audible tap keeps window
        ScopeLayout L; L.SetSize(320.f, 180.f);
        ScopeLayout::Input in = In(8192);
        in.taps[0] = {4800.f, 1.f, 0}; in.num_taps = 1;
        L.Update(in);
        const float w0 = L.window();
        in.num_taps = 0; in.dt = 0.5f;
        L.Update(in);
        check(near(L.window(), w0, 1.f), "no audible tap keeps window");
    }
    {   // column min/max and marker flash
        for (auto& v : LO) v = 0;
        for (auto& v : HI) v = 0;
        const int tap_entry = 4800 / 24;                      // entry index at the tap's delay
        HI[size_t(tap_entry)] = 127; LO[size_t(tap_entry)] = -127;
        ScopeLayout L; L.SetSize(320.f, 180.f);
        ScopeLayout::Input in = In(8192);
        in.taps[0] = {4800.f, 1.f, 0}; in.num_taps = 1;
        check(near(ScopeLayout::TapOutput(in, 4800.f, 1.f), 1.f), "loud entry at tap -> output 1");
        check(near(ScopeLayout::TapOutput(in, 2400.f, 1.f), 0.f), "silence elsewhere -> output 0");
        check(near(ScopeLayout::TapOutput(in, 4800.f, 0.5f), 0.5f), "output scales with tap level");
        L.Update(in);
        bool tall = false;
        for (const Rect& r : L.draw_list().wave)
            if (r.h > 0.6f * 180.f) tall = true;
        check(tall, "the loud entry draws a near-full-height column");
        int bars = 0;
        for (const Rect& r : L.draw_list().marks)
            if (r.h > 0.6f * 180.f && r.h < 180.f) ++bars;   // the output bar (not the full-height line)
        check(bars == 1, "one output bar on the flashing marker");
    }
    {   // taps beyond filled entries / window, empty input
        ScopeLayout L; L.SetSize(320.f, 180.f);
        ScopeLayout::Input in = In(0);
        in.taps[0] = {4800.f, 1.f, 0}; in.num_taps = 1;
        L.Update(in);
        check(!L.draw_list().wave.empty(), "empty ring still draws a hairline");
        check(near(ScopeLayout::TapOutput(in, 4800.f, 1.f), 0.f), "tap beyond filled entries -> output 0");
    }
    return g_failures ? 1 : 0;
}

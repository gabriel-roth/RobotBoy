#include "../../src/lucarne/ScopeLayout.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>
using namespace lucarne;
static int g_failures = 0;
static void check(bool c, const char* n) { std::printf(c ? "ok:   %s\n" : "FAIL: %s\n", n); if (!c) ++g_failures; }
static bool near(float a, float b, float e = 1e-2f) { return std::fabs(a - b) <= e; }
static bool sameColor(Rgb a, Rgb b) { return a.r == b.r && a.g == b.g && a.b == b.b; }

static std::vector<int8_t> LO(8192, 0), HI(8192, 0);
static ScopeLayout::Input In(int entries, float dt = 0.f) {
    ScopeLayout::Input in;
    in.epoch = 1; in.lo = LO.data(); in.hi = HI.data(); in.entries = entries;
    in.samplesPerEntry = 24; in.lineRate = 48000.f; in.maxDelay = 196608; in.dt = dt;
    return in;
}

int main() {
    {   // tap colors: ratio 0 warm white (the grain view's center), ratio 1 green
        const Rgb w = ScopeLayout::tapColor(0), w1 = ScopeLayout::tapColor(1), g = ScopeLayout::tapColor(2);
        check(w.r == 0xE8 && w.g == 0xE4 && w.b == 0xDA && sameColor(w, w1), "ratio 0 is warm white");
        check(g.r == 0x8F && g.g == 0xD3 && g.b == 0xA0, "ratio 1 is green");
    }
    {   // age -> x
        ScopeLayout L; L.setSize(320.f, 180.f);
        ScopeLayout::Input in = In(8192);
        in.taps[0] = {4800.f, 1.f, 0}; in.numTaps = 1;          // 100 ms
        L.update(in);                                              // first update snaps
        check(near(L.getWindow(), 4800.f * 1.15f, 1.f), "window = longest delay x margin");
        check(near(L.nowX(), 320.f * (1.f - ScopeLayout::kNowInset)), "now sits inset from the right edge");
        check(near(L.xForAge(0.f), L.nowX()), "newest audio at the now line");
        check(near(L.xForAge(L.getWindow()), 0.f), "window age at left edge");
    }
    {   // floor and cap
        ScopeLayout L; L.setSize(320.f, 180.f);
        ScopeLayout::Input in = In(8192);
        in.taps[0] = {100.f, 1.f, 0}; in.numTaps = 1;
        L.update(in);
        check(near(L.getWindow(), 0.05f * 48000.f, 1.f), "window floor 50 ms");
        ScopeLayout M; M.setSize(320.f, 180.f);
        in.taps[0] = {500000.f, 1.f, 0};
        M.update(in);
        check(near(M.getWindow(), 196608.f, 1.f), "window capped at max_delay");
    }
    {   // zero line rate never yields a zero or NaN window
        ScopeLayout L; L.setSize(320.f, 180.f);
        ScopeLayout::Input in = In(8192, 0.016f);
        in.lineRate = 0.f;
        in.taps[0] = {0.f, 1.f, 0}; in.numTaps = 1;
        L.update(in);
        L.update(in);
        check(std::isfinite(L.getWindow()) && L.getWindow() > 0.f, "zero line rate: window finite and positive");
    }
    {   // easing and snap
        ScopeLayout L; L.setSize(320.f, 180.f);
        ScopeLayout::Input in = In(8192);
        in.taps[0] = {4800.f, 1.f, 0}; in.numTaps = 1;
        L.update(in);
        const float w0 = L.getWindow();
        in.taps[0].delay = 19200.f; in.dt = 0.016f;
        L.update(in);
        check(L.getWindow() > w0 && L.getWindow() < 19200.f * 1.15f, "zoom eases part-way");
        for (int i = 0; i < 60; ++i) L.update(in);
        check(near(L.getWindow(), 19200.f * 1.15f, 50.f), "zoom settles within a second");
        L.clear(); in.taps[0].delay = 4800.f; L.update(in);
        check(near(L.getWindow(), 4800.f * 1.15f, 1.f), "Clear makes the next update snap");
        ScopeLayout::Input e = in; e.epoch = 2; e.taps[0].delay = 9600.f; e.dt = 0.016f;
        L.update(e);
        check(near(L.getWindow(), 9600.f * 1.15f, 1.f), "epoch change snaps");
    }
    {   // no audible tap keeps window
        ScopeLayout L; L.setSize(320.f, 180.f);
        ScopeLayout::Input in = In(8192);
        in.taps[0] = {4800.f, 1.f, 0}; in.numTaps = 1;
        L.update(in);
        const float w0 = L.getWindow();
        in.numTaps = 0; in.dt = 0.5f;
        L.update(in);
        check(near(L.getWindow(), w0, 1.f), "no audible tap keeps window");
    }
    {   // column min/max and marker flash
        for (auto& v : LO) v = 0;
        for (auto& v : HI) v = 0;
        const int tap_entry = 4800 / 24;                      // entry index at the tap's delay
        HI[size_t(tap_entry)] = 127; LO[size_t(tap_entry)] = -127;
        ScopeLayout L; L.setSize(320.f, 180.f);
        ScopeLayout::Input in = In(8192);
        in.taps[0] = {4800.f, 1.f, 0}; in.numTaps = 1;
        check(near(ScopeLayout::tapOutput(in, 4800.f, 1.f), 1.f), "loud entry at tap -> output 1");
        check(near(ScopeLayout::tapOutput(in, 2400.f, 1.f), 0.f), "silence elsewhere -> output 0");
        check(near(ScopeLayout::tapOutput(in, 4800.f, 0.5f), 0.5f), "output scales with tap level");
        L.update(in);
        bool tall = false;
        for (const Rect& r : L.getDrawList().wave)
            if (r.h > 0.6f * 180.f) tall = true;
        check(tall, "the loud entry draws a near-full-height column");
        int bars = 0;
        for (const Rect& r : L.getDrawList().marks)
            if (r.h > 0.6f * 180.f && r.h < 180.f) ++bars;   // the output bar (not the full-height line)
        check(bars == 1, "one output bar on the flashing marker");
    }
    {   // taps beyond filled entries / window, empty input
        ScopeLayout L; L.setSize(320.f, 180.f);
        ScopeLayout::Input in = In(0);
        in.taps[0] = {4800.f, 1.f, 0}; in.numTaps = 1;
        L.update(in);
        check(!L.getDrawList().wave.empty(), "empty ring still draws a hairline");
        check(near(ScopeLayout::tapOutput(in, 4800.f, 1.f), 0.f), "tap beyond filled entries -> output 0");
    }
    {   // dry playhead: a line at now that flashes with the newest input
        for (auto& v : LO) v = 0; for (auto& v : HI) v = 0;
        ScopeLayout L; L.setSize(320.f, 180.f);
        ScopeLayout::Input in = In(8192);
        in.taps[0] = {4800.f, 1.f, 0}; in.numTaps = 1;
        check(near(ScopeLayout::dryOutput(in), 0.f), "silent input -> dry output 0");
        L.update(in);
        const Rgb d = ScopeLayout::kDryColor;
        bool line = false;
        for (const Rect& r : L.getDrawList().marks)
            if (near(r.h, 180.f) && near(r.x + r.w * 0.5f, L.nowX(), 0.5f)) line = true;
        check(line, "dry line drawn at now even when silent");
        HI[3] = 127; LO[3] = -127;   // a loud entry within the newest 5 ms (3 x 24 samples = 1.5 ms)
        check(near(ScopeLayout::dryOutput(in), 1.f), "loud newest input -> dry output 1");
        HI[3] = 0; LO[3] = 0;
        HI[20] = 127; LO[20] = -127; // 10 ms old: outside the dry window
        check(near(ScopeLayout::dryOutput(in), 0.f), "older audio does not light the dry line");
        HI[20] = 0; LO[20] = 0;
        HI[0] = 64; LO[0] = -64;
        L.update(in);
        bool bar = false;
        for (const Rect& r : L.getDrawList().marks)
            if (r.h > 0.f && r.h < 180.f && near(r.x + r.w * 0.5f, L.nowX(), 0.5f)) bar = true;
        check(bar, "dry bar on the now line when input is loud");
        bool waveright = true;
        for (const Rect& r : L.getDrawList().wave) if (r.x + r.w > L.nowX() + 0.01f) waveright = false;
        check(waveright, "no waveform right of the now line");
        (void)d;
        HI[0] = 0; LO[0] = 0;
    }
    // --- Slice: frozen scope, one shaded band and moving marker per slice head ---
    auto sliceTap = [](float delay, int slot, float oldest, float length) {
        ScopeLayout::Tap t{delay, 1.f, slot};
        t.slice = true; t.sliceOldest = oldest; t.sliceLength = length;
        return t;
    };
    {   // one band over the slice, behind the waveform, in a faint tap color
        for (auto& v : LO) v = 0; for (auto& v : HI) v = 0;
        ScopeLayout L; L.setSize(320.f, 180.f);
        ScopeLayout::Input in = In(8192);
        in.taps[0] = sliceTap(2400.f, 0, 4800.f, 4800.f);
        in.taps[1] = {9600.f / 1.15f, 1.f, 1};   // a running tap sets a 9600-wide window
        in.numTaps = 2;
        L.update(in);
        check(near(L.getWindow(), 9600.f, 1.f), "slice band test: 9600-wide window");
        const auto& u = L.getDrawList().under;
        check(u.size() == 1, "one under rect for one slice tap");
        if (u.size() == 1) {
            check(near(u[0].x, L.xForAge(4800.f), 0.05f), "band starts at the slice's oldest end");
            check(near(u[0].x + u[0].w, L.xForAge(0.f), 0.05f), "band ends at the slice's newest end (now)");
            check(near(u[0].y, 0.f) && near(u[0].h, 180.f), "band is full height");
            const Rgb want = mix(L.getDrawList().background, ScopeLayout::tapColor(0), ScopeLayout::kSliceBandMix);
            check(sameColor(u[0].color, want), "band color: 15% tap color over the background");
        }
        check(near(ScopeLayout::kSliceBandMix, 0.15f), "slice band mix is 0.15");
    }
    {   // zoom uses the slice's oldest end, not the read point
        ScopeLayout L; L.setSize(320.f, 180.f);
        ScopeLayout::Input in = In(8192);
        in.taps[0] = sliceTap(1000.f, 0, 9600.f, 4800.f);
        in.numTaps = 1;
        L.update(in);
        check(near(L.getWindow(), 9600.f * 1.15f, 1.f), "slice zoom target = oldest end x margin");
    }
    {   // the slice marker sits at the read point and flashes like a running tap
        for (auto& v : LO) v = 0; for (auto& v : HI) v = 0;
        HI[size_t(2400 / 24)] = 127; LO[size_t(2400 / 24)] = -127;
        ScopeLayout L; L.setSize(320.f, 180.f);
        ScopeLayout::Input in = In(8192);
        in.taps[0] = sliceTap(2400.f, 0, 4800.f, 4800.f);
        in.numTaps = 1;
        L.update(in);
        const float x = L.xForAge(2400.f);
        bool line = false, bar = false;
        for (const Rect& r : L.getDrawList().marks) {
            if (!near(r.x + r.w * 0.5f, x, 0.05f)) continue;
            if (near(r.h, 180.f)) line = true;
            else if (r.h > 0.6f * 180.f) bar = true;
        }
        check(line, "slice marker line at the read point");
        check(bar, "slice marker flashes with a loud entry at its read point");
        for (auto& v : LO) v = 0; for (auto& v : HI) v = 0;
    }
    {   // second-ratio slice head: green band
        ScopeLayout L; L.setSize(320.f, 180.f);
        ScopeLayout::Input in = In(8192);
        in.taps[0] = sliceTap(2400.f, 2, 4800.f, 4800.f);
        in.numTaps = 1;
        L.update(in);
        const auto& u = L.getDrawList().under;
        const Rgb want = mix(L.getDrawList().background, ScopeLayout::kTapGreen, 0.15f);
        check(u.size() == 1 && sameColor(u[0].color, want), "second-ratio slice band is green");
    }
    {   // a band reaching past the window's left edge is clipped to [0, nowX()]
        ScopeLayout L; L.setSize(320.f, 180.f);
        ScopeLayout::Input in = In(8192);
        in.taps[0] = {4800.f, 1.f, 0}; in.numTaps = 1;
        L.update(in);                                  // window snaps to 5520
        in.taps[0] = sliceTap(2400.f, 0, 19200.f, 19200.f); in.dt = 0.016f;
        L.update(in);                                  // eases part-way toward 22080
        check(L.getWindow() < 19200.f, "window still easing out");
        const auto& u = L.getDrawList().under;
        check(u.size() == 1, "partly visible band still drawn");
        if (u.size() == 1) {
            check(near(u[0].x, 0.f), "band clipped at the left edge");
            check(near(u[0].x + u[0].w, L.nowX(), 0.05f), "band clipped at now");
        }
        ScopeLayout M; M.setSize(320.f, 180.f);
        ScopeLayout::Input c = In(8192);
        c.maxDelay = 10000;                           // window capped at 10000
        c.taps[0] = sliceTap(9000.f, 0, 40000.f, 20000.f);
        c.numTaps = 1;
        M.update(c);
        check(M.getDrawList().under.empty(), "band entirely outside the window is skipped");
    }
    {   // running taps draw no bands
        ScopeLayout L; L.setSize(320.f, 180.f);
        ScopeLayout::Input in = In(8192);
        in.taps[0] = {4800.f, 1.f, 0}; in.taps[1] = {2400.f, 1.f, 2}; in.numTaps = 2;
        L.update(in);
        check(L.getDrawList().under.empty(), "running taps produce no under rects");
        in.taps[0] = sliceTap(2400.f, 0, 4800.f, 4800.f); in.numTaps = 1;
        L.update(in);
        L.clear();
        check(L.getDrawList().under.empty(), "Clear empties the bands");
    }
    {   // Slice (frozen): the whole buffer, fixed, until Slice turns off
        for (auto& v : LO) v = 0; for (auto& v : HI) v = 0;
        ScopeLayout L; L.setSize(320.f, 180.f);
        ScopeLayout::Input run = In(8192, 0.016f);
        run.taps[0] = {4800.f, 1.f, 0}; run.numTaps = 1;
        L.update(run);
        ScopeLayout::Input in = run;
        in.frozen = true;
        in.taps[0] = {1000.f, 1.f, 0, true, 9600.f, 4800.f};
        L.update(in);
        check(near(L.getWindow(), 196608.f, 1.f), "Slice on: window jumps to the whole buffer");
        in.taps[0] = {1000.f, 1.f, 0, true, 2400.f, 1200.f};
        for (int i = 0; i < 30; ++i) L.update(in);
        in.numTaps = 0;
        for (int i = 0; i < 30; ++i) L.update(in);
        check(near(L.getWindow(), 196608.f, 1.f), "Slice: window never changes while frozen");
        for (int i = 0; i < 120; ++i) L.update(run);
        check(near(L.getWindow(), 4800.f * 1.15f, 50.f), "Slice off: zoom follows the taps again");
    }
    {   // Modulated: the zoom eases out to the whole buffer and stays
        ScopeLayout L; L.setSize(320.f, 180.f);
        ScopeLayout::Input in = In(8192, 0.016f);
        in.taps[0] = {4800.f, 1.f, 0}; in.numTaps = 1;
        L.update(in);
        const float w0 = L.getWindow();
        in.modulated = true;
        L.update(in);
        check(L.getWindow() > w0 && L.getWindow() < 196608.f, "modulated: zoom eases part-way out");
        for (int i = 0; i < 14; ++i) L.update(in);   // 0.24 s: the normal glide would be ~95% there
        {
            const float done = std::log(L.getWindow() / w0) / std::log(196608.f / w0);
            check(done > 0.6f && done < 0.85f, "modulated: the ease-out is slower than a knob glide");
        }
        for (int i = 0; i < 240; ++i) L.update(in);
        check(near(L.getWindow(), 196608.f, 1.f), "modulated: zoom reaches the whole buffer");
        in.taps[0].delay = 9600.f;
        for (int i = 0; i < 30; ++i) L.update(in);
        check(near(L.getWindow(), 196608.f, 1.f), "modulated: a tap change doesn't zoom");
        in.taps[0].level = 0.f;
        for (int i = 0; i < 120; ++i) L.update(in);
        check(near(L.getWindow(), 196608.f, 1.f), "modulated with no audible tap: whole buffer");
        in.taps[0].level = 1.f; in.modulated = false;
        for (int i = 0; i < 120; ++i) L.update(in);
        check(near(L.getWindow(), 9600.f * 1.15f, 50.f), "modulation off: zoom eases back to the taps");
    }
    {   // Modulated on the first update snaps
        ScopeLayout L; L.setSize(320.f, 180.f);
        ScopeLayout::Input in = In(8192, 0.016f);
        in.taps[0] = {4800.f, 1.f, 0}; in.numTaps = 1; in.modulated = true;
        L.update(in);
        check(near(L.getWindow(), 196608.f, 1.f), "modulated snap: whole buffer at once");
    }
    {   // Frozen (Slice) still jumps, even while modulated
        ScopeLayout L; L.setSize(320.f, 180.f);
        ScopeLayout::Input in = In(8192, 0.016f);
        in.taps[0] = {4800.f, 1.f, 0}; in.numTaps = 1;
        L.update(in);
        in.modulated = true; in.frozen = true;
        L.update(in);
        check(near(L.getWindow(), 196608.f, 1.f), "frozen wins over easing");
    }
    {   // Shape dip: the longest tap silent for 2% of its period doesn't move the zoom
        ScopeLayout L; L.setSize(320.f, 180.f);
        ScopeLayout::Input in = In(8192, 1.f / 60.f);
        in.taps[0] = {48000.f, 1.f, 0}; in.taps[1] = {4800.f, 1.f, 2}; in.numTaps = 2;   // 1 s and 0.1 s
        for (int i = 0; i < 120; ++i) L.update(in);
        const float w0 = L.getWindow();
        in.taps[0].level = 0.f;                   // 20 ms dip: 2 frames at 60 fps
        L.update(in); L.update(in);
        in.taps[0].level = 1.f;
        L.update(in);
        check(near(L.getWindow(), w0, 1.f), "Shape dip: zoom doesn't move");
    }
    {   // Real fade-out: held for the hold time, then eases in
        ScopeLayout L; L.setSize(320.f, 180.f);
        ScopeLayout::Input in = In(8192, 0.01f);
        in.taps[0] = {48000.f, 1.f, 0}; in.taps[1] = {4800.f, 1.f, 2}; in.numTaps = 2;
        for (int i = 0; i < 120; ++i) L.update(in);
        const float w0 = L.getWindow();
        in.taps[0].level = 0.f;
        for (int i = 0; i < 8; ++i) L.update(in);     // 80 ms of the 100 ms hold (5% of 1 s + 50 ms)
        check(near(L.getWindow(), w0, 1.f), "fade-out: zoom holds during the hold");
        for (int i = 0; i < 200; ++i) L.update(in);
        check(near(L.getWindow(), 4800.f * 1.15f, 50.f), "fade-out: zoom then eases to the next tap");
    }
    {   // A held, silent tap draws no marker
        ScopeLayout L; L.setSize(320.f, 180.f);
        ScopeLayout::Input in = In(8192, 0.016f);
        in.taps[0] = {48000.f, 1.f, 0}; in.numTaps = 1;
        L.update(in);
        const size_t marksAudible = L.getDrawList().marks.size();
        in.taps[0].level = 0.f;
        L.update(in);
        check(L.getDrawList().marks.size() < marksAudible, "held tap draws no marker");
        in.taps[0] = {1000.f, 0.f, 0, true, 9600.f, 4800.f};
        L.update(in);
        check(L.getDrawList().under.empty(), "silent slice head draws no band");
    }
    {   // Stale hold decays even when the slot isn't passed; clear() resets holds
        ScopeLayout L; L.setSize(320.f, 180.f);
        ScopeLayout::Input in = In(8192, 0.016f);
        in.taps[0] = {96000.f, 1.f, 0}; in.taps[1] = {4800.f, 1.f, 2}; in.numTaps = 2;
        L.update(in);                                        // slot 0 hold: 0.15 s
        in.taps[0] = in.taps[1]; in.numTaps = 1;             // slot 0 absent for 1 s
        in.dt = 1.f; L.update(in);
        in.dt = 0.016f;
        // The dt = 1 s update eased the window all the way to slot 2's zoom.
        // A stale slot 0 hold (0.15 s) would pull it back out within 5 frames.
        in.taps[0] = {96000.f, 0.f, 0}; in.taps[1] = {4800.f, 1.f, 2}; in.numTaps = 2;
        for (int i = 0; i < 5; ++i) L.update(in);
        check(near(L.getWindow(), 4800.f * 1.15f, 50.f), "stale hold decays");
        ScopeLayout M; M.setSize(320.f, 180.f);
        ScopeLayout::Input m = In(8192, 0.016f);
        m.taps[0] = {96000.f, 1.f, 0}; m.taps[1] = {4800.f, 1.f, 2}; m.numTaps = 2;
        M.update(m);
        M.clear();
        m.taps[0].level = 0.f;
        M.update(m);
        check(near(M.getWindow(), 4800.f * 1.15f, 1.f), "clear resets holds");
    }
    {   // Zero line rate: the hold stays finite
        ScopeLayout L; L.setSize(320.f, 180.f);
        ScopeLayout::Input in = In(8192, 0.016f);
        in.lineRate = 0.f;
        in.taps[0] = {48000.f, 1.f, 0}; in.taps[1] = {4800.f, 1.f, 2}; in.numTaps = 2;
        L.update(in);
        in.taps[0].level = 0.f;
        in.dt = 0.5f;
        for (int i = 0; i < 200; ++i) L.update(in);
        check(std::isfinite(L.getWindow()) && near(L.getWindow(), 4800.f * 1.15f, 50.f), "zero line rate hold finite");
    }
    {   // How much of the scope ring to read before an update
        check(ScopeLayout::entriesToRead(48000.f, 4800.f, false, 24, 8192) == 2002, "read: the current window when wider");
        check(ScopeLayout::entriesToRead(2400.f, 4800.f, false, 24, 8192) == 232, "read: the longest tap's zoom when wider");
        check(ScopeLayout::entriesToRead(5520.f, 4800.f, true, 24, 8192) == 8192, "read: everything when heading for the whole buffer");
        check(ScopeLayout::entriesToRead(1e9f, 0.f, false, 24, 8192) == 8192, "read: capped at the ring");
        check(ScopeLayout::entriesToRead(4800.f, 0.f, false, 0, 8192) > 0, "read: zero samples per entry is safe");
    }
    {   // primary ratio: the louder group is white, whichever slots it holds
        check(sameColor(ScopeLayout::tapColor(2, 1), ScopeLayout::kTapWhite), "ratio 1 is white when primary");
        check(sameColor(ScopeLayout::tapColor(0, 1), ScopeLayout::kTapGreen), "ratio 0 is green when not primary");
        auto markerColor = [](const ScopeLayout& L) {   // the first tap's line (after the dry playhead's)
            return L.getDrawList().marks.at(1).color;
        };
        ScopeLayout L; L.setSize(320.f, 180.f);
        ScopeLayout::Input in = In(8192);
        in.taps[0] = {4800.f, 1.f, 2}; in.numTaps = 1;     // a ratio-1 tap
        in.ratioGain[0] = 0.2f; in.ratioGain[1] = 0.8f;
        L.update(in);                                       // first update: louder group, no hysteresis
        check(L.getPrimary() == 1, "snap: the louder group is primary");
        const Rgb whiteLine = markerColor(L);
        in.ratioGain[0] = 0.8f; in.ratioGain[1] = 0.2f;
        L.update(in);
        check(L.getPrimary() == 0, "a clearly louder group takes over");
        check(!sameColor(markerColor(L), whiteLine), "the ratio-1 tap turns green");
        in.ratioGain[0] = 0.47f; in.ratioGain[1] = 0.53f;   // just past even: inside the hysteresis
        L.update(in);
        check(L.getPrimary() == 0, "hysteresis: a slight lead doesn't swap");
        in.ratioGain[0] = 0.4f; in.ratioGain[1] = 0.6f;
        L.update(in);
        check(L.getPrimary() == 1, "past the hysteresis it swaps");
        check(sameColor(markerColor(L), whiteLine), "the ratio-1 tap is white again");
        in.ratioGain[0] = 0.8f; in.ratioGain[1] = 0.8f;     // unclocked at full: a tie
        L.clear(); L.update(in);
        check(L.getPrimary() == 0, "a tie on snap goes to ratio 0");
    }
    {   // default Input: ratio 0 primary, as before
        ScopeLayout::Input in;
        check(in.ratioGain[0] > in.ratioGain[1], "default gains make ratio 0 primary");
    }
    return g_failures ? 1 : 0;
}

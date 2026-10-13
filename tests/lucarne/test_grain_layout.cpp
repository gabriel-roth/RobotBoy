#include "../../src/lucarne/GrainLayout.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace lucarne;
using robotboy_beads::GrainLiveFrame;
using robotboy_beads::GrainStartEvent;

static int g_failures = 0;
static void check(bool cond, const char* name) {
    if (!cond) { std::printf("FAIL: %s\n", name); ++g_failures; }
    else       { std::printf("ok:   %s\n", name); }
}
static bool near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) <= eps; }

static constexpr int32_t M = 1000;   // delay-line length for these tests
static int8_t lo[256], hi[256];

static GrainLiveFrame Frame() {
    GrainLiveFrame f;
    f.write_index = 500;
    f.max_delay = M;
    return f;
}
static void Live(GrainLiveFrame& f, int slot, uint32_t serial, int32_t pos, float elapsed = 0.f,
                 float level = 1.f) {
    f.slots[slot] = {serial, pos, level, false, elapsed};
}
static GrainLayout::Input In(const GrainLiveFrame* f, const GrainStartEvent* e, int n,
                             float dt = 0.f, uint32_t epoch = 1) {
    GrainLayout::Input in;
    in.epoch = epoch; in.peak_lo = lo; in.peak_hi = hi;
    in.frame = f; in.events = e; in.num_events = n; in.dt = dt; in.quality = 0;
    return in;
}

int main() {
    GrainLayout L;
    L.SetSize(320.f, 180.f);

    // x mapping: index M-1 at the left edge, index 0 at the right.
    check(near(L.XForIndex(M - 1, M), 0.f), "x: oldest index at left");
    check(near(L.XForIndex(0, M), 320.f * (M - 1) / M), "x: index 0 near right");

    // Rows: band is 36 tall centred at 90; rows are (180-36)/24 = 6 tall.
    check(near(L.RowY(+1, 0), 72.f - 3.f), "row 0 above sits on the band");
    check(near(L.RowY(-1, 0), 108.f + 3.f), "row 0 below sits on the band");
    check(near(L.RowY(+1, 11), 72.f - 69.f), "row 11 above at the top");

    // Pitch -> side and row.
    check(GrainLayout::SideFor(1.f) == +1 && GrainLayout::PreferredRow(1.f) == 0, "unison: above, row 0");
    check(GrainLayout::SideFor(4.f) == +1 && GrainLayout::PreferredRow(4.f) == 11, "+24 st: top row");
    check(GrainLayout::SideFor(0.5f) == -1 && GrainLayout::PreferredRow(0.5f) == 6, "-12 st: below, row 6");
    check(GrainLayout::PreferredRow(16.f) == 11, "beyond +24 st clamps");
    check(GrainLayout::SideFor(-2.f) == +1 && GrainLayout::PreferredRow(-2.f) == 6, "reverse +12 st: above, row 6");

    // Birth, live cursor, end, fade.
    {
        GrainLayout G; G.SetSize(320.f, 180.f);
        GrainLiveFrame f = Frame();
        Live(f, 3, 7, 600);
        GrainStartEvent e{7, 3, 600, 1.f, 100.f};   // speed 1, 100 samples
        G.Update(In(&f, &e, 1));
        check(G.grains().size() == 1, "birth creates one visual grain");
        const VisualGrain& g = G.grains()[0];
        check(near(g.x0, 320.f * (M - 1 - 600) / M), "start x from start index");
        check(near(g.span, 320.f * 100.f / M), "span = speed*duration in x");
        check(!g.ended && near(g.cursor, 0.f), "live at start");
        Live(f, 3, 7, 550, 50.f);                   // played 50 of 100
        G.Update(In(&f, nullptr, 0, 0.02f));
        check(near(G.grains()[0].cursor, 0.5f), "cursor halfway");
        f.slots[3].done = true;
        G.Update(In(&f, nullptr, 0, 0.02f));
        check(G.grains()[0].ended && near(G.grains()[0].cursor, 0.5f), "ended keeps its cursor");
        G.Update(In(&f, nullptr, 0, 0.10f));
        check(G.grains().size() == 1, "still fading at 100 ms");
        G.Update(In(&f, nullptr, 0, 0.06f));
        check(G.grains().empty(), "gone after 150 ms");
    }

    // A grain spanning more than half the buffer: the cursor must not wrap.
    {
        GrainLayout G; G.SetSize(320.f, 180.f);
        GrainLiveFrame f = Frame();
        Live(f, 0, 1, 800);
        GrainStartEvent e{1, 0, 800, 1.f, 800.f};   // speed 1, 0.8 of the buffer
        G.Update(In(&f, &e, 1));
        Live(f, 0, 1, 200, 600.f);                  // 600 of 800 played
        G.Update(In(&f, nullptr, 0, 0.02f));
        check(!G.grains()[0].ended && near(G.grains()[0].cursor, 0.75f), "cursor past half the buffer");
    }

    // A grain that ended before its event was read is born ended, cursor at 1.
    {
        GrainLayout G; G.SetSize(320.f, 180.f);
        GrainLiveFrame f = Frame();
        f.slots[2] = {9, 100, 0.f, true, 0.f};
        GrainStartEvent e{8, 2, 300, 1.f, 50.f};
        G.Update(In(&f, &e, 1));
        check(G.grains().size() == 1 && G.grains()[0].ended && near(G.grains()[0].cursor, 1.f),
              "born-ended grain shows with cursor at end");
    }

    // Reversed grain spans leftward and wraps at the left edge.
    {
        GrainLayout G; G.SetSize(320.f, 180.f);
        GrainLiveFrame f = Frame();
        Live(f, 0, 1, 990);
        GrainStartEvent e{1, 0, 990, -1.f, 100.f};   // x0 = 2.88, span = -32
        G.Update(In(&f, &e, 1));
        check(G.grains()[0].span < 0.f, "reversed span is negative");
        int pieces = 0;
        for (const Rect& r : G.draw_list().marks)
            if (r.y < 72.f && r.w > 1.f && r.h < 4.f) ++pieces;   // line rects above the band
        check(pieces == 2, "wrapped line draws as two pieces");
    }

    // Collisions push to the next free row; non-overlapping grains share a row.
    {
        GrainLayout G; G.SetSize(320.f, 180.f);
        GrainLiveFrame f = Frame();
        Live(f, 0, 1, 600); Live(f, 1, 1, 600); Live(f, 2, 1, 100);
        GrainStartEvent e[3] = {{1, 0, 600, 1.f, 100.f}, {1, 1, 600, 1.f, 100.f}, {1, 2, 100, 1.f, 100.f}};
        G.Update(In(&f, e, 3));
        check(G.grains()[0].row == 0 && G.grains()[1].row == 1, "overlap moves outward");
        check(G.grains()[2].row == 0, "no overlap: same row");
    }

    // Epoch change drops grains.
    {
        GrainLayout G; G.SetSize(320.f, 180.f);
        GrainLiveFrame f = Frame();
        Live(f, 0, 1, 600);
        GrainStartEvent e{1, 0, 600, 1.f, 100.f};
        G.Update(In(&f, &e, 1, 0.f, 1));
        G.Update(In(&f, nullptr, 0, 0.f, 2));
        check(G.grains().empty(), "epoch change drops grains");
    }

    // Pool is capped; born-ended grains still show.
    {
        GrainLayout G; G.SetSize(320.f, 180.f);
        GrainLiveFrame f = Frame();
        std::vector<GrainStartEvent> ev;
        for (uint32_t i = 0; i < 200; ++i) ev.push_back({i + 1, int32_t(i % 32), int32_t(i * 5 % M), 1.f, 20.f});
        G.Update(In(&f, ev.data(), int(ev.size())));
        check(int(G.grains().size()) == GrainLayout::kMaxGrains, "pool capped at kMaxGrains");
    }

    // Minimum length: a 1-sample grain still draws 2 px... of a 320-wide display.
    {
        GrainLayout G; G.SetSize(320.f, 180.f);
        GrainLiveFrame f = Frame();
        Live(f, 0, 1, 600);
        GrainStartEvent e{1, 0, 600, 1.f, 1.f};
        G.Update(In(&f, &e, 1));
        float widest = 0.f;
        for (const Rect& r : G.draw_list().marks) if (r.y < 72.f && r.h < 4.f) widest = std::max(widest, r.w);
        check(widest >= 2.f - 1e-3f, "tiny grain is at least 2 units long");
    }

    // Clear forgets everything.
    {
        GrainLayout G; G.SetSize(320.f, 180.f);
        GrainLiveFrame f = Frame();
        Live(f, 0, 1, 600);
        GrainStartEvent e{1, 0, 600, 1.f, 100.f};
        G.Update(In(&f, &e, 1));
        G.Clear();
        check(G.grains().empty() && G.draw_list().marks.empty(), "Clear empties grains and marks");
    }

    // Waveform: one rect per column, sized by the peaks.
    {
        GrainLayout G; G.SetSize(320.f, 180.f);
        for (int c = 0; c < 256; ++c) { lo[c] = -127; hi[c] = 127; }
        G.Update(In(nullptr, nullptr, 0));
        check(G.draw_list().wave.size() == 256, "256 waveform columns");
        const Rect& r = G.draw_list().wave[10];
        check(near(r.y, 72.f) && near(r.h, 36.f), "full-scale column fills the band");
    }

    std::printf(g_failures ? "\n%d FAILED\n" : "\nall passed\n", g_failures);
    return g_failures ? 1 : 0;
}

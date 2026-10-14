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
    in.frame = f; in.events = e; in.num_events = n; in.dt = dt;
    return in;
}

static GrainLiveFrame TapFrame(bool slice = false) {
    GrainLiveFrame f = Frame();          // write 500, M = 1000
    f.tap_mode = true;
    for (auto& s : f.slots) s.done = true;
    for (auto& t : f.taps) { t = {}; t.read_index = 500; t.slice = slice; }
    return f;
}
static int CountColor(const DrawList& l, Rgb c) {
    int n = 0;
    for (const Rect& r : l.marks) if (r.color.r == c.r && r.color.g == c.g && r.color.b == c.b) ++n;
    return n;
}

int main() {
    GrainLayout L;
    L.SetSize(320.f, 180.f);

    // x mapping: index M-1 at the left edge, index 0 at the right.
    check(near(L.XForIndex(M - 1, M), 0.f), "x: oldest index at left");
    check(near(L.XForIndex(0, M), 320.f * (M - 1) / M), "x: index 0 near right");

    // Rows: band is 36 tall centered at 90; rows are (180-36)/24 = 6 tall.
    check(near(L.RowY(+1, 0), 72.f - 3.f), "row 0 above sits on the band");
    check(near(L.RowY(-1, 0), 108.f + 3.f), "row 0 below sits on the band");
    check(near(L.RowY(+1, 11), 72.f - 69.f), "row 11 above at the top");

    // A taller display (VCV: 54 x 60) keeps the 16:9 band and row height
    // and gains rows: 160 x 177.8 -> band 18, row 3, 26 rows per side.
    {
        GrainLayout T; T.SetSize(160.f, 177.8f);
        check(L.rows_per_side() == 12, "16:9 display: 12 rows per side");
        check(T.rows_per_side() == 26, "tall display: 26 rows per side");
        check(near(T.RowY(+1, 0), (177.8f - 18.f) * 0.5f - 1.5f), "tall display: thin band, same row height");
        check(GrainLayout::PreferredRow(4.f, 26) == 25, "+24 st: top row of 26");
        GrainLiveFrame f = Frame();
        Live(f, 0, 1, 600);
        GrainStartEvent e{1, 0, 600, 2.f, 100.f};   // +12 st -> row round(0.5 * 25) = 13
        T.Update(In(&f, &e, 1));
        check(T.grains()[0].row == 13, "tall display: +12 st lands on row 13");
    }

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

    // Color follows pan: blue left, warm white center, orange right.
    {
        const Rgb l = GrainLayout::PanColor(0.f), c = GrainLayout::PanColor(0.5f), r = GrainLayout::PanColor(1.f);
        check(l.r == 0x3F && l.g == 0xA9 && l.b == 0xF5, "hard left is blue");
        check(c.r == 0xE8 && c.g == 0xE4 && c.b == 0xDA, "center is warm white");
        check(r.r == 0xFF && r.g == 0x8A && r.b == 0x3D, "hard right is orange");
        const Rgb nearL = GrainLayout::PanColor(0.45f);   // 10% left -> sqrt: 32% of the way
        check(nearL.b > c.b && nearL.r < c.r - 40, "small offsets are visibly tinted");
        GrainLayout G; G.SetSize(320.f, 180.f);
        GrainLiveFrame f = Frame();
        Live(f, 0, 1, 600);   // level 1: full brightness
        GrainStartEvent e{1, 0, 600, 1.f, 100.f, 1.f};
        G.Update(In(&f, &e, 1));
        bool found = false;
        for (const Rect& m : G.draw_list().marks)
            if (m.y < 72.f && m.h < 4.f && m.color.r == r.r && m.color.g == r.g && m.color.b == r.b) found = true;
        check(found, "a hard-right live grain draws orange");
    }

    // A fading grain fades into the background, never darker than it.
    {
        GrainLayout G; G.SetSize(320.f, 180.f);
        GrainLiveFrame f = Frame();
        f.slots[0] = {1, 600, 0.f, true};
        GrainStartEvent e{1, 0, 600, 1.f, 100.f, 1.f};   // born ended, hard right
        G.Update(In(&f, &e, 1));
        G.Update(In(&f, nullptr, 0, 0.149f));            // 1 ms of fade left
        const Rgb bg = G.draw_list().background;
        bool never_darker = true, near_bg = true;
        for (const Rect& m : G.draw_list().marks) {
            if (m.y >= 72.f) continue;                    // grain marks only (above the band)
            never_darker &= m.color.r >= bg.r && m.color.g >= bg.g && m.color.b >= bg.b;
            near_bg &= std::abs(m.color.r - bg.r) <= 4 && std::abs(m.color.g - bg.g) <= 4 && std::abs(m.color.b - bg.b) <= 4;
        }
        check(never_darker, "fading grain is never darker than the background");
        check(near_bg, "an almost-faded grain is almost the background color");
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

    // --- Tap view (Retours) ---
    {   // slot -> side, lane, color
        check(GrainLayout::TapSide(0) == +1 && GrainLayout::TapSide(1) == +1, "ratio 0 above");
        check(GrainLayout::TapSide(2) == -1 && GrainLayout::TapSide(7) == -1, "ratio 1 below");
        check(GrainLayout::TapLane(4) == 0 && GrainLayout::TapLane(5) == 1, "lane = slot bit");
        const Rgb g = GrainLayout::TapColor(2), w = GrainLayout::TapColor(0);
        check(g.r == 0x8F && g.g == 0xD3 && g.b == 0xA0, "ratio 1 is green");
        const Rgb c = GrainLayout::PanColor(0.5f);
        check(w.r == c.r && w.g == c.g && w.b == c.b, "ratio 0 is warm white");
        GrainLayout L; L.SetSize(320.f, 180.f);
        check(L.LaneY(+1, 0) > L.LaneY(+1, 1), "above: lane 0 nearer band");
        check(L.LaneY(-1, 0) < L.LaneY(-1, 1), "below: lane 0 nearer band");
        check(L.LaneY(+1, 1) > 0.f && L.LaneY(-1, 1) < 180.f, "lanes inside display");
    }
    {   // a tap frame without slices: band and write head only (ScopeLayout draws it)
        GrainLayout L; L.SetSize(320.f, 180.f);
        GrainLiveFrame f = TapFrame();
        f.taps[0].read_index = 700; f.taps[0].level = 1.f;
        f.taps[3].read_index = 50; f.taps[3].level = 0.5f;
        L.Update(In(&f, nullptr, 0));
        check(L.tap_view(), "tap view on");
        check(L.draw_list().marks.size() == 1, "tap frame without slices draws only band and write head");
        check(L.grains().empty(), "tap frame without slices keeps no visuals");
    }
    {   // routing: scope for running taps, GrainLayout for slices and grains
        GrainLiveFrame f = TapFrame();
        check(GrainLayout::ScopeView(f), "scope view: tap mode, no slice");
        f.taps[5].slice = true;
        check(!GrainLayout::ScopeView(f), "no scope view when any tap is a slice");
        check(!GrainLayout::ScopeView(Frame()), "no scope view for a grain frame");
    }
    {   // out-of-range slice indices are clamped
        GrainLayout L; L.SetSize(320.f, 180.f);
        GrainLiveFrame f = TapFrame(true);
        f.taps[0] = {-7, 1.f, true, 1, 5000, 300};
        f.taps[2] = {5000, 1.f, true, 1, -7, 5000};
        L.Update(In(&f, nullptr, 0));
        check(L.grains().size() == 2, "both clamped slices shown");
        bool inside = true;
        for (const Rect& r : L.draw_list().marks) if (r.x < -0.01f || r.x + r.w > 320.01f) inside = false;
        check(inside, "clamped slices stay inside the display");
    }
    {   // slice head: line of the slice's length, cursor, fade after serial change
        GrainLayout L; L.SetSize(320.f, 180.f);
        GrainLiveFrame f = TapFrame(true);
        f.taps[0] = {650, 0.8f, true, 1, 700, 100};   // read 650 inside slice 700 -> 600
        L.Update(In(&f, nullptr, 0, 0.01f));
        check(L.grains().size() == 1, "one slice visual");
        const VisualGrain& g = L.grains()[0];
        check(near(g.x0, L.XForIndex(700, M), 0.5f), "slice starts at its oldest end");
        check(near(g.span, 100.f / M * 320.f, 0.5f), "slice spans its length rightward");
        check(near(g.cursor, 0.5f, 0.02f), "cursor halfway");
        f.taps[0] = {700, 0.8f, true, 2, 700, 100};   // restarted
        L.Update(In(&f, nullptr, 0, 0.01f));
        int ended = 0, live = 0;
        for (const VisualGrain& v : L.grains()) (v.ended ? ended : live)++;
        check(ended == 1 && live == 1, "restart ends the old slice and births a new one");
        L.Update(In(&f, nullptr, 0, 0.2f));
        check(L.grains().size() == 1, "old slice gone after the fade");
    }
    {   // slice -> running switch drops the slice visuals
        GrainLayout L; L.SetSize(320.f, 180.f);
        GrainLiveFrame s = TapFrame(true);
        s.taps[0] = {650, 0.8f, true, 1, 700, 100};
        L.Update(In(&s, nullptr, 0, 0.01f));
        GrainLiveFrame r = TapFrame();
        r.taps[0].read_index = 700; r.taps[0].level = 1.f;
        L.Update(In(&r, nullptr, 0, 0.01f));
        check(L.grains().empty(), "slice visuals dropped when Slice ends");
        check(L.draw_list().marks.size() == 1, "only the write head after Slice ends");
    }
    {   // grain -> tap switch clears grains
        GrainLayout L; L.SetSize(320.f, 180.f);
        GrainLiveFrame f = Frame();
        Live(f, 0, 1, 400);
        GrainStartEvent e{1, 0, 400, 1.f, 100.f, 0.5f};
        L.Update(In(&f, &e, 1));
        check(L.grains().size() == 1, "a grain before the switch");
        GrainLiveFrame t = TapFrame();
        L.Update(In(&t, nullptr, 0));
        check(L.grains().empty() && L.tap_view(), "grains cleared in tap view");
    }

    std::printf(g_failures ? "\n%d FAILED\n" : "\nall passed\n", g_failures);
    return g_failures ? 1 : 0;
}

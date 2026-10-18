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
    in.epoch = epoch; in.peakLo = lo; in.peakHi = hi;
    in.frame = f; in.events = e; in.numEvents = n; in.dt = dt;
    return in;
}

int main() {
    GrainLayout L;
    L.setSize(320.f, 180.f);

    // x mapping: index M-1 at the left edge, index 0 at the right.
    check(near(L.xForIndex(M - 1, M), 0.f), "x: oldest index at left");
    check(near(L.xForIndex(0, M), 320.f * (M - 1) / M), "x: index 0 near right");

    // Rows: band is 36 tall centered at 90; rows are (180-36)/24 = 6 tall.
    check(near(L.rowY(+1, 0), 72.f - 3.f), "row 0 above sits on the band");
    check(near(L.rowY(-1, 0), 108.f + 3.f), "row 0 below sits on the band");
    check(near(L.rowY(+1, 11), 72.f - 69.f), "row 11 above at the top");

    // A taller display (VCV: 54 x 60) keeps the 16:9 band and row height
    // and gains rows: 160 x 177.8 -> band 18, row 3, 26 rows per side.
    {
        GrainLayout T; T.setSize(160.f, 177.8f);
        check(L.getRowsPerSide() == 12, "16:9 display: 12 rows per side");
        check(T.getRowsPerSide() == 26, "tall display: 26 rows per side");
        check(near(T.rowY(+1, 0), (177.8f - 18.f) * 0.5f - 1.5f), "tall display: thin band, same row height");
        check(GrainLayout::preferredRow(4.f, 26) == 25, "+24 st: top row of 26");
        GrainLiveFrame f = Frame();
        Live(f, 0, 1, 600);
        GrainStartEvent e{1, 0, 600, 2.f, 100.f};   // +12 st -> row round(0.5 * 25) = 13
        T.update(In(&f, &e, 1));
        check(T.getGrains()[0].row == 13, "tall display: +12 st lands on row 13");
    }

    // Pitch -> side and row.
    check(GrainLayout::sideFor(1.f) == +1 && GrainLayout::preferredRow(1.f) == 0, "unison: above, row 0");
    check(GrainLayout::sideFor(4.f) == +1 && GrainLayout::preferredRow(4.f) == 11, "+24 st: top row");
    check(GrainLayout::sideFor(0.5f) == -1 && GrainLayout::preferredRow(0.5f) == 6, "-12 st: below, row 6");
    check(GrainLayout::preferredRow(16.f) == 11, "beyond +24 st clamps");
    check(GrainLayout::sideFor(-2.f) == +1 && GrainLayout::preferredRow(-2.f) == 6, "reverse +12 st: above, row 6");

    // Birth, live cursor, end: a grain seen live vanishes when it ends.
    {
        GrainLayout G; G.setSize(320.f, 180.f);
        GrainLiveFrame f = Frame();
        Live(f, 3, 7, 600);
        GrainStartEvent e{7, 3, 600, 1.f, 100.f};   // speed 1, 100 samples
        G.update(In(&f, &e, 1));
        check(G.getGrains().size() == 1, "birth creates one visual grain");
        const VisualGrain& g = G.getGrains()[0];
        check(near(g.x0, 320.f * (M - 1 - 600) / M), "start x from start index");
        check(near(g.span, 320.f * 100.f / M), "span = speed*duration in x");
        check(!g.ended && near(g.cursor, 0.f), "live at start");
        Live(f, 3, 7, 550, 50.f);                   // played 50 of 100
        G.update(In(&f, nullptr, 0, 0.02f));
        check(near(G.getGrains()[0].cursor, 0.5f), "cursor halfway");
        f.slots[3].done = true;
        G.update(In(&f, nullptr, 0, 0.02f));
        check(G.getGrains().empty(), "an ended live grain vanishes at once");
        bool drawn = false;
        for (const Rect& m : G.getDrawList().marks) drawn |= m.y < 72.f;
        check(!drawn, "an ended live grain draws nothing");
    }
    {   // A voice-stolen grain (its slot's serial changes) vanishes too.
        GrainLayout G; G.setSize(320.f, 180.f);
        GrainLiveFrame f = Frame();
        Live(f, 3, 7, 600);
        GrainStartEvent e{7, 3, 600, 1.f, 100.f};
        G.update(In(&f, &e, 1));
        Live(f, 3, 8, 400);
        G.update(In(&f, nullptr, 0, 0.02f));
        check(G.getGrains().empty(), "a stolen grain vanishes at once");
    }

    // A grain spanning more than half the buffer: the cursor must not wrap.
    {
        GrainLayout G; G.setSize(320.f, 180.f);
        GrainLiveFrame f = Frame();
        Live(f, 0, 1, 800);
        GrainStartEvent e{1, 0, 800, 1.f, 800.f};   // speed 1, 0.8 of the buffer
        G.update(In(&f, &e, 1));
        Live(f, 0, 1, 200, 600.f);                  // 600 of 800 played
        G.update(In(&f, nullptr, 0, 0.02f));
        check(!G.getGrains()[0].ended && near(G.getGrains()[0].cursor, 0.75f), "cursor past half the buffer");
    }

    // A grain that ended before its event was read is born ended, cursor at 1.
    {
        GrainLayout G; G.setSize(320.f, 180.f);
        GrainLiveFrame f = Frame();
        f.slots[2] = {9, 100, 0.f, true, 0.f};
        GrainStartEvent e{8, 2, 300, 1.f, 50.f};
        G.update(In(&f, &e, 1));
        check(G.getGrains().size() == 1 && G.getGrains()[0].ended && near(G.getGrains()[0].cursor, 1.f),
              "born-ended grain shows with cursor at end");
        G.update(In(&f, nullptr, 0, GrainLayout::kBornEndedSeconds - 0.01f));
        check(G.getGrains().size() == 1, "born-ended grain held for kBornEndedSeconds");
        G.update(In(&f, nullptr, 0, 0.02f));
        check(G.getGrains().empty(), "born-ended grain gone after its hold");
    }

    // Reversed grain spans leftward and wraps at the left edge.
    {
        GrainLayout G; G.setSize(320.f, 180.f);
        GrainLiveFrame f = Frame();
        Live(f, 0, 1, 990);
        GrainStartEvent e{1, 0, 990, -1.f, 100.f};   // x0 = 2.88, span = -32
        G.update(In(&f, &e, 1));
        check(G.getGrains()[0].span < 0.f, "reversed span is negative");
        int pieces = 0;
        for (const Rect& r : G.getDrawList().marks)
            if (r.y < 72.f && r.w > 1.f && r.h < 4.f) ++pieces;   // line rects above the band
        check(pieces == 2, "wrapped line draws as two pieces");
    }

    // Collisions take the nearest free row (unison crosses the band);
    // non-overlapping grains share a row.
    {
        GrainLayout G; G.setSize(320.f, 180.f);
        GrainLiveFrame f = Frame();
        Live(f, 0, 1, 600); Live(f, 1, 1, 600); Live(f, 2, 1, 100);
        GrainStartEvent e[3] = {{1, 0, 600, 1.f, 100.f}, {1, 1, 600, 1.f, 100.f}, {1, 2, 100, 1.f, 100.f}};
        G.update(In(&f, e, 3));
        check(G.getGrains()[0].side == +1 && G.getGrains()[0].row == 0
              && G.getGrains()[1].side == -1 && G.getGrains()[1].row == 0, "unison overlap crosses the band");
        check(G.getGrains()[2].row == 0, "no overlap: same row");
    }

    // Epoch change drops grains.
    {
        GrainLayout G; G.setSize(320.f, 180.f);
        GrainLiveFrame f = Frame();
        Live(f, 0, 1, 600);
        GrainStartEvent e{1, 0, 600, 1.f, 100.f};
        G.update(In(&f, &e, 1, 0.f, 1));
        G.update(In(&f, nullptr, 0, 0.f, 2));
        check(G.getGrains().empty(), "epoch change drops grains");
    }

    // Pool is capped; born-ended grains still show.
    {
        GrainLayout G; G.setSize(320.f, 180.f);
        GrainLiveFrame f = Frame();
        std::vector<GrainStartEvent> ev;
        for (uint32_t i = 0; i < 200; ++i) ev.push_back({i + 1, int32_t(i % 32), int32_t(i * 5 % M), 1.f, 20.f});
        G.update(In(&f, ev.data(), int(ev.size())));
        check(int(G.getGrains().size()) == GrainLayout::kMaxGrains, "pool capped at kMaxGrains");
    }

    // Minimum length: a 1-sample grain still draws 2 px... of a 320-wide display.
    {
        GrainLayout G; G.setSize(320.f, 180.f);
        GrainLiveFrame f = Frame();
        Live(f, 0, 1, 600);
        GrainStartEvent e{1, 0, 600, 1.f, 1.f};
        G.update(In(&f, &e, 1));
        float widest = 0.f;
        for (const Rect& r : G.getDrawList().marks) if (r.y < 72.f && r.h < 4.f) widest = std::max(widest, r.w);
        check(widest >= 2.f - 1e-3f, "tiny grain is at least 2 units long");
    }

    // Clear forgets everything.
    {
        GrainLayout G; G.setSize(320.f, 180.f);
        GrainLiveFrame f = Frame();
        Live(f, 0, 1, 600);
        GrainStartEvent e{1, 0, 600, 1.f, 100.f};
        G.update(In(&f, &e, 1));
        G.clear();
        check(G.getGrains().empty() && G.getDrawList().marks.empty(), "Clear empties grains and marks");
    }

    // Color follows pan: blue left, warm white center, orange right.
    {
        const Rgb l = GrainLayout::panColor(0.f), c = GrainLayout::panColor(0.5f), r = GrainLayout::panColor(1.f);
        check(l.r == 0x3F && l.g == 0xA9 && l.b == 0xF5, "hard left is blue");
        check(c.r == 0xE8 && c.g == 0xE4 && c.b == 0xDA, "center is warm white");
        check(r.r == 0xFF && r.g == 0x8A && r.b == 0x3D, "hard right is orange");
        const Rgb nearL = GrainLayout::panColor(0.45f);   // 10% left -> sqrt: 32% of the way
        check(nearL.b > c.b && nearL.r < c.r - 40, "small offsets are visibly tinted");
        GrainLayout G; G.setSize(320.f, 180.f);
        GrainLiveFrame f = Frame();
        Live(f, 0, 1, 600);   // level 1: full brightness
        GrainStartEvent e{1, 0, 600, 1.f, 100.f, 1.f};
        G.update(In(&f, &e, 1));
        bool found = false;
        for (const Rect& m : G.getDrawList().marks)
            if (m.y < 72.f && m.h < 4.f && m.color.r == r.r && m.color.g == r.g && m.color.b == r.b) found = true;
        check(found, "a hard-right live grain draws orange");
    }

    // Colors come from the scheme: Lucarne's by default, bit-identical to
    // before; Paper after setScheme() (hi is black, so the cursor darkens).
    {
        const Rgb base{0xE8, 0xE4, 0xDA};
        auto drawGrain = [](GrainLayout& G) {
            G.setSize(320.f, 180.f);
            GrainLiveFrame f = Frame();
            Live(f, 0, 1, 600);
            GrainStartEvent e{1, 0, 600, 1.f, 100.f, 0.5f};   // centered
            G.update(In(&f, &e, 1));
        };
        GrainLayout G; drawGrain(G);
        const Rgb want = {uint8_t(base.r + (255 - base.r) * 0.5f + 0.5f), uint8_t(base.g + (255 - base.g) * 0.5f + 0.5f),
                          uint8_t(base.b + (255 - base.b) * 0.5f + 0.5f)};
        bool cursor = false, span = false;
        for (const Rect& m : G.getDrawList().marks) {
            if (m.color.r == want.r && m.color.g == want.g && m.color.b == want.b) cursor = true;
            if (m.h < 4.f && m.color.r == mix(G.getDrawList().background, base, 1.f).r) span = true;
        }
        check(cursor, "default scheme: cursor is the pan color lightened 50% toward white");
        check(span, "default scheme: span is the pan color at full level");
        const ColorScheme& paper = scheme(2);
        GrainLayout P; P.setScheme(paper); drawGrain(P);
        check(P.getDrawList().background.r == 0xEC && P.getDrawList().background.g == 0xE7
              && P.getDrawList().background.b == 0xDC, "Paper: background ECE7DC");
        check(P.getDrawList().waveColor.r == paper.wave.r, "Paper: wave color from the scheme");
        const Rgb pb = GrainLayout::panColor(0.5f, paper);
        const int baseSum = pb.r + pb.g + pb.b;
        bool darker = false;
        for (const Rect& m : P.getDrawList().marks)
            if (m.h > 4.f && m.h < 8.f && m.color.r + m.color.g + m.color.b < baseSum) darker = true;
        check(darker, "Paper: cursor is darker than the pan color");
    }

    // A born-ended grain holds steady through its hold, no fade.
    {
        GrainLayout G; G.setSize(320.f, 180.f);
        GrainLiveFrame f = Frame();
        f.slots[0] = {1, 600, 0.f, true};
        GrainStartEvent e{1, 0, 600, 1.f, 100.f, 1.f};   // born ended, hard right
        G.update(In(&f, &e, 1));
        G.update(In(&f, nullptr, 0, GrainLayout::kBornEndedSeconds - 0.001f));   // 1 ms of hold left
        const Rgb bg = G.getDrawList().background;
        const Rgb want = mix(bg, GrainLayout::panColor(1.f), GrainLayout::kBornEndedLevel);
        bool found = false;
        for (const Rect& m : G.getDrawList().marks)
            if (m.y < 72.f && m.color.r == want.r && m.color.g == want.g && m.color.b == want.b) found = true;
        check(found, "born-ended grain keeps its brightness to the end of its hold");
    }

    // Waveform: one rect per column, sized by the peaks.
    {
        GrainLayout G; G.setSize(320.f, 180.f);
        for (int c = 0; c < 256; ++c) { lo[c] = -127; hi[c] = 127; }
        G.update(In(nullptr, nullptr, 0));
        check(G.getDrawList().wave.size() == 256, "256 waveform columns");
        const Rect& r = G.getDrawList().wave[10];
        check(near(r.y, 72.f) && near(r.h, 36.f), "full-scale column fills the band");
    }

    std::printf(g_failures ? "\n%d FAILED\n" : "\nall passed\n", g_failures);
    return g_failures ? 1 : 0;
}

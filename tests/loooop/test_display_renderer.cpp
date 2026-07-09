#include "../../src/loooop/display/LoopWaveformRenderer.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <vector>

static int g_failures = 0;
static void check(bool cond, const char* name) {
    if (!cond) { std::printf("FAIL: %s\n", name); ++g_failures; }
    else       { std::printf("ok:   %s\n", name); }
}

// 64x64: laneH = 8, lanes rows 32..63; wave region rows 0..31 splits into
// L band rows 0..15 (midline 7.5) and R band rows 16..31 (midline 23.5),
// band half-height 7.5 — same per-wave resolution as the old mono layout.
static constexpr int W = 64, H = 64;
static uint32_t buf[W * H];
static uint32_t px(int x, int y) { return buf[y * W + x]; }

// Test pack: ARGB word, same as the MetaModule layout.
static uint32_t pack(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    return (uint32_t(a) << 24) | (uint32_t(r) << 16) | (uint32_t(g) << 8) | b;
}
static uint32_t C(const uint8_t rgb[3]) { return pack(rgb[0], rgb[1], rgb[2], 0xFF); }

static int countColor(uint32_t c) {
    int n = 0;
    for (uint32_t p : buf) n += (p == c);
    return n;
}

// Row-ranged counter over an arbitrary buffer/width, matching either the
// bright or dimmed variant of a head color (both count as "lane pixels").
static int countColorInRows(const std::vector<uint32_t>& b, int width, int y0, int y1,
                            const uint8_t rgb[3]) {
    const uint32_t bright = pack(rgb[0], rgb[1], rgb[2], 0xFF);
    const uint32_t dim = pack(uint8_t(int(rgb[0]) * LoopWaveformRenderer::DIM_NUM / LoopWaveformRenderer::DIM_DEN),
                              uint8_t(int(rgb[1]) * LoopWaveformRenderer::DIM_NUM / LoopWaveformRenderer::DIM_DEN),
                              uint8_t(int(rgb[2]) * LoopWaveformRenderer::DIM_NUM / LoopWaveformRenderer::DIM_DEN),
                              0xFF);
    int n = 0;
    const int h = int(b.size() / width);
    y0 = std::max(y0, 0);
    y1 = std::min(y1, h);
    for (int y = y0; y < y1; ++y)
        for (int x = 0; x < width; ++x) {
            const uint32_t p = b[std::size_t(y) * width + x];
            n += (p == bright || p == dim);
        }
    return n;
}

static uint8_t dimc(uint8_t v) {
    return uint8_t(int(v) * LoopWaveformRenderer::DIM_NUM / LoopWaveformRenderer::DIM_DEN);
}
static uint32_t laneDim(int i) {
    const uint8_t* c = LoopWaveformRenderer::HEAD_COLORS[i];
    return pack(dimc(c[0]), dimc(c[1]), dimc(c[2]), 0xFF);
}
static uint32_t laneBright(int i) {
    const uint8_t* c = LoopWaveformRenderer::HEAD_COLORS[i];
    return pack(c[0], c[1], c[2], 0xFF);
}

static void recordStereo(LoopEngine& e, const float* l, const float* r, int n) {
    std::array<LoopEngine::HeadOut, LoopEngine::NUM_HEADS> hs;
    e.toggleRecord();
    for (int i = 0; i < n; ++i) e.process(l[i], r[i], hs);
    e.toggleRecord();
}

static void test_blank() {
    LoopEngine e; e.reset(10.f, 100.f);      // peakBinSize == 1
    LoopWaveformRenderer::render(buf, W, H, e, pack);
    const uint32_t bg = C(LoopWaveformRenderer::BG);
    check(countColor(bg) == W * H, "blank: every pixel is background");
}

static void test_waveform_and_lanes() {
    LoopEngine e; e.reset(10.f, 100.f);
    e.toggleRecord();
    e.process(0.f); e.process(0.f); e.process(1.f); e.process(0.f);
    e.toggleRecord();                        // loop = {0, 0, 1, 0}; heads at pos 0
    LoopWaveformRenderer::render(buf, W, H, e, pack);
    const uint32_t wave = C(LoopWaveformRenderer::WAVE);
    const uint32_t bg   = C(LoopWaveformRenderer::BG);
    // Mono input mirrors into both bands. Sample 2 (peak 1.0 = -6 dB vs the
    // ±10 V ref) -> fullness ~0.7685, yScale 5.764 -> L y = round(7.5-5.764)
    // = 2, R y = round(23.5-5.764) = 18.
    check(px(32, 2) == wave, "wave: L band peak at x=32");
    check(px(32, 0) == bg && px(32, 1) == bg, "wave: rows above L peak clear");
    check(px(32, 18) == wave, "wave: R band mirrors mono peak");
    // Silent column x=16 -> midline pixels at round(7.5)=8 and round(23.5)=24.
    check(px(16, 8) == wave,  "wave: silent column draws L midline");
    check(px(16, 24) == wave, "wave: silent column draws R midline");
    // Full window + head at 0 in every lane: dim window bar across the lane,
    // bright playhead bar at x=0 (width 2, col -1 clipped).
    for (int i = 0; i < 4; ++i) {
        const int top = 32 + 8 * i;
        check(px(5, top) == laneDim(i),      "lane: window bar in dim head color");
        check(px(0, top) == laneBright(i),   "lane: playhead bright at x=0");
        check(px(5, top + 7) == bg,          "lane: gap row below lane");
    }
    // Lanes are distinct colors.
    check(laneBright(0) != laneBright(1) && laneBright(2) != laneBright(3),
          "lane: head colors distinct");
    // No full-height playhead bar crossing the waveform region.
    check(px(0, 12) == bg, "wave: no legacy full-height playhead");
}

static void test_stereo_bands() {
    const float sig[]    = {0.f, 1.f, 0.f, 0.f};
    const float silent[] = {0.f, 0.f, 0.f, 0.f};
    // Signal covers x=16..31 (sample 1 of 4). Shared peak 1.0 -> yScale 5.764.
    LoopEngine e; e.reset(10.f, 100.f);
    recordStereo(e, sig, silent, 4);
    LoopWaveformRenderer::render(buf, W, H, e, pack);
    const uint32_t wave = C(LoopWaveformRenderer::WAVE);
    const uint32_t bg   = C(LoopWaveformRenderer::BG);
    check(px(16, 2) == wave,  "stereo: L-only signal peaks in top band");
    check(px(16, 18) == bg,   "stereo: R band stays flat for L-only signal");
    check(px(16, 24) == wave, "stereo: silent R still draws its midline");

    LoopEngine e2; e2.reset(10.f, 100.f);
    recordStereo(e2, silent, sig, 4);
    LoopWaveformRenderer::render(buf, W, H, e2, pack);
    check(px(16, 18) == wave, "stereo: R-only signal peaks in bottom band");
    check(px(16, 2) == bg,    "stereo: L band stays flat for R-only signal");
    check(px(16, 8) == wave,  "stereo: silent L still draws its midline");
}

static void test_phase_cancellation() {
    // R = -L used to mono-sum to zero and draw near-flat. Both bands must
    // now draw at full deflection: L peak y=2, R trough y = round(23.5+5.764)
    // = 29.
    const float l[] = {0.f, 1.f, 0.f, 0.f};
    const float r[] = {0.f, -1.f, 0.f, 0.f};
    LoopEngine e; e.reset(10.f, 100.f);
    recordStereo(e, l, r, 4);
    LoopWaveformRenderer::render(buf, W, H, e, pack);
    const uint32_t wave = C(LoopWaveformRenderer::WAVE);
    check(px(16, 2) == wave,  "phase: L peak drawn");
    check(px(16, 29) == wave, "phase: R trough drawn, no mono-sum cancellation");
}

static void test_moved_window_lane() {
    LoopEngine e; e.reset(10.f, 100.f);
    e.toggleRecord();
    e.process(0.f); e.process(0.f); e.process(1.f); e.process(0.f);
    e.toggleRecord();                        // loop of 4
    e.setSize(2, 0.5f); e.setPosition(2, 0.5f);   // head 2: window [1,3) = [0.25,0.75]
    e.process(0.f);                          // advance; head2 snaps to 1 then moves to 2
    LoopWaveformRenderer::render(buf, W, H, e, pack);
    const uint32_t bg = C(LoopWaveformRenderer::BG);
    // Lane 2 rows 48..54: window bar spans x = round(.25*63)=16 .. round(.75*63)=47.
    check(px(15, 48) == bg,          "lane2: left of window is background");
    check(px(16, 48) == laneDim(2),  "lane2: window bar starts at x=16");
    check(px(47, 48) == laneDim(2),  "lane2: window bar ends at x=47");
    check(px(48, 48) == bg,          "lane2: right of window is background");
    // Head 2 at pos 2/4 -> x = round(.5*63) = 32; bar covers cols 31-32.
    check(px(31, 48) == laneBright(2) && px(32, 48) == laneBright(2),
          "lane2: playhead at loop midpoint");
    // Head 0 (full window) advanced to pos 1/4 -> x = round(.25*63) = 16.
    check(px(16, 32) == laneBright(0), "lane0: playhead at 1/4");
}

static void test_recording_view() {
    LoopEngine e; e.reset(10.f, 100.f);
    e.toggleRecord();
    for (int i = 0; i < 10; ++i) e.process(0.8f);   // still recording
    LoopWaveformRenderer::render(buf, W, H, e, pack);
    const uint32_t wave = C(LoopWaveformRenderer::WAVE);
    const uint32_t bg   = C(LoopWaveformRenderer::BG);
    // Peak 0.8 (-7.96 dB) -> fullness ~0.7422, yScale 6.958 -> L top y =
    // round(7.5-5.567) = 2.
    check(px(0, 2) == wave, "recording: waveform drawn from live peaks");
    // No lanes while the loop is unfrozen: lane region is pure background.
    int laneRegion = 0;
    for (int y = 32; y < H; ++y)
        for (int x = 0; x < W; ++x) laneRegion += (px(x, y) != bg);
    check(laneRegion == 0, "recording: lane region empty until loop freezes");
}

static int topWaveY(int x, uint32_t wave) {
    for (int y = 0; y < H; ++y) if (px(x, y) == wave) return y;
    return H;
}

static void test_level_aware_height() {
    const uint32_t wave = C(LoopWaveformRenderer::WAVE);
    // Same level logic as before, applied per band (half-height 7.5).
    LoopEngine full; full.reset(10.f, 100.f);
    full.toggleRecord();
    full.process(0.f); full.process(2.f); full.process(0.f); full.process(0.f);
    full.toggleRecord();                     // peak 2.0 (0 dB): L top y = round(7.5-6.375) = 1
    LoopWaveformRenderer::render(buf, W, H, full, pack);
    const int fullTop = topWaveY(16, wave);

    LoopEngine nom; nom.reset(10.f, 100.f);
    nom.toggleRecord();
    nom.process(0.f); nom.process(1.f); nom.process(0.f); nom.process(0.f);
    nom.toggleRecord();                      // peak 1.0 (-6 dB): L top y = 2
    LoopWaveformRenderer::render(buf, W, H, nom, pack);
    const int nomTop = topWaveY(16, wave);

    LoopEngine quiet; quiet.reset(10.f, 100.f);
    quiet.toggleRecord();
    quiet.process(0.f); quiet.process(0.25f); quiet.process(0.f); quiet.process(0.f);
    quiet.toggleRecord();                    // peak 0.25 (-18 dB): L top y = 3
    LoopWaveformRenderer::render(buf, W, H, quiet, pack);
    const int quietTop = topWaveY(16, wave);

    check(fullTop == 1,  "level: full-scale loop fills to headroom");
    check(nomTop == 2,   "level: nominal ±5V loop a bit shorter");
    check(quietTop == 3, "level: quiet loop shorter still");
    check(fullTop < nomTop && nomTop < quietTop && quietTop < 8,
          "level: height rises with level, all visible");
}

static void test_tiny_display_combined() {
    // 16x16: laneH = max(3, 16/8) = 3, lanes rows 4..15, wave region 4 rows
    // < MIN_SPLIT_ROWS -> combined L∪R envelope, midline 1.5, half-height 1.5.
    static constexpr int W2 = 16, H2 = 16;
    static uint32_t buf2[W2 * H2];
    const float l[] = {0.f, 1.f, 0.f, 0.f};
    const float r[] = {0.f, -1.f, 0.f, 0.f};
    LoopEngine e; e.reset(10.f, 100.f);
    recordStereo(e, l, r, 4);
    LoopWaveformRenderer::render(buf2, W2, H2, e, pack);
    const uint32_t wave = C(LoopWaveformRenderer::WAVE);
    // Combined envelope at x=4..7 (sample 1): lo=-1, hi=+1, yScale
    // 1.5*0.7685 = 1.153 -> y0 = round(1.5-1.153) = 0, y1 = round(1.5+1.153)
    // = 3 (clamped to the 4-row region).
    check(H2 - 4 * LoopWaveformRenderer::laneHeight(H2) <
              LoopWaveformRenderer::MIN_SPLIT_ROWS,
          "tiny: 16x16 wave region is below the split threshold");
    check(buf2[0 * W2 + 4] == wave, "tiny: combined envelope reaches top row");
    check(buf2[3 * W2 + 4] == wave, "tiny: combined envelope reaches bottom row");
}

static void test_single_head_single_lane() {
    LoopEngine e(1);
    e.reset(48000.f, 1.f);
    e.toggleRecord();
    for (int i = 0; i < 4800; ++i)
        e.process(std::sin(6.2831853f * i / 480.f) * 0.8f);
    e.toggleRecord();
    e.process(0.f);                       // one tick so the lane atomics update
    const int W2 = 60, H2 = 24;           // laneH = 3
    std::vector<uint32_t> buf2(W2 * H2);
    LoopWaveformRenderer::render(buf2.data(), W2, H2, e, pack);
    const int laneH = LoopWaveformRenderer::laneHeight(H2);   // 3
    const int lanesTop = H2 - 1 * laneH;                      // 21
    // head-0 color appears in the single bottom lane...
    check(countColorInRows(buf2, W2, lanesTop, H2, LoopWaveformRenderer::HEAD_COLORS[0]) > 0,
          "single_head: lane 0 drawn at bottom");
    // ...heads 1-3 colors appear nowhere, and no lane pixels above lanesTop
    for (int h = 1; h < LoopEngine::NUM_HEADS; ++h)
        check(countColorInRows(buf2, W2, 0, H2, LoopWaveformRenderer::HEAD_COLORS[h]) == 0,
              "single_head: no other head lanes");
    check(countColorInRows(buf2, W2, lanesTop - laneH, lanesTop,
                           LoopWaveformRenderer::HEAD_COLORS[0]) == 0,
          "single_head: wave region extends over old lane rows");
}

// The split render (renderWaveform + renderLanes into adjoining regions) must
// be byte-identical to the thin composed render() — the host caches call the
// split entry points directly, so any divergence would silently change what
// ships to a display.
static void test_split_render_matches_composed_render() {
    LoopEngine e;
    e.reset(10.f, 100.f);
    const float l[] = {0.1f, 0.8f, -0.4f, 0.3f};
    const float r[] = {-0.2f, 0.4f, -0.9f, 0.5f};
    recordStereo(e, l, r, 4);
    uint32_t composed[W * H]{};
    uint32_t split[W * H]{};
    LoopWaveformRenderer::render(composed, W, H, e, pack);
    const int laneH = LoopWaveformRenderer::laneHeight(H);
    const int lanesH = e.numHeads() * laneH;
    const int waveH = H - lanesH;
    LoopWaveformRenderer::renderWaveform(split, W, waveH, e, pack);
    LoopWaveformRenderer::renderLanes(split + waveH * W, W, lanesH, laneH, e, pack);
    bool identical = true;
    for (int i = 0; i < W * H; ++i) identical &= composed[i] == split[i];
    check(identical, "split renderer: composed pixels are byte-identical");
}

// geometry() must cap the lane region to the destination height (never
// overrunning it) and the wave+lane split must exactly cover the full
// destination on every tiny height, from 0 up through a few multiples of
// NUM_HEADS. Canary bytes bracket the buffer to catch any out-of-bounds
// write by either the composed or the split render path.
static void test_tiny_heights_stay_within_destination() {
    LoopEngine e;
    e.reset(10.f, 100.f);
    const float signal[] = {0.1f, 0.8f, -0.4f, 0.3f};
    recordStereo(e, signal, signal, 4);

    static constexpr int width = 7;
    static constexpr uint32_t canary = 0xDEADBEEFu;
    for (int height = 0; height < LoopEngine::NUM_HEADS * 3; ++height) {
        std::vector<uint32_t> guarded(std::size_t(width) * height + 2, canary);
        LoopWaveformRenderer::render(guarded.data() + 1, width, height, e, pack);
        check(guarded.front() == canary && guarded.back() == canary,
              "tiny geometry: composed render preserves canaries");

        const auto geometry = LoopWaveformRenderer::geometry(height, e.numHeads());
        check(geometry.waveHeight + geometry.lanesHeight == height,
              "tiny geometry: regions exactly cover destination height");
        check(geometry.lanesHeight <= height,
              "tiny geometry: lanes are capped to destination height");

        std::fill(guarded.begin(), guarded.end(), canary);
        auto* pixels = guarded.data() + 1;
        LoopWaveformRenderer::renderWaveform(
            pixels, width, geometry.waveHeight, e, pack);
        LoopWaveformRenderer::renderLanes(
            pixels + std::size_t(width) * geometry.waveHeight,
            width, geometry.lanesHeight, geometry.laneHeight, e, pack);
        check(guarded.front() == canary && guarded.back() == canary,
              "tiny geometry: split render preserves canaries");
    }
}

int main() {
    test_blank();
    test_waveform_and_lanes();
    test_stereo_bands();
    test_phase_cancellation();
    test_moved_window_lane();
    test_recording_view();
    test_level_aware_height();
    test_tiny_display_combined();
    test_single_head_single_lane();
    test_split_render_matches_composed_render();
    test_tiny_heights_stay_within_destination();
    if (g_failures == 0) std::printf("All display renderer tests passed.\n");
    return g_failures;
}

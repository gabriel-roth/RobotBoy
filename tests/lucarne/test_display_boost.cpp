#include "../../src/lucarne/ColorScheme.hpp"
#include "../../src/lucarne/DisplayBoost.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string_view>

using namespace lucarne;

static int g_failures = 0;
static void check(bool cond, const char* name) {
    if (!cond) { std::printf("FAIL: %s\n", name); ++g_failures; }
    else       { std::printf("ok:   %s\n", name); }
}
static bool same(Rgb a, Rgb b) { return a.r == b.r && a.g == b.g && a.b == b.b; }
static float luma(Rgb c) { return 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b; }
// HSV saturation: the channel spread relative to the brightest channel.
static float saturation(Rgb c) {
    const int mx = std::max({c.r, c.g, c.b});
    return mx ? float(mx - std::min({c.r, c.g, c.b})) / float(mx) : 0.f;
}
static float chroma(Rgb c) { return float(std::max({c.r, c.g, c.b}) - std::min({c.r, c.g, c.b})); }
static float hue(Rgb c) {
    float r = c.r / 255.f, g = c.g / 255.f, b = c.b / 255.f;
    float mx = std::max({r, g, b}), mn = std::min({r, g, b}), d = mx - mn;
    if (d <= 0.f) return 0.f;
    float h = mx == r ? std::fmod((g - b) / d, 6.f) : mx == g ? (b - r) / d + 2.f : (r - g) / d + 4.f;
    h *= 60.f;
    return h < 0.f ? h + 360.f : h;
}
static float hueDist(float a, float b) { float d = std::fabs(a - b); return std::min(d, 360.f - d); }

// Every color the schemes draw on the display.
template <typename F> static void forEachDisplayColor(F f) {
    for (const ColorScheme& s : kSchemes)
        for (Rgb c : {s.bg, s.wave, s.head, s.frozen, s.panL, s.panC, s.panR, s.dry, s.tapA, s.tapB, s.text, s.hi})
            f(s, c);
}

// Strengths in increasing order, the MetaModule setting among them.
static const float kStrengths[] = {0.f, 0.15f, 0.3f, 0.45f, DisplayBoost::kMetaModule, 0.6f};
static const int kNumStrengths = int(sizeof(kStrengths) / sizeof(kStrengths[0]));

int main() {
    {
        DisplayBoost off(0.f);
        bool identity = true;
        for (int r = 0; r < 256; r += 5)
            for (int g = 0; g < 256; g += 5)
                for (int b = 0; b < 256; b += 5)
                    identity &= same(off.apply({uint8_t(r), uint8_t(g), uint8_t(b)}), {uint8_t(r), uint8_t(g), uint8_t(b)});
        check(identity, "strength 0 leaves every color unchanged");
    }
    for (int i = 1; i < kNumStrengths; ++i) {
        DisplayBoost b(kStrengths[i]);
        check(same(b.apply({0, 0, 0}), {0, 0, 0}) && same(b.apply({255, 255, 255}), {255, 255, 255}),
              "black and white stay put");
        bool monotonic = true;
        for (int i = 1; i < 256; ++i)
            monotonic &= luma(b.apply({uint8_t(i), uint8_t(i), uint8_t(i)})) >=
                         luma(b.apply({uint8_t(i - 1), uint8_t(i - 1), uint8_t(i - 1)}));
        check(monotonic, "grays keep their order");
        bool hues = true, chromas = true;
        forEachDisplayColor([&](const ColorScheme&, Rgb c) {
            Rgb o = b.apply(c);
            if (chroma(c) >= 24.f) hues &= hueDist(hue(c), hue(o)) <= 3.f;
            chromas &= saturation(o) + 0.04f >= saturation(c);
        });
        check(hues, "scheme colors keep their hue (within 3 degrees, rounding)");
        check(chromas, "scheme colors never lose saturation");
    }
    {
        // Each level pushes colors darker than the pivot darker and the rest
        // brighter: dark backgrounds sink, Paper's paper whitens and its ink
        // darkens, waveforms and marks rise.
        bool ordered = true;
        for (int i = 1; i < kNumStrengths; ++i) {
            DisplayBoost lo(kStrengths[i - 1]), hi(kStrengths[i]);
            forEachDisplayColor([&](const ColorScheme& s, Rgb c) {
                const bool dark = luma(c) < DisplayBoost::kPivot * 255.f;
                const float a = luma(lo.apply(c)), b = luma(hi.apply(c));
                const bool ok = dark ? b <= a + 0.5f : b + 0.5f >= a;
                if (!ok) std::printf("  %s %02X%02X%02X strength %g\n", s.id, c.r, c.g, c.b, kStrengths[i]);
                ordered &= ok;
            });
        }
        check(ordered, "each step up darkens colors below the pivot and brightens the rest");
    }
    std::printf(g_failures ? "%d FAILED\n" : "all passed\n", g_failures);
    return g_failures ? 1 : 0;
}

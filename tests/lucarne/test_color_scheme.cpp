#include "../../src/lucarne/ColorScheme.hpp"
#include "../../src/lucarne/KeyLayout.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace lucarne;

static int g_failures = 0;
static void check(bool cond, const char* name) {
    if (!cond) { std::printf("FAIL: %s\n", name); ++g_failures; }
    else       { std::printf("ok:   %s\n", name); }
}
static bool eq(Rgb c, uint32_t hex) {
    return c.r == ((hex >> 16) & 0xFF) && c.g == ((hex >> 8) & 0xFF) && c.b == (hex & 0xFF);
}
// Within `tol` levels per channel.
static bool near(Rgb c, uint32_t hex, int tol) {
    return std::abs(c.r - int((hex >> 16) & 0xFF)) <= tol &&
           std::abs(c.g - int((hex >> 8) & 0xFF)) <= tol && std::abs(c.b - int(hex & 0xFF)) <= tol;
}
static float hue(Rgb c) {
    float r = c.r / 255.f, g = c.g / 255.f, b = c.b / 255.f;
    float mx = std::max({r, g, b}), mn = std::min({r, g, b}), d = mx - mn;
    if (d <= 0.f) return 0.f;
    float h;
    if (mx == r)      h = std::fmod((g - b) / d, 6.f);
    else if (mx == g) h = (b - r) / d + 2.f;
    else              h = (r - g) / d + 4.f;
    h *= 60.f;
    return h < 0.f ? h + 360.f : h;
}
static float hueGap(Rgb a, Rgb b) {
    float d = std::fabs(hue(a) - hue(b));
    return d > 180.f ? 360.f - d : d;
}

int main() {
    static const char* ids[kNumSchemes] = {"lucarne", "darkroom", "paper", "cyanotype",
                                           "thermal", "bakelite", "anaglyph"};
    bool idsOk = true, ascii = true;
    for (int i = 0; i < kNumSchemes; ++i) {
        idsOk = idsOk && std::strcmp(kSchemes[i].id, ids[i]) == 0;
        for (const char* p = kSchemes[i].name; *p; ++p) ascii = ascii && (unsigned char)*p < 128;
    }
    check(idsOk, "ids in order");
    check(ascii, "names are ASCII");

    const ColorScheme& l = kSchemes[0];
    check(eq(l.bg, 0x101418) && eq(l.wave, 0x51667A) && eq(l.head, 0xC0C8D0) &&
          eq(l.frozen, 0x505860), "lucarne bg/wave/head/frozen");
    check(eq(l.panL, 0x3FA9F5) && eq(l.panC, 0xE8E4DA) && eq(l.panR, 0xFF8A3D) &&
          eq(l.dry, 0xE6EAF0), "lucarne pan/dry");
    check(eq(l.tapA, 0xFFB23E) && eq(l.tapB, 0x3FD6FF) && eq(l.text, 0x808A94) &&
          eq(l.hi, 0xFFFFFF), "lucarne tap/text/hi");
    check(eq(l.keyWave, 0x7D9AB5) && eq(l.keyLineA, 0xF0AA42) && eq(l.keyLineB, 0x4BAECE) &&
          eq(l.keyLineDry, 0xC9CBD3), "lucarne key colors");

    // The key in the lucarne scheme matches today's panel colors (one level
    // per channel: the rule-derived chips and flash bars round differently).
    check(eq(keyColor(KeyRole::Wave, l), 0x7D9AB5), "key wave");
    check(eq(keyColor(KeyRole::PanL, l), 0x3FA9F5) && near(keyColor(KeyRole::PanLC, l), 0x94C6E8, 1) &&
          eq(keyColor(KeyRole::PanC, l), 0xE8E4DA) && near(keyColor(KeyRole::PanCR, l), 0xF4B78C, 1) &&
          eq(keyColor(KeyRole::PanR, l), 0xFF8A3D), "key pan chips");
    check(eq(keyColor(KeyRole::LineA, l), 0xF0AA42) && eq(keyColor(KeyRole::LineB, l), 0x4BAECE) &&
          eq(keyColor(KeyRole::LineDry, l), 0xC9CBD3), "key marker lines");
    check(near(keyColor(KeyRole::BarA, l), 0xFFBA51, 1) && near(keyColor(KeyRole::BarB, l), 0x52DAFF, 1) &&
          near(keyColor(KeyRole::BarDry, l), 0xE8ECF2, 1), "key flash bars");

    check(schemeFromPatch(nullptr) == 0, "null id -> 0");
    check(schemeFromPatch("nope") == 0, "unknown id -> 0");
    check(schemeFromPatch("paper") == 2, "paper -> 2");
    check(std::strcmp(scheme(99).id, "lucarne") == 0, "scheme(99) clamps");
    check(std::strcmp(scheme(-1).id, "lucarne") == 0, "scheme(-1) clamps");
    check(std::strcmp(scheme(2).id, "paper") == 0, "scheme(2) is paper");

    for (int i = 0; i < kNumSchemes; ++i) {
        char name[64];
        std::snprintf(name, sizeof name, "%s hue split panL/panR >= 30", kSchemes[i].id);
        check(hueGap(kSchemes[i].panL, kSchemes[i].panR) >= 30.f, name);
        std::snprintf(name, sizeof name, "%s hue split tapA/tapB >= 30", kSchemes[i].id);
        check(hueGap(kSchemes[i].tapA, kSchemes[i].tapB) >= 30.f, name);
    }

    std::printf(g_failures ? "%d FAILED\n" : "all passed\n", g_failures);
    return g_failures ? 1 : 0;
}

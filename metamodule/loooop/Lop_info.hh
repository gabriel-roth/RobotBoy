#pragma once
#include "QlpElements.hh"
#include "brand.hh"
#include <array>

namespace MetaModule
{

// HAND-MAINTAINED FILE — never overwrite with `panel_gen.py --metamodule`.
// Same contract as Loooop_info.hh: Elements array and Elem enum stay in the
// SAME order, mirroring the VCV enums in src/loooop/Lop.cpp. After a panel
// change run: python3 metamodule/loooop/sync_info_positions.py
struct LopInfo : ModuleInfoBase {
    static constexpr std::string_view slug{"Lop"};
    static constexpr std::string_view description{"Stereo RAM looper, one interpolating playhead. Lightweight Loooop."};
    static constexpr uint32_t width_hp = 12;
    // Brand-prefixed asset path — see Loooop_info.hh's png_filename comment.
    static constexpr std::string_view png_filename{ROBOTBOY_BRAND "/Loooop/Lop.png"};

    using enum Coords;

    // Order mirrors Loooop's per-head block (minus the pan/level Löp lacks) so
    // the two modules' MetaModule menus read the same: Size, Pos, Speed, Jitter,
    // Trig-mode, Speed V/Oct, then globals; input jacks follow the same order.
    // Keep this array and the Elem enum below in the SAME order, mirroring the
    // VCV enums in src/loooop/Lop.cpp.
    // Exception: WriteModeAlt below is a menu-only extra with no VCV
    // counterpart (VCV absorbed Write mode into its 5-state Overdub button;
    // this build keeps the alt-param for MM patch compat), so ids after
    // CrossfadeSwitch are offset by one from the VCV enums — the same
    // arrangement as Loooop_info.hh.
    static constexpr std::array<Element, 27> Elements{{
        // ── Params: Size, Pos, Speed, Jitter, Trig-mode, Speed V/Oct ──
        QlpKnob{{9.870f, 91.050f, Center, "Size", "", 9.f, 9.f}, 1.0f},
        QlpKnob{{30.480f, 46.350f, Center, "Position", "", 9.f, 9.f}, 0.5f},
        QlpKnob{{12.160f, 46.350f, Center, "Speed", "", 9.f, 9.f}, 0.75f},
        QlpKnob{{48.800f, 46.350f, Center, "Jitter", "", 9.f, 9.f}, 0.0f},
        QlpTrigModeAlt{{0.f, 0.f, Center, "Trigger", "", 0.f, 0.f}},
        QlpVoctAlt{{0.f, 0.f, Center, "Speed CV V/Oct", "", 0.f, 0.f}},
        // ── Global params ──
        QlpKnob{{23.610f, 91.050f, Center, "Dry/Wet", "", 9.f, 9.f}, 1.0f},
        QlpButtonLight{{51.708f, 91.050f, Center, "Record", "", 5.f, 5.f}},
        QlpButton{{37.968f, 91.050f, Center, "Clear", "", 5.f, 5.f}},
        QlpOverdubAlt{{0.f, 0.f, Center, "Overdub", "", 0.f, 0.f}, 1},
        QlpCrossfadeAlt{{0.f, 0.f, Center, "Crossfade", "", 0.f, 0.f}, 0},
        QlpWriteModeAlt{{0.f, 0.f, Center, "Write mode", "", 0.f, 0.f}},
        QlpGridAlt{{0.f, 0.f, Center, "Grid", "", 0.f, 0.f}},
        // ── Input jacks: Size CV, Pos CV, Speed CV, Jitter CV, Trig, Jump ──
        QlpJackIn{{9.870f, 103.400f, Center, "Size CV", "", 6.f, 6.f}},
        QlpJackIn{{30.480f, 58.700f, Center, "Position CV", "", 6.f, 6.f}},
        QlpJackIn{{12.160f, 58.700f, Center, "Speed CV", "", 6.f, 6.f}},
        QlpJackIn{{48.800f, 58.700f, Center, "Jitter CV", "", 6.f, 6.f}},
        QlpJackIn{{9.870f, 74.050f, Center, "Trig", "", 6.f, 6.f}},
        QlpJackIn{{23.610f, 74.050f, Center, "Jump", "", 6.f, 6.f}},
        // ── Global input jacks ──
        QlpJackIn{{7.350f, 116.050f, Center, "In L", "", 6.f, 6.f}},
        QlpJackIn{{17.050f, 116.050f, Center, "In R", "", 6.f, 6.f}},
        QlpJackIn{{51.708f, 103.400f, Center, "Rec Trig", "", 6.f, 6.f}},
        QlpJackIn{{37.968f, 103.400f, Center, "Clear Trig", "", 6.f, 6.f}},
        QlpJackIn{{23.610f, 103.400f, Center, "Dry/Wet CV", "", 6.f, 6.f}},
        // ── Output jacks ──
        QlpJackOut{{43.910f, 116.050f, Center, "Out L", "", 6.f, 6.f}},
        QlpJackOut{{53.610f, 116.050f, Center, "Out R", "", 6.f, 6.f}},
        QlpDisplay{{1.500f, 10.400f, TopLeft, "Display", "", 57.960f, 22.350f}},
    }};

    enum class Elem {
        SizeKnob, PositionKnob, SpeedKnob, JitterKnob, TrigModeAlt, SpeedVoctAlt,
        DryWetKnob, RecordButton, ClearButton, OverdubSwitch, CrossfadeSwitch, WriteModeAlt, GridAlt,
        SizeCvIn, PositionCvIn, SpeedCvIn, JitterCvIn, TrigIn, JumpIn,
        AudioInL, AudioInR, RecTrigIn, ClearTrigIn, DryWetCvIn,
        OutL, OutR,
        Display,
    };

    // Bypass: audio ins route straight to the outs. Raw jack indices
    // (Elem order among each type): inputs AudioInL=6, AudioInR=7 (they follow
    // the 6 per-head input jacks); outputs OutL=0, OutR=1.
    static constexpr std::array<BypassRoute, 2> bypass_routes{{{6, 0}, {7, 1}}};
};
} // namespace MetaModule

#pragma once
#include "QlpElements.hh"
#include "brand.hh"
#include <array>

namespace MetaModule
{

// HAND-MAINTAINED FILE — never overwrite with `panel_gen.py --metamodule`.
// Same contract as Loooop_info.hh: Elements array and Elem enum stay in the
// SAME order. INVARIANT: the param-type elements must appear in the SAME order
// as the VCV ParamId enum in src/loooop/Lop.cpp (and inputs/outputs mirror
// InputId/OutputId) — MetaModule assigns IDs by Elements-array position and
// VCV↔MM patch conversion maps by ID, so divergence scrambles a patch's knobs
// across hosts. After a panel change run:
// python3 metamodule/loooop/sync_info_positions.py
struct LopInfo : ModuleInfoBase {
    static constexpr std::string_view slug{"Lop"};
    static constexpr std::string_view description{"Stereo RAM looper, one interpolating playhead. Lightweight Loooop."};
    static constexpr uint32_t width_hp = 12;
    // Brand-prefixed asset path — see Loooop_info.hh's png_filename comment.
    static constexpr std::string_view png_filename{ROBOTBOY_BRAND "/Loooop/Lop.png"};

    using enum Coords;

    // Order mirrors Loooop's per-head block (minus the pan/level Löp lacks) so
    // the two modules' MetaModule menus read the same: Size, Pos, Speed,
    // Jitter, then globals; input jacks follow the same order. Keep this array
    // and the Elem enum below in the SAME order, mirroring the VCV enums in
    // src/loooop/Lop.cpp. Overdub and Grid are panel controls (Overdub is a
    // 5-position FlipSwitch with a per-mode colour button, Grid a stepped knob). Trig-mode, Speed V/Oct, and
    // Crossfade are menu-only (AltParamChoiceLabeled, position fields unused);
    // they sit in ONE contiguous block AFTER all the jacks and BEFORE the
    // display, grouped by command (Trigger when recording, Crossfade, Trigger,
    // Speed CV V/Oct) so the
    // MetaModule roller headers this block as an "Options:" section (a param
    // group that FOLLOWS the jacks), matching VCV's command-first menu layout
    // instead of interleaving with the panel knobs.
    static constexpr std::array<Element, 27> Elements{{
        // ── Params: Size, Pos, Speed, Jitter ──
        QlpKnob{{9.870f, 46.050f, Center, "Size", "", 9.f, 9.f}, 1.0f},
        QlpKnob{{23.610f, 46.050f, Center, "Position", "", 9.f, 9.f}, 0.5f},
        QlpKnob{{37.350f, 46.050f, Center, "Speed", "", 9.f, 9.f}, 0.75f},
        QlpKnob{{51.090f, 46.050f, Center, "Jitter", "", 9.f, 9.f}, 0.0f},
        // ── Global params ──
        QlpKnob{{12.160f, 92.950f, Center, "Dry/Wet", "", 9.f, 9.f}, 1.0f},
        QlpButtonLight{{30.480f, 92.950f, Center, "Record", "", 5.f, 5.f}},
        QlpButton{{48.800f, 92.950f, Center, "Clear", "", 5.f, 5.f}},
        QlpOverdubSwitch{{30.480f, 116.050f, Center, "Overdub mode", "", 5.f, 5.f}},
        QlpGridKnob{{48.800f, 74.150f, Center, "Grid", "", 9.f, 9.f}},
        // ── Input jacks: Size CV, Pos CV, Speed CV, Jitter CV, Trig, Jump ──
        QlpJackIn{{9.870f, 58.000f, Center, "Size CV", "", 6.f, 6.f}},
        QlpJackIn{{23.610f, 58.000f, Center, "Position CV", "", 6.f, 6.f}},
        QlpJackIn{{37.350f, 58.000f, Center, "Speed CV", "", 6.f, 6.f}},
        QlpJackIn{{51.090f, 58.000f, Center, "Jitter CV", "", 6.f, 6.f}},
        QlpJackIn{{12.160f, 74.150f, Center, "Trig", "", 6.f, 6.f}},
        QlpJackIn{{30.480f, 74.150f, Center, "Jump", "", 6.f, 6.f}},
        // ── Global input jacks ──
        QlpJackIn{{7.350f, 116.050f, Center, "In L", "", 6.f, 6.f}},
        QlpJackIn{{17.050f, 116.050f, Center, "In R", "", 6.f, 6.f}},
        QlpJackIn{{30.480f, 104.900f, Center, "Rec Trig", "", 6.f, 6.f}},
        QlpJackIn{{48.800f, 104.900f, Center, "Clear Trig", "", 6.f, 6.f}},
        QlpJackIn{{12.160f, 104.900f, Center, "Dry/Wet CV", "", 6.f, 6.f}},
        // ── Output jacks ──
        QlpJackOut{{43.910f, 116.050f, Center, "Out L", "", 6.f, 6.f}},
        QlpJackOut{{53.610f, 116.050f, Center, "Out R", "", 6.f, 6.f}},
        // ── Options (menu-only alt-params), grouped by command ──
        QlpTrigWhenRecAlt{{0.f, 0.f, Center, "Trigger when recording", "", 0.f, 0.f}},
        QlpCrossfadeAlt{{0.f, 0.f, Center, "Crossfade", "", 0.f, 0.f}, 0},
        QlpTrigModeAlt{{0.f, 0.f, Center, "Trigger", "", 0.f, 0.f}},
        QlpVoctAlt{{0.f, 0.f, Center, "Speed CV V/Oct", "", 0.f, 0.f}},
        QlpDisplay{{1.500f, 10.400f, TopLeft, "Display", "", 57.960f, 22.350f}},
    }};

    enum class Elem {
        SizeKnob, PositionKnob, SpeedKnob, JitterKnob,
        DryWetKnob, RecordButton, ClearButton, OverdubSwitch, GridKnob,
        SizeCvIn, PositionCvIn, SpeedCvIn, JitterCvIn, TrigIn, JumpIn,
        AudioInL, AudioInR, RecTrigIn, ClearTrigIn, DryWetCvIn,
        OutL, OutR,
        TrigWhenRecAlt, CrossfadeSwitch, TrigModeAlt, SpeedVoctAlt,
        Display,
    };

    // Bypass: audio ins route straight to the outs. Raw jack indices
    // (Elem order among each type): inputs AudioInL=6, AudioInR=7 (they follow
    // the 6 per-head input jacks); outputs OutL=0, OutR=1.
    static constexpr std::array<BypassRoute, 2> bypass_routes{{{6, 0}, {7, 1}}};
};
} // namespace MetaModule

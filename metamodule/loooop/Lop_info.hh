#pragma once
#include "QlpElements.hh"
#include <array>

namespace MetaModule
{

// HAND-MAINTAINED FILE — never overwrite with `panel_gen.py --metamodule`.
// Same contract as Loooop_info.hh: Elements array and Elem enum stay in the
// SAME order, mirroring the VCV enums in vcv/src/Lop.cpp. After a panel
// change run: python3 metamodule/sync_info_positions.py
struct LopInfo : ModuleInfoBase {
    static constexpr std::string_view slug{"Lop"};
    static constexpr std::string_view description{"Stereo RAM looper, one interpolating playhead. Lightweight Loooop."};
    static constexpr uint32_t width_hp = 12;
    // NOTE: leading "Foobar/" — see Loooop_info.hh's png_filename comment.
    static constexpr std::string_view png_filename{"Foobar/Loooop/Lop.png"};

    using enum Coords;

    static constexpr std::array<Element, 25> Elements{{
        QlpKnob{{12.160f, 46.350f, Center, "Speed", "", 9.f, 9.f}, 0.75f},
        QlpKnob{{30.480f, 46.350f, Center, "Position", "", 9.f, 9.f}, 0.5f},
        QlpKnob{{9.870f, 75.050f, Center, "Size", "", 9.f, 9.f}, 1.0f},
        QlpKnob{{48.800f, 46.350f, Center, "Jitter", "", 9.f, 9.f}, 0.0f},
        QlpKnob{{23.610f, 75.050f, Center, "Dry/Wet", "", 9.f, 9.f}, 1.0f},
        QlpButtonLight{{37.350f, 91.250f, Center, "Record", "", 5.f, 5.f}},
        QlpButton{{51.090f, 91.250f, Center, "Clear", "", 5.f, 5.f}},
        QlpOverdubAlt{{0.f, 0.f, Center, "Overdub", "", 0.f, 0.f}, 1},
        QlpTrigModeAlt{{0.f, 0.f, Center, "Trigger", "", 0.f, 0.f}},
        QlpVoctAlt{{0.f, 0.f, Center, "Speed CV V/Oct", "", 0.f, 0.f}},
        QlpCrossfadeAlt{{0.f, 0.f, Center, "Crossfade", "", 0.f, 0.f}, 0},
        QlpJackIn{{11.890f, 116.050f, Center, "In L", "", 6.f, 6.f}},
        QlpJackIn{{21.590f, 116.050f, Center, "In R", "", 6.f, 6.f}},
        QlpJackIn{{37.350f, 102.100f, Center, "Rec Trig", "", 6.f, 6.f}},
        QlpJackIn{{51.090f, 102.100f, Center, "Clear Trig", "", 6.f, 6.f}},
        QlpJackIn{{12.160f, 58.700f, Center, "Speed CV", "", 6.f, 6.f}},
        QlpJackIn{{30.480f, 58.700f, Center, "Position CV", "", 6.f, 6.f}},
        QlpJackIn{{9.870f, 87.400f, Center, "Size CV", "", 6.f, 6.f}},
        QlpJackIn{{48.800f, 58.700f, Center, "Jitter CV", "", 6.f, 6.f}},
        QlpJackIn{{23.610f, 87.400f, Center, "Dry/Wet CV", "", 6.f, 6.f}},
        QlpJackIn{{9.870f, 102.100f, Center, "Trig", "", 6.f, 6.f}},
        QlpJackIn{{23.610f, 102.100f, Center, "Jump", "", 6.f, 6.f}},
        QlpJackOut{{39.370f, 116.050f, Center, "Out L", "", 6.f, 6.f}},
        QlpJackOut{{49.070f, 116.050f, Center, "Out R", "", 6.f, 6.f}},
        QlpDisplay{{1.500f, 10.400f, TopLeft, "Display", "", 57.960f, 22.350f}},
    }};

    enum class Elem {
        SpeedKnob, PositionKnob, SizeKnob, JitterKnob, DryWetKnob,
        RecordButton, ClearButton,
        OverdubSwitch, TrigModeAlt, SpeedVoctAlt, CrossfadeSwitch,
        AudioInL, AudioInR, RecTrigIn, ClearTrigIn,
        SpeedCvIn, PositionCvIn, SizeCvIn, JitterCvIn, DryWetCvIn,
        TrigIn, JumpIn,
        OutL, OutR,
        Display,
    };

    // Bypass: audio ins route straight to the outs. Raw jack indices
    // (Elem order): inputs AudioInL=0, AudioInR=1; outputs OutL=0, OutR=1.
    static constexpr std::array<BypassRoute, 2> bypass_routes{{{0, 0}, {1, 1}}};
};
} // namespace MetaModule

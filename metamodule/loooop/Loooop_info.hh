#pragma once
#include "QlpElements.hh"
#include "brand.hh"
#include <array>

namespace MetaModule
{

struct LoooopInfo : ModuleInfoBase {
    static constexpr std::string_view slug{"Loooop"};
    static constexpr std::string_view description{"Stereo RAM looper, four interpolating playheads. Inspired by Cutlasses Gloop."};
    static constexpr uint32_t width_hp = 38;
    // The leading brand slug mirrors the top-level directory the SDK's
    // create_plugin() always wraps assets in (both the real .mmplugin tar and
    // the simulator's built-in-plugin hack namespace assets under the brand
    // slug from plugin.json — confirmed by inspecting `tar tf Foobar.mmplugin`
    // and by a "Could not read image" runtime failure in the sim without this
    // prefix). Derived from FOOBAR_BRAND (see brand.hh) so it can't drift on a
    // brand rename.
    static constexpr std::string_view png_filename{FOOBAR_BRAND "/Loooop/Loooop.png"};

    using enum Coords;

    // BaseElement: {x_mm, y_mm, coords, short_name, long_name, width_mm, height_mm}
    // Positions mirror the 38 HP VCV panel (res/Loooop.svg). Keep this array and
    // the Elem enum below in the SAME order — SmartCoreProcessor maps
    // enum->element by index. Param order (knobs, then buttons, then alt-params)
    // and jack order mirror the VCV Param/Input/Output enums so patch ids line
    // up between the two builds. Overdub, trigger flavor, and speed V/Oct are
    // menu-only (AltParamChoiceLabeled) so their position fields are unused.
    static constexpr std::array<Element, 85> Elements{{
        QlpKnob{{39.467f, 46.350f, Center, "Speed 1", "", 9.f, 9.f}, 0.75f},
        QlpKnob{{87.227f, 46.350f, Center, "Speed 2", "", 9.f, 9.f}, 0.75f},
        QlpKnob{{134.987f, 46.350f, Center, "Speed 3", "", 9.f, 9.f}, 0.75f},
        QlpKnob{{182.747f, 46.350f, Center, "Speed 4", "", 9.f, 9.f}, 0.75f},
        QlpKnob{{24.880f, 46.350f, Center, "Position 1", "", 9.f, 9.f}, 0.5f},
        QlpKnob{{72.640f, 46.350f, Center, "Position 2", "", 9.f, 9.f}, 0.5f},
        QlpKnob{{120.400f, 46.350f, Center, "Position 3", "", 9.f, 9.f}, 0.5f},
        QlpKnob{{168.160f, 46.350f, Center, "Position 4", "", 9.f, 9.f}, 0.5f},
        QlpKnob{{10.293f, 46.350f, Center, "Size 1", "", 9.f, 9.f}, 1.0f},
        QlpKnob{{58.053f, 46.350f, Center, "Size 2", "", 9.f, 9.f}, 1.0f},
        QlpKnob{{105.813f, 46.350f, Center, "Size 3", "", 9.f, 9.f}, 1.0f},
        QlpKnob{{153.573f, 46.350f, Center, "Size 4", "", 9.f, 9.f}, 1.0f},
        QlpKnob{{39.467f, 75.050f, Center, "Level 1", "", 9.f, 9.f}, 0.25f},
        QlpKnob{{87.227f, 75.050f, Center, "Level 2", "", 9.f, 9.f}, 0.25f},
        QlpKnob{{134.987f, 75.050f, Center, "Level 3", "", 9.f, 9.f}, 0.25f},
        QlpKnob{{182.747f, 75.050f, Center, "Level 4", "", 9.f, 9.f}, 0.25f},
        QlpKnob{{10.293f, 75.050f, Center, "Jitter 1", "", 9.f, 9.f}, 0.0f},
        QlpKnob{{58.053f, 75.050f, Center, "Jitter 2", "", 9.f, 9.f}, 0.0f},
        QlpKnob{{105.813f, 75.050f, Center, "Jitter 3", "", 9.f, 9.f}, 0.0f},
        QlpKnob{{153.573f, 75.050f, Center, "Jitter 4", "", 9.f, 9.f}, 0.0f},
        QlpKnob{{127.753f, 116.050f, Center, "Dry/Wet", "", 9.f, 9.f}, 1.0f},
        QlpButtonLight{{53.687f, 116.050f, Center, "Record", "", 7.f, 7.f}},
        QlpButton{{91.095f, 116.050f, Center, "Clear", "", 7.f, 7.f}},
        QlpOverdubAlt{{0.f, 0.f, Center, "Overdub", ""}, 1},
        QlpTrigModeAlt{{0.f, 0.f, Center, "Trig 1 mode", ""}},
        QlpTrigModeAlt{{0.f, 0.f, Center, "Trig 2 mode", ""}},
        QlpTrigModeAlt{{0.f, 0.f, Center, "Trig 3 mode", ""}},
        QlpTrigModeAlt{{0.f, 0.f, Center, "Trig 4 mode", ""}},
        QlpVoctAlt{{0.f, 0.f, Center, "Speed 1 V/Oct", ""}},
        QlpVoctAlt{{0.f, 0.f, Center, "Speed 2 V/Oct", ""}},
        QlpVoctAlt{{0.f, 0.f, Center, "Speed 3 V/Oct", ""}},
        QlpVoctAlt{{0.f, 0.f, Center, "Speed 4 V/Oct", ""}},
        QlpCrossfadeAlt{{0.f, 0.f, Center, "Crossfade", ""}, 0},
        QlpKnob{{24.880f, 75.050f, Center, "Pan 1", "", 9.f, 9.f}, 0.5f},
        QlpKnob{{72.640f, 75.050f, Center, "Pan 2", "", 9.f, 9.f}, 0.5f},
        QlpKnob{{120.400f, 75.050f, Center, "Pan 3", "", 9.f, 9.f}, 0.5f},
        QlpKnob{{168.160f, 75.050f, Center, "Pan 4", "", 9.f, 9.f}, 0.5f},
        QlpJackIn{{16.854f, 116.050f, Center, "In L", "", 8.f, 8.f}},
        QlpJackIn{{26.554f, 116.050f, Center, "In R", "", 8.f, 8.f}},
        QlpJackIn{{64.537f, 116.050f, Center, "Rec Trig", "", 8.f, 8.f}},
        QlpJackIn{{101.945f, 116.050f, Center, "Clear Trig", "", 8.f, 8.f}},
        QlpJackIn{{39.467f, 58.700f, Center, "Speed 1 CV", "", 8.f, 8.f}},
        QlpJackIn{{87.227f, 58.700f, Center, "Speed 2 CV", "", 8.f, 8.f}},
        QlpJackIn{{134.987f, 58.700f, Center, "Speed 3 CV", "", 8.f, 8.f}},
        QlpJackIn{{182.747f, 58.700f, Center, "Speed 4 CV", "", 8.f, 8.f}},
        QlpJackIn{{24.880f, 58.700f, Center, "Pos 1 CV", "", 8.f, 8.f}},
        QlpJackIn{{72.640f, 58.700f, Center, "Pos 2 CV", "", 8.f, 8.f}},
        QlpJackIn{{120.400f, 58.700f, Center, "Pos 3 CV", "", 8.f, 8.f}},
        QlpJackIn{{168.160f, 58.700f, Center, "Pos 4 CV", "", 8.f, 8.f}},
        QlpJackIn{{10.293f, 58.700f, Center, "Size 1 CV", "", 8.f, 8.f}},
        QlpJackIn{{58.053f, 58.700f, Center, "Size 2 CV", "", 8.f, 8.f}},
        QlpJackIn{{105.813f, 58.700f, Center, "Size 3 CV", "", 8.f, 8.f}},
        QlpJackIn{{153.573f, 58.700f, Center, "Size 4 CV", "", 8.f, 8.f}},
        QlpJackIn{{39.467f, 87.400f, Center, "Level 1 CV", "", 8.f, 8.f}},
        QlpJackIn{{87.227f, 87.400f, Center, "Level 2 CV", "", 8.f, 8.f}},
        QlpJackIn{{134.987f, 87.400f, Center, "Level 3 CV", "", 8.f, 8.f}},
        QlpJackIn{{182.747f, 87.400f, Center, "Level 4 CV", "", 8.f, 8.f}},
        QlpJackIn{{10.293f, 87.400f, Center, "Jitter 1 CV", "", 8.f, 8.f}},
        QlpJackIn{{58.053f, 87.400f, Center, "Jitter 2 CV", "", 8.f, 8.f}},
        QlpJackIn{{105.813f, 87.400f, Center, "Jitter 3 CV", "", 8.f, 8.f}},
        QlpJackIn{{153.573f, 87.400f, Center, "Jitter 4 CV", "", 8.f, 8.f}},
        QlpJackIn{{140.103f, 116.050f, Center, "Dry/Wet CV", "", 8.f, 8.f}},
        QlpJackIn{{8.470f, 102.100f, Center, "Trig 1", "", 8.f, 8.f}},
        QlpJackIn{{56.230f, 102.100f, Center, "Trig 2", "", 8.f, 8.f}},
        QlpJackIn{{103.990f, 102.100f, Center, "Trig 3", "", 8.f, 8.f}},
        QlpJackIn{{151.750f, 102.100f, Center, "Trig 4", "", 8.f, 8.f}},
        QlpJackIn{{19.410f, 102.100f, Center, "Jump 1", "", 8.f, 8.f}},
        QlpJackIn{{67.170f, 102.100f, Center, "Jump 2", "", 8.f, 8.f}},
        QlpJackIn{{114.930f, 102.100f, Center, "Jump 3", "", 8.f, 8.f}},
        QlpJackIn{{162.690f, 102.100f, Center, "Jump 4", "", 8.f, 8.f}},
        QlpJackIn{{24.880f, 87.400f, Center, "Pan 1 CV", "", 8.f, 8.f}},
        QlpJackIn{{72.640f, 87.400f, Center, "Pan 2 CV", "", 8.f, 8.f}},
        QlpJackIn{{120.400f, 87.400f, Center, "Pan 3 CV", "", 8.f, 8.f}},
        QlpJackIn{{168.160f, 87.400f, Center, "Pan 4 CV", "", 8.f, 8.f}},
        QlpJackOut{{30.970f, 102.100f, Center, "Out 1L", "", 8.f, 8.f}},
        QlpJackOut{{40.670f, 102.100f, Center, "Out 1R", "", 8.f, 8.f}},
        QlpJackOut{{78.730f, 102.100f, Center, "Out 2L", "", 8.f, 8.f}},
        QlpJackOut{{88.430f, 102.100f, Center, "Out 2R", "", 8.f, 8.f}},
        QlpJackOut{{126.490f, 102.100f, Center, "Out 3L", "", 8.f, 8.f}},
        QlpJackOut{{136.190f, 102.100f, Center, "Out 3R", "", 8.f, 8.f}},
        QlpJackOut{{174.250f, 102.100f, Center, "Out 4L", "", 8.f, 8.f}},
        QlpJackOut{{183.950f, 102.100f, Center, "Out 4R", "", 8.f, 8.f}},
        QlpJackOut{{166.486f, 116.050f, Center, "Mix L", "", 8.f, 8.f}},
        QlpJackOut{{176.186f, 116.050f, Center, "Mix R", "", 8.f, 8.f}},
        QlpDisplay{{76.653f, 10.400f, TopLeft, "Display", "", 39.733f, 22.350f}},
    }};

    enum class Elem {
        Speed1Knob, Speed2Knob, Speed3Knob, Speed4Knob,
        Position1Knob, Position2Knob, Position3Knob, Position4Knob,
        Size1Knob, Size2Knob, Size3Knob, Size4Knob,
        Level1Knob, Level2Knob, Level3Knob, Level4Knob,
        Jitter1Knob, Jitter2Knob, Jitter3Knob, Jitter4Knob,
        DryWetKnob, RecordButton, ClearButton, OverdubSwitch,
        TrigMode1Alt, TrigMode2Alt, TrigMode3Alt, TrigMode4Alt,
        SpeedVoct1Alt, SpeedVoct2Alt, SpeedVoct3Alt, SpeedVoct4Alt,
        CrossfadeSwitch,
        Pan1Knob, Pan2Knob, Pan3Knob, Pan4Knob,
        AudioInL, AudioInR, RecTrigIn, ClearTrigIn,
        Speed1CvIn, Speed2CvIn, Speed3CvIn, Speed4CvIn,
        Position1CvIn, Position2CvIn, Position3CvIn, Position4CvIn,
        Size1CvIn, Size2CvIn, Size3CvIn, Size4CvIn,
        Level1CvIn, Level2CvIn, Level3CvIn, Level4CvIn,
        Jitter1CvIn, Jitter2CvIn, Jitter3CvIn, Jitter4CvIn,
        DryWetCvIn,
        Trig1In, Trig2In, Trig3In, Trig4In,
        Jump1In, Jump2In, Jump3In, Jump4In,
        Pan1CvIn, Pan2CvIn, Pan3CvIn, Pan4CvIn,
        Head1OutL, Head1OutR, Head2OutL, Head2OutR,
        Head3OutL, Head3OutR, Head4OutL, Head4OutR,
        MixOutL, MixOutR,
        Display,
    };

    // Bypass: audio ins route straight to the mix outs. Raw jack indices
    // (Elem order): inputs AudioInL=0, AudioInR=1; outputs MixOutL=8, MixOutR=9.
    static constexpr std::array<BypassRoute, 2> bypass_routes{{{0, 8}, {1, 9}}};
};
} // namespace MetaModule

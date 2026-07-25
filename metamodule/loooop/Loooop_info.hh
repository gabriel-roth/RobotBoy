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
    // slug from plugin.json — confirmed by inspecting `tar tf RobotBoy.mmplugin`
    // and by a "Could not read image" runtime failure in the sim without this
    // prefix). Derived from ROBOTBOY_BRAND (see brand.hh) so it can't drift on a
    // brand rename.
    static constexpr std::string_view png_filename{ROBOTBOY_BRAND "/Loooop/Loooop.png"};

    using enum Coords;

    // BaseElement: {x_mm, y_mm, coords, short_name, long_name, width_mm, height_mm}
    // Positions mirror the 38 HP VCV panel (res/Loooop.svg). Keep this array and
    // the Elem enum below in the SAME order — SmartCoreProcessor maps
    // enum->element by index.
    //
    // INVARIANT: the PARAM-type elements here must appear in the SAME order as
    // the VCV ParamId enum (src/loooop/Loooop.cpp). MetaModule assigns param IDs
    // by Elements-array position and VCV↔MM patch conversion maps by ID, so if
    // the two orders diverge a patch's knob values load onto the wrong control
    // when it crosses hosts. (Inputs/outputs likewise mirror InputId/OutputId.)
    //
    // Global params and jacks come FIRST (so the
    // MetaModule manual lists them at the top), then params and jacks grouped
    // PER HEAD (head 1's Size/Pos/Speed/Jitter/Pan/Level, then head 2, etc.) so
    // the MetaModule mapping menu lists everything for one head together.
    // Overdub and Grid are panel controls (Overdub is a 5-position FlipSwitch
    // with a per-mode colour button, Grid a stepped knob). Trig-mode, Speed V/Oct, Grid exclude, and Crossfade
    // are menu-only (AltParamChoiceLabeled, position fields unused); they sit
    // in ONE contiguous block AFTER all the jacks and BEFORE the display,
    // grouped by command (When-recording-ends, Crossfade, then all
    // Trig-modes, then all Speed V/Oct, then all Grid-excludes, then Record
    // jack) so the MetaModule roller headers this
    // block as an "Options:" section (a param group that FOLLOWS the jacks),
    // matching VCV's command-first menu layout instead of interleaving with
    // the panel knobs.
    static constexpr std::array<Element, 92> Elements{{
        // ── Global params ──
        QlpButtonLight{{36.452f, 116.050f, Center, "Record", "", 7.f, 7.f}},
        QlpOverdubSwitch{{69.543f, 116.050f, Center, "Overdub mode", "", 7.f, 7.f}},
        QlpButton{{91.785f, 116.050f, Center, "Clear", "", 7.f, 7.f}},
        QlpGridKnob{{122.687f, 116.050f, Center, "Grid", "", 9.f, 9.f}},
        QlpKnob{{143.488f, 116.050f, Center, "Dry/Wet", "", 9.f, 9.f}, 1.0f},

        // ── Params, grouped per head: Size, Pos, Speed, Jitter, Pan, Level ──
        QlpKnob{{10.293f, 46.350f, Center, "Red Size", "", 9.f, 9.f}, 1.0f},
        QlpKnob{{24.880f, 46.350f, Center, "Red Position", "", 9.f, 9.f}, 0.5f},
        QlpKnob{{39.467f, 46.350f, Center, "Red Speed", "", 9.f, 9.f}, 0.75f},
        QlpKnob{{10.293f, 75.050f, Center, "Red Jitter", "", 9.f, 9.f}, 0.0f},
        QlpKnob{{24.880f, 75.050f, Center, "Red Pan", "", 9.f, 9.f}, 0.5f},
        QlpKnob{{39.467f, 75.050f, Center, "Red Level", "", 9.f, 9.f}, 0.25f},

        QlpKnob{{58.053f, 46.350f, Center, "Yellow Size", "", 9.f, 9.f}, 1.0f},
        QlpKnob{{72.640f, 46.350f, Center, "Yellow Position", "", 9.f, 9.f}, 0.5f},
        QlpKnob{{87.227f, 46.350f, Center, "Yellow Speed", "", 9.f, 9.f}, 0.75f},
        QlpKnob{{58.053f, 75.050f, Center, "Yellow Jitter", "", 9.f, 9.f}, 0.0f},
        QlpKnob{{72.640f, 75.050f, Center, "Yellow Pan", "", 9.f, 9.f}, 0.5f},
        QlpKnob{{87.227f, 75.050f, Center, "Yellow Level", "", 9.f, 9.f}, 0.25f},

        QlpKnob{{105.813f, 46.350f, Center, "Blue Size", "", 9.f, 9.f}, 1.0f},
        QlpKnob{{120.400f, 46.350f, Center, "Blue Position", "", 9.f, 9.f}, 0.5f},
        QlpKnob{{134.987f, 46.350f, Center, "Blue Speed", "", 9.f, 9.f}, 0.75f},
        QlpKnob{{105.813f, 75.050f, Center, "Blue Jitter", "", 9.f, 9.f}, 0.0f},
        QlpKnob{{120.400f, 75.050f, Center, "Blue Pan", "", 9.f, 9.f}, 0.5f},
        QlpKnob{{134.987f, 75.050f, Center, "Blue Level", "", 9.f, 9.f}, 0.25f},

        QlpKnob{{153.573f, 46.350f, Center, "Purple Size", "", 9.f, 9.f}, 1.0f},
        QlpKnob{{168.160f, 46.350f, Center, "Purple Position", "", 9.f, 9.f}, 0.5f},
        QlpKnob{{182.747f, 46.350f, Center, "Purple Speed", "", 9.f, 9.f}, 0.75f},
        QlpKnob{{153.573f, 75.050f, Center, "Purple Jitter", "", 9.f, 9.f}, 0.0f},
        QlpKnob{{168.160f, 75.050f, Center, "Purple Pan", "", 9.f, 9.f}, 0.5f},
        QlpKnob{{182.747f, 75.050f, Center, "Purple Level", "", 9.f, 9.f}, 0.25f},

        // ── Global jacks ──
        QlpJackIn{{7.350f, 116.050f, Center, "In L", "", 8.f, 8.f}},
        QlpJackIn{{17.050f, 116.050f, Center, "In R", "", 8.f, 8.f}},
        QlpJackIn{{47.302f, 116.050f, Center, "Record Trigger", "", 8.f, 8.f}},
        QlpJackIn{{102.635f, 116.050f, Center, "Clear Trigger", "", 8.f, 8.f}},
        QlpJackIn{{155.838f, 116.050f, Center, "Dry/Wet CV", "", 8.f, 8.f}},
        QlpJackOut{{175.990f, 116.050f, Center, "Mix L", "", 8.f, 8.f}},
        QlpJackOut{{185.690f, 116.050f, Center, "Mix R", "", 8.f, 8.f}},

        // ── Input jacks, grouped per head: Size CV, Pos CV, Speed CV, Jitter CV, Pan CV, Level CV, Trig, Jump ──
        QlpJackIn{{10.293f, 58.700f, Center, "Red Size CV", "", 8.f, 8.f}},
        QlpJackIn{{24.880f, 58.700f, Center, "Red Pos CV", "", 8.f, 8.f}},
        QlpJackIn{{39.467f, 58.700f, Center, "Red Speed CV", "", 8.f, 8.f}},
        QlpJackIn{{10.293f, 87.400f, Center, "Red Jitter CV", "", 8.f, 8.f}},
        QlpJackIn{{24.880f, 87.400f, Center, "Red Pan CV", "", 8.f, 8.f}},
        QlpJackIn{{39.467f, 87.400f, Center, "Red Level CV", "", 8.f, 8.f}},
        QlpJackIn{{7.350f, 102.100f, Center, "Red Trig", "", 8.f, 8.f}},
        QlpJackIn{{20.030f, 102.100f, Center, "Red Jump", "", 8.f, 8.f}},

        QlpJackIn{{58.053f, 58.700f, Center, "Yellow Size CV", "", 8.f, 8.f}},
        QlpJackIn{{72.640f, 58.700f, Center, "Yellow Pos CV", "", 8.f, 8.f}},
        QlpJackIn{{87.227f, 58.700f, Center, "Yellow Speed CV", "", 8.f, 8.f}},
        QlpJackIn{{58.053f, 87.400f, Center, "Yellow Jitter CV", "", 8.f, 8.f}},
        QlpJackIn{{72.640f, 87.400f, Center, "Yellow Pan CV", "", 8.f, 8.f}},
        QlpJackIn{{87.227f, 87.400f, Center, "Yellow Level CV", "", 8.f, 8.f}},
        QlpJackIn{{55.110f, 102.100f, Center, "Yellow Trig", "", 8.f, 8.f}},
        QlpJackIn{{67.790f, 102.100f, Center, "Yellow Jump", "", 8.f, 8.f}},

        QlpJackIn{{105.813f, 58.700f, Center, "Blue Size CV", "", 8.f, 8.f}},
        QlpJackIn{{120.400f, 58.700f, Center, "Blue Pos CV", "", 8.f, 8.f}},
        QlpJackIn{{134.987f, 58.700f, Center, "Blue Speed CV", "", 8.f, 8.f}},
        QlpJackIn{{105.813f, 87.400f, Center, "Blue Jitter CV", "", 8.f, 8.f}},
        QlpJackIn{{120.400f, 87.400f, Center, "Blue Pan CV", "", 8.f, 8.f}},
        QlpJackIn{{134.987f, 87.400f, Center, "Blue Level CV", "", 8.f, 8.f}},
        QlpJackIn{{102.870f, 102.100f, Center, "Blue Trig", "", 8.f, 8.f}},
        QlpJackIn{{115.550f, 102.100f, Center, "Blue Jump", "", 8.f, 8.f}},

        QlpJackIn{{153.573f, 58.700f, Center, "Purple Size CV", "", 8.f, 8.f}},
        QlpJackIn{{168.160f, 58.700f, Center, "Purple Pos CV", "", 8.f, 8.f}},
        QlpJackIn{{182.747f, 58.700f, Center, "Purple Speed CV", "", 8.f, 8.f}},
        QlpJackIn{{153.573f, 87.400f, Center, "Purple Jitter CV", "", 8.f, 8.f}},
        QlpJackIn{{168.160f, 87.400f, Center, "Purple Pan CV", "", 8.f, 8.f}},
        QlpJackIn{{182.747f, 87.400f, Center, "Purple Level CV", "", 8.f, 8.f}},
        QlpJackIn{{150.630f, 102.100f, Center, "Purple Trig", "", 8.f, 8.f}},
        QlpJackIn{{163.310f, 102.100f, Center, "Purple Jump", "", 8.f, 8.f}},

        // ── Head output jacks ──
        QlpJackOut{{32.710f, 102.100f, Center, "Red Out L", "", 8.f, 8.f}},
        QlpJackOut{{42.410f, 102.100f, Center, "Red Out R", "", 8.f, 8.f}},
        QlpJackOut{{80.470f, 102.100f, Center, "Yellow Out L", "", 8.f, 8.f}},
        QlpJackOut{{90.170f, 102.100f, Center, "Yellow Out R", "", 8.f, 8.f}},
        QlpJackOut{{128.230f, 102.100f, Center, "Blue Out L", "", 8.f, 8.f}},
        QlpJackOut{{137.930f, 102.100f, Center, "Blue Out R", "", 8.f, 8.f}},
        QlpJackOut{{175.990f, 102.100f, Center, "Purple Out L", "", 8.f, 8.f}},
        QlpJackOut{{185.690f, 102.100f, Center, "Purple Out R", "", 8.f, 8.f}},

        // ── Options (menu-only alt-params), grouped by command ──
        // Param order deliberately reordered 2026-07-25 (pre-release):
        // Record jack sits above When recording ends, matching the VCV menu.
        // Breaks MM patches saved before this commit.
        QlpRecGateAlt{{0.f, 0.f, Center, "Record jack", ""}},
        QlpTrigWhenRecAlt{{0.f, 0.f, Center, "When recording ends", ""}},
        QlpCrossfadeAlt{{0.f, 0.f, Center, "Crossfade", ""}, 0},
        QlpTrigModeAlt{{0.f, 0.f, Center, "Red Trig mode", ""}},
        QlpTrigModeAlt{{0.f, 0.f, Center, "Yellow Trig mode", ""}},
        QlpTrigModeAlt{{0.f, 0.f, Center, "Blue Trig mode", ""}},
        QlpTrigModeAlt{{0.f, 0.f, Center, "Purple Trig mode", ""}},
        QlpVoctAlt{{0.f, 0.f, Center, "Red Speed V/Oct", ""}},
        QlpVoctAlt{{0.f, 0.f, Center, "Yellow Speed V/Oct", ""}},
        QlpVoctAlt{{0.f, 0.f, Center, "Blue Speed V/Oct", ""}},
        QlpVoctAlt{{0.f, 0.f, Center, "Purple Speed V/Oct", ""}},
        QlpExcludeGridAlt{{0.f, 0.f, Center, "Red Grid exclude", ""}},
        QlpExcludeGridAlt{{0.f, 0.f, Center, "Yellow Grid exclude", ""}},
        QlpExcludeGridAlt{{0.f, 0.f, Center, "Blue Grid exclude", ""}},
        QlpExcludeGridAlt{{0.f, 0.f, Center, "Purple Grid exclude", ""}},

        QlpDisplay{{76.653f, 10.400f, TopLeft, "Display", "", 39.733f, 22.350f}},
    }};

    enum class Elem {
        // Global params
        RecordButton, OverdubSwitch, ClearButton, GridKnob, DryWetKnob,
        // Params, per head
        Size1Knob, Position1Knob, Speed1Knob, Jitter1Knob, Pan1Knob, Level1Knob,
        Size2Knob, Position2Knob, Speed2Knob, Jitter2Knob, Pan2Knob, Level2Knob,
        Size3Knob, Position3Knob, Speed3Knob, Jitter3Knob, Pan3Knob, Level3Knob,
        Size4Knob, Position4Knob, Speed4Knob, Jitter4Knob, Pan4Knob, Level4Knob,
        // Global jacks
        AudioInL, AudioInR, RecTrigIn, ClearTrigIn, DryWetCvIn, MixOutL, MixOutR,
        // Input jacks, per head
        Size1CvIn, Position1CvIn, Speed1CvIn, Jitter1CvIn, Pan1CvIn, Level1CvIn, Trig1In, Jump1In,
        Size2CvIn, Position2CvIn, Speed2CvIn, Jitter2CvIn, Pan2CvIn, Level2CvIn, Trig2In, Jump2In,
        Size3CvIn, Position3CvIn, Speed3CvIn, Jitter3CvIn, Pan3CvIn, Level3CvIn, Trig3In, Jump3In,
        Size4CvIn, Position4CvIn, Speed4CvIn, Jitter4CvIn, Pan4CvIn, Level4CvIn, Trig4In, Jump4In,
        // Head output jacks
        Head1OutL, Head1OutR, Head2OutL, Head2OutR,
        Head3OutL, Head3OutR, Head4OutL, Head4OutR,
        // Options (menu-only alt-params), grouped by command; order matches
        // the Elements array (reordered 2026-07-25 pre-release, see above)
        RecGateAlt,
        TrigWhenRecAlt,
        CrossfadeSwitch,
        TrigMode1Alt, TrigMode2Alt, TrigMode3Alt, TrigMode4Alt,
        SpeedVoct1Alt, SpeedVoct2Alt, SpeedVoct3Alt, SpeedVoct4Alt,
        ExcludeGrid1Alt, ExcludeGrid2Alt, ExcludeGrid3Alt, ExcludeGrid4Alt,
        Display,
    };

    // Bypass: audio ins route straight to the mix outs. Raw jack indices
    // (Elem order among each type): inputs AudioInL=0, AudioInR=1; outputs
    // MixOutL=0, MixOutR=1 (the global jacks lead both jack lists).
    static constexpr std::array<BypassRoute, 2> bypass_routes{{{0, 0}, {1, 1}}};
};
} // namespace MetaModule

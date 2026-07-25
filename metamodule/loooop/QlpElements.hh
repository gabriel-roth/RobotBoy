#pragma once
#include "CoreModules/elements/element_info.hh"
#include "brand.hh"
#include <array>

namespace MetaModule
{

// Element art is drawn on top of the background faceplate by the firmware.
// We reuse the stock 4ms component images, which are always present in the
// firmware's asset store (ram:/4ms/comp/*.png) regardless of which plugins are
// installed -- the same images 4ms's own modules use (see firmware
// lib/CoreModules/4ms/helpers/4ms_elements.hh).
//
// HAND-MAINTAINED FILE — never overwrite with `panel_gen.py --metamodule`.
// The generator cannot express the menu-only alt-params, per-param defaults,
// or the name/order contract shared with LoooopCore.cc/LopCore.cc and
// test/mm_wiring_test.py. After a panel layout change, update positions with:
//     python3 metamodule/sync_info_positions.py
// (See docs/superpowers/specs/2026-07-03-loooop-mm-header-recovery-design.md.)
struct QlpKnob : Knob {
    constexpr QlpKnob(BaseElement b, float defaultValue)
        : Knob{{{b, "4ms/comp/knob9mm_x.png"}, defaultValue}} {}
};
struct QlpButtonLight : MomentaryButtonLight {
    constexpr QlpButtonLight(BaseElement b)
        : MomentaryButtonLight{{{b, "4ms/comp/button_x.png"}, ""}, Colors565::Red} {}
};
struct QlpButton : MomentaryButton {
    constexpr QlpButton(BaseElement b)
        : MomentaryButton{{b, "4ms/comp/button_x.png"}, "4ms/comp/button_x.png"} {}
};
// Overdub is a five-position FlipSwitch (Layer/Decay/Add/Replace/Lock). Each
// frame is a dark button with the mode's colour LED baked in, so the MetaModule
// firmware shows both the colour (frame image) and the mode name (pos_names) as
// you step it. Being one self-contained element, nothing occludes it — a knob's
// redraw would otherwise yank itself in front of an overlapping light element
// (firmware redraw.hh move_foreground), which is why a knob+LED overlay failed.
// Frames live in the plugin's own asset bundle (metamodule/assets/Loooop/),
// reached via the brand slug just like the faceplate PNGs.
struct QlpOverdubSwitch : FlipSwitch {
    constexpr QlpOverdubSwitch(BaseElement b)
        : FlipSwitch{
              {{b}, 5, 0},
              {ROBOTBOY_BRAND "/Loooop/overdub_layer.png",
               ROBOTBOY_BRAND "/Loooop/overdub_decay.png",
               ROBOTBOY_BRAND "/Loooop/overdub_add.png",
               ROBOTBOY_BRAND "/Loooop/overdub_replace.png",
               ROBOTBOY_BRAND "/Loooop/overdub_lock.png"},
              {"Layer", "Decay", "Add", "Replace", "Lock"},
          } {}
};
struct QlpGridKnob : KnobSnapped {
    constexpr QlpGridKnob(BaseElement b)
        : KnobSnapped{{{{b, "4ms/comp/knob9mm_x.png"}, 0.f, 0.f, 5.f}}, 6,
                      {"Off", "4", "8", "16", "32", "64"}} {}
};
struct QlpTrigModeAlt : AltParamChoiceLabeled {
    constexpr QlpTrigModeAlt(BaseElement b)
        : AltParamChoiceLabeled{{{b}, 2, 0}, {"Loop start", "One-shot"}} {}
};
struct QlpVoctAlt : AltParamChoiceLabeled {
    constexpr QlpVoctAlt(BaseElement b)
        : AltParamChoiceLabeled{{{b}, 2, 0}, {"Off", "On"}} {}
};
// Choice order is inverted (On first) on purpose: the MM patch loader
// zero-inits unset alt-params (SmartCoreProcessor paramValues{}) and does NOT
// apply default_value, so index 0 must be the crossfade-ON state for the
// declick to default on for fresh modules and patches saved before this param.
struct QlpCrossfadeAlt : AltParamChoiceLabeled {
    constexpr QlpCrossfadeAlt(BaseElement b, unsigned defaultValue)
        : AltParamChoiceLabeled{{{b}, 2, defaultValue}, {"On", "Off"}} {}
};
// Index 0 = "Plays back" (legacy "Stops recording" behavior, same stored
// value): the loader zero-inits unset alt-params, so fresh modules/patches
// keep the record toggle stopping instead of rolling straight into an
// overdub pass.
struct QlpTrigWhenRecAlt : AltParamChoiceLabeled {
    constexpr QlpTrigWhenRecAlt(BaseElement b)
        : AltParamChoiceLabeled{{{b}, 2, 0}, {"Plays back", "Keeps overdubbing"}} {}
};
// Index 0 = Trigger (today's behavior byte-for-byte): the loader zero-inits
// unset alt-params, so fresh modules/patches keep the legacy button||jack
// combined-edge behavior.
struct QlpRecGateAlt : AltParamChoiceLabeled {
    constexpr QlpRecGateAlt(BaseElement b)
        : AltParamChoiceLabeled{{{b}, 2, 0}, {"Trigger", "Gate"}} {}
};
// Index 0 = Off so patches saved before this param (loader zero-inits unset
// alt-params) keep every head on the grid.
struct QlpExcludeGridAlt : AltParamChoiceLabeled {
    constexpr QlpExcludeGridAlt(BaseElement b)
        : AltParamChoiceLabeled{{{b}, 2, 0}, {"Off", "On"}} {}
};
struct QlpJackIn : JackInput {
    constexpr QlpJackIn(BaseElement b) : JackInput{{b, "4ms/comp/jack_x.png"}} {}
};
struct QlpJackOut : JackOutput {
    constexpr QlpJackOut(BaseElement b) : JackOutput{{b, "4ms/comp/jack_x.png"}} {}
};
struct QlpDisplay : DynamicGraphicDisplay {
    constexpr QlpDisplay(BaseElement b) : DynamicGraphicDisplay{{{b}}} {}
};

} // namespace MetaModule

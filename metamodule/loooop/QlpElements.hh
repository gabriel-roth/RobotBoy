#pragma once
#include "CoreModules/elements/element_info.hh"
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
struct QlpOverdubAlt : AltParamChoiceLabeled {
    constexpr QlpOverdubAlt(BaseElement b, unsigned defaultValue)
        : AltParamChoiceLabeled{{{b}, 2, defaultValue}, {"Off", "On"}} {}
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
// Choice order pins Add to index 0: the MM patch loader zero-inits unset
// alt-params, so patches saved before this param must load in Add (the
// legacy overdub-sum behavior).
struct QlpWriteModeAlt : AltParamChoiceLabeled {
    constexpr QlpWriteModeAlt(BaseElement b)
        : AltParamChoiceLabeled{{{b}, 4, 0}, {"Add", "Replace", "Layer", "Decay"}} {}
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

#pragma once
#include "plugin.hpp"
#include "dsp/LoopEngine.hpp"
#include "LooperModuleDSP.hpp"

namespace loooop {

// Overdub is one 5-state control shared by Loooop and Löp: four write modes
// + Lock (= overdub off, loop untouchable). While Locked the last write mode
// stays set; the engine ignores it with overdub off. The index->write-mode
// map lives in LooperModuleDSP.hpp (overdubWriteMode) so the MetaModule cores
// share it.

// Overdub state color (Layer/Decay/Add/Replace/Lock), Quality-button style:
// an RGB LED in the bezel, driven from this table in each module's process().
static constexpr float kOverdubColors[5][3] = {
    {0.247f, 0.549f, 1.f},      // Layer   - blue   #3f8cff
    {1.f,    0.624f, 0.039f},   // Decay   - amber  #ff9f0a
    {0.188f, 0.820f, 0.345f},   // Add     - green  #30d158
    {1.f,    0.231f, 0.188f},   // Replace - red    #ff3b30
    {0.749f, 0.353f, 0.949f},   // Lock    - purple #bf5af2
};

inline void applyOverdub(LoopEngine& engine, int od) {
    engine.setOverdub(od != 4);   // 4 = Lock
    if (od >= 0 && od < 4)
        engine.setWriteMode(overdubWriteMode(od));
}

// Drive the Overdub RGB bezel LED from kOverdubColors. rLight is the module's
// OVERDUB_R_LIGHT id; the G and B lights must follow it consecutively (they do
// in both Loooop and Löp). Keeps the two modules' LEDs in lockstep the way
// applyOverdub does for the engine side.
template <typename Lights>
inline void setOverdubLED(Lights& lights, int rLight, int od) {
    lights[rLight + 0].setBrightness(kOverdubColors[od][0]);
    lights[rLight + 1].setBrightness(kOverdubColors[od][1]);
    lights[rLight + 2].setBrightness(kOverdubColors[od][2]);
}

} // namespace loooop

// Five-state overdub button, Quality-button style (see Particules): the
// stock light bezel with an RGB LED, made non-momentary so a click cycles
// the stepped param Layer/Decay/Add/Replace/Lock with wraparound.
struct OverdubButton : VCVLightBezel<RedGreenBlueLight> {
    OverdubButton() {
        momentary = false;
    }
};

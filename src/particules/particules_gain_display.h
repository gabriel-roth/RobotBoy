#pragma once

#include <cmath>
#include <cstdio>
#include <string>

// Format AutoGain::InputLevel() (linear peak, ±5 V → 1.0) for the gain
// section of the context menu. At or below -60 dB (the AutoGain::kMinGainDb
// floor) the level reads as "silent". The negated comparison also routes
// NaN and negative inputs to "silent".
inline std::string FormatInputLevelDb(float linear_level) {
    if (!(linear_level > 0.001f))
        return "silent";
    float db = 20.0f * std::log10(linear_level);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.1f dB", db);
    return buf;
}

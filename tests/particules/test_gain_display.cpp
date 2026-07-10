#include <cassert>
#include <cmath>
#include <cstdio>
#include <string>
#include "../../src/particules/particules_gain_display.h"

int main() {
    assert(FormatInputLevelDb(1.0f)  == "0.0 dB");
    assert(FormatInputLevelDb(0.5f)  == "-6.0 dB");   // 20*log10(0.5) = -6.02
    assert(FormatInputLevelDb(0.1f)  == "-20.0 dB");

    // At/below the -60 dB floor (10^(-60/20) = 0.001) reads as silent.
    assert(FormatInputLevelDb(0.0f)     == "silent");
    assert(FormatInputLevelDb(0.0009f)  == "silent");
    assert(FormatInputLevelDb(0.001f)   == "silent");

    // Garbage in never renders garbage out.
    assert(FormatInputLevelDb(std::nanf(""))   == "silent");
    assert(FormatInputLevelDb(-0.5f)           == "silent");

    std::printf("test_gain_display: all assertions passed\n");
    return 0;
}

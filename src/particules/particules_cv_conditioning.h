#pragma once
#include <cmath>
#include <cstddef>

// CV-conditioner settings for the Particules wrapper.
//
// The wrapper steps each beads::ControlConditioner once per wrapper block —
// every 64 samples, on both VCV and MetaModule. These helpers convert
// per-sample-tuned constants into equivalents for the actual block size so
// both platforms condition CVs on the same timescale.
namespace particules {

// Sample-and-hold decimation in conditioner *steps*: targets one CV sample
// per ~8 audio samples. For block sizes >= 8 every step already spans >= 8
// samples, so no further decimation. Block size is clamped to >= 1 first
// (avoids a divide-by-zero for a hypothetical 0-sized block; every block
// size actually in use is >= 1, so the clamp never changes the result).
constexpr int CvDecimationForBlock(std::size_t block_size) {
    const std::size_t effective = block_size < 1 ? 1 : block_size;
    return effective >= 8 ? 1 : static_cast<int>(8 / effective);
}

// Convert a per-sample one-pole coefficient into the per-block equivalent:
// applying the result once per block matches applying per_sample every
// sample of that block.
inline float CvSmoothingForBlock(float per_sample, std::size_t block_size) {
    // Block size 1 must return per_sample bit-exactly (VCV behavior
    // unchanged); the 1-(1-s)^n round trip is not exact in float.
    if (block_size <= 1) return per_sample;
    return 1.0f - std::pow(1.0f - per_sample, static_cast<float>(block_size));
}

constexpr float kCvSmoothing        = 0.5f;   // time/size/shape, per sample
constexpr float kPitchCvSmoothing   = 0.35f;  // pitch, per sample
constexpr float kMenuCvQuantizeStep = 0.01f;  // volts; de-noises time/size/shape CVs

// Volts. MUST stay 0: any nonzero step quantizes the 1 V/oct pitch input to
// a grid coarser than a semitone (the old 0.05 V step put notes up to
// ±0.3 semitones out of tune).
constexpr float kPitchCvQuantizeStep = 0.0f;

}  // namespace particules

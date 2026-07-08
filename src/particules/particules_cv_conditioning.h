#pragma once
#include <cmath>
#include <cstddef>

// CV-conditioner settings for the Particules wrapper.
//
// The wrapper steps each beads::ControlConditioner once per wrapper block —
// every sample in VCV (block 1), every 64 samples on MetaModule. These
// helpers convert per-sample-tuned constants into equivalents for the actual
// block size so both platforms condition CVs on the same timescale.
namespace particules {

// Sample-and-hold decimation in conditioner *steps*: targets one CV sample
// per ~8 audio samples. For block sizes >= 8 every step already spans >= 8
// samples, so no further decimation.
constexpr int CvDecimationForBlock(std::size_t block_size) {
    return block_size >= 8 ? 1 : static_cast<int>(8 / block_size);
}

// Convert a per-sample one-pole coefficient into the per-block equivalent:
// applying the result once per block matches applying per_sample every
// sample of that block.
inline float CvSmoothingForBlock(float per_sample, std::size_t block_size) {
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

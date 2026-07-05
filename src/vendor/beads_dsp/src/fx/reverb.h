#pragma once

#include "fx_engine.h"
#include "../util/dsp_utils.h"

namespace beads {

// Dattorro plate reverb with proper partitioned delay lines.
//
// Topology:
//   4 input allpass diffusers (pre-delay)
//   2-path cross-coupled feedback tank, each path has:
//     modulated delay → 2 allpass → LP → delay
//   Output tapped from multiple points in both tank paths
//
// Retuned for Beads (vs Clouds):
//   Delay lengths ~15% shorter (smaller room / "Thoreau's cabin" character)
//   Warmer tone: lower LP cutoff in feedback loop
//   Quality mode shifts reverb LP coefficient
class Reverb {
public:
    void Init(float* buffer, size_t buffer_size, float sample_rate);
    void SetAmount(float amount);       // 0-1, overall wet level
    void SetDecay(float decay);         // 0-1, mapped to feedback gain
    void SetDiffusion(float diff);      // 0-1
    void SetLpCutoff(float cutoff);     // Normalized frequency for feedback LP
    void Process(float left_in, float right_in,
                 float* left_out, float* right_out);

    // Minimum buffer size (in floats) required for Init
    static constexpr size_t kMinBufferSize = 12000;

private:
    float sample_rate_ = 48000.0f;
    float amount_ = 0.0f;
    float decay_ = 0.5f;
    float diffusion_ = 0.7f;
    float lp_ = 0.7f;

    // LFO for chorus-like modulation in the tank
    float lfo_phase_ = 0.0f;
    float lfo_increment_ = 0.0f;

    // Feedback state (from previous sample, cross-coupled)
    float feedback_l_ = 0.0f;
    float feedback_r_ = 0.0f;

    // LP filter state in feedback loop
    float lp_state_l_ = 0.0f;
    float lp_state_r_ = 0.0f;

    // DC blocker: slowly-tracking DC estimate subtracted from tank signal.
    // Prevents DC accumulation in the feedback loop at high decay settings.
    float dc_estimate_l_ = 0.0f;
    float dc_estimate_r_ = 0.0f;
    float dc_block_coeff_ = 0.0005f;  // Computed from sample rate in Init()
    static constexpr float kDcBlockTargetHz = 3.8f;

    // -- 12 individual delay lines --

    // Input diffusion allpass delay lines
    DelayLine ap_in_1_;
    DelayLine ap_in_2_;
    DelayLine ap_in_3_;
    DelayLine ap_in_4_;

    // Left tank
    DelayLine delay_l1_;    // Modulated delay
    DelayLine ap_l1_;       // Tank allpass 1
    DelayLine ap_l2_;       // Tank allpass 2
    DelayLine delay_l2_;    // Feedback delay

    // Right tank
    DelayLine delay_r1_;    // Modulated delay
    DelayLine ap_r1_;       // Tank allpass 1
    DelayLine ap_r2_;       // Tank allpass 2
    DelayLine delay_r2_;    // Feedback delay

    // -- Delay lengths (samples, ~15% shorter than Clouds) --
    static constexpr size_t kApIn1Len = 113;
    static constexpr size_t kApIn2Len = 162;
    static constexpr size_t kApIn3Len = 241;
    static constexpr size_t kApIn4Len = 399;

    static constexpr size_t kDelayL1Len = 1559;
    static constexpr size_t kApL1Len = 569;
    static constexpr size_t kApL2Len = 829;
    static constexpr size_t kDelayL2Len = 2656;

    static constexpr size_t kDelayR1Len = 1493;
    static constexpr size_t kApR1Len = 601;
    static constexpr size_t kApR2Len = 887;
    static constexpr size_t kDelayR2Len = 2423;

    // Modulated delay buffers need extra headroom for LFO variation
    static constexpr size_t kModHeadroom = 32;

    // Modulation parameters
    static constexpr float kModDepth = 16.0f;
    static constexpr float kLfoHz = 0.5f;

    // Process one Schroeder allpass: read delayed, compute, write, advance.
    // Returns the allpass output.
    static float ProcessAllpass(DelayLine& dl, float input, float g) {
        float delayed = dl.ReadOldest();
        float w = input - g * delayed;
        dl.Write(w);
        dl.Advance();
        return g * w + delayed;
    }
};

} // namespace beads

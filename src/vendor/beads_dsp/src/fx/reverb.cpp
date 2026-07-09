#include "reverb.h"
#include "../util/cosine_table.h"
#include <cmath>

namespace beads {

void Reverb::Init(float* buffer, size_t buffer_size, float sample_rate) {
    // Guard: the shared buffer must hold all 12 partitioned delay lines
    // (kMinBufferSize is their required total). Previously a too-small
    // buffer left some delay lines pointing at nullptr (see the `alloc`
    // fallback below), which Process() then wrote through unconditionally
    // -- undefined behavior. Disable instead and stay silent-but-safe.
    if (!buffer || buffer_size < kMinBufferSize) {
        enabled_ = false;
        return;
    }
    enabled_ = true;

    sample_rate_ = sample_rate;

    amount_ = 0.0f;
    decay_ = 0.5f;
    diffusion_ = 0.7f;
    lp_ = 0.7f;

    lfo_phase_ = 0.0f;
    lfo_increment_ = (sample_rate_ > 0.0f) ? (kLfoHz / sample_rate_) : 0.0f;

    lp_state_l_ = 0.0f;
    lp_state_r_ = 0.0f;
    dc_estimate_l_ = 0.0f;
    dc_estimate_r_ = 0.0f;
    dc_block_coeff_ = (sample_rate > 0.0f)
        ? (kTwoPi * kDcBlockTargetHz / sample_rate)
        : 0.0005f;
    feedback_l_ = 0.0f;
    feedback_r_ = 0.0f;

    // Partition the shared buffer into 12 individual delay lines.
    // Each delay line gets its own contiguous region.
    float* ptr = buffer;
    size_t used = 0;

    auto alloc = [&](DelayLine& dl, size_t len) {
        if (used + len <= buffer_size) {
            dl.Init(ptr, len);
            ptr += len;
            used += len;
        } else {
            dl.Init(nullptr, 0);
        }
    };

    // Input diffusion allpasses
    alloc(ap_in_1_, kApIn1Len);
    alloc(ap_in_2_, kApIn2Len);
    alloc(ap_in_3_, kApIn3Len);
    alloc(ap_in_4_, kApIn4Len);

    // Left tank
    alloc(delay_l1_, kDelayL1Len + kModHeadroom);
    alloc(ap_l1_, kApL1Len);
    alloc(ap_l2_, kApL2Len);
    alloc(delay_l2_, kDelayL2Len);

    // Right tank
    alloc(delay_r1_, kDelayR1Len + kModHeadroom);
    alloc(ap_r1_, kApR1Len);
    alloc(ap_r2_, kApR2Len);
    alloc(delay_r2_, kDelayR2Len);
}

void Reverb::SetAmount(float amount) {
    amount_ = Clamp(amount, 0.0f, 1.0f);
    // Equal-power dry/wet gains so the sweep holds a constant level instead of
    // dipping ~3 dB through the middle. CosLookup phase 0..0.25 = quarter cycle.
    float phase = amount_ * 0.25f;
    dry_xfade_ = CosLookup(phase);
    wet_xfade_ = CosLookup(phase - 0.25f);   // sin = cos(x - pi/2)
}

void Reverb::SetMakeupGain(float gain) {
    makeup_gain_ = Clamp(gain, 0.0f, 8.0f);
}

void Reverb::SetDecay(float decay) {
    decay_ = Clamp(decay, 0.0f, 1.0f);
}

void Reverb::SetDiffusion(float diff) {
    diffusion_ = Clamp(diff, 0.0f, 1.0f);
}

void Reverb::SetLpCutoff(float cutoff) {
    lp_ = Clamp(cutoff, 0.0f, 1.0f);
}

void Reverb::Process(float left_in, float right_in,
                     float* left_out, float* right_out) {
    if (!enabled_) {
        *left_out = 0.0f;
        *right_out = 0.0f;
        return;
    }

    // Map decay knob (0-1) to feedback gain.
    // Non-linear: slow ramp then steep near 1.0 for long tails.
    float fb = 0.2f + decay_ * 0.75f;
    if (decay_ > 0.9f) {
        fb += (decay_ - 0.9f) * 0.5f;
    }
    fb = Clamp(fb, 0.0f, 0.9995f);

    // LP coefficient for feedback loop (one-pole).
    float lp_coeff = lp_;

    // Diffusion coefficient for allpass stages
    float ap_coeff = diffusion_ * 0.75f;

    // ------------------------------------------------------------------
    // LFO for modulated delay taps (chorus-like effect in tank)
    // ------------------------------------------------------------------
    lfo_phase_ += lfo_increment_;
    if (lfo_phase_ >= 1.0f) lfo_phase_ -= 1.0f;

    float lfo_cos = CosLookup(lfo_phase_);
    float lfo_sin = CosLookup(lfo_phase_ - 0.25f);  // sin = cos(phase - pi/2)
    float mod_l = kModDepth * lfo_sin;
    float mod_r = kModDepth * lfo_cos;

    // ------------------------------------------------------------------
    // Input diffusion: 4 series allpass filters
    // Each has its own delay line, producing proper diffusion.
    // ------------------------------------------------------------------

    // Gate input to zero when bypassed so new audio doesn't accumulate
    // in the tank during bypass. The tank still runs (so it decays
    // naturally via feedback), but no fresh material is added.
    // We don't flush the delay lines on the 0→nonzero transition because
    // that would require tracking prev_amount_ and coordinating with the
    // caller's smoothing to avoid a click — not worth the complexity for
    // this rare edge case.
    float input = (amount_ > 0.0f) ? (left_in + right_in) * 0.5f : 0.0f;

    float diffused = ProcessAllpass(ap_in_1_, input, ap_coeff);
    diffused = ProcessAllpass(ap_in_2_, diffused, ap_coeff);
    diffused = ProcessAllpass(ap_in_3_, diffused, ap_coeff);
    diffused = ProcessAllpass(ap_in_4_, diffused, ap_coeff);

    // ------------------------------------------------------------------
    // Tank: two cross-coupled feedback paths (Dattorro topology)
    //
    // Each path: modulated delay → allpass → allpass → LP → delay
    // Cross-coupling: left feedback feeds right input, and vice versa.
    // feedback_l_ and feedback_r_ carry values from the previous sample.
    // ------------------------------------------------------------------

    // Save previous-sample feedback for cross-coupling.
    // Both paths must use values from the PREVIOUS sample to maintain
    // the symmetric Dattorro figure-8 topology.
    float prev_fb_l = feedback_l_;
    float prev_fb_r = feedback_r_;

    // LEFT PATH --------------------------------------------------------
    float tank_l_in = diffused + fb * prev_fb_r;

    // Modulated delay L1: write input, read with modulation
    delay_l1_.Write(tank_l_in);
    float dl1_out = delay_l1_.ReadInterpolated(
        static_cast<float>(kDelayL1Len) + mod_l);
    delay_l1_.Advance();

    // Two allpass filters in left tank
    float al1_out = ProcessAllpass(ap_l1_, dl1_out, ap_coeff);
    float al2_out = ProcessAllpass(ap_l2_, al1_out, ap_coeff);

    // One-pole LP in feedback path (frequency-dependent decay)
    OnePole(lp_state_l_, al2_out, lp_coeff);

    // DC blocker: subtract slowly-tracked DC estimate to prevent
    // low-frequency buildup at high decay settings.
    OnePole(dc_estimate_l_, lp_state_l_, dc_block_coeff_);
    float tank_l_dc_blocked = lp_state_l_ - dc_estimate_l_;

    // Feedback delay L2
    float dl2_out = delay_l2_.ReadOldest();
    delay_l2_.Write(tank_l_dc_blocked);
    delay_l2_.Advance();

    // Safety: prevent unbounded energy accumulation
    feedback_l_ = SoftClip(dl2_out);

    // RIGHT PATH -------------------------------------------------------
    float tank_r_in = diffused + fb * prev_fb_l;

    // Modulated delay R1
    delay_r1_.Write(tank_r_in);
    float dr1_out = delay_r1_.ReadInterpolated(
        static_cast<float>(kDelayR1Len) + mod_r);
    delay_r1_.Advance();

    // Two allpass filters in right tank
    float ar1_out = ProcessAllpass(ap_r1_, dr1_out, ap_coeff);
    float ar2_out = ProcessAllpass(ap_r2_, ar1_out, ap_coeff);

    // One-pole LP
    OnePole(lp_state_r_, ar2_out, lp_coeff);

    // DC blocker
    OnePole(dc_estimate_r_, lp_state_r_, dc_block_coeff_);
    float tank_r_dc_blocked = lp_state_r_ - dc_estimate_r_;

    // Feedback delay R2
    float dr2_out = delay_r2_.ReadOldest();
    delay_r2_.Write(tank_r_dc_blocked);
    delay_r2_.Advance();

    feedback_r_ = SoftClip(dr2_out);

    // ------------------------------------------------------------------
    // Output: tap from multiple points for decorrelated stereo
    // ------------------------------------------------------------------
    // Makeup gain lifts the wet tail toward the dry level (it is intrinsically
    // quieter: energy spread across the decay tail and low-passed).
    float wet_l = (dl1_out * 0.6f + dr2_out * 0.4f) * makeup_gain_;
    float wet_r = (dr1_out * 0.6f + dl2_out * 0.4f) * makeup_gain_;

    // Equal-power crossfade: full wet still fully replaces the dry (dreamy
    // wash), but the makeup keeps that wash as loud as the grains it replaced.
    *left_out  = left_in  * dry_xfade_ + wet_l * wet_xfade_;
    *right_out = right_in * dry_xfade_ + wet_r * wet_xfade_;
}

} // namespace beads

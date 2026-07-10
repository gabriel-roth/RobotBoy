#include "grain_scheduler.h"
#include "../util/dsp_utils.h"

#include <cmath>
#include <algorithm>

namespace beads {

// Derived once at load time (before audio starts) rather than as a
// function-local magic static on the audio path.
const float GrainScheduler::kRateExponent = std::log2(GrainScheduler::kMaxRateHz / 0.25f);

void GrainScheduler::Init(float sample_rate) {
    sample_rate_ = sample_rate;
    prev_trigger_mode_ = TriggerMode::kLatched;
    latched_phase_ = 0.0f;
    prev_gate_ = false;
    gate_phase_ = 0.0f;
    prev_clock_ = false;
    random_.Init(0xBEAD5EED);
}

float GrainScheduler::DensityToRate(float density) {
    // density 0.5 → 0 Hz (silence)
    // density 0.0 → kMaxRateHz (fast, regular)
    // density 1.0 → kMaxRateHz average (fast, random)
    // Exponential mapping from center distance to rate.
    float distance = std::abs(density - 0.5f) * 2.0f;  // 0..1
    if (distance < 0.001f) return 0.0f;

    return 0.25f * std::exp2(distance * kRateExponent);
}

int GrainScheduler::Process(const BeadsParameters& params, size_t block_size,
                            int* trigger_samples, int max_triggers) {
    int trigger_count = 0;

    if (params.trigger_mode != prev_trigger_mode_) {
        // gate_phase_ is reused across modes for different purposes (a
        // continuous kGated inter-grain phasor, an integer kClocked
        // division counter, a kMidi repeat-rate phasor); prev_gate_ and
        // prev_clock_ are per-mode edge-detect latches. Carrying any of
        // this over a mode switch caused the first clock division after
        // gated->clocked to be wrong. Reset unconditionally on ANY mode
        // change so no transition — not just gated->clocked — can
        // inherit another mode's stale interpretation of these fields.
        latched_phase_ = 0.0f;
        prev_gate_ = false;
        gate_phase_ = 0.0f;
        prev_clock_ = false;
        prev_trigger_mode_ = params.trigger_mode;
    }

    switch (params.trigger_mode) {
    case TriggerMode::kLatched: {
        // Internal phasor mode.
        // eff_density is constant across the block, so rate/phase_inc are too.
        float eff_density = Clamp(params.density + params.density_cv, 0.0f, 1.0f);
        float rate = DensityToRate(eff_density);
        if (rate <= 0.0f) break;

        float phase_inc = rate / sample_rate_;
        bool use_random = (eff_density > 0.5f);

        for (size_t i = 0; i < block_size && trigger_count < max_triggers; ++i) {
            latched_phase_ += phase_inc;

            if (latched_phase_ >= 1.0f) {
                latched_phase_ -= 1.0f;
                trigger_samples[trigger_count++] = static_cast<int>(i);

                if (use_random) {
                    // CW density randomizes the inter-grain interval.
                    float exp_rand = random_.NextExponential();
                    latched_phase_ = Clamp(1.0f - exp_rand, -2.0f, 0.99f);
                }
            }
        }
        break;
    }

    case TriggerMode::kGated: {
        // On gate rising edge: always fire one grain immediately.
        // While gate is held: repeat at density-controlled rate.
        // At noon (rate=0): only the single rising-edge grain plays.
        // Gate low: silence, reset phase for next gate.
        float eff_density = Clamp(params.density + params.density_cv, 0.0f, 1.0f);
        float grain_rate = DensityToRate(eff_density);

        bool gate = params.gate;
        bool rising_edge = gate && !prev_gate_;

        if (rising_edge) {
            if (trigger_count < max_triggers) {
                trigger_samples[trigger_count++] = 0;
            }
            gate_phase_ = 0.0f;
        }

        if (gate) {
            if (grain_rate > 0.0f) {
                float phase_inc = grain_rate / sample_rate_;
                bool use_random = (eff_density > 0.5f);
                bool skip_first = rising_edge;  // don't double-fire on the first phasor wrap
                for (size_t i = 0; i < block_size && trigger_count < max_triggers; ++i) {
                    gate_phase_ += phase_inc;
                    if (gate_phase_ >= 1.0f) {
                        gate_phase_ -= 1.0f;
                        if (skip_first) {
                            skip_first = false;
                        } else {
                            trigger_samples[trigger_count++] = static_cast<int>(i);
                            if (use_random) {
                                float exp_rand = random_.NextExponential();
                                gate_phase_ = Clamp(1.0f - exp_rand, -2.0f, 0.99f);
                            }
                        }
                    }
                }
            }
        } else {
            gate_phase_ = 0.0f;
        }

        prev_gate_ = gate;
        break;
    }

    case TriggerMode::kClocked: {
        bool clock = params.gate;
        bool rising_edge = clock && !prev_clock_;

        if (rising_edge) {
            float eff_density = Clamp(params.density + params.density_cv, 0.0f, 1.0f);
            if (eff_density < 0.5f) {
                // CCW from noon: clock division.
                // Map 0.0 → /16, 0.5 → /1
                float div_amount = (0.5f - eff_density) * 2.0f;  // 0..1
                // Exponential mapping to division ratios: 1, 2, 4, 8, 16
                int division = 1 << static_cast<int>(div_amount * 4.0f);
                division = std::min(division, 16);

                // Use a simple counter to divide.
                // We repurpose gate_phase_ as a clock-division counter.
                // Increment first, then check: the first clock starts at 1
                // and triggers when counter reaches the division value.
                gate_phase_ += 1.0f;
                if (static_cast<int>(gate_phase_) >= division) {
                    gate_phase_ = 0.0f;
                    if (trigger_count < max_triggers) {
                        trigger_samples[trigger_count++] = 0;
                    }
                }
                // Note: for division=1, every clock triggers (1 >= 1).
                // For division=2, every other clock triggers (1 < 2, 2 >= 2).
                // This means the first clock after init (gate_phase_ starts
                // at 0) triggers for division=1 but is delayed by one for
                // division>=2.  This matches typical clock divider behavior
                // where the first output aligns with the Nth input clock.
            } else if (eff_density > 0.5f) {
                // CW from noon: probability trigger.
                // Map 0.5 → 0%, 1.0 → 100%
                float probability = (eff_density - 0.5f) * 2.0f;
                if (random_.NextFloat() < probability) {
                    if (trigger_count < max_triggers) {
                        trigger_samples[trigger_count++] = 0;
                    }
                }
            } else {
                // Exactly 0.5: trigger on every clock.
                if (trigger_count < max_triggers) {
                    trigger_samples[trigger_count++] = 0;
                }
            }
        }

        prev_clock_ = clock;
        break;
    }
    case TriggerMode::kMidi: {
        // MIDI host sets gate/pitch/velocity; density controls repeat rate and burst count.
        float eff_density = Clamp(params.density + params.density_cv, 0.0f, 1.0f);

        bool gate = params.gate;
        bool rising_edge = gate && !prev_gate_;

        if (rising_edge) {
            if (trigger_count < max_triggers) {
                trigger_samples[trigger_count++] = 0;
            }
            gate_phase_ = 0.0f;
        }

        if (gate && params.density != 0.5f) {
            if (eff_density > 0.5f) {
                float repeat_rate = DensityToRate(eff_density);
                if (repeat_rate > 0.0f) {
                    float phase_inc = repeat_rate / sample_rate_;
                    bool skip_first = rising_edge;
                    for (size_t i = 0; i < block_size && trigger_count < max_triggers; ++i) {
                        gate_phase_ += phase_inc;
                        if (gate_phase_ >= 1.0f) {
                            gate_phase_ -= 1.0f;
                            if (skip_first) {
                                skip_first = false;
                            } else {
                                trigger_samples[trigger_count++] = static_cast<int>(i);
                            }
                        }
                    }
                }
            } else if (rising_edge && eff_density < 0.5f) {
                float burst_amount = (0.5f - eff_density) * 2.0f;
                int burst_count = static_cast<int>(burst_amount * 15.0f);
                for (int b = 0; b < burst_count && trigger_count < max_triggers; ++b) {
                    int offset = static_cast<int>(
                        (static_cast<float>(b + 1) / static_cast<float>(burst_count + 1))
                        * static_cast<float>(block_size));
                    offset = std::min(offset, static_cast<int>(block_size) - 1);
                    trigger_samples[trigger_count++] = offset;
                }
            }
        }

        prev_gate_ = gate;
        break;
    }
    } // switch

    grain_triggered_ = (trigger_count > 0);
    return trigger_count;
}

} // namespace beads

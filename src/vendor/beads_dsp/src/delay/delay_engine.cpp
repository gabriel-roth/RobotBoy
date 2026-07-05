#include "delay_engine.h"
#include "../buffer/recording_buffer.h"
#include "../util/cosine_table.h"
#include "../util/dsp_utils.h"
#include "../util/interpolation.h"

#include <cmath>

namespace beads {

// Maps DENSITY knob position to a subdivision ratio of the clock interval.
//
// CCW half (density 0.5→0.0): binary subdivisions only
//   1/1, 1/2, 1/4, 1/8, 1/16
//
// CW half (density 0.5→1.0): wider variety of ratios
//   1/1, 1/2, 1/3, 1/4, 1/6, 1/8, 1/10, 1/12, 1/16
//
// Nearest-neighbor snapping (discrete detents matching hardware).
static float DensityToSubdivision(float density) {
    // CCW table: 5 entries, evenly spaced over [0.5, 0.0]
    static const float kCCW[] = { 1.0f, 0.5f, 0.25f, 0.125f, 0.0625f };
    static const int kCCWCount = 5;

    // CW table: 9 entries, evenly spaced over [0.5, 1.0]
    static const float kCW[] = {
        1.0f,           // 1/1
        0.5f,           // 1/2
        1.0f / 3.0f,    // 1/3
        0.25f,          // 1/4
        1.0f / 6.0f,    // 1/6
        0.125f,         // 1/8
        0.1f,           // 1/10
        1.0f / 12.0f,   // 1/12
        0.0625f         // 1/16
    };
    static const int kCWCount = 9;

    if (density <= 0.5f) {
        float t = (0.5f - density) * 2.0f;  // 0 at noon, 1 at fully CCW
        int idx = static_cast<int>(std::round(t * (kCCWCount - 1)));
        if (idx < 0) idx = 0;
        if (idx >= kCCWCount) idx = kCCWCount - 1;
        return kCCW[idx];
    } else {
        float t = (density - 0.5f) * 2.0f;  // 0 at noon, 1 at fully CW
        int idx = static_cast<int>(std::round(t * (kCWCount - 1)));
        if (idx < 0) idx = 0;
        if (idx >= kCWCount) idx = kCWCount - 1;
        return kCW[idx];
    }
}

void DelayEngine::Init(float sample_rate, RecordingBuffer* buffer) {
    sample_rate_   = sample_rate;
    stable_needed_ = static_cast<int>(kStabilizeTime * sample_rate);
    buffer_ = buffer;

    read_position_ = 0.0f;
    smoothed_envelope_gain_ = 1.0f;
    smoothed_secondary_mix_ = 0.0f;

    head_drift_           = 0.0f;
    xfade_progress_       = 0.0f;
    xfade_drift_at_start_ = 0.0f;
    xfade_length_inv_     = 0.0f;
    pitch_shift_increment_ = 0.0f;

    envelope_phase_ = 0.0f;

    frozen_ = false;
    loop_start_ = 0.0f;
    loop_length_ = 0.0f;

    smoothed_base_delay_ = 0.0f;

    tap_eff_time_[0]   = 0.0f;
    tap_eff_time_[1]   = 0.0f;
    tap_gain_[0]       = 1.0f;
    tap_gain_[1]       = 0.0f;
    active_tap_        = 0;
    xfade_rate_        = 1.0f / (kXFadeTime * sample_rate_);
    xfade_in_progress_ = false;

    time_last_seen_      = 0.0f;
    time_stable_counter_ = 0;
    time_waiting_        = false;
    cold_start_          = true;
}

void DelayEngine::Process(const BeadsParameters& params,
                          StereoFrame* output,
                          size_t num_frames) {
    if (!buffer_ || buffer_->size() == 0) {
        for (size_t i = 0; i < num_frames; ++i) {
            output[i] = {0.0f, 0.0f};
        }
        return;
    }

    const float buffer_size = static_cast<float>(buffer_->size());
    const int df = buffer_->decimation_factor();
    const float df_f = static_cast<float>(df);

    // Apply AR + LFO modulation for all four parameters.
    // ApplyLfoAr encodes the four-way Beads attenurandomizer logic.
    // Clamped to each parameter's valid range.
    const float effective_time = std::max(0.0f, std::min(1.0f,
        ApplyLfoAr(params.time, params.time_ar,
                   params.time_cv, params.time_cv_connected,
                   params.time_lfo)));

    const float effective_size = std::max(-1.0f, std::min(1.0f,
        ApplyLfoAr(params.size, params.size_ar,
                   params.size_cv, params.size_cv_connected,
                   params.size_lfo)));

    const float effective_shape = std::max(0.0f, std::min(1.0f,
        ApplyLfoAr(params.shape, params.shape_ar,
                   params.shape_cv, params.shape_cv_connected,
                   params.shape_lfo)));

    float raw_pitch = ApplyLfoAr(params.pitch, params.pitch_ar,
                                  params.pitch_cv, params.pitch_cv_connected,
                                  params.pitch_lfo);
    const float effective_pitch = (params.pitch_lock != 0)
        ? QuantizePitchLock(raw_pitch, params.pitch_lock)
        : raw_pitch;

    // ---------------------------------------------------------------
    // DENSITY -> base delay time (matches original Beads manual)
    // At noon (0.5): base delay = full buffer duration.
    // CCW from noon (< 0.5): shorter base delay down to ~1ms.
    // CW from noon (> 0.5): shorter base delay + secondary tap.
    // ---------------------------------------------------------------

    // min_delay_samples only changes with quality mode (decimation factor).
    // Cache it; recompute only when df changes.
    if (df != cached_df_) {
        cached_df_ = df;
        cached_min_delay_samples_ = sample_rate_ * 0.001f / df_f;  // ~1ms
    }
    const float min_delay_samples = cached_min_delay_samples_;

    // SIZE -> effective buffer length (non-frozen delay mode only).
    // Maps params.size (-1..1) linearly to min_delay_samples..buffer_size:
    //   size =  1.0 (fully CW)  → full buffer (same as before this feature)
    //   size =  0.0 (noon)      → ~half buffer
    //   size = -1.0 (fully CCW) → min_delay_samples (~1ms)
    // Frozen mode uses effective_size (post-AR) for loop_length, so SIZE AR
    // modulation applies there too.
    // Clocked mode: SIZE also caps the clock-interval delay ceiling (intentional).
    const float size_scale = (effective_size + 1.0f) * 0.5f;  // -1..1 → 0..1
    float effective_buffer_size = min_delay_samples +
        size_scale * std::max(0.0f, buffer_size - min_delay_samples);
    effective_buffer_size = Clamp(effective_buffer_size, min_delay_samples, buffer_size);

    const float max_delay_samples = effective_buffer_size;

    // Map density to base delay time.
    // Clocked mode: DENSITY selects a quantized subdivision of the clock interval.
    // Unclocked mode: base delay from an exponential density→rate curve
    //   rate = 0.25 * 2^(distance * 9.03), delay = sample_rate / (rate * df)
    // This curve is intentionally independent of GrainScheduler::DensityToRate
    // (which is capped at 80 Hz to reduce CPU): the delay engine deliberately
    // keeps its full audio-rate range so DENSITY still reaches flanger/comb
    // delay times. The two mappings are not coupled.
    float base_delay;
    if (params.clock_interval_samples > 0.0f && params.seed_connected) {
        // CLOCKED MODE — snap DENSITY to nearest subdivision ratio
        float subdivision = DensityToSubdivision(params.density);
        float target_base = params.clock_interval_samples * subdivision;
        target_base = Clamp(target_base, min_delay_samples, max_delay_samples);
        // One-pole smooth so subdivision snaps don't create abrupt jumps
        OnePole(clocked_base_delay_, target_base, 0.002f);
        base_delay = clocked_base_delay_;
    } else {
        // UNCLOCKED MODE — grain rate formula for consistent 1V/oct CV response
        clocked_base_delay_ = 0.0f;  // reset so re-entry to clocked starts clean
        float eff_density = Clamp(params.density + params.density_cv, 0.0f, 1.0f);
        float distance = std::fabs(eff_density - 0.5f) * 2.0f;  // 0 at noon, 1 at extremes
        float rate_hz = 0.25f * std::exp2(distance * 9.03f);
        base_delay = Clamp(sample_rate_ / (rate_hz * df_f), min_delay_samples, max_delay_samples);
    }

    // ---------------------------------------------------------------
    // TIME -> delay time multiplier (matches original Beads manual)
    // "Selects the actual delay time, as a multiple of the base delay
    // time set by DENSITY."
    // time=0 (CCW) -> 1x base (most recent, shortest)
    // time=1 (CW)  -> max multiple (oldest, longest)
    // ---------------------------------------------------------------
    float max_multiplier = max_delay_samples / std::max(base_delay, 1.0f);
    max_multiplier = std::max(max_multiplier, 1.0f);

    // ---------------------------------------------------------------
    // TIME crossfade trigger.
    // Approach A (knob only, no CV/AR): stabilization timer borrowed from Veno-Echo.
    //   Delay stays frozen while the knob moves; 25ms after it stops, a crossfade fires.
    // Approach B (CV or AR active): threshold-triggered. Crossfade fires immediately
    //   when effective_time diverges from the active tap by > kCVChangeThreshold.
    //   Active tap is frozen between crossfades — no pitch artifacts.
    // Cold start: snap directly to the current TIME value on the very first call,
    //   bypassing the stabilization timer to avoid a ~25ms wrong-time artifact.
    // ---------------------------------------------------------------
    if (cold_start_) {
        tap_eff_time_[active_tap_] = effective_time;
        time_last_seen_             = effective_time;
        cold_start_                 = false;
    }
    {
        bool knob_only = !params.time_cv_connected
                      && std::fabs(params.time_ar) < 0.001f;

        if (!xfade_in_progress_ && !frozen_) {
            if (knob_only) {
                if (std::fabs(effective_time - time_last_seen_) > kStabilizeThreshold) {
                    time_last_seen_      = effective_time;
                    time_stable_counter_ = 0;
                    time_waiting_        = true;
                } else if (time_waiting_) {
                    time_stable_counter_ += static_cast<int>(num_frames);
                    if (time_stable_counter_ >= stable_needed_) {
                        int inactive            = 1 - active_tap_;
                        tap_eff_time_[inactive] = effective_time;
                        tap_gain_[inactive]     = 0.0f;
                        active_tap_             = inactive;
                        xfade_in_progress_      = true;
                        time_waiting_           = false;
                    }
                }
            } else {
                if (std::fabs(effective_time - tap_eff_time_[active_tap_]) > kCVChangeThreshold) {
                    int inactive            = 1 - active_tap_;
                    tap_eff_time_[inactive] = effective_time;
                    tap_gain_[inactive]     = 0.0f;
                    active_tap_             = inactive;
                    xfade_in_progress_      = true;
                }
            }
        }
    }

    // ---------------------------------------------------------------
    // Secondary tap: golden ratio of primary, fades in when density > 0.5
    // (manual: density CW from noon adds "an additional, unevenly spaced, tap")
    // ---------------------------------------------------------------
    float secondary_mix_target = 0.0f;
    if (params.density > 0.5f) {
        secondary_mix_target = (params.density - 0.5f) * 2.0f;  // 0 at 0.5, 1 at 1.0
    }

    // ---------------------------------------------------------------
    // PITCH -> pitch shifter ratio
    // ---------------------------------------------------------------
    // pitch_ratio: buffer-frame advance per input sample for frozen-mode
    // read_position_.  Dividing by df_f converts from "real-time pitch ratio"
    // to "buffer-frame units per input sample", since the buffer records at
    // 1/df the input rate.
    //
    // pitch_shift_increment_: the rotary-head phase advances (pitch_ratio - 1)
    // extra buffer frames per input sample relative to primary_pos, which
    // itself moves at 1/df per input sample.  So the desired increment is:
    //   1 + (target_ratio - 1) / df
    // This ensures the pitch ratio is correct regardless of quality mode.
    // effective_pitch is computed above via ApplyLfoAr.
    float target_ratio = SemitonesToRatio(effective_pitch) * pitch_mod_ratio_;
    float pitch_ratio = target_ratio / df_f;
    pitch_shift_increment_ = 1.0f + (target_ratio - 1.0f) / df_f;

    // ---------------------------------------------------------------
    // SHAPE -> tremolo / slicer envelope
    // 0.0 = no modulation, 0.5 = sine tremolo, 1.0 = hard slicer
    // Rate derived from delay time for tempo-synced feel
    // ---------------------------------------------------------------
    float envelope_rate = 0.0f;
    {
        float active_delay = smoothed_base_delay_
            * std::exp2f(tap_eff_time_[active_tap_] * std::log2f(max_multiplier));
        active_delay = Clamp(active_delay, 1.0f, max_delay_samples);
        if (active_delay > 0.0f)
            envelope_rate = 1.0f / (active_delay * df_f);
    }

    // ---------------------------------------------------------------
    // FREEZE handling
    // ---------------------------------------------------------------
    if (params.freeze && !frozen_) {
        // Entering freeze: capture loop slice
        frozen_ = true;
        loop_start_ = static_cast<float>(buffer_->write_head());
        // SIZE selects loop duration (scaled from minimum to buffer length)
        float min_loop = sample_rate_ * 0.01f / df_f;  // 10ms minimum loop in buffer frames
        loop_length_ = min_loop + effective_size * std::max(0.0f, buffer_size - min_loop);
        loop_length_ = Clamp(loop_length_, min_loop, buffer_size);

        // Set read_position_ to match where we were reading before freeze,
        // so there's no position discontinuity at the freeze boundary.
        // Before freeze: primary_pos = write_pos - delay.
        // After freeze: primary_pos = loop_start_ + read_position_.
        // For continuity: read_position_ = -(active tap delay) (mod loop_length_).
        float entry_delay = smoothed_base_delay_
            * std::exp2f(tap_eff_time_[active_tap_] * std::log2f(max_multiplier));
        entry_delay = Clamp(entry_delay, 1.0f, max_delay_samples);
        if (loop_length_ > 0.0f) {
            float d = std::fmod(entry_delay, loop_length_);
            read_position_ = (d > 0.0f) ? (loop_length_ - d) : 0.0f;
        } else {
            read_position_ = 0.0f;
        }

        // Reset pitch shifter phases to avoid timbral discontinuity
        // from stale phase values in the new context.
        head_drift_     = 0.0f;
        xfade_progress_ = 0.0f;

        // Any in-progress TIME crossfade must resolve immediately on freeze entry —
        // frozen mode gates new crossfades but the fading taps must settle.
        if (xfade_in_progress_) {
            tap_gain_[active_tap_]     = 1.0f;
            tap_gain_[1 - active_tap_] = 0.0f;
            xfade_in_progress_         = false;
        }
        time_waiting_ = false;
    } else if (!params.freeze && frozen_) {
        // Exiting freeze: reset pitch shifter phases
        frozen_ = false;
        head_drift_     = 0.0f;
        xfade_progress_ = 0.0f;
    }

    // If frozen, TIME selects which portion of the buffer to loop
    if (frozen_) {
        float min_loop = sample_rate_ * 0.01f / df_f;
        loop_length_ = min_loop + effective_size * std::max(0.0f, buffer_size - min_loop);
        loop_length_ = Clamp(loop_length_, min_loop, buffer_size);
        float scan_range = std::max(0.0f, buffer_size - loop_length_);
        loop_start_ = effective_time * scan_range;
    }

    // ---------------------------------------------------------------
    // Per-sample processing
    // ---------------------------------------------------------------
    delay_trigger_ = false;
    for (size_t i = 0; i < num_frames; ++i) {
        // Advance TIME crossfade gain ramp (constant-power: output = sqrt(tap_gain_[t]))
        if (xfade_in_progress_) {
            tap_gain_[active_tap_] += xfade_rate_;
            if (tap_gain_[active_tap_] >= 1.0f) {
                tap_gain_[active_tap_]     = 1.0f;
                tap_gain_[1 - active_tap_] = 0.0f;
                xfade_in_progress_         = false;
            } else {
                tap_gain_[1 - active_tap_] = 1.0f - tap_gain_[active_tap_];
            }
        }

        // DENSITY-only smoothing: OnePole on base_delay gives tape-like pitch on DENSITY changes.
        // TIME contribution is locked per-tap in tap_eff_time_[] — never smoothed.
        OnePole(smoothed_base_delay_, base_delay, 0.001f);
        OnePole(smoothed_secondary_mix_, secondary_mix_target, 0.002f);

        // Active tap's delay — used for trigger phase and pitch max_drift.
        float current_delay = smoothed_base_delay_
            * std::exp2f(tap_eff_time_[active_tap_] * std::log2f(max_multiplier));
        current_delay = Clamp(current_delay, 1.0f, max_delay_samples);

        float secondary_mix = smoothed_secondary_mix_;

        // Accumulate trigger phase: one full period = active-tap delay in input samples.
        if (current_delay > 0.0f) {
            trigger_phase_ += 1.0f / (current_delay * df_f);
            if (trigger_phase_ >= 1.0f) {
                trigger_phase_ -= 1.0f;
                delay_trigger_ = true;
            }
        }

        // ---------------------------------------------------------------
        // Advance pitch shift state once per sample.
        // Applied identically to all TIME taps in the loop below.
        // ---------------------------------------------------------------
        float drift_per_sample = pitch_shift_increment_ - 1.0f;

        if (drift_per_sample == 0.0f) {
            // No pitch shift — reset head state so next non-zero engagement starts clean.
            head_drift_     = 0.0f;
            xfade_progress_ = 0.0f;
        } else {
            // Adaptive max_drift: 70% of active delay, hard-capped.
            float max_drift = std::min(kMaxHeadDrift, current_delay * 0.7f);
            max_drift = std::max(max_drift, 64.0f);

            head_drift_ += drift_per_sample;

            // Trigger pitch crossfade when drift exceeds threshold.
            if (std::fabs(head_drift_) >= max_drift && xfade_progress_ <= 0.0f) {
                xfade_drift_at_start_ = head_drift_;
                xfade_progress_       = 1.0f;
                float xfade_length    = std::max(64.0f, max_drift * 0.5f);
                xfade_length_inv_     = 1.0f / xfade_length;
            }
        }

        // ---------------------------------------------------------------
        // Two-tap read: blend TIME taps with constant-power gains.
        // In frozen mode only the active tap is non-zero (crossfades are
        // blocked in frozen mode), so the loop runs exactly once.
        // ---------------------------------------------------------------
        StereoFrame pitched_sample  = {0.0f, 0.0f};
        StereoFrame secondary_accum = {0.0f, 0.0f};
        float loop_xfade_gain = 1.0f;

        const float write_pos = static_cast<float>(buffer_->write_head());

        for (int t = 0; t < 2; ++t) {
            if (tap_gain_[t] < 1e-6f) continue;

            // This tap's delay = smoothed DENSITY base × locked TIME multiplier.
            float tap_delay = smoothed_base_delay_
                * std::exp2f(tap_eff_time_[t] * std::log2f(max_multiplier));
            tap_delay = Clamp(tap_delay, 1.0f, max_delay_samples);

            // ---- Primary read position ----
            float primary_pos;
            if (frozen_) {
                // Frozen mode: position driven by read_position_ + pitch_ratio,
                // independent of tap_delay. Only one tap runs here (active tap),
                // so read_position_ advances exactly once per sample.
                read_position_ += pitch_ratio;
                if (loop_length_ > 0.0f) {
                    while (read_position_ >= loop_length_) read_position_ -= loop_length_;
                    while (read_position_ < 0.0f)          read_position_ += loop_length_;
                } else {
                    read_position_ = 0.0f;
                }

                float xfade_len = static_cast<float>(kLoopXfadeSamples);
                if (xfade_len > loop_length_ * 0.25f)
                    xfade_len = loop_length_ * 0.25f;
                if (xfade_len >= 1.0f && read_position_ < xfade_len)
                    loop_xfade_gain = read_position_ / xfade_len;

                primary_pos = loop_start_ + read_position_;
                if (buffer_size > 0.0f) {
                    while (primary_pos >= buffer_size) primary_pos -= buffer_size;
                    while (primary_pos < 0.0f)         primary_pos += buffer_size;
                }
            } else {
                primary_pos = write_pos - tap_delay;
                if (buffer_size > 0.0f) {
                    while (primary_pos >= buffer_size) primary_pos -= buffer_size;
                    while (primary_pos < 0.0f)         primary_pos += buffer_size;
                }
            }

            // ---- Apply pitch shift state to this tap ----
            float left, right;
            if (drift_per_sample == 0.0f) {
                buffer_->ReadHermiteStereoFast(primary_pos, &left, &right);
            } else {
                float head_pos = primary_pos + head_drift_;
                if (buffer_size > 0.0f) {
                    while (head_pos >= buffer_size) head_pos -= buffer_size;
                    while (head_pos < 0.0f)        head_pos += buffer_size;
                }
                buffer_->ReadHermiteStereoFast(head_pos, &left, &right);

                if (xfade_progress_ > 0.0f) {
                    float new_drift = head_drift_ - xfade_drift_at_start_;
                    float new_pos   = primary_pos + new_drift;
                    if (buffer_size > 0.0f) {
                        while (new_pos >= buffer_size) new_pos -= buffer_size;
                        while (new_pos < 0.0f)        new_pos += buffer_size;
                    }
                    float nl, nr;
                    buffer_->ReadHermiteStereoFast(new_pos, &nl, &nr);
                    float og = xfade_progress_;
                    float ng = 1.0f - xfade_progress_;
                    left  = left  * og + nl * ng;
                    right = right * og + nr * ng;
                }
            }

            // Accumulate with constant-power gain
            float g = std::sqrt(tap_gain_[t]);
            pitched_sample.l += left  * g;
            pitched_sample.r += right * g;

            // ---- Secondary tap for this TIME tap ----
            if (secondary_mix > 0.0f) {
                float secondary_delay = tap_delay * secondary_tap_ratio_;
                if (secondary_delay > effective_buffer_size)
                    secondary_delay = effective_buffer_size;

                float secondary_pos;
                if (frozen_) {
                    float sec_offset = secondary_delay;
                    if (loop_length_ > 0.0f) {
                        sec_offset = std::fmod(sec_offset, loop_length_);
                        if (sec_offset < 0.0f) sec_offset += loop_length_;
                    }
                    secondary_pos = loop_start_ + sec_offset;
                    if (buffer_size > 0.0f) {
                        while (secondary_pos >= buffer_size) secondary_pos -= buffer_size;
                        while (secondary_pos < 0.0f)         secondary_pos += buffer_size;
                    }
                } else {
                    secondary_pos = write_pos - secondary_delay;
                    if (buffer_size > 0.0f) {
                        while (secondary_pos >= buffer_size) secondary_pos -= buffer_size;
                        while (secondary_pos < 0.0f)         secondary_pos += buffer_size;
                    }
                }

                float sec_l = 0.0f, sec_r = 0.0f;
                if (drift_per_sample == 0.0f) {
                    buffer_->ReadHermiteStereoFast(secondary_pos, &sec_l, &sec_r);
                } else {
                    float sec_pos = secondary_pos + head_drift_;
                    if (buffer_size > 0.0f) {
                        while (sec_pos >= buffer_size) sec_pos -= buffer_size;
                        while (sec_pos < 0.0f)        sec_pos += buffer_size;
                    }
                    buffer_->ReadHermiteStereoFast(sec_pos, &sec_l, &sec_r);

                    if (xfade_progress_ > 0.0f) {
                        float sec_new_pos = secondary_pos + (head_drift_ - xfade_drift_at_start_);
                        if (buffer_size > 0.0f) {
                            while (sec_new_pos >= buffer_size) sec_new_pos -= buffer_size;
                            while (sec_new_pos < 0.0f)        sec_new_pos += buffer_size;
                        }
                        float sl, sr;
                        buffer_->ReadHermiteStereoFast(sec_new_pos, &sl, &sr);
                        sec_l = sec_l * xfade_progress_ + sl * (1.0f - xfade_progress_);
                        sec_r = sec_r * xfade_progress_ + sr * (1.0f - xfade_progress_);
                    }
                }

                secondary_accum.l += sec_l * g;
                secondary_accum.r += sec_r * g;
            }
        } // end two-tap loop

        // ---------------------------------------------------------------
        // Complete pitch xfade (advance once per sample, after all reads)
        // ---------------------------------------------------------------
        if (drift_per_sample != 0.0f && xfade_progress_ > 0.0f) {
            xfade_progress_ -= xfade_length_inv_;
            if (xfade_progress_ <= 0.0f) {
                head_drift_    -= xfade_drift_at_start_;
                xfade_progress_ = 0.0f;
            }
        }

        // ---------------------------------------------------------------
        // Loop boundary crossfade (frozen mode only)
        // When near the loop start, blend with audio from the loop end
        // to create a seamless loop without clicks.
        // ---------------------------------------------------------------
        if (frozen_ && loop_xfade_gain < 1.0f) {
            // Read from near the end of the loop (mirror position)
            float xfade_len = static_cast<float>(kLoopXfadeSamples);
            if (xfade_len > loop_length_ * 0.25f) {
                xfade_len = loop_length_ * 0.25f;
            }
            float mirror_pos_in_loop = loop_length_ - xfade_len + read_position_;
            float mirror_pos = loop_start_ + mirror_pos_in_loop;
            if (buffer_size > 0.0f) {
                while (mirror_pos >= buffer_size) mirror_pos -= buffer_size;
                while (mirror_pos < 0.0f) mirror_pos += buffer_size;
            }
            float mirror_l, mirror_r;
            buffer_->ReadHermiteStereoFast(mirror_pos, &mirror_l, &mirror_r);
            // Blend: at read_position_=0, 100% mirror; at xfade boundary, 100% primary
            pitched_sample.l = pitched_sample.l * loop_xfade_gain + mirror_l * (1.0f - loop_xfade_gain);
            pitched_sample.r = pitched_sample.r * loop_xfade_gain + mirror_r * (1.0f - loop_xfade_gain);
        }

        // ---------------------------------------------------------------
        // Secondary tap mixing (reads accumulated in two-tap loop above)
        // ---------------------------------------------------------------
        StereoFrame mixed_sample = pitched_sample;

        if (secondary_mix > 0.0f) {
            float primary_gain   = 1.0f - secondary_mix * 0.5f;
            float secondary_gain = secondary_mix * 0.5f;
            mixed_sample.l = pitched_sample.l * primary_gain + secondary_accum.l * secondary_gain;
            mixed_sample.r = pitched_sample.r * primary_gain + secondary_accum.r * secondary_gain;
        }

        // ---------------------------------------------------------------
        // SHAPE -> tremolo / slicer amplitude envelope
        // ---------------------------------------------------------------
        float env_gain = 1.0f;
        if (effective_shape > 0.001f) {
            // Advance envelope phase
            envelope_phase_ += envelope_rate;
            if (envelope_phase_ >= 1.0f) {
                envelope_phase_ -= 1.0f;
            }

            // Generate envelope waveform based on shape
            // sin(x) = cos(x - pi/2), i.e. phase shifted by 0.25 in [0,1] range
            float sine_env = 0.5f + 0.5f * CosLookup(envelope_phase_ - 0.25f);

            if (effective_shape <= 0.5f) {
                // 0.0 -> 0.5: blend from steady (1.0) to sine tremolo
                float tremolo_depth = effective_shape * 2.0f;  // 0 at shape=0, 1 at shape=0.5
                env_gain = 1.0f - tremolo_depth * (1.0f - sine_env);
            } else {
                // 0.5 -> 1.0: morph from sine tremolo to hard slicer (square gating)
                // Instead of a hard square, use a raised-cosine pulse that
                // sharpens as slicer_blend increases.  This avoids the
                // instant 0->1 and 1->0 transitions of a true square wave
                // which produce audible clicks.
                float slicer_blend = (effective_shape - 0.5f) * 2.0f;  // 0 at 0.5, 1 at 1.0

                // Steepness increases from 1 (sine) to 8 (near-square with
                // smooth edges).  The raised-cosine pulse: take the sine
                // envelope and apply a power curve to steepen it.
                float steepness = 1.0f + slicer_blend * 7.0f;
                float steep_env = FastPowUnit(sine_env, steepness);
                // Normalize so the peak stays at 1.0
                // (pow(1.0, n) == 1.0, pow(0.0, n) == 0.0, so extremes are fine)
                env_gain = steep_env;
            }
        }

        // Smooth the envelope gain to catch any remaining abrupt transitions
        // (e.g. when shape parameter itself changes suddenly)
        OnePole(smoothed_envelope_gain_, env_gain, 0.05f);

        mixed_sample.l *= smoothed_envelope_gain_;
        mixed_sample.r *= smoothed_envelope_gain_;

        output[i] = mixed_sample;
    }
}

} // namespace beads

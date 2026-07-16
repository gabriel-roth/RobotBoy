#include "base_time.h"
#include <algorithm>
#include <cmath>

namespace beadsdelay_dsp {

namespace {

// CCW (density < noon): 4 equal zones over knob [0, 0.5), nearest-noon first.
constexpr float kCcwSubdivisions[4] = {0.5f, 0.25f, 0.125f, 0.0625f};
constexpr float kCcwZoneWidth = 0.125f;  // 0.5 / 4

// CW (density > noon): 8 equal zones over knob (0.5, 1.0], nearest-noon first.
constexpr float kCwSubdivisions[8] = {
    0.5f, 1.f / 3.f, 0.25f, 1.f / 6.f, 0.125f, 0.1f, 1.f / 12.f, 1.f / 16.f};
constexpr float kCwZoneWidth = 0.0625f;  // 0.5 / 8

constexpr float kNoonEpsilon = 1e-4f;    // knob==0.5 (within float slop) → 1/1
constexpr float kHysteresis = 0.02f;     // knob units, per brief

// Clocked-mode TIME multiplier snaps to the nearest of these.
constexpr float kMultiplierTable[8] = {1.f, 2.f, 3.f, 4.f, 6.f, 8.f, 12.f, 16.f};

float SnapMultiplier(float m) {
    float best = kMultiplierTable[0];
    float best_dist = std::fabs(m - best);
    for (float v : kMultiplierTable) {
        float dist = std::fabs(m - v);
        if (dist < best_dist) {
            best_dist = dist;
            best = v;
        }
    }
    return best;
}

// Continuous manual-mode multiplier: 16^knob, i.e. 1..16 over [0, 1].
float ManualMultiplier(float time_knob) {
    return std::exp2(4.f * time_knob);  // log2(16) == 4
}

// Zone-index encodings stored in subdivision_zone_ for hysteresis persistence.
// -1 = unset, 0 = noon (1/1), 1..4 = CCW zone+1, 11..18 = CW zone+11.
constexpr int kNoonZoneCode = 0;
constexpr int kCcwZoneBase = 1;
constexpr int kCwZoneBase = 11;

}  // namespace

void BaseTimeControl::Init(float sample_rate, float buffer_seconds) {
    sample_rate_ = (sample_rate > 0.f) ? sample_rate : 48000.f;
    float requested = sample_rate_ * buffer_seconds;
    buffer_samples_ = std::clamp(requested, 1.f, static_cast<float>(kBufferFrames));

    samples_since_tick_ = 0.f;
    clock_interval_ = 0.f;
    clocked_ = false;
    subdivision_zone_ = -1;
    last_base_samples_ = 0.f;
    has_tick_ = false;
    density_at_last_tick_ = 0.5f;
}

void BaseTimeControl::UpdateClockTiming(bool clock_connected, int clock_tick_offset,
                                          size_t block_frames, float density_knob) {
    bool tick = clock_tick_offset >= 0;

    if (tick) {
        // Clamp a stray offset into this block's valid range (defensive; not
        // expected from a well-behaved caller).
        float offset = std::clamp(static_cast<float>(clock_tick_offset), 0.f,
                                   static_cast<float>(block_frames));
        float elapsed = samples_since_tick_ + offset;

        if (has_tick_) {
            constexpr float kMinInterval = 32.f;               // debounce floor
            float max_interval = 10.f * sample_rate_;          // 10 s ceiling
            if (elapsed >= kMinInterval && elapsed <= max_interval) {
                if (clock_interval_ <= 0.f) {
                    clock_interval_ = elapsed;  // first known interval
                } else {
                    float ratio = elapsed / clock_interval_;
                    if (ratio <= 4.f && ratio >= 0.25f) {
                        clock_interval_ += 0.5f * (elapsed - clock_interval_);
                    } else {
                        clock_interval_ = elapsed;  // outlier: hard reset
                    }
                }
                clocked_ = true;
            }
            // else: implausible interval, ignore for timing purposes but the
            // tick still resets the timeout clock below.
        }

        has_tick_ = true;
        density_at_last_tick_ = density_knob;
        // Residual samples from this tick to the end of the current block;
        // this doubles as the "time since last tick" used for the 5 s
        // clock-timeout / tap-tempo-abandon check below.
        samples_since_tick_ = static_cast<float>(block_frames) - offset;
    } else {
        samples_since_tick_ += static_cast<float>(block_frames);
    }

    if (!clock_connected && clocked_) {
        bool timed_out = samples_since_tick_ > 5.f * sample_rate_;
        bool density_moved = std::fabs(density_knob - density_at_last_tick_) > 0.05f;
        if (timed_out || density_moved) {
            clocked_ = false;
            clock_interval_ = 0.f;
            has_tick_ = false;
            subdivision_zone_ = -1;
        }
    }
}

float BaseTimeControl::ResolveSubdivision(float density_knob) {
    float d = density_knob - 0.5f;

    if (std::fabs(d) < kNoonEpsilon) {
        subdivision_zone_ = kNoonZoneCode;
        return 1.0f;  // noon = 1/1
    }

    if (d < 0.f) {
        float mag = -d;
        int raw = static_cast<int>(mag / kCcwZoneWidth);
        raw = std::clamp(raw, 0, 3);

        int zone = raw;
        if (subdivision_zone_ >= kCcwZoneBase && subdivision_zone_ <= kCcwZoneBase + 3) {
            int prev = subdivision_zone_ - kCcwZoneBase;
            float lo = prev * kCcwZoneWidth;
            float hi = (prev + 1) * kCcwZoneWidth;
            if (mag >= lo - kHysteresis && mag < hi + kHysteresis) {
                zone = prev;
            }
        }
        subdivision_zone_ = kCcwZoneBase + zone;
        return kCcwSubdivisions[zone];
    }

    float mag = d;
    int raw = static_cast<int>(mag / kCwZoneWidth);
    raw = std::clamp(raw, 0, 7);

    int zone = raw;
    if (subdivision_zone_ >= kCwZoneBase && subdivision_zone_ <= kCwZoneBase + 7) {
        int prev = subdivision_zone_ - kCwZoneBase;
        float lo = prev * kCwZoneWidth;
        float hi = (prev + 1) * kCwZoneWidth;
        if (mag >= lo - kHysteresis && mag < hi + kHysteresis) {
            zone = prev;
        }
    }
    subdivision_zone_ = kCwZoneBase + zone;
    return kCwSubdivisions[zone];
}

float BaseTimeControl::ApplyDensityCvZoneShift(float subdivision, float density_cv_volts) const {
    // Cheap, documented approximation: DENSITY CV shifts the zone index by
    // whole zones within whichever table the knob currently sits in. At
    // noon there is no zone family to shift within, so CV is a no-op there.
    int shift = static_cast<int>(density_cv_volts);
    if (shift == 0) return subdivision;

    if (subdivision_zone_ >= kCcwZoneBase && subdivision_zone_ <= kCcwZoneBase + 3) {
        int zone = subdivision_zone_ - kCcwZoneBase;
        zone = std::clamp(zone + shift, 0, 3);
        return kCcwSubdivisions[zone];
    }
    if (subdivision_zone_ >= kCwZoneBase && subdivision_zone_ <= kCwZoneBase + 7) {
        int zone = subdivision_zone_ - kCwZoneBase;
        zone = std::clamp(zone + shift, 0, 7);
        return kCwSubdivisions[zone];
    }
    return subdivision;
}

BaseTimeControl::Result BaseTimeControl::Update(float density_knob, float density_cv_volts,
                                                  float time_knob, bool clock_connected,
                                                  int clock_tick_offset, size_t block_frames) {
    UpdateClockTiming(clock_connected, clock_tick_offset, block_frames, density_knob);

    Result r{};
    r.clocked = clocked_;
    r.multi_tap = !clocked_ && density_knob > 0.55f;  // manual mode only, small dead zone above noon

    float base;
    float multiplier;

    if (clocked_) {
        float subdivision = ResolveSubdivision(density_knob);
        subdivision = ApplyDensityCvZoneShift(subdivision, density_cv_volts);
        float interval = (clock_interval_ > 0.f) ? clock_interval_ : buffer_samples_;
        base = interval * subdivision;
        multiplier = SnapMultiplier(ManualMultiplier(time_knob));
    } else {
        float d = std::clamp(std::fabs(density_knob - 0.5f) * 2.f, 0.f, 1.f);
        base = buffer_samples_ * std::exp2(-kManualOctaves * d);
        base *= std::exp2(-density_cv_volts);
        multiplier = ManualMultiplier(time_knob);
    }

    float min_samples = kMinDelaySeconds * sample_rate_;
    base = std::clamp(base, min_samples, buffer_samples_);

    r.base_samples = base;
    r.multiplier = multiplier;
    r.delay_samples = std::clamp(base * multiplier, min_samples, buffer_samples_);
    // Floor with epsilon: exp2()-derived bases carry float noise (e.g. 7.9999993
    // → 8), which we absorb with epsilon. Ensures count×base ≤ buffer always,
    // so the last slice won't extend past the buffer end for freeze consumers.
    r.slice_count = std::max(1, static_cast<int>(buffer_samples_ / base + 1e-3f));
    r.slice_index = static_cast<int>(std::lround(time_knob * static_cast<float>(r.slice_count - 1)));

    last_base_samples_ = base;
    return r;
}

void BaseTimeControl::SetBufferSeconds(float buffer_seconds) {
    // Mirrors Init()'s "sample_rate_ * buffer_seconds" conversion, but skips
    // Init's kBufferFrames upper clamp. That clamp encodes the decimation=1
    // capacity (the buffer literally holds kBufferFrames host samples when
    // every Write() advances the write head). At decimation > 1 the true
    // host-sample capacity is kBufferFrames * decimation (each buffer frame
    // now spans `decimation` host samples), which the caller computes and
    // passes in directly — reapplying the decimation=1 cap here would
    // silently discard the extra buffer time a quality-mode switch is meant
    // to unlock, defeating the DENSITY-scales-with-buffer-size behavior.
    float requested = sample_rate_ * buffer_seconds;
    buffer_samples_ = std::max(requested, 1.f);
}

float BaseTimeControl::BaseSeconds() const {
    return last_base_samples_ / sample_rate_;
}

bool BaseTimeControl::IsClocked() const {
    return clocked_;
}

}  // namespace beadsdelay_dsp

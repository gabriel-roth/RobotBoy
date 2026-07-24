#pragma once
#include "../../include/retours_delay_dsp/types.h"

namespace retours_delay_dsp {

// Computes base delay time and the tap-1 target delay, in HOST samples.
// Call Update() once per block. Conversion to buffer frames (decimation)
// happens in EchoEngine, not here.
class BaseTimeControl {
public:
    void Init(float sample_rate, float buffer_seconds);

    // Update the buffer's representable duration (in HOST samples) without
    // resetting clock/tap-tempo state. Call on a quality-mode change: the
    // effective buffer duration grows with the new decimation factor
    // (kBufferFrames buffer-frames × decimation host-samples-per-frame),
    // and DENSITY's manual-mode mapping (base = buffer_samples_ × ...) must
    // see that larger duration to produce a proportionally longer delay for
    // the same knob position.
    void SetBufferSeconds(float buffer_seconds);

    struct Result {
        float base_samples;      // base delay time
        float delay_samples;     // base × TIME multiplier, clamped to buffer
        float multiplier;        // resolved TIME multiplier
        bool  clocked;
        bool  multi_tap;         // manual mode: DENSITY on CW side above noon
        int   slice_count;       // buffer_samples / base (≥1), freeze use
        int   slice_index;       // TIME as slice selector, freeze use
    };

    // block_frames: host frames in this block (for clock timeout bookkeeping)
    // clock_tick_offset: rising-edge offset within block, -1 if none
    Result Update(float density_knob, float density_cv_volts,
                  float time_knob, bool clock_connected,
                  int clock_tick_offset, size_t block_frames);

    float BaseSeconds() const;
    bool  IsClocked() const;

    // Measured clock beat (tick-to-tick interval) in seconds; 0 when not
    // clocked or no interval known yet. Drives the Clock light's beat-rate,
    // clock-anchored blink (see Retours.cpp) — distinct from BaseSeconds(),
    // which carries the subdivided base Interval.
    float ClockIntervalSeconds() const;

    // Abandon any measured tempo (tap or cable) and return to free-running.
    // Backs the "Clear tapped tempo" menu item. Since tempos now hold
    // indefinitely (no timeout, no Interval-move exit), this is the only path
    // back to free-running short of establishing a new tempo.
    void  ClearClock();

private:
    float sample_rate_ = 48000.f;
    float buffer_samples_ = 192000.f;
    // clock state
    float samples_since_tick_ = 0.f;
    float clock_interval_ = 0.f;       // 0 = no interval known
    bool  clocked_ = false;
    int   subdivision_zone_ = -1;      // hysteresis state
    float last_base_samples_ = 0.f;

    bool  has_tick_ = false;            // false until the first tick ever seen

    void  UpdateClockTiming(int clock_tick_offset, size_t block_frames);
    float ResolveSubdivision(float density_knob);
    float ApplyDensityCvZoneShift(float subdivision, float density_cv_volts) const;
};

} // namespace retours_delay_dsp

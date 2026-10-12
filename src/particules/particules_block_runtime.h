#pragma once

#include "particules_dsp/types.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cmath>

template <size_t BlockSize>
class ParticulesBlockRuntime {
public:
    static_assert(BlockSize >= 1, "Particules block runtime requires BlockSize >= 1");

    bool PushInputSample(particules_dsp::StereoFrame in) {
        input_buf_[input_index_] = in;
        ++input_index_;
        if (input_index_ >= BlockSize) {
            input_index_ = 0;
            output_index_ = 0;
            block_ready_ = true;
            return true;
        }
        return false;
    }

    bool BlockReady() const { return block_ready_; }
    size_t InputIndex() const { return input_index_; }
    size_t OutputIndex() const { return output_index_; }

    particules_dsp::StereoFrame* InputBuffer() { return input_buf_.data(); }
    const particules_dsp::StereoFrame* InputBuffer() const { return input_buf_.data(); }

    void CommitProcessedBlock(const particules_dsp::StereoFrame* processed, size_t count) {
        const size_t copy_count = std::min(count, BlockSize);
        for (size_t i = 0; i < copy_count; ++i) {
            output_buf_[i] = processed[i];
        }
        block_ready_ = false;
        output_index_ = 0;
    }

    particules_dsp::StereoFrame ReadOutputSample() {
        particules_dsp::StereoFrame out = output_buf_[output_index_];
        ++output_index_;
        if (output_index_ >= BlockSize) {
            output_index_ = 0;
        }
        return out;
    }

    // A Start arriving while a pulse is running (or during the gap sample)
    // queues the new pulse behind exactly one forced low sample, so
    // downstream Schmitt triggers see separate events instead of one long
    // gate (1 ms pulses are shorter than the ~1.33 ms block, so dense
    // grain clouds otherwise merge into a DC-ish high level).
    void StartGrainTriggerPulse(int samples) {
        int n = samples > 0 ? samples : 0;
        if (grain_trigger_remaining_ > 0 || gap_pending_) {
            pending_pulse_ = n;
            gap_pending_ = true;
        } else {
            grain_trigger_remaining_ = n;
        }
    }

    bool ConsumeTriggerPulseSample() {
        if (grain_trigger_remaining_ > 0) {
            --grain_trigger_remaining_;
            return true;
        }
        if (gap_pending_) {
            // This sample is the forced low; the pending pulse starts next.
            gap_pending_ = false;
            grain_trigger_remaining_ = pending_pulse_;
            pending_pulse_ = 0;
            return false;
        }
        return false;
    }

    void SetGrainLed(float value) { grain_led_ = value; }
    float GrainLed() const { return grain_led_; }

    // Grain-density LED: one grain still flashes visibly (floor 0.25),
    // ~10+ concurrent grains reads full-bright. `triggered` is the
    // any-activity OR-in so grains whose whole lifetime fits inside one
    // block still register even when the count snapshot missed them.
    // Only ever brightens — DecayGrainLed() owns dimming.
    void NoteGrainActivity(int active_count, bool triggered) {
        float target = 0.0f;
        if (active_count > 0) {
            target = std::min(1.0f, 0.25f + 0.75f * static_cast<float>(active_count) / 10.0f);
        } else if (triggered) {
            target = 0.25f;
        }
        if (target > grain_led_) grain_led_ = target;
    }

    // Per-block LED decay factor for the current sample rate. Old code used a
    // fixed 0.9999^BlockSize, which decayed twice as fast (wall-clock) at 96 kHz.
    // f = 0.9999^(48000·BlockSize/sr) keeps the per-second decay constant.
    void ConfigureSampleRate(float sample_rate) {
        float sr = sample_rate > 0.f ? sample_rate : 48000.f;
        grain_led_decay_ = std::pow(0.9999f, 48000.f * static_cast<float>(BlockSize) / sr);
    }

    void DecayGrainLed() {
        if (grain_led_ <= 0.0001f) {
            grain_led_ = 0.0f;
            return;
        }
        grain_led_ *= grain_led_decay_;
    }

    // Per-sample SEED gate latch. The engine only sees the gate once per
    // block, so a short trigger landing between block-boundary samples would
    // otherwise be lost (~25% of 1 ms triggers at 48 kHz / 64-sample blocks).
    // Note every sample; consume (and clear) once per processed block.
    void NoteSeedGateSample(bool high) { seed_gate_latch_ = seed_gate_latch_ || high; }
    bool ConsumeSeedGateLatch() {
        bool v = seed_gate_latch_;
        seed_gate_latch_ = false;
        return v;
    }

private:
    std::array<particules_dsp::StereoFrame, BlockSize> input_buf_ {};
    std::array<particules_dsp::StereoFrame, BlockSize> output_buf_ {};
    size_t input_index_ = 0;
    size_t output_index_ = 0;
    bool block_ready_ = false;
    int grain_trigger_remaining_ = 0;
    int pending_pulse_ = 0;
    bool gap_pending_ = false;
    float grain_led_ = 0.0f;
    float grain_led_decay_ = 0.9999f;
    bool seed_gate_latch_ = false;
};

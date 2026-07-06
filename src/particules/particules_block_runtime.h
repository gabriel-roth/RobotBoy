#pragma once

#include "beads/types.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cmath>

template <size_t BlockSize>
class ParticulesBlockRuntime {
public:
    static_assert(BlockSize >= 1, "Particules block runtime requires BlockSize >= 1");

    bool PushInputSample(beads::StereoFrame in) {
        input_buf_[input_index_] = in;
        output_index_ = input_index_ + 1;
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

    beads::StereoFrame* InputBuffer() { return input_buf_.data(); }
    const beads::StereoFrame* InputBuffer() const { return input_buf_.data(); }

    void CommitProcessedBlock(const beads::StereoFrame* processed, size_t count) {
        const size_t copy_count = std::min(count, BlockSize);
        for (size_t i = 0; i < copy_count; ++i) {
            output_buf_[i] = processed[i];
        }
        block_ready_ = false;
        output_index_ = 0;
    }

    beads::StereoFrame ReadOutputSample() {
        beads::StereoFrame out = output_buf_[output_index_];
        ++output_index_;
        if (output_index_ >= BlockSize) {
            output_index_ = 0;
        }
        return out;
    }

    void StartGrainTriggerPulse(int samples) {
        grain_trigger_remaining_ = samples > 0 ? samples : 0;
    }

    bool ConsumeTriggerPulseSample() {
        if (grain_trigger_remaining_ <= 0) {
            return false;
        }
        --grain_trigger_remaining_;
        return true;
    }

    void SetGrainLed(float value) { grain_led_ = value; }
    float GrainLed() const { return grain_led_; }

    void DecayGrainLed() {
        if (grain_led_ <= 0.0001f) {
            grain_led_ = 0.0f;
            return;
        }
        grain_led_ *= std::pow(0.9999f, static_cast<float>(BlockSize));
    }

private:
    std::array<beads::StereoFrame, BlockSize> input_buf_ {};
    std::array<beads::StereoFrame, BlockSize> output_buf_ {};
    size_t input_index_ = 0;
    size_t output_index_ = 0;
    bool block_ready_ = false;
    int grain_trigger_remaining_ = 0;
    float grain_led_ = 0.0f;
};

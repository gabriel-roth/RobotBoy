#pragma once

#include <cmath>

namespace particules_dsp {

class ControlConditioner {
public:
    void Init(int decimation, float smoothing, float quantize_step, float initial) {
        decimation_ = decimation < 1 ? 1 : decimation;
        smoothing_ = smoothing < 0.0f ? 0.0f : (smoothing > 1.0f ? 1.0f : smoothing);
        quantize_step_ = quantize_step < 0.0f ? 0.0f : quantize_step;
        Reset(initial);
    }

    void Reset(float initial) {
        counter_ = decimation_ - 1;
        held_ = Quantize(initial);
        state_ = held_;
        initialized_ = true;
    }

    float Process(float input) {
        if (!initialized_) {
            Reset(input);
            return state_;
        }

        counter_++;
        if (counter_ >= decimation_) {
            held_ = Quantize(input);
            counter_ = 1;
        }

        if (smoothing_ <= 0.0f) {
            state_ = held_;
        } else {
            state_ += smoothing_ * (held_ - state_);
        }
        return state_;
    }

private:
    float Quantize(float input) const {
        if (quantize_step_ <= 0.0f) return input;
        return std::round(input / quantize_step_) * quantize_step_;
    }

    int decimation_ = 1;
    int counter_ = 0;
    float smoothing_ = 0.0f;
    float quantize_step_ = 0.0f;
    float held_ = 0.0f;
    float state_ = 0.0f;
    bool initialized_ = false;
};

}  // namespace particules_dsp

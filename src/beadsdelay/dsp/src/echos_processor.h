#pragma once
// Internal header — defines EchosProcessor::Impl. Not public API.
#include "../include/beadsdelay_dsp/echos_dsp.h"
#include "buffer/recording_buffer.h"   // particules_dsp
#include "fx/saturation.h"
#include "quality/quality_processor.h"
#include "util/svf.h"

namespace beadsdelay_dsp {

struct EchosProcessor::Impl {
    particules_dsp::RecordingBuffer recording_buffer;
    particules_dsp::QualityProcessor quality_processor;
    particules_dsp::Saturation saturation;
    particules_dsp::StateVariableFilter feedback_hp_l, feedback_hp_r;

    EchosParameters params;
    float sample_rate = 48000.f;

    // Smoothed mix params (zipper prevention)
    float smoothed_dry_wet = 0.5f;
    float smoothed_feedback = 0.f;

    StereoFrame wet_buf[kMaxBlockSize];

    static constexpr size_t kAlignment = 16;
};

} // namespace beadsdelay_dsp

#pragma once

#include <cstddef>
#include "../../include/beads/types.h"
#include "../../include/beads/parameters.h"
#include "../random/random.h"
#include "../random/attenurandomizer.h"
#include "../pitch/pitch_quantizer.h"
#include "grain.h"
#include "grain_scheduler.h"

namespace beads {

class RecordingBuffer;

class GrainEngine {
public:
    void Init(float sample_rate, RecordingBuffer* buffer);

    // Process one block of audio
    void Process(const BeadsParameters& params, StereoFrame* output,
                 size_t num_frames);

    // Set pitch modulation ratio (tape mode wow/flutter). 1.0 = no modulation.
    void SetPitchModulation(float ratio) { pitch_mod_ratio_ = ratio; }

    int ActiveGrainCount() const;
    bool GrainTriggeredThisBlock() const { return scheduler_.GrainTriggeredThisBlock(); }

    // Scale quantization
    void LoadScale(const double* ratios, uint32_t num_notes) { pitch_quantizer_.loadRatios(ratios, num_notes); }
    void ClearScale() { pitch_quantizer_.clear(); }
    void SetScaleRoot(int midi_note) { pitch_quantizer_.set_root(midi_note); }

private:
    enum class RenderLoadTier {
        kNormal,
        kHigh
    };

    Grain grains_[kMaxGrains];
    GrainScheduler scheduler_;
    Attenurandomizer ar_time_;
    Attenurandomizer ar_size_;
    Attenurandomizer ar_shape_;
    Attenurandomizer ar_pitch_;
    Random random_;
    PitchQuantizer pitch_quantizer_;

    RecordingBuffer* buffer_ = nullptr;
    float sample_rate_ = 48000.0f;

    // Overlap normalization
    float overlap_count_lp_ = 0.0f;   // Smoothed active grain count
    float gain_normalization_ = 1.0f;  // Smoothed gain factor

    // Tape mode wow/flutter pitch modulation (ratio, 1.0 = none)
    float pitch_mod_ratio_ = 1.0f;

    // Cache for grain_dur calculation: avoid exp2+log2f every sample.
    // Decimation ensures we only recompute every N samples even when
    // size is being actively modulated.
    static constexpr int kGrainDurDecimation = 32;
    int grain_dur_counter_ = 0;
    float cached_size_ = -999.f;
    int cached_decimation_ = -1;
    float cached_grain_dur_ = 0.f;
    int cached_max_active_ = kMaxGrains;
    RenderLoadTier render_load_tier_ = RenderLoadTier::kNormal;
    static constexpr int kHighLoadActiveGrains = 12;

    // Startup ramp: limit grain count for the first second to avoid
    // CPU spike when loading a patch with high density settings.
    int startup_samples_remaining_ = 0;

    // Allocate a grain from the pool (returns nullptr if full after stealing)
    Grain* AllocateGrain();

    // Compute grain parameters from BeadsParameters + attenurandomizers
    Grain::GrainParameters ComputeGrainParams(const BeadsParameters& params,
                                               int pre_delay);
};

} // namespace beads

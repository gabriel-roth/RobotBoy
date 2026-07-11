#pragma once

#include <cstddef>
#include "../../include/particules_dsp/types.h"
#include "../../include/particules_dsp/parameters.h"
#include "../random/random.h"
#include "../random/attenurandomizer.h"
#include "../pitch/pitch_quantizer.h"
#include "grain.h"
#include "grain_scheduler.h"

namespace particules_dsp {

class RecordingBuffer;

class GrainEngine {
public:
    void Init(float sample_rate, RecordingBuffer* buffer);

    // Process one block of audio
    void Process(const ParticulesParameters& params, StereoFrame* output,
                 size_t num_frames);

    // Set pitch modulation ratio (tape mode wow/flutter). 1.0 = no modulation.
    void SetPitchModulation(float ratio) { pitch_mod_ratio_ = ratio; }

    int ActiveGrainCount() const;
    bool GrainTriggeredThisBlock() const { return scheduler_.GrainTriggeredThisBlock(); }

    // Test-only accessors: expose per-slot spawn order and kill state to
    // verify the kill-fallback picks the true oldest-by-spawn-order grain
    // (see AllocateGrain), not just the first non-pending array slot.
    bool ActiveAt(int index) const { return grains_[index].active(); }
    bool PendingKillAt(int index) const { return grains_[index].pending_kill(); }
    uint32_t SpawnSerialAt(int index) const { return grains_[index].spawn_serial(); }

    // Test-only: directly exercises AllocateGrain's full-pool kill-
    // fallback branch. In production this same branch is reached from
    // Process() when a trigger arrives with no free grain slot; the
    // separate CPU-based max-active-grain cap in Process() means that in
    // practice a genuinely full pool (all kMaxGrains slots active) also
    // has max_active <= kMaxGrains, so Process() drops the excess trigger
    // before ever calling AllocateGrain again. This hook lets tests drive
    // the fallback branch deterministically without depending on that cap.
    void ForceAllocateGrainForTest() { AllocateGrain(); }

    // Scale quantization
    void LoadScale(const double* ratios, uint32_t num_notes) { pitch_quantizer_.loadRatios(ratios, num_notes); }
    void ClearScale() { pitch_quantizer_.clear(); }
    void SetScaleRoot(int midi_note) { pitch_quantizer_.set_root(midi_note); }

private:
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

    // Monotonically increasing spawn-order counter, stamped onto each
    // grain at activation (see Process()). Used by AllocateGrain's kill-
    // fallback to find the true oldest active grain.
    uint32_t spawn_serial_ = 0;

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

    // Startup ramp: limit grain count for the first second to avoid
    // CPU spike when loading a patch with high density settings.
    int startup_samples_remaining_ = 0;

    // Allocate a grain from the pool (returns nullptr if full after stealing)
    Grain* AllocateGrain();

    // Compute grain parameters from ParticulesParameters + attenurandomizers
    Grain::GrainParameters ComputeGrainParams(const ParticulesParameters& params,
                                               int pre_delay);
};

} // namespace particules_dsp

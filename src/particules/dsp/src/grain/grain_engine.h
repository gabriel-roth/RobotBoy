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

    // Deactivate every grain immediately (no fade) and invalidate the
    // grain-duration cache. Call at a config-change apply point, where the
    // wet output is muted (the hard cut is inaudible) and stale grain read
    // positions would be invalid after the buffer resize.
    void KillAllGrains();

    // Test-only accessors: expose per-slot spawn order and kill state to
    // verify the steal path picks the true oldest-by-spawn-order grain
    // (see FindOldestActiveGrain), not just the first non-pending array slot.
    bool ActiveAt(int index) const { return grains_[index].active(); }
    bool PendingKillAt(int index) const { return grains_[index].pending_kill(); }
    uint32_t SpawnSerialAt(int index) const { return grains_[index].spawn_serial(); }
    // Test-only: the grain's actual playback-rate ratio (phase_increment
    // is stored as-computed by ComputeGrainParams' pitch_ratio -> Q32.32
    // conversion in Grain::Start). Used to pin that non-finite pitch
    // inputs resolve to the intended unity fallback, not just "some
    // finite value" (see the fast_exp2.h NaN-safety fix).
    float PhaseIncrementAt(int index) const { return grains_[index].phase_increment(); }

    // Test-only: marks the pool's true oldest grain for the click-free
    // pending-kill, exactly as Process()'s steal path does at saturation.
    // Lets tests drive victim selection deterministically without depending
    // on the CPU-based max-active cap in Process().
    void ForceAllocateGrainForTest() {
        int v = FindOldestActiveGrain();
        if (v >= 0) grains_[v].StartPendingKill();
    }

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
    float inv_sample_rate_ = 1.0f / 48000.0f;  // kept in sync with sample_rate_ (set in Init)

    // Monotonically increasing spawn-order counter, stamped onto each
    // grain at activation (see Process()). Used by FindOldestActiveGrain
    // to pick the true oldest active grain when stealing at saturation.
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
    // log2(buf_dur / kMinGrainDurationSeconds): the transcendental argument
    // to GrainDurationSeconds()'s exp2. Depends only on decimation/buffer
    // size, not per-grain SIZE modulation, so it's recomputed alongside
    // cached_decimation_ (block/decimated rate) instead of every spawn.
    float cached_log2_dur_range_ = 0.f;
    // 1.0f / decimation_factor(), recomputed alongside cached_decimation_
    // (see above) -- turns the per-spawn "/ df_f" divides in
    // ComputeGrainParams into multiplies.
    float inv_df_ = 1.0f;

    // Overlap-normalization block coefficient cache: 1-pow(1-slope_coeff,
    // num_frames) only takes on a handful of distinct values in practice
    // (slope_coeff is one of two constants; num_frames is normally the
    // fixed engine block size), so avoid re-running pow() every block.
    float cached_slope_coeff_ = -1.0f;
    size_t cached_overlap_num_frames_ = 0;
    float cached_block_coefficient_ = 0.0f;

    // Upward cap slew: the effective max-active cap falls to
    // cached_max_active_ immediately but rises toward it at
    // kCapSlewPerSecond, so fast SIZE moves (and patch loads -- Init seeds
    // the slew at the floor of 2, subsuming the old 1-second startup ramp)
    // can't refill the pool with long grains all at once. See the
    // 2026-07-26 spec addendum.
    static constexpr float kCapSlewPerSecond = 28.0f;
    float max_active_slew_ = 2.0f;

    // Allocate a free grain slot from the pool (returns nullptr if none free).
    Grain* AllocateGrain();

    // Index of the active, not-yet-pending-kill grain with the lowest spawn
    // serial (the true oldest), or -1 if none. Used by Process()'s saturation
    // steal path and the test hook to pick a victim.
    int FindOldestActiveGrain() const;

    // Compute grain parameters from ParticulesParameters + attenurandomizers
    Grain::GrainParameters ComputeGrainParams(const ParticulesParameters& params,
                                               int pre_delay);
};

} // namespace particules_dsp

#include "grain_engine.h"
#include "../buffer/recording_buffer.h"
#include "../util/dsp_utils.h"
#include "../util/fast_exp2.h"

#include <cmath>
#include <algorithm>

namespace particules_dsp {

// Boundary at 11 o'clock on the -1..1 knob (120° from fully CCW in a 300° sweep).
// At this value abs_size=0 → 30ms minimum grain (hardware Beads manual).
// CW (> boundary) = forward; CCW (< boundary) = reverse.
static constexpr float kSizeBoundary = -0.2f;
static constexpr float kMinGrainDurationSeconds = 0.030f;
// Precomputed reciprocals for NormalizedGrainSize's two range widths.
static constexpr float kInvOneMinusBoundary = 1.0f / (1.0f - kSizeBoundary);
static constexpr float kInvBoundaryPlusOne = 1.0f / (kSizeBoundary + 1.0f);

// Normalizes a raw SIZE value (forward or reverse range) to [0, 1) around
// kSizeBoundary, matching the mapping used for both grain-param computation
// and the cached max-active-grain estimate.
static float NormalizedGrainSize(float size) {
    const float normalized = size >= kSizeBoundary
        ? (size - kSizeBoundary) * kInvOneMinusBoundary
        : (kSizeBoundary - size) * kInvBoundaryPlusOne;
    return Clamp(normalized, 0.0f, 0.999f);
}

// Exponential mapping from normalized SIZE (0..1) to 30ms..max_duration.
// log2_dur_range is log2(max_duration / kMinGrainDurationSeconds), cached by
// the caller (see cached_log2_dur_range_) so this reduces to a single exp2
// per call instead of a log2f+exp2 pair.
static float GrainDurationSeconds(float size, float log2_dur_range) {
    const float normalized = NormalizedGrainSize(size);
    return kMinGrainDurationSeconds * std::exp2(normalized * log2_dur_range);
}


void GrainEngine::Init(float sample_rate, RecordingBuffer* buffer) {
    sample_rate_ = sample_rate;
    inv_sample_rate_ = 1.0f / sample_rate_;
    buffer_ = buffer;
    overlap_count_lp_ = 0.0f;
    gain_normalization_ = 1.0f;
    spawn_serial_ = 0;

    for (int i = 0; i < kMaxGrains; ++i) {
        grains_[i].Init();
    }

    scheduler_.Init(sample_rate);
    random_.Init(0x68A14CED);

    ar_time_.Init(&random_);
    ar_size_.Init(&random_);
    ar_shape_.Init(&random_);
    ar_pitch_.Init(&random_);

    // Reset grain_dur cache and decimation counter
    grain_dur_counter_ = 0;
    cached_size_ = -999.f;
    cached_decimation_ = -1;
    cached_grain_dur_ = 0.f;
    cached_max_active_ = kMaxGrains;
    cached_log2_dur_range_ = 0.f;
    inv_df_ = 1.0f;
    cached_slope_coeff_ = -1.0f;
    cached_overlap_num_frames_ = 0;
    cached_block_coefficient_ = 0.0f;

    // Cap slew starts at the floor; rising at kCapSlewPerSecond gives the
    // same ~1-second patch-load grace period as the old startup ramp.
    max_active_slew_ = 2.0f;
}

int GrainEngine::ActiveGrainCount() const {
    int count = 0;
    for (int i = 0; i < kMaxGrains; ++i) {
        if (grains_[i].active()) ++count;
    }
    return count;
}

void GrainEngine::KillAllGrains() {
    for (int i = 0; i < kMaxGrains; ++i) {
        grains_[i].Init();
    }
    overlap_count_lp_ = 0.0f;
    // Buffer size may change at the same decimation (Cold/Sunny/Scorched are
    // all /2): force the max-active/duration cache to recompute.
    cached_decimation_ = -1;
}

Grain* GrainEngine::AllocateGrain() {
    for (int i = 0; i < kMaxGrains; ++i) {
        if (!grains_[i].active()) {
            return &grains_[i];
        }
    }
    return nullptr;
}

// Index of the active, not-yet-pending-kill grain with the lowest spawn
// serial (the true oldest), or -1 if every grain is inactive or already
// dying.
//
// Array index does NOT track spawn order: as grains finish, their slots
// free up and get reused by AllocateGrain, so a low array index can hold
// the *newest* grain in the pool. spawn_serial_ is a monotonically
// increasing counter stamped on each grain at activation (see Process());
// pick by serial, not by index.
//
// spawn_serial_ wraps at 2^32 (~4.29 billion grains). Even under a
// sustained worst-case trigger rate (e.g. an audio-rate signal into a
// gate/clock input firing a grain on every rising edge, tens of kHz),
// that's still on the order of a day of continuous playing. Wraparound
// only affects *relative* ordering, and grains live at most a few
// hundred ms — far shorter than the wrap period — so no two
// simultaneously-active grains can ever straddle a wrap; ordering
// self-heals immediately.
int GrainEngine::FindOldestActiveGrain() const {
    int victim = -1;
    for (int i = 0; i < kMaxGrains; ++i) {
        if (!grains_[i].active() || grains_[i].pending_kill()) continue;
        if (victim < 0 || grains_[i].spawn_serial() < grains_[victim].spawn_serial()) {
            victim = i;
        }
    }
    return victim;
}

Grain::GrainParameters GrainEngine::ComputeGrainParams(
        const ParticulesParameters& params, int pre_delay) {
    Grain::GrainParameters gp;

    // --- SIZE → grain duration + direction ---
    // Boundary at kSizeBoundary (-0.2 = 11 o'clock). At the boundary abs_size=0 → 30ms.
    // CW (> -0.2): forward, [-0.2, 1.0] normalized to [0, 1].
    // CCW (< -0.2): reverse, [-1.0, -0.2) normalized to [0, 1].
    float mod_size = ar_size_.Process(params.size, params.size_ar,
                                       params.size_cv, params.size_cv_connected);
    bool reverse = (mod_size < kSizeBoundary);

    // Exponential mapping from 0..1 to 30ms..max duration. cached_log2_dur_range_
    // and inv_df_ track decimation_factor()/buffer size and are refreshed at
    // block/decimated rate in Process() (see cached_decimation_), so this
    // spawn-rate path only pays for the exp2, not a log2f or a divide.
    float duration = GrainDurationSeconds(mod_size, cached_log2_dur_range_);
    gp.size = duration * sample_rate_;

    // --- TIME → buffer read position ---
    // Matches the hardware Beads manual:
    //   0.0 (fully CCW) → most recent audio (near write head)
    //   1.0 (fully CW)  → oldest audio (far from write head)
    float mod_time = ar_time_.Process(params.time, params.time_ar,
                                       params.time_cv, params.time_cv_connected);
    mod_time = Clamp(mod_time, 0.0f, 1.0f);

    // --- PITCH → playback rate ---
    // (computed before position so we can calculate grain span for headroom)
    float mod_pitch = ar_pitch_.Process(params.pitch, params.pitch_ar,
                                         params.pitch_cv, params.pitch_cv_connected);
    mod_pitch += params.midi_pitch_offset;
    // Quantize pitch to scale degrees (convert semitones <-> V/oct)
    mod_pitch = pitch_quantizer_.quantize(mod_pitch / 12.0f) * 12.0f;
    // Pitch lock is the final constraint — applied after AR and scale quantizer
    if (params.pitch_lock != 0)
        mod_pitch = QuantizePitchLock(mod_pitch, params.pitch_lock);
    // SemitonesToRatioFast (Exp2Fast) requires a *finite* argument: unlike
    // std::exp2, which propagates NaN straight through, Exp2Fast's
    // `(int)y` truncation is UB on a non-finite y and has been observed to
    // produce small but FINITE garbage (not NaN) -- which would then sail
    // straight past the isfinite(gp.pitch_ratio) fence below undetected. A
    // NaN pitch_cv with pitch_ar engaged (or a NaN from the scale
    // quantizer/pitch lock, though both are guarded/finite by construction)
    // can reach here, so sanitize before the fast path ever sees it. 0
    // semitones = unity ratio, the intended fallback.
    if (!std::isfinite(mod_pitch)) mod_pitch = 0.0f;
    gp.pitch_ratio = SemitonesToRatioFast(mod_pitch) * pitch_mod_ratio_ * inv_df_;
    if (reverse) {
        gp.pitch_ratio = -gp.pitch_ratio;
    }
    // Backstop for NaN/Inf entering multiplicatively from pitch_mod_ratio_
    // or inv_df_ (mod_pitch itself is already sanitized above). Grain::
    // Start() stores this directly into phase_increment_, which is added
    // into read_position_ every sample in the hot loop -- once
    // read_position_ goes NaN, the very next ReadHermiteStereoFast() call
    // does an unguarded float->int cast on it, which is undefined
    // behavior. That happens before the per-sample isfinite(gl/gr) check
    // in Grain::ProcessBlock ever gets a chance to catch it, so this needs
    // its own fence, same idea as the gp.position guard below.
    if (!std::isfinite(gp.pitch_ratio)) gp.pitch_ratio = reverse ? -1.0f : 1.0f;

    // Convert to an absolute position in the recording buffer.
    // time=0.0 means read from write_head (newest), time=1.0 means read
    // from one full buffer behind the write_head (oldest).
    float buf_size_f = static_cast<float>(buffer_->size());
    float offset_frames = mod_time * buf_size_f;

    // Ensure the grain has enough headroom to play without reading past the
    // write head (forward) or before the oldest valid data (reverse).
    // Without this, TIME near 0 + large SIZE reads stale/unwritten data.
    float span = gp.size * std::fabs(gp.pitch_ratio);
    // Also account for write head advancing during grain lifetime
    float write_advance = gp.size * inv_df_;
    float min_offset = span + write_advance;
    min_offset = std::min(min_offset, buf_size_f - 1.0f);
    offset_frames = std::max(offset_frames, min_offset);

    float pos = static_cast<float>(buffer_->write_head()) - offset_frames;
    if (pos < 0.0f) pos += buf_size_f;
    gp.position = pos;

    // Guard against NaN/huge positions before they reach Grain::Start() —
    // and before the reverse wrap below, whose while-loop would spin
    // forever on +Inf and silently skip on NaN. Robust by construction,
    // not by upstream luck.
    if (!std::isfinite(gp.position)) gp.position = 0.0f;

    if (reverse) {
        // Offset start position to the END of the segment a forward grain
        // would play.  The reverse grain then reads backwards through the
        // same audio, producing true reversed playback of the intended
        // segment rather than reading into unrelated older audio.
        gp.position += span;
        while (gp.position >= buf_size_f) gp.position -= buf_size_f;
    }

    // --- SHAPE → envelope shape ---
    float mod_shape = ar_shape_.Process(params.shape, params.shape_ar,
                                         params.shape_cv, params.shape_cv_connected);
    gp.shape = Clamp(mod_shape, 0.0f, 1.0f);

    // --- PAN ---
    // Slight random panning for stereo width.
    gp.pan = random_.NextBipolar() * 0.5f;

    // --- GAIN (MIDI velocity) ---
    gp.gain = params.midi_velocity_gain;

    // --- PRE-DELAY ---
    gp.pre_delay = pre_delay;

    return gp;
}

void GrainEngine::Process(const ParticulesParameters& params,
                          StereoFrame* output, size_t num_frames) {
    if (!buffer_ || buffer_->size() == 0) {
        for (size_t i = 0; i < num_frames; ++i) {
            output[i] = {0.0f, 0.0f};
        }
        return;
    }

    // --- Schedule triggers ---
    static constexpr int kMaxTriggers = 32;
    int trigger_samples[kMaxTriggers];
    bool trigger_droppable[kMaxTriggers];
    int num_triggers = scheduler_.Process(params, num_frames,
                                          trigger_samples, kMaxTriggers,
                                          trigger_droppable);

    // Compute a dynamic grain cap based on grain size.
    // Long grains don't benefit from heavy overlap — they play the same
    // audio and just waste CPU.  Cap = buffer_duration / grain_duration,
    // scaled by 2 for texture, clamped to [2, kMaxGrains].
    // Use decimation to limit exp2+log2f calls when size is being modulated.
    int df = buffer_->decimation_factor();
    float raw_size = params.size;
    bool needs_update = (df != cached_decimation_);
    if (!needs_update && ++grain_dur_counter_ >= kGrainDurDecimation) {
        grain_dur_counter_ = 0;
        needs_update = (raw_size != cached_size_);
    }
    if (needs_update) {
        cached_size_ = raw_size;
        cached_decimation_ = df;
        inv_df_ = 1.0f / static_cast<float>(df);
        float buf_dur = static_cast<float>(buffer_->size()) * static_cast<float>(df)
                      * inv_sample_rate_;
        cached_log2_dur_range_ = std::log2f(buf_dur / kMinGrainDurationSeconds);
        cached_grain_dur_ = GrainDurationSeconds(raw_size, cached_log2_dur_range_);
        cached_max_active_ = static_cast<int>(buf_dur / cached_grain_dur_ * 1.5f);
        cached_max_active_ = std::max(cached_max_active_, 2);
        cached_max_active_ = std::min(cached_max_active_, kMaxGrains);
    }
    // Upward cap slew: fall to the target immediately (shrinking only
    // tightens the spawn gate; it never kills grains) but rise at a
    // bounded rate. Without this, a fast SIZE sweep refills the pool to
    // the new cap instantly with multi-second grains that then persist --
    // measured as a 2-10x transient CPU spike over the local steady state
    // (see the 2026-07-26 spec addendum). Init seeds the slew at the
    // floor of 2, which also provides the old startup ramp's patch-load
    // protection (2 -> kMaxGrains in ~1 s).
    float cap_target = static_cast<float>(cached_max_active_);
    if (max_active_slew_ > cap_target) {
        max_active_slew_ = cap_target;
    } else {
        max_active_slew_ += kCapSlewPerSecond * static_cast<float>(num_frames)
                            * inv_sample_rate_;
        if (max_active_slew_ > cap_target) max_active_slew_ = cap_target;
    }
    int max_active = static_cast<int>(max_active_slew_);

    int active_before = ActiveGrainCount();

    // Start new grains at their trigger points. At saturation (CPU cap or
    // full pool) the policy splits by trigger kind: manual triggers (gate
    // rising edges, clock ticks) steal-and-replace so performed events
    // always sound (decided 2026-07-11); automatic density ticks are
    // dropped so playing grains finish their envelopes instead of
    // churning (cap floor 2026-07-25, generalized to every cap
    // 2026-07-26). See docs/superpowers/specs/
    // 2026-07-26-particules-midrange-saturation-drop-design.md.
    for (int t = 0; t < num_triggers; ++t) {
        Grain* g = nullptr;
        bool reused_active_slot = false;
        if (active_before < max_active) g = AllocateGrain();
        if (!g) {
            // Saturated (dynamic cap, slewed cap, or full pool):
            // automatic (droppable) triggers are dropped -- no steal, no
            // spawn -- at ANY cap value. One rule everywhere; the
            // 2026-07-25 cap-floor-only gate and its startup-ramp
            // carve-out are deliberately superseded (see the 2026-07-26
            // spec).
            if (trigger_droppable[t]) continue;

            int victim = FindOldestActiveGrain();
            if (victim < 0) break;   // every active grain is already dying; drop the rest
            Grain* free_slot = AllocateGrain();
            if (free_slot) {
                // CPU-cap saturation with pool headroom: fade the victim out
                // click-free and start the new grain in a free slot (active
                // count exceeds the cap by 1 for <=36 samples).
                grains_[victim].StartPendingKill();
                g = free_slot;
            } else {
                // Pool truly full: hard-replace the victim. The new grain's
                // envelope opens at zero; the victim's cut is the accepted
                // cost of never dropping the newest event.
                g = &grains_[victim];
                reused_active_slot = true;
            }
        }
        auto gp = ComputeGrainParams(params, trigger_samples[t]);
        g->Start(gp);
        g->SetSpawnSerial(++spawn_serial_);
        if (!reused_active_slot) ++active_before;
    }

    // --- Zero output buffer ---
    for (size_t i = 0; i < num_frames; ++i) {
        output[i] = {0.0f, 0.0f};
    }

    // --- Render grains: grain-major order for cache locality ---
    // Processing each grain across the full block keeps buffer reads
    // sequential (consecutive samples read adjacent memory), which is
    // much faster than sample-major order where each sample touches
    // all active grain positions and thrashes L1 cache.
    int active_count = 0;
    int64_t buf_size_q = static_cast<int64_t>(buffer_->size()) << 32;
    // Resolved once per block (format/channels/pointers/mu-law LUT): every
    // grain-sample read in the loop below uses this instead of re-deriving
    // from *buffer_, which the compiler can't prove is unaliased with the
    // output[] accumulation stores. Block-lifetime only -- buffer_ must not
    // be written/reconfigured until this block's grains are all processed
    // (it isn't; recording writes happen in a separate pass, not here).
    RecordingBuffer::ReadContext read_ctx = buffer_->MakeReadContext();
    for (int g = 0; g < kMaxGrains; ++g) {
        if (!grains_[g].active()) continue;
        ++active_count;

        grains_[g].ProcessBlock(read_ctx, buf_size_q, output, num_frames);
    }

    // --- Overlap normalization (matches Clouds approach) ---
    // Asymmetric tracking: fast rise (when grains pile up, reduce gain
    // quickly to prevent clipping), slow fall (when grains end, restore
    // gain slowly to prevent pumping).
    float count_f = static_cast<float>(active_count);
    float slope_coeff = (count_f > overlap_count_lp_) ? 0.9f : 0.2f;
    // slope_coeff only takes two values and num_frames is normally the
    // engine's fixed block size, so cache the pow() result keyed on both.
    if (slope_coeff != cached_slope_coeff_ || num_frames != cached_overlap_num_frames_) {
        cached_slope_coeff_ = slope_coeff;
        cached_overlap_num_frames_ = num_frames;
        cached_block_coefficient_ = 1.0f
            - std::pow(1.0f - slope_coeff, static_cast<float>(num_frames));
    }
    const float block_coefficient = cached_block_coefficient_;
    OnePole(overlap_count_lp_, count_f, block_coefficient);

    // 1/sqrt(n-1) for n > 2, unity gain for 1-2 grains.
    float gain_norm = (overlap_count_lp_ > 2.0f)
        ? 1.0f / std::sqrt(overlap_count_lp_ - 1.0f)
        : 1.0f;

    // Per-sample smooth the gain itself to avoid clicks on sudden changes.
    for (size_t i = 0; i < num_frames; ++i) {
        OnePole(gain_normalization_, gain_norm, 0.01f);
        output[i].l *= gain_normalization_;
        output[i].r *= gain_normalization_;
    }
}

} // namespace particules_dsp

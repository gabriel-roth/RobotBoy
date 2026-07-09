#include "grain_engine.h"
#include "../buffer/recording_buffer.h"
#include "../util/dsp_utils.h"

#include <cmath>
#include <algorithm>

namespace beads {

// Boundary at 11 o'clock on the -1..1 knob (120° from fully CCW in a 300° sweep).
// At this value abs_size=0 → 30ms minimum grain (hardware Beads manual).
// CW (> boundary) = forward; CCW (< boundary) = reverse.
static constexpr float kSizeBoundary = -0.2f;
static constexpr float kMinGrainDurationSeconds = 0.030f;

// Normalizes a raw SIZE value (forward or reverse range) to [0, 1) around
// kSizeBoundary, matching the mapping used for both grain-param computation
// and the cached max-active-grain estimate.
static float NormalizedGrainSize(float size) {
    const float normalized = size >= kSizeBoundary
        ? (size - kSizeBoundary) / (1.0f - kSizeBoundary)
        : (kSizeBoundary - size) / (kSizeBoundary + 1.0f);
    return Clamp(normalized, 0.0f, 0.999f);
}

// Exponential mapping from normalized SIZE (0..1) to 30ms..max_duration.
static float GrainDurationSeconds(float size, float max_duration) {
    const float normalized = NormalizedGrainSize(size);
    return kMinGrainDurationSeconds
        * std::exp2(normalized * std::log2f(max_duration / kMinGrainDurationSeconds));
}


void GrainEngine::Init(float sample_rate, RecordingBuffer* buffer) {
    sample_rate_ = sample_rate;
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

    // Startup ramp: ~1 second grace period
    startup_samples_remaining_ = static_cast<int>(sample_rate_);
}

int GrainEngine::ActiveGrainCount() const {
    int count = 0;
    for (int i = 0; i < kMaxGrains; ++i) {
        if (grains_[i].active()) ++count;
    }
    return count;
}

Grain* GrainEngine::AllocateGrain() {
    // First pass: find an inactive grain.
    for (int i = 0; i < kMaxGrains; ++i) {
        if (!grains_[i].active()) {
            return &grains_[i];
        }
    }

    // Pool is full — mark the true oldest grain (by spawn order) for
    // zero-crossing kill. The grain will be killed at the next zero
    // crossing (or after a short fallback fade), avoiding clicks without
    // smearing transients. We do NOT return the grain for immediate
    // reuse; the new trigger is simply dropped.
    //
    // Array index does NOT track spawn order: as grains finish, their
    // slots free up and get reused by the first loop above, so a low
    // array index can hold the *newest* grain in the pool. spawn_serial_
    // is a monotonically increasing counter stamped on each grain at
    // activation (see Process()); pick the active, not-yet-pending-kill
    // grain with the lowest serial.
    //
    // spawn_serial_ wraps at 2^32 (~4.29 billion grains). Even under a
    // sustained worst-case trigger rate (e.g. an audio-rate signal into a
    // gate/clock input firing a grain on every rising edge, tens of kHz),
    // that's still on the order of a day of continuous playing. Wraparound
    // only affects *relative* ordering, and grains live at most a few
    // hundred ms — far shorter than the wrap period — so no two
    // simultaneously-active grains can ever straddle a wrap; ordering
    // self-heals immediately.
    int victim = -1;
    for (int i = 0; i < kMaxGrains; ++i) {
        if (grains_[i].pending_kill()) continue;
        if (victim < 0 || grains_[i].spawn_serial() < grains_[victim].spawn_serial()) {
            victim = i;
        }
    }
    if (victim >= 0) {
        grains_[victim].StartPendingKill();
    }
    return nullptr;
}

Grain::GrainParameters GrainEngine::ComputeGrainParams(
        const BeadsParameters& params, int pre_delay) {
    Grain::GrainParameters gp;

    // --- SIZE → grain duration + direction ---
    // Boundary at kSizeBoundary (-0.2 = 11 o'clock). At the boundary abs_size=0 → 30ms.
    // CW (> -0.2): forward, [-0.2, 1.0] normalized to [0, 1].
    // CCW (< -0.2): reverse, [-1.0, -0.2) normalized to [0, 1].
    float mod_size = ar_size_.Process(params.size, params.size_ar,
                                       params.size_cv, params.size_cv_connected);
    bool reverse = (mod_size < kSizeBoundary);

    // Exponential mapping from 0..1 to 30ms..max duration
    // Decimation extends effective buffer duration
    int df = buffer_->decimation_factor();
    float df_f = static_cast<float>(df);
    float max_dur = static_cast<float>(buffer_->size()) * df_f / sample_rate_;
    float duration = GrainDurationSeconds(mod_size, max_dur);
    gp.size = duration * sample_rate_;

    // --- TIME → buffer read position ---
    // Matches original Beads manual:
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
    gp.pitch_ratio = SemitonesToRatio(mod_pitch) * pitch_mod_ratio_ / df_f;
    if (reverse) {
        gp.pitch_ratio = -gp.pitch_ratio;
    }

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
    float write_advance = gp.size / df_f;
    float min_offset = span + write_advance;
    min_offset = std::min(min_offset, buf_size_f - 1.0f);
    offset_frames = std::max(offset_frames, min_offset);

    float pos = static_cast<float>(buffer_->write_head()) - offset_frames;
    if (pos < 0.0f) pos += buf_size_f;
    gp.position = pos;

    if (reverse) {
        // Offset start position to the END of the segment a forward grain
        // would play.  The reverse grain then reads backwards through the
        // same audio, producing true reversed playback of the intended
        // segment rather than reading into unrelated older audio.
        gp.position += span;
        while (gp.position >= buf_size_f) gp.position -= buf_size_f;
    }

    // Guard against NaN/huge positions before they reach Grain::Start(),
    // which stores gp.position directly into read_position_ and feeds it to
    // RecordingBuffer::ReadHermiteStereoFast — an unguarded hot-loop reader
    // whose float->int cast is undefined behavior for non-finite input.
    // Land on a safe, always-valid position rather than propagate the NaN.
    if (!std::isfinite(gp.position)) gp.position = 0.0f;

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

void GrainEngine::Process(const BeadsParameters& params,
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
    int num_triggers = scheduler_.Process(params, num_frames,
                                          trigger_samples, kMaxTriggers);

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
        float buf_dur = static_cast<float>(buffer_->size()) * static_cast<float>(df)
                      / sample_rate_;
        cached_grain_dur_ = GrainDurationSeconds(raw_size, buf_dur);
        cached_max_active_ = static_cast<int>(buf_dur / cached_grain_dur_ * 1.5f);
        cached_max_active_ = std::max(cached_max_active_, 2);
        cached_max_active_ = std::min(cached_max_active_, kMaxGrains);
    }
    int max_active = cached_max_active_;

    // Startup ramp: gradually increase grain limit over first second
    // to avoid CPU spike when loading a patch with high density.
    if (startup_samples_remaining_ > 0) {
        startup_samples_remaining_ -= static_cast<int>(num_frames);
        // Ramp from 2 to max_active over 1 second
        float progress = 1.0f - static_cast<float>(startup_samples_remaining_) / sample_rate_;
        int ramped = 2 + static_cast<int>(progress * static_cast<float>(max_active - 2));
        max_active = std::min(max_active, std::max(ramped, 2));
    }

    int active_before = ActiveGrainCount();

    // Start new grains at their trigger points.
    for (int t = 0; t < num_triggers; ++t) {
        if (active_before >= max_active) break;
        Grain* g = AllocateGrain();
        if (g) {
            auto gp = ComputeGrainParams(params, trigger_samples[t]);
            g->Start(gp);
            g->SetSpawnSerial(++spawn_serial_);
            ++active_before;
        }
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
    float buf_size_f = static_cast<float>(buffer_->size());
    for (int g = 0; g < kMaxGrains; ++g) {
        if (!grains_[g].active()) continue;
        ++active_count;

        grains_[g].ProcessBlock(*buffer_, buf_size_f, output, num_frames);
    }

    // --- Overlap normalization (matches Clouds approach) ---
    // Asymmetric tracking: fast rise (when grains pile up, reduce gain
    // quickly to prevent clipping), slow fall (when grains end, restore
    // gain slowly to prevent pumping).
    float count_f = static_cast<float>(active_count);
    float slope_coeff = (count_f > overlap_count_lp_) ? 0.9f : 0.2f;
    const float block_coefficient = 1.0f
        - std::pow(1.0f - slope_coeff, static_cast<float>(num_frames));
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

} // namespace beads

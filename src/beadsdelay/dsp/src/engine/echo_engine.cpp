#include "echo_engine.h"
#include <algorithm>
#include <cmath>

namespace beadsdelay_dsp {

namespace {

// Wrap a position (in frames) into [0, size) — required precondition for
// RecordingBuffer::ReadHermiteStereoFast.
inline float WrapPosition(float position, float size_f) {
    if (size_f <= 0.f || !std::isfinite(position)) return 0.f;
    position = std::fmod(position, size_f);
    if (position < 0.f) position += size_f;
    return position;
}

}  // namespace

void EchoEngine::Init(particules_dsp::RecordingBuffer* buffer, float sample_rate) {
    buf_ = buffer;
    sample_rate_ = (sample_rate > 0.f) ? sample_rate : 48000.f;
    // Sentinel: "no real target set yet". SetTargets() snaps instead of
    // slewing on the first call it sees this, so the very first block's
    // target doesn't cause a spurious glide-in from an arbitrary default.
    delay_frames_ = target_frames_ = -1.f;
    slew_coeff_ = 0.001f;
    multi_tap_ = false;
    mode_ = TimeChangeMode::kTape;
    fade_from_frames_ = 0.f;
    fade_pos_ = 1.f;  // no fade in progress
    fade_step_ = 1.f / static_cast<float>(kJumpCrossfadeFrames);
    queued_target_ = -1.f;
    frozen_ = false;
    slice_start_ = 0.f;
    slice_len_frames_ = 1.f;
    slice_phase_ = 0.f;
    frozen_anchor_ = 0.f;
    read_subsample_ = buf_ ? static_cast<float>(buf_->write_head()) : 0.f;
}

void EchoEngine::SetTargets(float delay_samples, bool multi_tap,
                            TimeChangeMode mode, float slew_seconds) {
    if (!buf_) return;

    int decimation = std::max(1, buf_->decimation_factor());
    float frames = delay_samples / static_cast<float>(decimation);
    float size_f = static_cast<float>(buf_->size());
    // Keep at least 1 frame of headroom so wrapped reads never straddle the
    // write head in a way that reads unwritten/garbage frames.
    float max_frames = (size_f > 1.f) ? size_f - 1.f : 0.f;
    frames = std::clamp(frames, 0.f, max_frames);

    multi_tap_ = multi_tap;
    mode_ = mode;

    float slew_s = std::max(slew_seconds, 1e-4f);
    slew_coeff_ = 1.f - std::exp(-1.f / (slew_s * sample_rate_));

    if (delay_frames_ < 0.f) {
        // First-ever target: snap instead of gliding from the placeholder.
        delay_frames_ = frames;
        target_frames_ = frames;
    }

    if (mode_ == TimeChangeMode::kTape) {
        target_frames_ = frames;
        // Crossfade state stays "settled" so a later switch into kCrossfade
        // mode starts clean (no stale fade-in-progress).
        fade_pos_ = 1.f;
        queued_target_ = -1.f;
    } else {  // kCrossfade
        bool fading = fade_pos_ < 1.f;
        if (!fading) {
            if (frames != target_frames_) {
                fade_from_frames_ = target_frames_;
                target_frames_ = frames;
                fade_pos_ = 0.f;
            }
        } else if (frames != target_frames_) {
            // Retarget mid-fade: queue it, current fade runs to completion.
            queued_target_ = frames;
        }
    }

    // Re-sync the continuous write-position tracker to the buffer's actual
    // write head at this block boundary (see ReadWet()).
    read_subsample_ = static_cast<float>(buf_->write_head());
}

void EchoEngine::NotifyFreeze(bool frozen, float slice_len_samples, int slice_index) {
    // Task 7 completes freeze behavior. For now just store the state so the
    // interface is stable; unfrozen (normal) behavior is unaffected.
    frozen_ = frozen;
    int decimation = buf_ ? std::max(1, buf_->decimation_factor()) : 1;
    slice_len_frames_ = std::max(1.f, slice_len_samples / static_cast<float>(decimation));
    slice_start_ = slice_len_frames_ * static_cast<float>(slice_index);
    slice_phase_ = 0.f;
    if (buf_) frozen_anchor_ = static_cast<float>(buf_->write_head());
}

StereoFrame EchoEngine::ReadWet() {
    if (!buf_) return StereoFrame{0.f, 0.f};

    int decimation = std::max(1, buf_->decimation_factor());
    float size_f = static_cast<float>(buf_->size());

    // write_pos_continuous = write_head + decimation_counter/decimation,
    // approximated by an internally-tracked accumulator that re-syncs to
    // write_head() at block boundaries (SetTargets) and advances by
    // 1/decimation per ReadWet() call. Use the pre-increment value: at the
    // top of this sample's processing, the buffer has not yet been written
    // to for this sample (the processor calls ReadWet() before Write()).
    float write_pos_continuous = read_subsample_;
    read_subsample_ += 1.f / static_cast<float>(decimation);

    StereoFrame wet{0.f, 0.f};
    float delay_used = delay_frames_;

    if (mode_ == TimeChangeMode::kTape) {
        delay_frames_ += slew_coeff_ * (target_frames_ - delay_frames_);
        delay_used = delay_frames_;

        float read_pos = WrapPosition(write_pos_continuous - delay_frames_, size_f);
        float l, r;
        buf_->ReadHermiteStereoFast(read_pos, &l, &r);
        wet.l = l;
        wet.r = r;
    } else {  // kCrossfade
        if (fade_pos_ < 1.f) {
            fade_pos_ += fade_step_;
            if (fade_pos_ > 1.f) fade_pos_ = 1.f;
        }
        float t = fade_pos_;
        float g_old = 1.f - t;
        float g_new = t;

        float pos_old = WrapPosition(write_pos_continuous - fade_from_frames_, size_f);
        float pos_new = WrapPosition(write_pos_continuous - target_frames_, size_f);
        float lo, ro, ln, rn;
        buf_->ReadHermiteStereoFast(pos_old, &lo, &ro);
        buf_->ReadHermiteStereoFast(pos_new, &ln, &rn);

        wet.l = g_old * lo + g_new * ln;
        wet.r = g_old * ro + g_new * rn;
        delay_frames_ = g_old * fade_from_frames_ + g_new * target_frames_;
        delay_used = delay_frames_;

        if (fade_pos_ >= 1.f && queued_target_ >= 0.f) {
            fade_from_frames_ = target_frames_;
            target_frames_ = queued_target_;
            queued_target_ = -1.f;
            fade_pos_ = 0.f;
        }
    }

    if (multi_tap_) {
        // The golden-ratio tap is the prominent CW-side voice (full gain);
        // the originally-targeted delay becomes the secondary, quieter one
        // (kTap2Gain) alongside it. Snapped to the nearest frame: it's a
        // coarse texture tap, and snapping avoids a sub-sample Hermite read
        // of an otherwise-narrowband signal losing most of its amplitude to
        // interpolation rolloff at an arbitrary (golden-ratio-derived) frac.
        float tap2_pos = WrapPosition(std::round(write_pos_continuous - delay_used * kTap2Ratio), size_f);
        float l2, r2;
        buf_->ReadHermiteStereoFast(tap2_pos, &l2, &r2);
        wet.l = kTap2Gain * wet.l + l2;
        wet.r = kTap2Gain * wet.r + r2;
    }

    return wet;
}

float EchoEngine::CurrentDelaySamples() const {
    int decimation = buf_ ? std::max(1, buf_->decimation_factor()) : 1;
    return std::max(0.f, delay_frames_) * static_cast<float>(decimation);
}

}  // namespace beadsdelay_dsp

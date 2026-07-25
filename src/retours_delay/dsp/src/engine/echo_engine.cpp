#include "echo_engine.h"
#include <algorithm>
#include <cmath>

namespace retours_delay_dsp {

namespace {

// Wrap a position (in frames) into [0, size) — required precondition for
// RecordingBuffer::ReadHermiteStereoFast.
inline float WrapPosition(float position, float size_f) {
    if (size_f <= 0.f || !std::isfinite(position)) return 0.f;
    position = std::fmod(position, size_f);
    if (position < 0.f) position += size_f;
    return position;
}

// Wrap for positions already within (-size, 2*size): one conditional each
// way, exactly equal to fmod there (Sterbenz: x-s exact for s<=x<=2s).
// ReadWet's positions are bounded by construction (read_subsample_ resyncs
// to write_head() < size every block and advances <= 64/decimation; delay
// <= size-1) -- see ReadWet call sites for the per-site bound. The general
// WrapPosition stays for the block-rate NotifyFreeze caller.
//
// The frozen-path call (pos_main = slice_start_pos_ + slice_phase_) is the
// tightest of these: slice_len_frames_ can equal size_f exactly (no margin),
// so pos_main's upper bound of 2*size_f is only as good as the invariant
// that the host-sample slice length passed into NotifyFreeze never exceeds
// the buffer's live duration (BaseTimeControl clamps base_samples to
// buffer_samples_, and buffer_samples_ is kept in sync with size()*
// decimation_factor() on every quality change -- see SetBufferSeconds's
// caller in retours_processor.cpp). If that invariant were ever violated,
// slice_len_frames_ > size_f and this bound (and WrapBounded's single
// conditional) would no longer hold.
//
// Found by review + stress testing: the NON-frozen call sites (tape read,
// crossfade old/new, multi-tap) read size_f fresh every call, but combine it
// with delay_frames_/target_frames_/fade_from_frames_/read_subsample_, which
// are only refreshed once per BLOCK (SetTargets, at the top of
// RetoursProcessor::ProcessBlock). A quality-mode change's apply point
// (retours_processor.cpp's kFadeOut case) calls recording_buffer.Configure()
// -- which can change size()/decimation_factor() -- from INSIDE that same
// block's per-sample loop, at whatever sample index the fade-out counter
// reaches zero, not just at the block boundary. For the rest of that block
// (until the next SetTargets() resync), size_f here reflects the NEW buffer
// while delay_frames_ etc. are still clamped against the OLD one, which can
// push the position outside (-size_f, 2*size_f) -- the single-conditional
// wrap no longer fully corrects it, producing an out-of-[0, size_f) result
// that sends the bounds-unchecked ReadHermiteStereoFast out of bounds
// (reproduced as a SIGSEGV via tests/retours_delay_dsp/test_hardening.cpp's
// freeze/quality-churn corner-stress test). This can't happen to the frozen
// call site above: quality transitions are blocked outright while frozen
// (retours_processor.cpp gates the apply point on `!params.freeze &&
// !freeze_falling_edge`, and aborts back to kFadeIn if freeze re-engages
// mid-fade-out).
//
// Fall back to the always-correct WrapPosition rather than re-deriving a
// tighter bound proof against a moving buffer size mid-block. The re-check
// below only ever fires when the two conditionals above failed to land in
// [0, size_f) -- i.e. only in the rare cross-block state mismatch above --
// so it costs two cheap, well-predicted (never-taken in the fast path)
// comparisons and never touches the result for a genuinely in-domain input
// (unlike clamping, which would also silently perturb any in-domain value
// that happens to land within one ULP of size_f -- caught here by
// pin_multi_tap's hash moving during this fix's own verification when an
// earlier, clamp-based version of this safety net was tried; the fallback
// below reproduces that scenario's pre-fix hash exactly). The result in the
// rare mismatch window is a wrong-but-harmless sample (same as this
// function's job everywhere else in the rare/edge case), not a crash.
inline float WrapBounded(float p, float size_f) {
    if (p >= size_f) p -= size_f;
    else if (p < 0.f) p += size_f;
    if (p < 0.f || p >= size_f) p = WrapPosition(p, size_f);
    return p;
}

// Frames of linear crossfade applied at the tail of a frozen slice, toward
// the sample at the slice's start, so the loop-point wrap doesn't click.
constexpr float kSeamCrossfadeFrames = 64.f;

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
    read_rate_scale_ = 1.f;
    // Matches decimation 1 (the initial Configure() quality mode, kBrightDigital
    // -- see retours_processor.cpp) in case ReadWet() is ever called before the
    // first SetTargets(); SetTargets() (always called block-rate before ReadWet
    // in normal use) refreshes this from the buffer's actual decimation factor.
    inv_decimation_ = 1.f;
    slice_start_pos_ = 0.f;
    slice_fade_len_ = kSeamCrossfadeFrames;
    inv_slice_fade_len_ = 1.f / kSeamCrossfadeFrames;
    seam_l_ = 0.f;
    seam_r_ = 0.f;
}

void EchoEngine::SetReadRateScale(float scale) {
    read_rate_scale_ = (std::isfinite(scale) && scale > 0.f) ? scale : 1.f;
}

void EchoEngine::SetTargets(float delay_samples, bool multi_tap,
                            TimeChangeMode mode, float slew_seconds) {
    if (!buf_) return;

    int decimation = std::max(1, buf_->decimation_factor());
    inv_decimation_ = 1.f / static_cast<float>(decimation);  // exact for 1, 2
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

    if (delay_frames_ < 0.f || !std::isfinite(delay_frames_)) {
        // First-ever target: snap instead of gliding from the placeholder.
        // Also snaps out of a NaN-poisoned state: `delay_frames_ < 0.f` is
        // false for NaN (every comparison with NaN is false), so without the
        // explicit isfinite() check a single glitched target would leave
        // delay_frames_ NaN forever (the tape-mode slew below is
        // `delay_frames_ += slew_coeff_ * (target - delay_frames_)`, which
        // never recovers once NaN). Belt-and-braces: SetParameters() sanitizes
        // the upstream params NaN can enter from, so `frames` here should
        // already be finite in practice.
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
    if (!buf_) {
        frozen_ = frozen;
        return;
    }

    int decimation = std::max(1, buf_->decimation_factor());
    float size_f = static_cast<float>(buf_->size());
    int k = std::max(0, slice_index);

    float new_slice_len = slice_len_samples / static_cast<float>(decimation);
    if (!std::isfinite(new_slice_len) || new_slice_len < 1.f) new_slice_len = 1.f;

    bool was_frozen = frozen_;

    // Refresh the ReadWet frozen-branch hoists: the wrapped start position,
    // the seam crossfade length (and its reciprocal), and the Hermite read AT
    // that start position. This runs unconditionally in both branches below
    // that keep frozen_ true (rising edge, and "still frozen" -- the latter
    // taken every block for as long as freeze holds, since NotifyFreeze is
    // called every block regardless of whether TIME/DENSITY actually moved;
    // see retours_processor.cpp's "FREEZE: called every block" comment at its
    // call site). RecordingBuffer::Write() is skipped by the caller for the
    // whole freeze duration in the normal case, but ClearBuffer()'s
    // ImmediateClear() is NOT gated on freeze and can still mutate the buffer
    // out from under a frozen slice. That's fine here specifically because
    // this cache doesn't need content to be provably static -- it only needs
    // staleness bounded to at most one block (<=64 samples), and refreshing
    // unconditionally every block while frozen (regardless of whether
    // slice_start_ moved) provides exactly that bound.
    auto refresh_seam_cache = [&]() {
        slice_start_pos_ = slice_start_;  // already wrapped into [0, size_f)
        slice_fade_len_ = std::min(kSeamCrossfadeFrames, slice_len_frames_ * 0.5f);
        inv_slice_fade_len_ = (slice_fade_len_ > 0.f) ? 1.f / slice_fade_len_ : 0.f;
        buf_->ReadHermiteStereoFast(slice_start_pos_, &seam_l_, &seam_r_);
    };

    if (frozen && !was_frozen) {
        // Rising edge: anchor to the current write head (buffer frames,
        // frozen for the duration) and start the slice read fresh from
        // phase 0. buf_->NotifyFreeze(true) declicks the write seam so the
        // now-static write head doesn't leave a hard edge for playback to
        // cross every time the slice loops past it. The seam cache is
        // refreshed AFTER that declick so it reflects the post-fade content.
        frozen_anchor_ = static_cast<float>(buf_->write_head());
        slice_len_frames_ = new_slice_len;
        slice_start_ = WrapPosition(
            frozen_anchor_ - static_cast<float>(k + 1) * slice_len_frames_, size_f);
        slice_phase_ = 0.f;
        buf_->NotifyFreeze(true);
        refresh_seam_cache();
    } else if (frozen && was_frozen) {
        // Still frozen: TIME (slice index) or DENSITY (slice length) may
        // have changed live. Re-anchor the slice window and re-clamp phase
        // so a shrinking slice doesn't leave it out of range.
        slice_len_frames_ = new_slice_len;
        slice_start_ = WrapPosition(
            frozen_anchor_ - static_cast<float>(k + 1) * slice_len_frames_, size_f);
        if (slice_phase_ < 0.f || slice_phase_ >= slice_len_frames_) {
            slice_phase_ = std::fmod(slice_phase_, slice_len_frames_);
            if (slice_phase_ < 0.f) slice_phase_ += slice_len_frames_;
        }
        refresh_seam_cache();
    } else if (!frozen && was_frozen) {
        // Falling edge: derive the delay-frame offset that reproduces the
        // last frozen read position under the normal read formula
        // (read_pos = write_pos_continuous - delay_frames_), so the normal
        // tape/crossfade path resumes with no jump and then slews/crossfades
        // away from there toward the live TIME target on its own.
        float frozen_read_pos = WrapPosition(slice_start_ + slice_phase_, size_f);
        // Wrap into [0, size): a high slice index can anchor slice_start_
        // just past the write head (slice_count*base sits just under the
        // buffer duration, so it wraps to a small positive offset ahead of
        // the anchor rather than behind it), making the raw difference
        // negative even though it's a perfectly valid delay once wrapped.
        // Left unwrapped, that negative value satisfies SetTargets()'s
        // "delay_frames_ < 0.f" first-target sentinel on the very next
        // block, snapping straight to the live target instead of slewing
        // away from here.
        float equiv_delay = WrapPosition(read_subsample_ - frozen_read_pos, size_f);
        if (mode_ == TimeChangeMode::kTape) {
            delay_frames_ = equiv_delay;
        } else {  // kCrossfade: fade from the frozen-equivalent offset to
                   // whatever target SetTargets already resolved this block.
            fade_from_frames_ = equiv_delay;
            fade_pos_ = 0.f;
            queued_target_ = -1.f;
        }
        buf_->NotifyFreeze(false);
    }
    // (!frozen && !was_frozen): steady-state unfrozen, nothing to do.

    frozen_ = frozen;
}

StereoFrame EchoEngine::ReadWet() {
    if (!buf_) return StereoFrame{0.f, 0.f};

    float size_f = static_cast<float>(buf_->size());

    // write_pos_continuous = write_head + decimation_counter/decimation,
    // approximated by an internally-tracked accumulator that re-syncs to
    // write_head() at block boundaries (SetTargets) and advances by
    // 1/decimation per ReadWet() call. Use the pre-increment value: at the
    // top of this sample's processing, the buffer has not yet been written
    // to for this sample (the processor calls ReadWet() before Write()).
    // read_subsample_ resyncs to write_head() (in [0, size_f)) every block
    // (<=64 samples) and advances by read_rate_scale_*inv_decimation_ <= ~1
    // per sample, so it stays within [0, size_f + 64) here.
    float write_pos_continuous = read_subsample_;
    read_subsample_ += read_rate_scale_ * inv_decimation_;

    if (frozen_) {
        slice_phase_ += read_rate_scale_ * inv_decimation_;
        if (slice_phase_ < 0.f || slice_phase_ >= slice_len_frames_) {
            slice_phase_ = std::fmod(slice_phase_, slice_len_frames_);
            if (slice_phase_ < 0.f) slice_phase_ += slice_len_frames_;
        }

        // pos_main = slice_start_pos_ (in [0, size_f)) + slice_phase_ (in
        // [0, slice_len_frames_), and slice_len_frames_ <= size_f since the
        // host-sample slice length is clamped to the buffer duration in
        // BaseTimeControl::Update before reaching NotifyFreeze) is in
        // [0, 2*size_f): WrapBounded's single-branch range.
        float pos_main = WrapBounded(slice_start_pos_ + slice_phase_, size_f);
        float l, r;
        buf_->ReadHermiteStereoFast(pos_main, &l, &r);

        // Seam declick: crossfade the tail of the slice toward the (fixed)
        // sample at the slice start, so the loop-point wrap doesn't click.
        // slice_fade_len_/inv_slice_fade_len_/the slice-start read (seam_l_/
        // seam_r_) are hoisted into NotifyFreeze -- see refresh_seam_cache
        // there for why the cached read stays valid for the whole freeze.
        if (slice_fade_len_ > 0.f && slice_phase_ >= slice_len_frames_ - slice_fade_len_) {
            float w = (slice_phase_ - (slice_len_frames_ - slice_fade_len_)) * inv_slice_fade_len_;
            w = std::clamp(w, 0.f, 1.f);
            l = (1.f - w) * l + w * seam_l_;
            r = (1.f - w) * r + w * seam_r_;
        }

        return StereoFrame{l, r};
    }

    StereoFrame wet{0.f, 0.f};
    float delay_used = delay_frames_;

    if (mode_ == TimeChangeMode::kTape) {
        delay_frames_ += slew_coeff_ * (target_frames_ - delay_frames_);
        delay_used = delay_frames_;

        // write_pos_continuous in [0, size_f+64); delay_frames_ clamped to
        // [0, size_f-1] in SetTargets, so read_pos in (-(size_f-1), size_f+64)
        // -- within WrapBounded's (-size_f, 2*size_f) range.
        float read_pos = WrapBounded(write_pos_continuous - delay_frames_, size_f);
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

        // Same bound as the tape read_pos above: fade_from_frames_ and
        // target_frames_ are both frames-clamped-to-[0,size_f-1] values
        // (assigned from SetTargets()'s `frames`, or from each other).
        float pos_old = WrapBounded(write_pos_continuous - fade_from_frames_, size_f);
        float pos_new = WrapBounded(write_pos_continuous - target_frames_, size_f);
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
        // Additional golden-ratio tap at kTap2Gain, summed under the main
        // (full-gain) tap. Its read position is snapped to the nearest frame:
        // it's a coarse texture tap, and snapping avoids a sub-sample Hermite
        // read losing amplitude to interpolation rolloff at an arbitrary
        // (golden-ratio-derived) fractional offset.
        //
        // Wrap first (write_pos_continuous - delay_used*kTap2Ratio bounded the
        // same way as the tape read_pos above, since delay_used is delay_frames_
        // taken before or after this block's slew step -- either way within
        // [0, size_f-1] -- and kTap2Ratio < 1 only shrinks the subtracted
        // term), then snap to the nearest integer frame without libm: adding
        // 0.5 and truncating is round-half-up for t2 >= 0 (guaranteed by the
        // wrap), differing from std::round only on an exact-tie fraction
        // (float noise on this golden-ratio-derived, coarse-texture tap).
        float t2 = WrapBounded(write_pos_continuous - delay_used * kTap2Ratio, size_f);
        float tap2_pos = static_cast<float>(static_cast<int>(t2 + 0.5f));  // t2 >= 0
        if (tap2_pos >= size_f) tap2_pos -= size_f;   // snap can land exactly on size
        float l2, r2;
        buf_->ReadHermiteStereoFast(tap2_pos, &l2, &r2);
        wet.l += kTap2Gain * l2;
        wet.r += kTap2Gain * r2;
    }

    return wet;
}

float EchoEngine::CurrentDelaySamples() const {
    int decimation = buf_ ? std::max(1, buf_->decimation_factor()) : 1;
    return std::max(0.f, delay_frames_) * static_cast<float>(decimation);
}

}  // namespace retours_delay_dsp

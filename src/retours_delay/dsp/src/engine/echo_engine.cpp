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
// Found by review + stress testing -- root cause is SLEWED/LATCHED state
// surviving a quality-mode SIZE SHRINK, not a mid-block Configure() race:
// a quality change (e.g. Scorched, 768012 frames, -> Bright, 192000 frames)
// calls recording_buffer.Configure(), which changes size()/decimation_factor()
// immediately. SetTargets() re-clamps this block's fresh `frames`/
// target_frames_ against the new (smaller) size_f right away -- but the
// NON-frozen call sites don't read target_frames_ directly, they read the
// slewed/latched state that decays TOWARD it over time: delay_frames_ in
// tape mode (only advanced per-sample by `delay_frames_ += slew_coeff_ *
// (target_frames_ - delay_frames_)`, never re-clamped by SetTargets itself),
// and fade_from_frames_/target_frames_ in crossfade mode while a fade is
// already in progress (a shrink mid-fade queues the new target instead of
// applying it -- see SetTargets' `queued_target_` branch -- so the stale,
// now-oversized value persists until the CURRENT fade completes). Either
// way, that latched value can keep holding the old, larger buffer's
// magnitude for the entire slew/fade decay -- observed empirically at 65+
// blocks post-Configure, with delay_frames_ ~= 292057 against a size_f of
// 192000 -- not just until the next SetTargets() call (SetTargets runs every
// block regardless and does not shorten this). This is a real duration (tens
// of ms to whole seconds for a slow slew_seconds), during which
// write_pos_continuous - delay_frames_ can land far outside (-size_f,
// 2*size_f): the single-conditional wrap no longer fully corrects it,
// producing an out-of-[0, size_f) result that sends the bounds-unchecked
// ReadHermiteStereoFast out of bounds (reproduced as a SIGSEGV via
// tests/retours_delay_dsp/test_hardening.cpp's freeze/quality-churn
// corner-stress test). Note this exposure exists independent of WHERE within
// a block Configure() happens to land -- it would exist even if the apply
// point were moved to an exact block boundary, because the latched state
// only unwinds via the slew/fade process, not via any per-block resync; the
// fallback below must stay regardless of that timing. This can't happen to
// the frozen call site above: quality transitions are blocked outright while
// frozen (retours_processor.cpp gates the apply point on `!params.freeze &&
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
//
// The re-check is written as `!(p >= 0.f && p < size_f)`, not
// `p < 0.f || p >= size_f`: those are equivalent for finite p, but a NaN p
// (e.g. from a stale latched value poisoned upstream) fails BOTH direct
// comparisons, so the "or" form would skip the fallback and let a NaN reach
// the int cast in ReadWet's callers. The negated-and form routes any NaN/Inf
// into WrapPosition, whose own isfinite guard returns a safe 0.f.
inline float WrapBounded(float p, float size_f) {
    if (p >= size_f) p -= size_f;
    else if (p < 0.f) p += size_f;
    if (!(p >= 0.f && p < size_f)) p = WrapPosition(p, size_f);
    return p;
}

// Frames of linear crossfade applied at the tail of a frozen slice, toward
// the sample at the slice's start, so the loop-point wrap doesn't click.
constexpr float kSeamCrossfadeFrames = 64.f;

// Correlation-aligned splice -- the fix for garbled Interval sweeps in
// Crossfade mode. A fade blends the tap at `cur` with the tap at `want`; on a
// knob sweep those are hundreds of milliseconds apart in the buffer, so their
// phase relationship is random and the blend comes out as a chirpy, partly
// cancelling mess. This nudges `want` by up to +/-kAlignSearchFrames (~2 ms,
// inaudible as a delay-time error) to the offset whose recent buffer history
// best cross-correlates with the old tap's, so the two copies line up in phase
// and the blend is coherent.
//
// Measured (inharmonic residual over a 220 Hz burst train, see
// .superpowers/sdd/crossfade-variants-report.md): 5-8 dB cleaner mean and
// 2-10 dB cleaner worst-case (p95) on every single-tap crossfade sweep, at
// unchanged responsiveness -- the delay still reaches the knob's target within
// one fade cycle, because this changes only WHERE the fade lands, never how
// many fades it takes to get there.
//
// `write_pos` is the continuous write position; `cur`/`want` are delays in
// frames. Returns the nudged delay, clamped to [0, max_frames].
inline float AlignedFadeTarget(const particules_dsp::RecordingBuffer* buf,
                               float write_pos, float cur, float want,
                               float max_frames) {
    if (!buf) return want;
    float size_f = static_cast<float>(buf->size());
    if (size_f <= static_cast<float>(kAlignWindowFrames + kAlignSearchFrames + 4)) return want;
    if (!std::isfinite(cur) || cur <= 0.f || !std::isfinite(want) || !std::isfinite(write_pos)) {
        return want;
    }

    // Search radius, the smallest of three caps:
    //  * absolute (kAlignSearchFrames ~ 2 ms at 48 kHz, decimation 1) -- the
    //    inaudibility budget for landing off the requested delay;
    //  * relative to the SHORTER of the two delays being blended
    //    (kAlignSearchMaxFraction) -- a 4 ms delay is a tuned comb that a
    //    fixed 2 ms of slack would detune audibly, while 2 ms out of 1.8 s is
    //    nothing. Keyed on min(cur, want) because the nudge has to be
    //    inaudible against BOTH taps in the blend, and because measurement
    //    preferred it: keying on `want` alone let the aligner engage on jumps
    //    up out of very short delays, which came out 0.8-3.5 dB dirtier;
    //  * relative to the MOVE (kAlignMoveFraction * |want - cur|) -- the best
    //    correlation for a move smaller than the radius is always the one that
    //    cancels the move outright (identical content correlates perfectly),
    //    so without this cap small knob nudges would land nowhere. Capping the
    //    nudge at a fraction of the move guarantees the delay always travels
    //    at least (1 - kAlignMoveFraction) of the distance asked for.
    float radius_f = std::min(static_cast<float>(kAlignSearchFrames),
                              kAlignSearchMaxFraction * std::min(cur, want));
    radius_f = std::min(radius_f, kAlignMoveFraction * std::fabs(want - cur));
    int radius = static_cast<int>(radius_f);
    radius -= radius % kAlignSearchStride;
    // Too little slack to find a real match: don't perturb the delay at all.
    if (radius < kAlignMinRadiusFrames) return want;

    auto ctx = buf->MakeReadContext();
    int size = static_cast<int>(ctx.size);
    int base_old = static_cast<int>(WrapPosition(write_pos - cur, size_f));
    int base_new = static_cast<int>(WrapPosition(write_pos - want, size_f));
    if (base_old < 0 || base_old >= size || base_new < 0 || base_new >= size) return want;

    // One conditional is enough to wrap every window index: the guard above
    // established size_f > kAlignWindowFrames + kAlignSearchFrames + 4, and
    // the furthest an index reaches back is (kAlignWindowTaps - 1) *
    // kAlignWindowStride + radius < kAlignWindowFrames + kAlignSearchFrames,
    // so base - k*stride can never fall below -size.
    float ref[kAlignWindowTaps];
    for (int k = 0; k < kAlignWindowTaps; ++k) {
        int idx = base_old - k * kAlignWindowStride;
        if (idx < 0) idx += size;
        ref[k] = particules_dsp::RecordingBuffer::MonoSampleAt(ctx, static_cast<size_t>(idx));
    }

    // Unnormalized cross-correlation is enough here: the search spans only
    // ~2 ms, over which signal level is essentially flat, so there is no
    // loud-region bias to divide out (and no per-candidate divide to pay).
    auto score_at = [&](int delta) {
        int base = base_new - delta;   // +delta = longer delay = earlier frame
        if (base < 0) base += size;
        else if (base >= size) base -= size;
        float sum = 0.f;
        for (int k = 0; k < kAlignWindowTaps; ++k) {
            int idx = base - k * kAlignWindowStride;
            if (idx < 0) idx += size;
            sum += ref[k] * particules_dsp::RecordingBuffer::MonoSampleAt(
                                ctx, static_cast<size_t>(idx));
        }
        return sum;
    };

    int best_delta = 0;
    float best = -1e30f;
    for (int delta = -radius; delta <= radius; delta += kAlignSearchStride) {
        float s = score_at(delta);
        if (s > best) { best = s; best_delta = delta; }
    }
    // Refine to whole-frame resolution around the coarse winner.
    for (int delta = best_delta - kAlignRefineRadius;
         delta <= best_delta + kAlignRefineRadius; ++delta) {
        if (delta < -radius || delta > radius || delta == best_delta) continue;
        float s = score_at(delta);
        if (s > best) { best = s; best_delta = delta; }
    }
    if (best <= 0.f) return want;   // no positive-correlation match: don't nudge

    return std::clamp(want + static_cast<float>(best_delta), 0.f, max_frames);
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
    cached_slew_s_ = -1.f;
    cached_slew_sr_ = -1.f;
    multi_tap_ = false;
    mode_ = TimeChangeMode::kTape;
    fade_from_frames_ = 0.f;
    fade_pos_ = 1.f;  // no fade in progress
    fade_step_ = 1.f / static_cast<float>(kJumpCrossfadeFrames);
    queued_target_ = -1.f;
    requested_frames_ = -1.f;
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
    // Cache: exp() is a transcendental call, and (slew_s, sample_rate_) is
    // usually unchanged block to block (see cached_slew_s_'s comment in
    // echo_engine.h). Recompute only when either input actually moved.
    if (slew_s != cached_slew_s_ || sample_rate_ != cached_slew_sr_) {
        cached_slew_s_ = slew_s;
        cached_slew_sr_ = sample_rate_;
        slew_coeff_ = 1.f - std::exp(-1.f / (slew_s * sample_rate_));
    }

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
        requested_frames_ = frames;
    }

    if (mode_ == TimeChangeMode::kTape) {
        target_frames_ = frames;
        requested_frames_ = frames;
        // Crossfade state stays "settled" so a later switch into kCrossfade
        // mode starts clean (no stale fade-in-progress).
        fade_pos_ = 1.f;
        queued_target_ = -1.f;
    } else {  // kCrossfade
        bool fading = fade_pos_ < 1.f;
        // Trigger off the RAW request rather than target_frames_: splice
        // alignment leaves target_frames_ up to kAlignSearchFrames away from
        // what was asked for, and comparing against that would restart a fade
        // every block even with a perfectly static knob.
        bool changed = (frames != requested_frames_);
        requested_frames_ = frames;
        if (!fading) {
            if (changed) {
                float want = frames;
                // write_head() is the write position at this block boundary --
                // the same reference ReadWet's read positions are taken from
                // (read_subsample_ is re-synced to it at the end of this
                // function), and unaffected by the wow/flutter drift that
                // read_subsample_ accumulates within a block.
                float landed = AlignedFadeTarget(buf_,
                                                 static_cast<float>(buf_->write_head()),
                                                 delay_frames_, want, max_frames);
                // Hardening: re-clamp both ends of the fade to the LIVE
                // buffer range. target_frames_ can be stale-oversized here
                // (a quality-mode size shrink mid-fade queues the new target
                // instead of applying it -- see the queued_target_ branch
                // below -- so the old, larger buffer's magnitude can survive
                // into this fade), and it becomes this fade's start point.
                fade_from_frames_ = std::clamp(target_frames_, 0.f, max_frames);
                target_frames_ = std::clamp(landed, 0.f, max_frames);
                fade_pos_ = 0.f;
            }
        } else if (changed) {
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
            // Same fade-start step as SetTargets' idle->fade path above:
            // `target_frames_` here is the just-completed fade's destination,
            // i.e. the current effective delay at this dequeue instant
            // (delay_frames_ also equals it exactly at fade_pos_ == 1, see the
            // fade math above).
            //
            // Hardening, same reason as the SetTargets site: re-clamp against
            // the LIVE buffer size, which a quality-mode shrink can have
            // reduced under a stale target_frames_.
            float max_frames = (size_f > 1.f) ? size_f - 1.f : 0.f;
            float cur = std::clamp(target_frames_, 0.f, max_frames);
            float want = queued_target_;
            float landed = AlignedFadeTarget(buf_, write_pos_continuous,
                                             cur, want, max_frames);
            fade_from_frames_ = cur;
            target_frames_ = std::clamp(landed, 0.f, max_frames);
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

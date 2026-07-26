#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include "../../include/particules_dsp/types.h"
#include "../util/interpolation.h"
#include "sample_codec.h"

namespace particules_dsp {

// Circular recording buffer with interpolated reads, storing 1 or 2
// interleaved channels in one of three packed formats (StorageFormat).
//
// The three format pointers (f32_/i16_/u8_) all alias the same externally
// allocated byte pool; only one is live at a time, selected by format_.
// Packing samples at reduced width lets the same pool hold more frames,
// decoupling recording rate from buffer length -- see Configure().
//
// An extra tail of kInterpolationTail frames is appended after the main
// region and mirrors the first frames (in the STORED representation) so
// Hermite interpolation at the wrap boundary needs no special-casing.
//
// Memory is externally allocated. Call RequiredBytes() to learn the size,
// allocate, then pass the pointer to Init().
class RecordingBuffer {
public:
    void Init(float* buffer, size_t num_frames, int num_channels = 2);

    // Reconfigure decimation, storage format, and channel count (1 = mono,
    // 2 = stereo). Frame count is derived from the byte pool fixed at Init,
    // optionally capped by max_bytes (0 = full pool). Resets the write head
    // and decimation counter when the layout actually changes; a no-op
    // reconfigure (same format, channels, and frame count) leaves state
    // untouched. If the layout changed, the pool's existing bytes are
    // garbage under the new interpretation: the caller MUST follow with
    // Clear() and keep wet output muted until the clear completes.
    void Configure(int decimation_factor, StorageFormat format, int channels,
                   size_t max_bytes = 0);

    StorageFormat format() const { return format_; }
    int channels() const { return channels_; }
    size_t capacity_bytes() const { return capacity_bytes_; }

    static size_t FramesForConfig(size_t capacity_bytes, int channels,
                                  StorageFormat format, size_t max_bytes);

    // Write one frame and advance the write head. In mono, the stored sample
    // is the average (l+r)*0.5f and every reader duplicates it to both
    // outputs.
    void Write(float left, float right);
    void Write(const StereoFrame& frame);

    // Read a single channel with Hermite cubic interpolation.
    // |position| is in frames (0 .. size_-1), fractional part drives
    // the interpolation.
    // Test-only reference implementation; production uses
    // ReadHermiteStereoFast.
    float ReadHermite(int channel, float position) const;

    // Read both channels with Hermite interpolation in a single call.
    // Computes indices once for both channels (saves redundant wrapping
    // and index arithmetic compared to two separate ReadHermite calls).
    // Test-only reference implementation; production uses
    // ReadHermiteStereoFast.
    void ReadHermiteStereo(float position, float* out_l, float* out_r) const;

    // Read a single channel with linear interpolation (cheaper).
    // Test-only reference implementation; production uses
    // ReadHermiteStereoFast.
    float ReadLinear(int channel, float position) const;

    // Block-lifetime read context: resolves format_/channels_/data pointers/
    // mu-law LUT ONCE, into plain local values that live outside *this*.
    // The per-sample hot read (ReadHermiteStereoFrac's ctx overload below)
    // then dereferences only fields of a caller-held local ReadContext
    // rather than `this->format_` etc. on every grain-sample -- the compiler
    // cannot prove those member loads survive across the interleaved
    // `output[i].l/.r +=` accumulation stores (different aliasing class, no
    // visibility into RecordingBuffer's internals), so without this it
    // reloads and re-switches on every single sample. A plain stack-local
    // ReadContext has no such aliasing hazard, so its fields stay in
    // registers for the whole grain-major render loop.
    //
    // POINTERS AND LAYOUT ARE BLOCK-LIFETIME ONLY: Configure() or Init() can
    // change format_/channels_/size_ (a full re-layout of the pool), which
    // invalidates every field of a previously-resolved context. Write(),
    // Clear()/ImmediateClear(), and SetDecimationFactor() do NOT touch any
    // field a context holds -- they only change buffer content (or, for
    // SetDecimationFactor, unrelated counters) under the SAME layout, so
    // new writes are visible through an already-resolved context's pointers
    // with no re-resolution needed. Callers (GrainEngine) still call
    // MakeReadContext() once per render block, before any grain in that
    // block is processed, and don't retain the context across blocks --
    // that's a block-lifetime discipline for clarity/consistency, not a
    // requirement forced by Write()/Clear()/SetDecimationFactor.
    struct ReadContext {
        int channels;
        StorageFormat format;
        const float* f32;
        const int16_t* i16;
        const uint8_t* u8;
        const float* mulaw_lut;
        size_t size;                 // frame count, for the i0==0 wrap
    };

    ReadContext MakeReadContext() const {
        return ReadContext{channels_, format_, f32_, i16_, u8_,
                            mulaw_lut_, size_};
    }

    // Context-resolved variant of ReadHermiteStereoFrac: identical math to
    // the single-call overload below, but every format/channel/pointer
    // value comes from `ctx` (resolved once per block by MakeReadContext)
    // instead of being re-derived from `this` on every call. Static (no
    // buffer instance needed) so callers holding only a ctx -- e.g.
    // Grain::Process, which no longer keeps a RecordingBuffer reference at
    // all -- can call it directly. Preconditions unchanged: i0 < ctx.size,
    // 0 <= frac < 1.
    //
    // Forced always-inline: the delegating legacy overload below (the only
    // production caller of which is Retours' EchoEngine::ReadWet, via
    // ReadHermiteStereoFast, 5 per-sample call sites) calls this out of
    // line otherwise -- neither clang nor gcc will inline this switch-heavy
    // body into that hot path on their own, costing Retours a real desktop
    // regression (materializing this ReadContext on the stack + a call) for
    // zero benefit to it (Retours reads through the legacy signature, not
    // the ctx one -- only Particules' grain loop passes a real block ctx).
    static inline void ReadHermiteStereoFrac(const ReadContext& ctx, size_t i0, float frac,
                                             float* out_l, float* out_r)
        __attribute__((always_inline)) {
        size_t i_1 = (i0 == 0) ? ctx.size - 1 : i0 - 1;
        size_t i1 = i0 + 1;   // tail guarantees valid data
        size_t i2 = i0 + 2;   // tail guarantees valid data
        if (ctx.channels == 2) {
            switch (ctx.format) {
                case StorageFormat::kFloat32: {
                    const float* p_1 = &ctx.f32[i_1 * 2];
                    const float* p0  = &ctx.f32[i0  * 2];
                    const float* p1  = &ctx.f32[i1  * 2];
                    const float* p2  = &ctx.f32[i2  * 2];
                    *out_l = InterpolateHermite(p_1[0], p0[0], p1[0], p2[0], frac);
                    *out_r = InterpolateHermite(p_1[1], p0[1], p1[1], p2[1], frac);
                    return;
                }
                case StorageFormat::kInt12: {
                    const int16_t* p_1 = &ctx.i16[i_1 * 2];
                    const int16_t* p0  = &ctx.i16[i0  * 2];
                    const int16_t* p1  = &ctx.i16[i1  * 2];
                    const int16_t* p2  = &ctx.i16[i2  * 2];
                    constexpr float kS = 1.0f / 2047.0f;
                    *out_l = InterpolateHermite(p_1[0] * kS, p0[0] * kS,
                                                p1[0] * kS, p2[0] * kS, frac);
                    *out_r = InterpolateHermite(p_1[1] * kS, p0[1] * kS,
                                                p1[1] * kS, p2[1] * kS, frac);
                    return;
                }
                case StorageFormat::kMuLaw8: {
                    const uint8_t* p_1 = &ctx.u8[i_1 * 2];
                    const uint8_t* p0  = &ctx.u8[i0  * 2];
                    const uint8_t* p1  = &ctx.u8[i1  * 2];
                    const uint8_t* p2  = &ctx.u8[i2  * 2];
                    const float* lut = ctx.mulaw_lut;
                    *out_l = InterpolateHermite(lut[p_1[0]], lut[p0[0]],
                                                lut[p1[0]], lut[p2[0]], frac);
                    *out_r = InterpolateHermite(lut[p_1[1]], lut[p0[1]],
                                                lut[p1[1]], lut[p2[1]], frac);
                    return;
                }
            }
        }
        // Mono: one interpolation, duplicated to both outputs (cheaper than
        // stereo -- half the loads).
        float m;
        switch (ctx.format) {
            case StorageFormat::kFloat32:
                m = InterpolateHermite(ctx.f32[i_1], ctx.f32[i0], ctx.f32[i1], ctx.f32[i2], frac);
                break;
            case StorageFormat::kInt12: {
                constexpr float kS = 1.0f / 2047.0f;
                m = InterpolateHermite(ctx.i16[i_1] * kS, ctx.i16[i0] * kS,
                                       ctx.i16[i1] * kS, ctx.i16[i2] * kS, frac);
                break;
            }
            case StorageFormat::kMuLaw8:
                m = InterpolateHermite(ctx.mulaw_lut[ctx.u8[i_1]], ctx.mulaw_lut[ctx.u8[i0]],
                                       ctx.mulaw_lut[ctx.u8[i1]], ctx.mulaw_lut[ctx.u8[i2]], frac);
                break;
            default: m = 0.0f; break;
        }
        *out_l = m;
        *out_r = m;
    }

    // Fast interpolated read with a pre-split position (integer frame +
    // fraction). Preconditions: i0 < size(), 0 <= frac < 1. Grains used to
    // call this directly for Q32.32 precision; Grain::Process now goes
    // through the ctx overload above instead (see GrainEngine's per-block
    // ReadContext). This overload's only production caller today is
    // ReadHermiteStereoFast below (i.e. Retours' EchoEngine::ReadWet, 5
    // per-sample call sites); it's also exercised directly by RecordingBuffer
    // unit tests. Delegates to the context overload (forced inline above) so
    // the two can't drift, at no extra cost to this path.
    inline void ReadHermiteStereoFrac(size_t i0, float frac,
                                      float* out_l, float* out_r) const {
        ReadHermiteStereoFrac(MakeReadContext(), i0, frac, out_l, out_r);
    }

    // Nearest-frame, non-interpolated read of the LEFT channel (the mono
    // sample in mono configs) through a resolved ReadContext. Purely
    // additive: no existing reader routes through it, so it cannot perturb
    // any interpolated output. Exists for cheap analysis passes that compare
    // buffer content rather than render it -- today Retours' crossfade
    // splice aligner (echo_engine.cpp), which needs a few hundred raw frames
    // per fade and must not pay Hermite cost per sample. Precondition:
    // frame < ctx.size.
    static inline float MonoSampleAt(const ReadContext& ctx, size_t frame) {
        size_t idx = (ctx.channels == 2) ? frame * 2 : frame;
        switch (ctx.format) {
            case StorageFormat::kFloat32: return ctx.f32[idx];
            case StorageFormat::kInt12:   return ctx.i16[idx] * (1.0f / 2047.0f);
            case StorageFormat::kMuLaw8:  return ctx.mulaw_lut[ctx.u8[idx]];
        }
        return 0.0f;
    }

    // Float-position variant (Retours' EchoEngine; positions re-sync per
    // block there, so float precision suffices). Precondition unchanged:
    // position finite and in [0, size_).
    inline void ReadHermiteStereoFast(float position, float* out_l, float* out_r) const {
        int pos_int = static_cast<int>(position);
        float frac = position - static_cast<float>(pos_int);
        ReadHermiteStereoFrac(static_cast<size_t>(pos_int), frac, out_l, out_r);
    }

    // Freeze-transition declicking. Call on every freeze-state change.
    // Entering freeze: one-shot symmetric fade-to-zero at the write seam
    // (2 × kCrossfadeSamples frames) so looped playback crosses silence
    // instead of a discontinuity.
    // Leaving freeze: arms a write crossfade — the next kCrossfadeSamples
    // accepted writes blend from the old buffer content into the incoming
    // audio, so recorded content transitions smoothly back to live input.
    void NotifyFreeze(bool frozen);

    // Zero the buffer and reset the write head.
    // The memset is deferred — call TickClear() each Process() block to
    // spread the cost. Init() still clears synchronously at startup.
    // The clear extent is computed from the configuration at call time —
    // call Configure() BEFORE Clear() when changing layouts.
    void Clear();

    // Zero the buffer immediately (synchronous memset) without resetting
    // write_head_. Use for user-triggered clears where the delay read position
    // must see silence right away rather than old content from the far end.
    void ImmediateClear();

    // Zero up to max_bytes of the pending clear started by Clear(). Byte-
    // based so wall-clock drain time is identical in every storage config.
    void TickClear(size_t max_bytes);
    // True while a deferred Clear() has not finished draining.
    bool ClearPending() const { return clear_cursor_ < clear_total_; }

    // True until a non-zero sample has been recorded since the last clear /
    // (re)configure / Init. Backs the "Clear buffer" menu grey-out; the flag
    // is set at the write site (any non-silent input) and reset by every
    // clear path, so it self-corrects across quality/channel reformats.
    bool empty() const { return !dirty_; }

    void SetDecimationFactor(int factor);
    int decimation_factor() const { return decimation_factor_; }

    size_t size() const { return size_; }
    size_t write_head() const { return write_head_; }

    // How many bytes the caller must allocate for a buffer of the given
    // sample rate and duration.
    static size_t RequiredBytes(float sample_rate,
                                float duration_seconds,
                                int channels = 2);

private:
    // Shared guard + wrap for the out-of-line readers: rejects an empty
    // buffer and non-finite positions (read yields silence), wraps position
    // into [0, size_) in O(1) via fmod, and splits into integer index +
    // fraction. Tap-index wrapping stays per-reader (Hermite vs linear).
    bool ResolveReadPosition(float position, size_t* i0, float* frac) const {
        if (size_ == 0 || !u8_) return false;
        if (!std::isfinite(position)) return false;
        float size_f = static_cast<float>(size_);
        position = std::fmod(position, size_f);
        if (position < 0.0f) position += size_f;
        int pos_int = static_cast<int>(position);
        *frac = position - static_cast<float>(pos_int);
        *i0 = static_cast<size_t>(pos_int);
        return true;
    }

    size_t bytes_per_sample() const {
        switch (format_) {
            case StorageFormat::kFloat32: return 4;
            case StorageFormat::kInt12:   return 2;
            case StorageFormat::kMuLaw8:  return 1;
        }
        return 4;
    }

    // Format-dispatched single-sample access (non-hot paths). In mono,
    // channel is clamped to 0 by the callers below.
    float SampleAt(size_t frame, int ch) const {
        size_t idx = frame * static_cast<size_t>(channels_) + static_cast<size_t>(ch);
        switch (format_) {
            case StorageFormat::kFloat32: return f32_[idx];
            case StorageFormat::kInt12:   return Int12Decode(i16_[idx]);
            case StorageFormat::kMuLaw8:  return mulaw_lut_[u8_[idx]];
        }
        return 0.0f;
    }
    void StoreSample(size_t frame, int ch, float v) {
        size_t idx = frame * static_cast<size_t>(channels_) + static_cast<size_t>(ch);
        switch (format_) {
            case StorageFormat::kFloat32: f32_[idx] = v; break;
            case StorageFormat::kInt12:   i16_[idx] = Int12Encode(v); break;
            case StorageFormat::kMuLaw8:  u8_[idx] = MuLaw8Encode(v); break;
        }
    }
    // Copy one frame's STORED representation into the tail mirror.
    void CopyFrameToTail(size_t frame) {
        size_t row = static_cast<size_t>(channels_) * bytes_per_sample();
        std::memcpy(u8_ + (size_ + frame) * row, u8_ + frame * row, row);
    }

    float* f32_ = nullptr;      // all three alias the same pool
    int16_t* i16_ = nullptr;
    uint8_t* u8_ = nullptr;
    size_t capacity_bytes_ = 0;
    StorageFormat format_ = StorageFormat::kFloat32;
    const float* mulaw_lut_ = nullptr;

    size_t size_ = 0;           // Number of frames (not samples)
    int channels_ = 2;
    size_t write_head_ = 0;

    // Decimation state (sample-and-hold: keep every Nth sample)
    int decimation_factor_ = 1;
    int decimation_counter_ = 0;

    // Deferred clear state (set by Clear(), drained by TickClear())
    size_t clear_cursor_ = 0;  // next byte to zero
    size_t clear_total_  = 0;  // total bytes to zero (0 = none pending)

    // Content flag: false = known-empty (all silence since last clear).
    bool dirty_ = false;

    // Unfreeze write-crossfade state (counts accepted writes remaining)
    int write_ramp_remaining_ = 0;
    static constexpr int kCrossfadeSamples = 32;
};

} // namespace particules_dsp

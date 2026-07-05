#pragma once

#include <cstddef>
#include <cstring>
#include "../../include/beads/types.h"
#include "../util/interpolation.h"

namespace beads {

// Circular stereo recording buffer with interpolated reads.
//
// Stores interleaved float samples (L, R, L, R, ...) with an extra tail of
// kInterpolationTail frames appended after the main buffer. The tail mirrors
// the first frames so Hermite interpolation at the wrap boundary needs no
// special-casing.
//
// Memory is externally allocated. Call RequiredBytes() to learn the size,
// allocate, then pass the pointer to Init().
class RecordingBuffer {
public:
    void Init(float* buffer, size_t num_frames, int num_channels = 2);

    // Write one frame and advance the write head.
    void Write(float left, float right);
    void Write(const StereoFrame& frame);

    // Read a single channel with Hermite cubic interpolation.
    // |position| is in frames (0 .. size_-1), fractional part drives
    // the interpolation.
    float ReadHermite(int channel, float position) const;

    // Read both channels with Hermite interpolation in a single call.
    // Computes indices once for both channels (saves redundant wrapping
    // and index arithmetic compared to two separate ReadHermite calls).
    void ReadHermiteStereo(float position, float* out_l, float* out_r) const;

    // Read a single channel with linear interpolation (cheaper).
    float ReadLinear(int channel, float position) const;

    // Fast stereo linear interpolation for per-grain hot loops.
    // Precondition: position must be finite and in [0, size_).
    // No guards or wrapping — caller must ensure validity.
    // ~3x cheaper than ReadHermiteStereo (2 loads vs 4, no polynomial).
    inline void ReadLinearStereoFast(float position, float* out_l, float* out_r) const {
        int pos_int = static_cast<int>(position);
        float frac = position - static_cast<float>(pos_int);
        size_t i0 = static_cast<size_t>(pos_int);
        size_t i1 = i0 + 1;  // tail guarantees valid data at size_
        const float* p0 = &buffer_[i0 * channels_];
        const float* p1 = &buffer_[i1 * channels_];
        *out_l = p0[0] + frac * (p1[0] - p0[0]);
        *out_r = p0[1] + frac * (p1[1] - p0[1]);
    }

    // Fast stereo Hermite interpolation for per-grain hot loops.
    // Precondition: position must be finite and in [0, size_).
    // Only i-1 needs wrapping; i0+1 and i0+2 read from the interpolation tail.
    inline void ReadHermiteStereoFast(float position, float* out_l, float* out_r) const {
        int pos_int = static_cast<int>(position);
        float frac = position - static_cast<float>(pos_int);
        size_t i0 = static_cast<size_t>(pos_int);
        size_t i_1 = (i0 == 0) ? size_ - 1 : i0 - 1;  // unsigned: can't subtract 1 from 0
        size_t i1 = i0 + 1;  // tail guarantees valid data
        size_t i2 = i0 + 2;  // tail guarantees valid data
        const float* p_1 = &buffer_[i_1 * channels_];
        const float* p0  = &buffer_[i0  * channels_];
        const float* p1  = &buffer_[i1  * channels_];
        const float* p2  = &buffer_[i2  * channels_];
        *out_l = InterpolateHermite(p_1[0], p0[0], p1[0], p2[0], frac);
        *out_r = InterpolateHermite(p_1[1], p0[1], p1[1], p2[1], frac);
    }

    // Copy a contiguous region of stereo frames from the circular buffer
    // into a linear destination array. Handles wrap-around transparently.
    // start_frame may be negative (wraps to buffer end).
    // dest must hold num_frames * channels_ floats.
    inline void CopyRegionTo(int start_frame, int num_frames_to_copy,
                             float* dest) const {
        int buf_frames = static_cast<int>(size_);
        // Normalize start_frame to [0, buf_frames)
        int src = ((start_frame % buf_frames) + buf_frames) % buf_frames;
        int remaining = num_frames_to_copy;
        float* dst = dest;
        while (remaining > 0) {
            int chunk = remaining;
            if (chunk > buf_frames - src) chunk = buf_frames - src;
            std::memcpy(dst, &buffer_[src * channels_],
                        static_cast<size_t>(chunk * channels_) * sizeof(float));
            dst += chunk * channels_;
            remaining -= chunk;
            src = 0;  // After first chunk, continue from start of buffer
        }
    }

    // Freeze-transition crossfade.  Call StartFreezeCrossfade() when the
    // freeze state changes, then call ProcessFreezeCrossfade() once per
    // sample during the fade.
    void StartFreezeCrossfade();
    void ProcessFreezeCrossfade();

    // Zero the buffer and reset the write head.
    // The memset is deferred — call TickClear() each Process() block to
    // spread the cost. Init() still clears synchronously at startup.
    void Clear();

    // Zero the buffer immediately (synchronous memset) without resetting
    // write_head_. Use for user-triggered clears where the delay read position
    // must see silence right away rather than old content from the far end.
    void ImmediateClear();

    // Zero up to max_floats pending floats from the last Clear() call.
    // Call once per Process() block to drain the deferred clear incrementally.
    void TickClear(size_t max_floats);

    void SetDecimationFactor(int factor);
    int decimation_factor() const { return decimation_factor_; }

    size_t size() const { return size_; }
    size_t write_head() const { return write_head_; }
    bool crossfading() const { return crossfading_; }

    // How many bytes the caller must allocate for a buffer of the given
    // sample rate and duration.
    static size_t RequiredBytes(float sample_rate,
                                float duration_seconds,
                                int channels = 2);

private:
    float* buffer_ = nullptr;
    size_t size_ = 0;           // Number of frames (not samples)
    int channels_ = 2;
    size_t write_head_ = 0;

    // Decimation state (sample-and-hold: keep every Nth sample)
    int decimation_factor_ = 1;
    int decimation_counter_ = 0;

    // Deferred clear state (set by Clear(), drained by TickClear())
    size_t clear_cursor_ = 0;  // next float index to zero
    size_t clear_total_  = 0;  // total floats to zero (0 = none pending)

    // Freeze crossfade state
    bool crossfading_ = false;
    int crossfade_counter_ = 0;
    static constexpr int kCrossfadeSamples = 32;
};

} // namespace beads

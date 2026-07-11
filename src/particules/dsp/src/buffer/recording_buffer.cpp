#include "recording_buffer.h"
#include "../util/interpolation.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace particules_dsp {

void RecordingBuffer::Init(float* buffer, size_t num_frames, int num_channels) {
    buffer_ = buffer;
    size_ = num_frames;
    channels_ = num_channels;
    write_head_ = 0;
    decimation_factor_ = 1;
    decimation_counter_ = 0;
    write_ramp_remaining_ = 0;

    // Zero the entire allocation (main buffer + tail).
    size_t total_samples =
        (size_ + kInterpolationTail) * static_cast<size_t>(channels_);
    std::memset(buffer_, 0, total_samples * sizeof(float));
}

// ---------------------------------------------------------------------------
// Write
// ---------------------------------------------------------------------------

void RecordingBuffer::Write(float left, float right) {
    if (size_ == 0 || !buffer_ || channels_ < 2) return;

    // Sample-and-hold decimation: keep every Nth sample.
    // The SVF pre-filter in QualityProcessor handles anti-aliasing;
    // averaging here would redundantly smear transient attacks.
    decimation_counter_++;
    if (decimation_counter_ < decimation_factor_) return;
    decimation_counter_ = 0;

    size_t idx = write_head_ * channels_;

    // Unfreeze crossfade: blend from the frozen content into live input.
    if (write_ramp_remaining_ > 0) {
        float g = 1.0f - static_cast<float>(write_ramp_remaining_)
                       / static_cast<float>(kCrossfadeSamples);   // 0 → 1
        left  = buffer_[idx]     + (left  - buffer_[idx])     * g;
        right = buffer_[idx + 1] + (right - buffer_[idx + 1]) * g;
        --write_ramp_remaining_;
    }

    buffer_[idx] = left;
    buffer_[idx + 1] = right;

    // Keep the tail in sync when writing into the first kInterpolationTail
    // frames. Only the frame just written changed, so copy only that one.
    if (write_head_ < static_cast<size_t>(kInterpolationTail)) {
        size_t tail_dst = (size_ + write_head_) * channels_;
        buffer_[tail_dst]     = buffer_[idx];
        buffer_[tail_dst + 1] = buffer_[idx + 1];
    }

    write_head_++;
    if (write_head_ >= size_) {
        write_head_ = 0;
    }
}

void RecordingBuffer::Write(const StereoFrame& frame) {
    Write(frame.l, frame.r);
}

void RecordingBuffer::Clear() {
    if (!buffer_ || size_ == 0) return;
    write_head_ = 0;
    decimation_counter_ = 0;
    clear_cursor_ = 0;
    clear_total_  = (size_ + kInterpolationTail) * static_cast<size_t>(channels_);
}

void RecordingBuffer::ImmediateClear() {
    if (!buffer_ || size_ == 0) return;
    size_t total_samples = (size_ + kInterpolationTail) * static_cast<size_t>(channels_);
    std::memset(buffer_, 0, total_samples * sizeof(float));
    // Cancel any pending deferred clear
    clear_cursor_ = clear_total_;
}

void RecordingBuffer::TickClear(size_t max_floats) {
    if (clear_cursor_ >= clear_total_) return;
    size_t remaining = clear_total_ - clear_cursor_;
    size_t chunk = std::min(max_floats, remaining);
    std::memset(buffer_ + clear_cursor_, 0, chunk * sizeof(float));
    clear_cursor_ += chunk;
}

void RecordingBuffer::SetDecimationFactor(int factor) {
    if (factor < 1) factor = 1;
    decimation_factor_ = factor;
    decimation_counter_ = 0;
}

// ---------------------------------------------------------------------------
// ReadHermite (test-only reference implementation; production uses
// ReadHermiteStereoFast)
// ---------------------------------------------------------------------------

float RecordingBuffer::ReadHermite(int channel, float position) const {
    size_t i0;
    float frac;
    if (!ResolveReadPosition(position, &i0, &frac)) return 0.0f;

    // Four sample positions for Hermite: -1, 0, +1, +2 relative to pos_int.
    // Because the tail copies the first kInterpolationTail frames right after
    // the main buffer, indices 0/+1/+2 that land past size_ can read
    // directly from the tail. Only the -1 case needs explicit wrapping.
    size_t i_1 = (i0 == 0) ? size_ - 1 : i0 - 1;
    // i1 and i2 can overflow into the tail region -- that is fine; the tail
    // holds valid data for up to kInterpolationTail frames past the end.
    size_t i1 = i0 + 1;
    size_t i2 = i0 + 2;

    // If i1 or i2 land past the tail, wrap explicitly (only possible when
    // size_ < kInterpolationTail, which shouldn't happen in practice, but
    // defensive coding).
    if (i1 >= size_ + kInterpolationTail) i1 -= size_;
    if (i2 >= size_ + kInterpolationTail) i2 -= size_;

    float y_1 = buffer_[i_1 * channels_ + channel];
    float y0  = buffer_[i0  * channels_ + channel];
    float y1  = buffer_[i1  * channels_ + channel];
    float y2  = buffer_[i2  * channels_ + channel];

    return InterpolateHermite(y_1, y0, y1, y2, frac);
}

// ---------------------------------------------------------------------------
// ReadHermiteStereo (test-only reference implementation; production uses
// ReadHermiteStereoFast)
// ---------------------------------------------------------------------------

void RecordingBuffer::ReadHermiteStereo(float position, float* out_l, float* out_r) const {
    if (channels_ < 2) {
        *out_l = 0.0f;
        *out_r = 0.0f;
        return;
    }

    size_t i0;
    float frac;
    if (!ResolveReadPosition(position, &i0, &frac)) {
        *out_l = 0.0f;
        *out_r = 0.0f;
        return;
    }

    size_t i_1 = (i0 == 0) ? size_ - 1 : i0 - 1;
    size_t i1 = i0 + 1;
    size_t i2 = i0 + 2;

    if (i1 >= size_ + kInterpolationTail) i1 -= size_;
    if (i2 >= size_ + kInterpolationTail) i2 -= size_;

    // Read both channels from interleaved buffer, sharing index computation.
    const float* p_1 = &buffer_[i_1 * channels_];
    const float* p0  = &buffer_[i0  * channels_];
    const float* p1  = &buffer_[i1  * channels_];
    const float* p2  = &buffer_[i2  * channels_];

    *out_l = InterpolateHermite(p_1[0], p0[0], p1[0], p2[0], frac);
    *out_r = InterpolateHermite(p_1[1], p0[1], p1[1], p2[1], frac);
}

// ---------------------------------------------------------------------------
// ReadLinear (test-only reference implementation; production uses
// ReadHermiteStereoFast)
// ---------------------------------------------------------------------------

float RecordingBuffer::ReadLinear(int channel, float position) const {
    size_t i0;
    float frac;
    if (!ResolveReadPosition(position, &i0, &frac)) return 0.0f;

    // The tail covers at least 1 frame past the end, so i0+1 == size_ is
    // fine -- it reads from the tail.
    size_t i1 = i0 + 1;
    if (i1 >= size_ + kInterpolationTail) i1 -= size_;

    float y0 = buffer_[i0 * channels_ + channel];
    float y1 = buffer_[i1 * channels_ + channel];

    return InterpolateLinear(y0, y1, frac);
}

// ---------------------------------------------------------------------------
// Freeze declicking
// ---------------------------------------------------------------------------

void RecordingBuffer::NotifyFreeze(bool frozen) {
    if (!buffer_ || size_ == 0 || channels_ < 2) return;

    if (!frozen) {
        // Leaving freeze: blend the next writes from frozen content into
        // live input. No buffer mutation here.
        write_ramp_remaining_ = kCrossfadeSamples;
        return;
    }

    write_ramp_remaining_ = 0;

    // Entering freeze: fade both sides of the seam to zero so grain playback
    // crossing write_head_ passes through silence instead of a hard step
    // from the newest audio into the oldest. One-shot, O(kCrossfadeSamples).
    int size_int = static_cast<int>(size_);
    int fade = kCrossfadeSamples;
    if (fade > size_int / 2) fade = size_int / 2;   // degenerate small buffers

    for (int j = 0; j < fade; ++j) {
        float gain = static_cast<float>(j) / static_cast<float>(fade);
        int newest = static_cast<int>(write_head_) - 1 - j;
        newest = ((newest % size_int) + size_int) % size_int;
        int oldest = (static_cast<int>(write_head_) + j) % size_int;
        const int frames[2] = {newest, oldest};
        for (int f = 0; f < 2; ++f) {
            size_t idx = static_cast<size_t>(frames[f]) * channels_;
            for (int c = 0; c < channels_; ++c) {
                buffer_[idx + c] *= gain;
            }
            // Keep the interpolation-tail mirror in sync.
            if (frames[f] < static_cast<int>(kInterpolationTail)) {
                size_t tail = (size_ + static_cast<size_t>(frames[f])) * channels_;
                for (int c = 0; c < channels_; ++c) {
                    buffer_[tail + c] = buffer_[idx + c];
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// RequiredBytes
// ---------------------------------------------------------------------------

size_t RecordingBuffer::RequiredBytes(float sample_rate,
                                      float duration_seconds,
                                      int channels) {
    size_t num_frames =
        static_cast<size_t>(sample_rate * duration_seconds);
    // Main buffer + tail for interpolation.
    size_t total_samples =
        (num_frames + kInterpolationTail) * static_cast<size_t>(channels);
    return total_samples * sizeof(float);
}

} // namespace particules_dsp

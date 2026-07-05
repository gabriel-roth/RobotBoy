#include "recording_buffer.h"
#include "../util/interpolation.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace beads {

void RecordingBuffer::Init(float* buffer, size_t num_frames, int num_channels) {
    buffer_ = buffer;
    size_ = num_frames;
    channels_ = num_channels;
    write_head_ = 0;
    decimation_factor_ = 1;
    decimation_counter_ = 0;
    crossfading_ = false;
    crossfade_counter_ = 0;

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
// ReadHermite
// ---------------------------------------------------------------------------

float RecordingBuffer::ReadHermite(int channel, float position) const {
    if (size_ == 0 || !buffer_) return 0.0f;

    // Guard against NaN (which would cause infinite loops below).
    if (std::isnan(position)) return 0.0f;

    // Wrap position into [0, size_). Callers pre-wrap positions so these
    // loops execute 0-1 times. NaN is caught by the isnan guard above.
    float size_f = static_cast<float>(size_);
    while (position >= size_f) position -= size_f;
    while (position < 0.0f) position += size_f;

    // Integer and fractional parts.
    int pos_int = static_cast<int>(position);
    float frac = position - static_cast<float>(pos_int);

    // Four sample positions for Hermite: -1, 0, +1, +2 relative to pos_int.
    // Because the tail copies the first kInterpolationTail frames right after
    // the main buffer, indices 0/+1/+2 that land past size_ can read
    // directly from the tail. Only the -1 case needs explicit wrapping.
    size_t i0 = static_cast<size_t>(pos_int);
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
// ReadHermiteStereo
// ---------------------------------------------------------------------------

void RecordingBuffer::ReadHermiteStereo(float position, float* out_l, float* out_r) const {
    if (size_ == 0 || !buffer_ || channels_ < 2) {
        *out_l = 0.0f;
        *out_r = 0.0f;
        return;
    }

    if (std::isnan(position)) {
        *out_l = 0.0f;
        *out_r = 0.0f;
        return;
    }

    float size_f = static_cast<float>(size_);
    while (position >= size_f) position -= size_f;
    while (position < 0.0f) position += size_f;

    int pos_int = static_cast<int>(position);
    float frac = position - static_cast<float>(pos_int);

    size_t i0 = static_cast<size_t>(pos_int);
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
// ReadLinear
// ---------------------------------------------------------------------------

float RecordingBuffer::ReadLinear(int channel, float position) const {
    if (size_ == 0 || !buffer_) return 0.0f;

    // Guard against NaN (which would cause infinite loops below).
    if (std::isnan(position)) return 0.0f;

    // Wrap position into [0, size_). Callers pre-wrap positions so these
    // loops execute 0-1 times. NaN is caught by the isnan guard above.
    float size_f = static_cast<float>(size_);
    while (position >= size_f) position -= size_f;
    while (position < 0.0f) position += size_f;

    int pos_int = static_cast<int>(position);
    float frac = position - static_cast<float>(pos_int);

    size_t i0 = static_cast<size_t>(pos_int);
    // The tail covers at least 1 frame past the end, so i0+1 == size_ is
    // fine -- it reads from the tail.
    size_t i1 = i0 + 1;
    if (i1 >= size_ + kInterpolationTail) i1 -= size_;

    float y0 = buffer_[i0 * channels_ + channel];
    float y1 = buffer_[i1 * channels_ + channel];

    return InterpolateLinear(y0, y1, frac);
}

// ---------------------------------------------------------------------------
// Freeze crossfade
// ---------------------------------------------------------------------------

void RecordingBuffer::StartFreezeCrossfade() {
    // Scan up to 64 samples backward from write_head_ for a zero crossing
    // in the mono sum. If found, no crossfade needed.
    static constexpr int kZeroCrossScan = 64;
    if (size_ > 0 && buffer_ && channels_ >= 2) {
        int size_int = static_cast<int>(size_);
        float prev_mono = 0.0f;
        // Read the sample at write_head_ - 1
        int first_frame = (static_cast<int>(write_head_) - 1 + size_int) % size_int;
        size_t first_idx = static_cast<size_t>(first_frame) * channels_;
        prev_mono = buffer_[first_idx] + buffer_[first_idx + 1];

        for (int i = 2; i <= kZeroCrossScan && i <= size_int; ++i) {
            int frame = (static_cast<int>(write_head_) - i + size_int) % size_int;
            size_t idx = static_cast<size_t>(frame) * channels_;
            float mono = buffer_[idx] + buffer_[idx + 1];
            // Sign change with minimum amplitude
            if ((prev_mono > 1e-5f && mono <= 0.0f) ||
                (prev_mono < -1e-5f && mono >= 0.0f)) {
                // Found a zero crossing — no crossfade needed
                crossfading_ = false;
                crossfade_counter_ = 0;
                return;
            }
            prev_mono = mono;
        }
    }

    // No zero crossing found — fall back to existing crossfade
    crossfading_ = true;
    crossfade_counter_ = kCrossfadeSamples;
}

void RecordingBuffer::ProcessFreezeCrossfade() {
    if (!crossfading_ || size_ == 0 || !buffer_) return;

    // Apply a short linear fade at the current write head to smooth the
    // transition.  Each call attenuates the sample at (write_head_ - counter)
    // by the crossfade envelope so there is no discontinuity at the freeze
    // point.
    if (crossfade_counter_ > 0) {
        float gain =
            static_cast<float>(crossfade_counter_) / kCrossfadeSamples;

        // The frame we want to fade is the one that was most recently frozen.
        // write_head_ points to where the *next* write would go, so the
        // frozen edge is at (write_head_ - kCrossfadeSamples + counter - 1)
        // but we step backwards from the freeze point one sample at a time.
        int offset = kCrossfadeSamples - crossfade_counter_;
        int frame = static_cast<int>(write_head_) - 1 - offset;
        int size_int = static_cast<int>(size_);
        // Use modular wrap that handles frame being multiple buffer-lengths negative
        frame = ((frame % size_int) + size_int) % size_int;

        size_t idx = static_cast<size_t>(frame) * channels_;
        for (int c = 0; c < channels_; ++c) {
            buffer_[idx + c] *= gain;
        }

        crossfade_counter_--;
        if (crossfade_counter_ == 0) {
            crossfading_ = false;
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

} // namespace beads

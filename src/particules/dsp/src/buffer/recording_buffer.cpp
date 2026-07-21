#include "recording_buffer.h"
#include "../util/interpolation.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace particules_dsp {

void RecordingBuffer::Init(float* buffer, size_t num_frames, int num_channels) {
    f32_ = buffer;
    i16_ = reinterpret_cast<int16_t*>(buffer);
    u8_  = reinterpret_cast<uint8_t*>(buffer);
    capacity_bytes_ = (num_frames + kInterpolationTail)
                      * static_cast<size_t>(num_channels) * sizeof(float);
    size_ = num_frames;
    channels_ = num_channels;
    format_ = StorageFormat::kFloat32;
    mulaw_lut_ = MuLaw8DecodeTable();   // prime the table off the audio thread
    write_head_ = 0;
    decimation_factor_ = 1;
    decimation_counter_ = 0;
    write_ramp_remaining_ = 0;

    std::memset(u8_, 0, capacity_bytes_);
}

size_t RecordingBuffer::FramesForConfig(size_t capacity_bytes, int channels,
                                        StorageFormat format, size_t max_bytes) {
    size_t bps = 4;
    if (format == StorageFormat::kInt12) bps = 2;
    if (format == StorageFormat::kMuLaw8) bps = 1;
    size_t pool = capacity_bytes;
    if (max_bytes > 0 && max_bytes < pool) pool = max_bytes;
    size_t frames = pool / (bps * static_cast<size_t>(channels));
    return (frames > static_cast<size_t>(kInterpolationTail))
               ? frames - kInterpolationTail : 0;
}

void RecordingBuffer::Configure(int decimation_factor, StorageFormat format,
                                int channels, size_t max_bytes) {
    if (!u8_ || capacity_bytes_ == 0) return;
    if (channels < 1) channels = 1;
    if (channels > 2) channels = 2;
    SetDecimationFactor(decimation_factor);
    size_t frames = FramesForConfig(capacity_bytes_, channels, format, max_bytes);
    if (format == format_ && channels == channels_ && frames == size_) return;
    format_ = format;
    channels_ = channels;
    size_ = frames;
    write_head_ = 0;
    write_ramp_remaining_ = 0;
    // Pool bytes are now garbage under the new interpretation; caller must
    // Clear() and mute until ClearPending() (see header contract).
}

// ---------------------------------------------------------------------------
// Write
// ---------------------------------------------------------------------------

void RecordingBuffer::Write(float left, float right) {
    if (size_ == 0 || !u8_ || channels_ < 1) return;

    // Sample-and-hold decimation: keep every Nth sample.
    // The SVF pre-filter in QualityProcessor handles anti-aliasing;
    // averaging here would redundantly smear transient attacks.
    decimation_counter_++;
    if (decimation_counter_ < decimation_factor_) return;
    decimation_counter_ = 0;

    if (channels_ == 1) {
        float mono = (left + right) * 0.5f;
        if (write_ramp_remaining_ > 0) {
            float g = 1.0f - static_cast<float>(write_ramp_remaining_)
                           / static_cast<float>(kCrossfadeSamples);   // 0 → 1
            float old_m = SampleAt(write_head_, 0);
            mono = old_m + (mono - old_m) * g;
            --write_ramp_remaining_;
        }
        StoreSample(write_head_, 0, mono);
    } else {
        // Unfreeze crossfade: blend from the frozen content into live input.
        if (write_ramp_remaining_ > 0) {
            float g = 1.0f - static_cast<float>(write_ramp_remaining_)
                           / static_cast<float>(kCrossfadeSamples);   // 0 → 1
            float old_l = SampleAt(write_head_, 0);
            float old_r = SampleAt(write_head_, 1);
            left  = old_l + (left  - old_l) * g;
            right = old_r + (right - old_r) * g;
            --write_ramp_remaining_;
        }
        StoreSample(write_head_, 0, left);
        StoreSample(write_head_, 1, right);
    }

    // Keep the tail in sync when writing into the first kInterpolationTail
    // frames. Only the frame just written changed, so copy only that one.
    if (write_head_ < static_cast<size_t>(kInterpolationTail)) {
        CopyFrameToTail(write_head_);
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
    if (!u8_ || size_ == 0) return;
    write_head_ = 0;
    decimation_counter_ = 0;
    clear_cursor_ = 0;
    clear_total_ = (size_ + kInterpolationTail)
                   * static_cast<size_t>(channels_) * bytes_per_sample();
}

void RecordingBuffer::ImmediateClear() {
    if (!u8_ || size_ == 0) return;
    std::memset(u8_, 0, (size_ + kInterpolationTail)
                        * static_cast<size_t>(channels_) * bytes_per_sample());
    clear_cursor_ = clear_total_;
}

void RecordingBuffer::TickClear(size_t max_bytes) {
    if (clear_cursor_ >= clear_total_) return;
    size_t chunk = std::min(max_bytes, clear_total_ - clear_cursor_);
    std::memset(u8_ + clear_cursor_, 0, chunk);
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

    int ch = (channels_ < 2) ? 0 : channel;
    float y_1 = SampleAt(i_1, ch);
    float y0  = SampleAt(i0,  ch);
    float y1  = SampleAt(i1,  ch);
    float y2  = SampleAt(i2,  ch);

    return InterpolateHermite(y_1, y0, y1, y2, frac);
}

// ---------------------------------------------------------------------------
// ReadHermiteStereo (test-only reference implementation; production uses
// ReadHermiteStereoFast)
// ---------------------------------------------------------------------------

void RecordingBuffer::ReadHermiteStereo(float position, float* out_l, float* out_r) const {
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

    if (channels_ < 2) {
        // Mono: one interpolation, duplicated to both outputs.
        float y_1 = SampleAt(i_1, 0);
        float y0  = SampleAt(i0,  0);
        float y1  = SampleAt(i1,  0);
        float y2  = SampleAt(i2,  0);
        float m = InterpolateHermite(y_1, y0, y1, y2, frac);
        *out_l = m;
        *out_r = m;
        return;
    }

    float l_1 = SampleAt(i_1, 0), l0 = SampleAt(i0, 0), l1 = SampleAt(i1, 0), l2 = SampleAt(i2, 0);
    float r_1 = SampleAt(i_1, 1), r0 = SampleAt(i0, 1), r1 = SampleAt(i1, 1), r2 = SampleAt(i2, 1);

    *out_l = InterpolateHermite(l_1, l0, l1, l2, frac);
    *out_r = InterpolateHermite(r_1, r0, r1, r2, frac);
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

    int ch = (channels_ < 2) ? 0 : channel;
    float y0 = SampleAt(i0, ch);
    float y1 = SampleAt(i1, ch);

    return InterpolateLinear(y0, y1, frac);
}

// ---------------------------------------------------------------------------
// Freeze declicking
// ---------------------------------------------------------------------------

void RecordingBuffer::NotifyFreeze(bool frozen) {
    if (!f32_ || size_ == 0 || channels_ < 1) return;

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
                f32_[idx + c] *= gain;
            }
            // Keep the interpolation-tail mirror in sync.
            if (frames[f] < static_cast<int>(kInterpolationTail)) {
                size_t tail = (size_ + static_cast<size_t>(frames[f])) * channels_;
                for (int c = 0; c < channels_; ++c) {
                    f32_[tail + c] = f32_[idx + c];
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

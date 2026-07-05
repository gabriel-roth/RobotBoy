#pragma once
#include <cstddef>

namespace beads {

// Individual delay line for use in reverb and other effects.
// Operates on a sub-region of a shared buffer.
// Optimized: no modulo operations in hot paths (branch instead).
class DelayLine {
public:
    void Init(float* buffer, size_t size) {
        buffer_ = buffer;
        size_ = size;
        write_ptr_ = 0;
        if (buffer_) {
            for (size_t i = 0; i < size_; ++i) buffer_[i] = 0.0f;
        }
    }

    // Write value at current write position
    inline void Write(float value) {
        buffer_[write_ptr_] = value;
    }

    // Read the oldest sample (at write_ptr, about to be overwritten).
    // This is the most common read pattern in allpass and feedback delays.
    inline float ReadOldest() const {
        return buffer_[write_ptr_];
    }

    // Read from offset samples behind write pointer.
    // offset must be in [1, size_].
    inline float Read(size_t offset) const {
        size_t idx = write_ptr_ + size_ - offset;
        if (idx >= size_) idx -= size_;
        return buffer_[idx];
    }

    // Read with linear interpolation for fractional/modulated delays.
    // offset must be in [1.0, size_].
    inline float ReadInterpolated(float offset) const {
        if (offset < 1.0f) offset = 1.0f;
        float max_off = static_cast<float>(size_);
        if (offset > max_off) offset = max_off;

        size_t off_int = static_cast<size_t>(offset);
        float frac = offset - static_cast<float>(off_int);

        // Compute index for off_int
        size_t idx0 = write_ptr_ + size_ - off_int;
        if (idx0 >= size_) idx0 -= size_;

        // idx1 is one sample older (idx0 - 1, with wrap)
        size_t idx1 = (idx0 > 0) ? idx0 - 1 : size_ - 1;

        float s0 = buffer_[idx0];
        float s1 = buffer_[idx1];
        return s0 + frac * (s1 - s0);
    }

    // Advance write pointer by one position
    inline void Advance() {
        if (++write_ptr_ >= size_) write_ptr_ = 0;
    }

    size_t size() const { return size_; }

private:
    float* buffer_ = nullptr;
    size_t size_ = 0;
    size_t write_ptr_ = 0;
};

} // namespace beads

#pragma once

#include <cstddef>
#include <cstdint>
#include <cmath>

namespace particules_dsp {

struct StereoFrame {
    float l, r;

    StereoFrame operator+(const StereoFrame& other) const {
        return {l + other.l, r + other.r};
    }
    StereoFrame operator*(float gain) const {
        return {l * gain, r * gain};
    }
    StereoFrame& operator+=(const StereoFrame& other) {
        l += other.l;
        r += other.r;
        return *this;
    }
    StereoFrame& operator*=(float gain) {
        l *= gain;
        r *= gain;
        return *this;
    }
};

// Recording buffer size in frames (fixed memory budget).
// Packed storage + decimation set effective duration — stereo 4/8/16/32 s,
// mono 8/16/32/64 s at 48 kHz.
static constexpr size_t kDefaultBufferFrames = 48000 * 4;  // 192000 frames

// Hermite interpolation requires 4 samples (declared here, ahead of
// kRecordingPoolBytes below, which needs it).
static constexpr int kInterpolationTail = 4;

enum class QualityMode : uint8_t {
    kBrightDigital = 0,
    kColdDigital = 1,
    kSunnyTape = 2,
    kScorchedCassette = 3
};

// Recording-buffer storage formats. Packing samples at reduced width lets the
// same byte pool hold more frames, decoupling recording rate from buffer
// length (see docs/superpowers/plans/2026-07-20-quality-buffer-decoupling.md).
enum class StorageFormat : uint8_t {
    kFloat32 = 0,   // 4 bytes/sample
    kInt12 = 1,     // 12-bit signed in an int16 container, 2 bytes/sample
    kMuLaw8 = 2     // 8-bit segment mu-law, 1 byte/sample
};

static constexpr size_t kRecordingPoolBytes =
    (kDefaultBufferFrames + kInterpolationTail) * 2 * sizeof(float);

// Per-mode recording configuration. decimation divides the host rate; format
// is the storage packing; max_bytes caps the pool (0 = all of it). Channel
// count is NOT part of the mode — it follows the input jacks (mono input
// doubles duration for the same bytes).
struct QualityConfig {
    int decimation;
    StorageFormat format;
    size_t max_bytes;
};

inline QualityConfig QualityConfigFor(QualityMode mode) {
    switch (mode) {
        case QualityMode::kBrightDigital:
            return {1, StorageFormat::kFloat32, 0};
        case QualityMode::kColdDigital:
            // Half pool: Cold stays 8 s stereo / 16 s mono like hardware
            // (uncapped int12 would give 16/32 s).
            return {2, StorageFormat::kInt12, kRecordingPoolBytes / 2};
        case QualityMode::kSunnyTape:
            return {2, StorageFormat::kInt12, 0};
        case QualityMode::kScorchedCassette:
            return {2, StorageFormat::kMuLaw8, 0};
    }
    return {1, StorageFormat::kFloat32, 0};
}

inline int DecimationFactorForQuality(QualityMode mode) {
    return QualityConfigFor(mode).decimation;
}

enum class TriggerMode : uint8_t {
    kLatched = 0,
    kGated = 1,
    kClocked = 2,
    kMidi = 3
};

// Maximum number of simultaneous grains — matches hardware Beads (30 replay heads)
static constexpr int kMaxGrains = 30;

// Reverb delay memory size (12 partitioned delay lines, ~12K samples needed)
static constexpr size_t kReverbBufferSize = 16384;

// Processing block size (kept small to limit stack usage; the Process() loop
// handles arbitrary num_frames by iterating in chunks of this size)
static constexpr size_t kMaxBlockSize = 64;

// Wavetable size (Ondes oscillator)
static constexpr int kWavetableSize = 256;

// Wavetable data provider — implemented by the host wrapper (RackWavetableProvider).
struct WavetableProvider {
    virtual ~WavetableProvider() = default;
    // Returns kWavetableSize samples, normalized float [-1, 1].
    virtual const float* GetWaveform(int bank, int index) const = 0;
    virtual int NumBanksAvailable() const = 0;
    virtual int WaveformsPerBank() const = 0;
};

} // namespace particules_dsp

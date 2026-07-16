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

enum class QualityMode : uint8_t {
    kBrightDigital = 0,
    kColdDigital = 1,
    kSunnyTape = 2,
    kScorchedCassette = 3
};

inline int DecimationFactorForQuality(QualityMode mode) {
    switch (mode) {
        case QualityMode::kBrightDigital:      return 1;
        case QualityMode::kColdDigital:    return 2;
        case QualityMode::kSunnyTape: return 8;
        case QualityMode::kScorchedCassette:      return 4;
    }
    return 1;
}

enum class TriggerMode : uint8_t {
    kLatched = 0,
    kGated = 1,
    kClocked = 2,
    kMidi = 3
};

// Maximum number of simultaneous grains — matches hardware Beads (30 replay heads)
static constexpr int kMaxGrains = 30;

// Recording buffer size in frames (fixed memory budget).
// 4 seconds at 48kHz; at 96kHz the same frame count gives 2 seconds.
// Decimation extends effective duration: HiFi 4s, Clouds 8s, Tape 16s, LoFi 32s.
static constexpr size_t kDefaultBufferFrames = 48000 * 4;  // 192000 frames

// Reverb delay memory size (12 partitioned delay lines, ~12K samples needed)
static constexpr size_t kReverbBufferSize = 16384;

// Processing block size (kept small to limit stack usage; the Process() loop
// handles arbitrary num_frames by iterating in chunks of this size)
static constexpr size_t kMaxBlockSize = 64;

// Hermite interpolation requires 4 samples
static constexpr int kInterpolationTail = 4;

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

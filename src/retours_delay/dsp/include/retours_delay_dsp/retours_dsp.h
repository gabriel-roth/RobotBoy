#pragma once
#include "types.h"

namespace retours_delay_dsp {

class RetoursProcessor {
public:
    struct MemoryRequirements { size_t total_bytes; size_t alignment; };
    static MemoryRequirements GetMemoryRequirements(float sample_rate);

    void Init(void* memory, size_t memory_size, float sample_rate);
    void SetParameters(const RetoursParameters& params);
    void Process(const StereoFrame* input, StereoFrame* output, size_t num_frames);
    void ClearBuffer();
    void ClearTappedTempo();          // abandon measured tempo -> free-running
    bool BufferEmpty() const;         // no audio recorded since last clear

    // Telemetry (block-rate; for panel lights)
    float BaseTimeSeconds() const;    // current base delay time
    bool  IsClocked() const;
    float ClockBeatSeconds() const;   // measured clock beat, 0 if not clocked
    float DelayTimeSeconds() const;   // actual tap-1 delay after multiplier

private:
    struct Impl;
    Impl* impl_ = nullptr;
    void ProcessBlock(const StereoFrame* input, StereoFrame* output, size_t n);
};

} // namespace retours_delay_dsp

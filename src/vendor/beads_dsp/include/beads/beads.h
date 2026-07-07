#pragma once

// Beads DSP Library — Single public header
// A faithful recreation of the Mutable Instruments Beads granular texture synthesizer.
//
// Usage:
//   1. Call BeadsProcessor::GetMemoryRequirements() to learn buffer size
//   2. Allocate memory (e.g. from DRAM, static array, or heap)
//   3. Create a BeadsProcessor on the stack or wherever you like
//   4. Call Init() with the memory pointer
//   5. Each audio block: call SetParameters(), then Process()
//
// Memory model:
//   The BeadsProcessor object itself is small (~stack-friendly).
//   All large buffers (recording, reverb) live in the user-provided memory block.
//   No heap allocations occur during Process().

#include "types.h"
#include "parameters.h"

namespace beads {

class BeadsProcessor {
public:
    struct MemoryRequirements {
        size_t total_bytes;     // DRAM requirement (includes reverb as fallback)
        size_t dtc_bytes;       // DTC requirement (grain cache + reverb)
        size_t alignment;
    };

    static MemoryRequirements GetMemoryRequirements(float sample_rate);

    // Init with DRAM only (reverb in DRAM, no grain DTC cache)
    void Init(void* memory, size_t memory_size, float sample_rate);
    // Init with DRAM + optional DTC (reverb + grain cache in DTC if provided)
    void Init(void* memory, size_t memory_size, float sample_rate,
              void* dtc_memory, size_t dtc_size);
    void SetParameters(const BeadsParameters& params);
    void Process(const StereoFrame* input, StereoFrame* output, size_t num_frames);

    int ActiveGrainCount() const;
    bool GrainTriggeredThisBlock() const;
    float InputLevel() const;
    float AutoGainDb() const;
    void TriggerAutoGainCalibration();
    void ClearBuffer();

    // Scale quantization
    void LoadScale(const double* ratios, uint32_t num_notes);
    void ClearScale();
    void SetScaleRoot(int midi_note);

private:
    struct Impl;
    Impl* impl_ = nullptr;
};

} // namespace beads

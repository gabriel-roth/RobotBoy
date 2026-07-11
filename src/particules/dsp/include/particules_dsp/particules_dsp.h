#pragma once

// Particules DSP Library — Single public header
// An independent granular texture engine inspired by the Mutable Instruments
// Beads hardware module. Written without access to the original Beads source
// code; behavior is modeled on the hardware and its manual.
//
// Usage:
//   1. Call ParticulesProcessor::GetMemoryRequirements() to learn buffer size
//   2. Allocate memory (e.g. from DRAM, static array, or heap)
//   3. Create a ParticulesProcessor on the stack or wherever you like
//   4. Call Init() with the memory pointer
//   5. Each audio block: call SetParameters(), then Process()
//
// Memory model:
//   The ParticulesProcessor object itself is small (~stack-friendly).
//   All large buffers (recording, reverb) live in the user-provided memory block.
//   No heap allocations occur during Process().

#include "types.h"
#include "parameters.h"

namespace particules_dsp {

class ParticulesProcessor {
public:
    struct MemoryRequirements {
        size_t total_bytes;     // DRAM requirement (includes reverb)
        size_t alignment;
    };

    static MemoryRequirements GetMemoryRequirements(float sample_rate);

    void Init(void* memory, size_t memory_size, float sample_rate);
    void SetParameters(const ParticulesParameters& params);
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

    // One internal block (num_frames <= kMaxBlockSize). Public Process()
    // chunks arbitrary num_frames into these.
    void ProcessBlock(const StereoFrame* input, StereoFrame* output,
                      size_t num_frames);
};

} // namespace particules_dsp

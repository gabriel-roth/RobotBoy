#pragma once

#include "retours_delay_dsp/types.h"

#include <algorithm>
#include <array>
#include <cstddef>

// Block accumulator for the Retours VCV adapter. Data-path only (mirrors the
// non-grain parts of particules_block_runtime.h's ParticulesBlockRuntime):
// PushInputSample/BlockReady/InputBuffer/CommitProcessedBlock/
// ReadOutputSample. Grain LED and trigger-pulse machinery are dropped (Retours
// has no grains); clock/tap edge tracking is added instead.
template <size_t BlockSize>
class RetoursBlockRuntime {
public:
    static_assert(BlockSize >= 1, "Retours block runtime requires BlockSize >= 1");

    bool PushInputSample(retours_delay_dsp::StereoFrame in) {
        input_buf_[input_index_] = in;
        ++input_index_;
        if (input_index_ >= BlockSize) {
            input_index_ = 0;
            output_index_ = 0;
            block_ready_ = true;
            return true;
        }
        return false;
    }

    bool BlockReady() const { return block_ready_; }
    size_t InputIndex() const { return input_index_; }
    size_t OutputIndex() const { return output_index_; }

    retours_delay_dsp::StereoFrame* InputBuffer() { return input_buf_.data(); }
    const retours_delay_dsp::StereoFrame* InputBuffer() const { return input_buf_.data(); }

    void CommitProcessedBlock(const retours_delay_dsp::StereoFrame* processed, size_t count) {
        const size_t copy_count = std::min(count, BlockSize);
        for (size_t i = 0; i < copy_count; ++i) {
            output_buf_[i] = processed[i];
        }
        block_ready_ = false;
        output_index_ = 0;
    }

    retours_delay_dsp::StereoFrame ReadOutputSample() {
        retours_delay_dsp::StereoFrame out = output_buf_[output_index_];
        ++output_index_;
        if (output_index_ >= BlockSize) {
            output_index_ = 0;
        }
        return out;
    }

    // Clock/tap edge tracking: SEED jack (Schmitt) and SEED button edges are
    // both noted per sample by the caller; this latches the sample offset of
    // the FIRST rising edge seen within the current block (whichever source
    // reaches it first) into RetoursParameters::clock_tick_offset. Consuming
    // resets the latch so the next block starts clean.
    void NoteClockEdgeSample(bool rising, size_t index_in_block) {
        if (rising && clock_tick_offset_ < 0) {
            clock_tick_offset_ = static_cast<int>(index_in_block);
        }
    }

    int TakeClockTickOffset() {
        int offset = clock_tick_offset_;
        clock_tick_offset_ = -1;
        return offset;
    }

private:
    std::array<retours_delay_dsp::StereoFrame, BlockSize> input_buf_ {};
    std::array<retours_delay_dsp::StereoFrame, BlockSize> output_buf_ {};
    size_t input_index_ = 0;
    size_t output_index_ = 0;
    bool block_ready_ = false;
    int clock_tick_offset_ = -1;
};

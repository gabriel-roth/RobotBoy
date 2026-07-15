#include "echos_processor.h"
#include "util/dsp_utils.h"
#include <algorithm>
#include <cstdint>
#include <new>

namespace beadsdelay_dsp {

static constexpr size_t kImplAlignment = 16;

static size_t AlignUp(size_t size, size_t alignment = kImplAlignment) {
    return (size + alignment - 1) & ~(alignment - 1);
}

EchosProcessor::MemoryRequirements EchosProcessor::GetMemoryRequirements(float sample_rate) {
    (void)sample_rate;  // Frame budget is fixed; duration varies with rate.
    MemoryRequirements req;

    size_t impl_bytes = AlignUp(sizeof(Impl));
    size_t recording_bytes =
        (kBufferFrames + particules_dsp::kInterpolationTail) * 2 * sizeof(float);

    req.total_bytes = (kImplAlignment - 1) + impl_bytes + AlignUp(recording_bytes);
    req.alignment = kImplAlignment;
    return req;
}

void EchosProcessor::Init(void* memory, size_t memory_size, float sample_rate) {
    impl_ = nullptr;
    if (!memory || memory_size == 0) return;

    auto req = GetMemoryRequirements(sample_rate);
    if (memory_size < req.total_bytes) return;

    // Align the base pointer to the required alignment before placement-new.
    uintptr_t addr = reinterpret_cast<uintptr_t>(memory);
    addr = (addr + kImplAlignment - 1) & ~(kImplAlignment - 1);
    uint8_t* ptr = reinterpret_cast<uint8_t*>(addr);

    // Placement-new the Impl at the aligned start of the memory block.
    impl_ = new (ptr) Impl();
    ptr += AlignUp(sizeof(Impl));

    impl_->sample_rate = sample_rate;
    impl_->params = EchosParameters{};

    // Recording buffer occupies the space right after Impl.
    size_t recording_bytes =
        (kBufferFrames + particules_dsp::kInterpolationTail) * 2 * sizeof(float);
    impl_->recording_buffer.Init(reinterpret_cast<float*>(ptr), kBufferFrames, 2);
    ptr += AlignUp(recording_bytes);

    impl_->saturation.Init();
    impl_->quality_processor.Init(sample_rate);

    // Feedback HP filter at 10 Hz to remove DC buildup in the feedback path.
    impl_->feedback_hp_l.Init();
    impl_->feedback_hp_l.SetFrequencyHz(10.0f, sample_rate);
    impl_->feedback_hp_r.Init();
    impl_->feedback_hp_r.SetFrequencyHz(10.0f, sample_rate);
}

void EchosProcessor::SetParameters(const EchosParameters& params) {
    if (!impl_) return;
    impl_->params = params;
}

void EchosProcessor::Process(const StereoFrame* input, StereoFrame* output, size_t num_frames) {
    if (!impl_) {
        for (size_t i = 0; i < num_frames; ++i) {
            output[i] = {0.0f, 0.0f};
        }
        return;
    }
    size_t offset = 0;
    while (offset < num_frames) {
        size_t block = std::min(num_frames - offset, kMaxBlockSize);
        ProcessBlock(input + offset, output + offset, block);
        offset += block;
    }
}

void EchosProcessor::ProcessBlock(const StereoFrame* in, StereoFrame* out, size_t n) {
    Impl& s = *impl_;
    // one-pole toward target, applied per sample to avoid zipper noise
    for (size_t i = 0; i < n; ++i) {
        particules_dsp::OnePole(s.smoothed_dry_wet, s.params.dry_wet, 0.05f);
        // placeholder wet = silence until Task 3
        StereoFrame wet{0.f, 0.f};
        float mix = s.smoothed_dry_wet;
        out[i].l = in[i].l * (1.f - mix) + wet.l * mix;
        out[i].r = in[i].r * (1.f - mix) + wet.r * mix;
    }
}

void EchosProcessor::ClearBuffer() {
    if (impl_) impl_->recording_buffer.ImmediateClear();
}

float EchosProcessor::BaseTimeSeconds() const {
    return 0.0f;
}

bool EchosProcessor::IsClocked() const {
    return false;
}

float EchosProcessor::DelayTimeSeconds() const {
    return 0.0f;
}

} // namespace beadsdelay_dsp

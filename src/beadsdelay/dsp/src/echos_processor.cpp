#include "echos_processor.h"
#include "util/dsp_utils.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <new>

namespace beadsdelay_dsp {

static constexpr size_t kImplAlignment = 16;

// kBufferFrames is a fixed frame count sized for 4 s @48k; BaseTimeControl
// wants a duration, so express that same 4 s nominal here regardless of the
// actual sample rate (it clamps the resulting sample count to kBufferFrames).
static constexpr float kBufferSeconds = static_cast<float>(kBufferFrames) / 48000.f;

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

    impl_->base_time.Init(sample_rate, kBufferSeconds);
    impl_->engine.Init(&impl_->recording_buffer, sample_rate);
    impl_->shifter.Init(sample_rate);
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

    // Block-rate: resolve base/delay time and push targets into the engine.
    BaseTimeControl::Result bt = s.base_time.Update(
        s.params.density, s.params.density_cv, s.params.time,
        s.params.clock_connected, s.params.clock_tick_offset, n);
    float slew_seconds = std::max(s.params.slew_seconds, 1e-3f);
    s.engine.SetTargets(bt.delay_samples, bt.multi_tap, s.params.time_change_mode, slew_seconds);
    // Task 7 completes freeze; for now this only stores state (stub).
    s.engine.NotifyFreeze(s.params.freeze, bt.base_samples, bt.slice_index);

    // Block-rate: linear input trim gain.
    float input_gain = particules_dsp::DbToGain(s.params.input_trim_db);

    // Block-rate: pitch-shift ratio for the rotary shifter (raw semitones
    // for now; Task 6 adds attenurandomized pitch on top).
    float pitch_semitones = s.params.pitch_semitones;
    float shift_ratio = std::fabs(pitch_semitones) < kShifterBypassSemitones
                             ? 1.f
                             : std::exp2(pitch_semitones / 12.f);
    s.shifter.SetRatio(shift_ratio);

    bool feedback_state_bad = false;

    for (size_t i = 0; i < n; ++i) {
        particules_dsp::OnePole(s.smoothed_dry_wet, s.params.dry_wet, 0.05f);
        particules_dsp::OnePole(s.smoothed_feedback, s.params.feedback, 0.05f);

        // NaN guard: sanitize input before it can reach the engine/buffer.
        StereoFrame input = in[i];
        if (!std::isfinite(input.l)) input.l = 0.f;
        if (!std::isfinite(input.r)) input.r = 0.f;

        // Pitch shift sits inside the feedback loop (shimmer): the shifted
        // signal is what gets tapped back for feedback and mixed as wet.
        StereoFrame wet = s.shifter.Process(s.engine.ReadWet());

        StereoFrame fb_in{wet.l * s.smoothed_feedback, wet.r * s.smoothed_feedback};
        StereoFrame fb = s.saturation.LimitFeedback(fb_in, particules_dsp::QualityMode::kHiFi);
        fb.l = s.feedback_hp_l.ProcessHP(fb.l);
        fb.r = s.feedback_hp_r.ProcessHP(fb.r);
        if (!std::isfinite(fb.l) || !std::isfinite(fb.r)) {
            fb.l = 0.f;
            fb.r = 0.f;
            feedback_state_bad = true;
        }

        float trimmed_l = input.l * input_gain;
        float trimmed_r = input.r * input_gain;
        s.recording_buffer.Write(trimmed_l + fb.l, trimmed_r + fb.r);

        float mix = s.smoothed_dry_wet;
        out[i].l = input.l * (1.f - mix) + wet.l * mix;
        out[i].r = input.r * (1.f - mix) + wet.r * mix;
    }

    // NaN guard: flush feedback/filter state once per block (not per sample)
    // if it ever went non-finite.
    if (feedback_state_bad || !std::isfinite(s.smoothed_feedback) ||
        !std::isfinite(s.smoothed_dry_wet)) {
        s.smoothed_feedback = particules_dsp::Clamp(s.params.feedback, 0.f, 1.f);
        s.smoothed_dry_wet = particules_dsp::Clamp(s.params.dry_wet, 0.f, 1.f);
        s.feedback_hp_l.Reset();
        s.feedback_hp_r.Reset();
    }
}

void EchosProcessor::ClearBuffer() {
    if (impl_) impl_->recording_buffer.ImmediateClear();
}

float EchosProcessor::BaseTimeSeconds() const {
    return impl_ ? impl_->base_time.BaseSeconds() : 0.0f;
}

bool EchosProcessor::IsClocked() const {
    return impl_ ? impl_->base_time.IsClocked() : false;
}

float EchosProcessor::DelayTimeSeconds() const {
    if (!impl_ || impl_->sample_rate <= 0.f) return 0.0f;
    return impl_->engine.CurrentDelaySamples() / impl_->sample_rate;
}

} // namespace beadsdelay_dsp

#include "particules_processor.h"
#include "util/dsp_utils.h"
#include "util/cosine_table.h"
#include <cstdint>
#include <cstring>
#include <cmath>
#include <new>
#include <algorithm>

namespace particules_dsp {

static constexpr size_t kImplAlignment = 16;

static size_t AlignUp(size_t size, size_t alignment = kImplAlignment) {
    return (size + alignment - 1) & ~(alignment - 1);
}

ParticulesProcessor::MemoryRequirements ParticulesProcessor::GetMemoryRequirements(float sample_rate) {
    (void)sample_rate;  // Frame budget is fixed; duration varies with rate.
    MemoryRequirements req;

    size_t impl_bytes = AlignUp(sizeof(Impl));
    size_t recording_bytes =
        (kDefaultBufferFrames + kInterpolationTail) * 2 * sizeof(float);
    size_t reverb_bytes = kReverbBufferSize * sizeof(float);

    req.total_bytes = (kImplAlignment - 1) + impl_bytes + AlignUp(recording_bytes) + AlignUp(reverb_bytes);
    req.alignment = kImplAlignment;
    return req;
}

void ParticulesProcessor::Init(void* memory, size_t memory_size, float sample_rate) {
    if (!memory || memory_size == 0) return;

    // Verify the caller provided enough memory.
    auto req = GetMemoryRequirements(sample_rate);
    if (memory_size < req.total_bytes) return;

    // Align the base pointer to the required alignment before placement-new.
    uintptr_t addr = reinterpret_cast<uintptr_t>(memory);
    addr = (addr + kImplAlignment - 1) & ~(kImplAlignment - 1);
    uint8_t* ptr = reinterpret_cast<uint8_t*>(addr);

    // Placement-new the Impl at the aligned start of the memory block
    impl_ = new (ptr) Impl();
    ptr += AlignUp(sizeof(Impl));

    impl_->sample_rate = sample_rate;
    impl_->params = ParticulesParameters{};
    impl_->prev_wet_len = 0;
    impl_->prev_freeze = false;

    // Allocate recording buffer (fixed frame budget; duration varies with sample rate)
    size_t recording_bytes =
        (kDefaultBufferFrames + kInterpolationTail) * 2 * sizeof(float);
    impl_->recording_buffer.Init(reinterpret_cast<float*>(ptr), kDefaultBufferFrames, 2);
    ptr += AlignUp(recording_bytes);

    // Reverb delay memory (DRAM)
    float* reverb_buffer = reinterpret_cast<float*>(ptr);
    ptr += AlignUp(kReverbBufferSize * sizeof(float));
    impl_->reverb.Init(reverb_buffer, kReverbBufferSize, sample_rate);

    // Set initial decimation factor (HiFi = 1x, no change from current behavior)
    impl_->recording_buffer.SetDecimationFactor(
        DecimationFactorForQuality(QualityMode::kHiFi));

    // Initialize sub-processors
    impl_->grain_engine.Init(sample_rate, &impl_->recording_buffer);
    impl_->saturation.Init();
    impl_->quality_processor.Init(sample_rate);
    impl_->auto_gain.Init(sample_rate);

    // Feedback HP filter at ~30Hz to remove DC and sub-bass buildup
    impl_->feedback_hp_l.Init();
    impl_->feedback_hp_l.SetFrequencyHz(30.0f, sample_rate);
    impl_->feedback_hp_l.SetQ(0.707f);
    impl_->feedback_hp_r.Init();
    impl_->feedback_hp_r.SetFrequencyHz(30.0f, sample_rate);
    impl_->feedback_hp_r.SetQ(0.707f);
}

void ParticulesProcessor::SetParameters(const ParticulesParameters& params) {
    if (!impl_) return;
    impl_->params = params;

    // Guard feedback/dry_wet/reverb/density against NaN at the parameter-
    // ingestion boundary. Unlike time/size/shape/pitch CV, these don't get
    // re-derived from scratch every grain -- they drive persistent OnePole
    // smoothing state (smoothed_feedback, smoothed_dry_wet in ProcessBlock()),
    // the reverb's own persistent coefficients (amount_/decay_/diffusion_ in
    // Reverb::SetAmount/SetDecay/SetDiffusion), and the grain scheduler's
    // persistent phasor (latched_phase_ in grain_scheduler.cpp). particules_dsp::Clamp()
    // and rack::math::clamp() are both built on std::min/std::max, which leave
    // a NaN operand unclamped, so nothing upstream reliably sanitizes this.
    // Once one of those state variables is poisoned by a single NaN
    // parameter frame, `state += coeff * (NaN - state)` keeps it NaN
    // forever -- even after the caller goes back to sending valid values --
    // and for the phasor specifically, `latched_phase_ += NaN` poisons it
    // permanently since neither `NaN <= 0` nor `NaN >= 1` is ever true, so
    // the scheduler would never trigger another grain -- so this has to be
    // fixed at ingestion, not downstream.
    if (!std::isfinite(impl_->params.feedback))    impl_->params.feedback    = 0.0f;
    if (!std::isfinite(impl_->params.dry_wet))     impl_->params.dry_wet     = 0.5f;
    if (!std::isfinite(impl_->params.reverb))      impl_->params.reverb      = 0.0f;
    if (!std::isfinite(impl_->params.density))     impl_->params.density     = 0.5f;
    if (!std::isfinite(impl_->params.density_cv))  impl_->params.density_cv  = 0.0f;

    // Detect quality mode change → update decimation factor + start wet duck
    if (params.quality_mode != impl_->prev_quality_mode) {
        impl_->prev_quality_mode = params.quality_mode;
        impl_->recording_buffer.SetDecimationFactor(
            DecimationFactorForQuality(params.quality_mode));
        impl_->recording_buffer.Clear();
        impl_->quality_xfade_counter = Impl::kQualityXfadeSamples;
    }

    // Configure reverb from parameters (use the sanitized copy so a NaN
    // params.reverb from the caller can't reach Reverb::SetAmount/SetDecay).
    impl_->reverb.SetAmount(impl_->params.reverb);
    impl_->reverb.SetDecay(0.3f + impl_->params.reverb * 0.65f);
    impl_->reverb.SetDiffusion(0.7f);
    // Fixed wet makeup: +4 dB, compensating the reverb tail being quieter than
    // the dry (its energy is spread across the decay tail and low-passed).
    static constexpr float kReverbMakeupGain = 1.5849f;  // 10^(4/20)
    impl_->reverb.SetMakeupGain(kReverbMakeupGain);

    // Quality mode affects reverb LP: Tape=warmest, HiFi=brightest
    float reverb_lp;
    switch (params.quality_mode) {
        case QualityMode::kTape:      reverb_lp = 0.3f; break;
        case QualityMode::kCleanLoFi: reverb_lp = 0.5f; break;
        case QualityMode::kClouds:    reverb_lp = 0.6f; break;
        default:                      reverb_lp = 0.7f; break;
    }
    impl_->reverb.SetLpCutoff(reverb_lp);
}

void ParticulesProcessor::Process(const StereoFrame* input, StereoFrame* output,
                              size_t num_frames) {
    if (!impl_) {
        for (size_t i = 0; i < num_frames; ++i) {
            output[i] = {0.0f, 0.0f};
        }
        return;
    }
    // Chunk the full pipeline so every per-block buffer (dry_input_buf,
    // wet_buf) is indexed only by the intra-block offset. This is what makes
    // the types.h promise of arbitrary num_frames true.
    size_t offset = 0;
    while (offset < num_frames) {
        size_t block = std::min(num_frames - offset, kMaxBlockSize);
        ProcessBlock(input + offset, output + offset, block);
        offset += block;
    }
}

void ParticulesProcessor::ProcessBlock(const StereoFrame* input, StereoFrame* output,
                                   size_t num_frames) {
    auto& s = *impl_;  // shorthand

    // Drain any deferred buffer clear (post-quality-change) incrementally.
    // Runs once per <=64-frame block on every host, so the clear rate and the
    // quality-transition duck stay aligned at any caller block size.
    static constexpr size_t kClearChunkFloats = (kDefaultBufferFrames / 128) * 2;
    s.recording_buffer.TickClear(kClearChunkFloats);

    // Freeze transitions: declick the seam / arm the unfreeze write ramp.
    if (s.params.freeze != s.prev_freeze) {
        s.recording_buffer.NotifyFreeze(s.params.freeze);
        s.prev_freeze = s.params.freeze;
    }

    // --- Per-sample input processing (steps 1-4) ---
    for (size_t i = 0; i < num_frames; ++i) {
        StereoFrame in = input[i];

        // Dry tap for the DRY/WET crossfade: pre-gain keeps DRY/WET=0 a
        // bit-exact bypass; post-gain (default) keeps mid-knob mixes
        // level-matched against the auto-gained wet path. Note the post-gain
        // dry also passes AutoGain's SoftLimit, so it is not bit-clean at
        // hot inputs — documented in the manual.
        if (!s.params.dry_post_gain) s.dry_input_buf[i] = in;

        // 1. Auto-gain
        in = s.auto_gain.Process(in, s.params.manual_gain_db, s.params.auto_gain);
        if (s.params.dry_post_gain) s.dry_input_buf[i] = in;

        // 2. Quality input processing
        in = s.quality_processor.ProcessInput(in, s.params.quality_mode);

        // 3. Feedback mix (smoothed to prevent zipper noise)
        OnePole(s.smoothed_feedback, s.params.feedback, 0.002f);
        StereoFrame fb_src = {0.0f, 0.0f};
        if (s.prev_wet_len > 0) {
            size_t fb_idx = (i < s.prev_wet_len) ? i : s.prev_wet_len - 1;
            fb_src = s.prev_wet_buf[fb_idx];
        }
        StereoFrame fb = {
            s.feedback_hp_l.ProcessHP(fb_src.l),
            s.feedback_hp_r.ProcessHP(fb_src.r)
        };
        // Scale source down by (feedback × 0.5) to leave headroom for
        // additive feedback.
        float source_scale = 1.0f - s.smoothed_feedback * 0.5f;
        in.l *= source_scale;
        in.r *= source_scale;
        StereoFrame mixed = in + fb * (s.smoothed_feedback * s.smoothed_feedback);
        in = s.saturation.LimitFeedback(mixed, s.params.quality_mode);

        // 4. Record to buffer (unless frozen)
        if (!s.params.freeze) {
            s.recording_buffer.Write(in);
        }
    }

    // --- Block-based wet signal generation + output processing (steps 5-10) ---
    // wet lives in Impl (DRAM) to keep audio-thread stack usage low.
    StereoFrame* wet = s.wet_buf;

    // Tape mode wow/flutter: compute pitch modulation for this block.
    // The modulation is very slow (0.5Hz wow) so one value per block is fine.
    float pitch_mod = s.quality_processor.GetPitchModulation(s.params.quality_mode, num_frames);
    s.grain_engine.SetPitchModulation(pitch_mod);
    s.grain_engine.Process(s.params, wet, num_frames);

    // Advance dry/wet smoothing and compute equal-power gains once per
    // block.  The OnePole with 0.002 coefficient changes < 0.13% across
    // 64 samples, so per-block cos/sin is inaudible vs per-sample.
    // Closed-form equivalent of running OnePole block times: avoids
    // the O(N) loop and gives the correct end-state in one step.
    {
        // Closed-form one-pole advance for the block; the pow only reruns
        // when the host's block size changes (it never does within a run).
        if (num_frames != s.dry_wet_coeff_frames) {
            s.dry_wet_coeff_frames = num_frames;
            s.dry_wet_coeff =
                1.0f - std::pow(1.0f - 0.002f, static_cast<float>(num_frames));
        }
        s.smoothed_dry_wet += s.dry_wet_coeff * (s.params.dry_wet - s.smoothed_dry_wet);
    }
    float dw_phase = s.smoothed_dry_wet * 0.25f;
    float dry_gain = CosLookup(dw_phase);
    float wet_gain = CosLookup(dw_phase - 0.25f);

    // Per-sample output processing for this block
    for (size_t i = 0; i < num_frames; ++i) {
        StereoFrame wet_frame = wet[i];

        // Quality mode transition: V-shaped duck on wet signal
        if (s.quality_xfade_counter > 0) {
            float phase = 1.0f - static_cast<float>(s.quality_xfade_counter)
                        / static_cast<float>(Impl::kQualityXfadeSamples);
            // phase: 0 at start → 1 at end
            float duck;
            if (phase < 0.5f) {
                duck = 1.0f - phase * 2.0f;   // 1 → 0
            } else {
                duck = (phase - 0.5f) * 2.0f;  // 0 → 1
            }
            wet_frame *= duck;
            s.quality_xfade_counter--;
        }

        // 6. Quality output processing
        wet_frame = s.quality_processor.ProcessOutput(wet_frame, s.params.quality_mode);

        // 7. Capture this block's wet frame for the next block's feedback
        // (before reverb). Guard against NaN/inf poisoning the loop.
        if (std::isfinite(wet_frame.l) && std::isfinite(wet_frame.r)) {
            s.prev_wet_buf[i] = wet_frame;
        } else {
            s.prev_wet_buf[i] = {0.0f, 0.0f};
        }

        // 8. Dry/wet crossfade (equal-power, gains precomputed per block)
        StereoFrame in_frame = s.dry_input_buf[i];
        StereoFrame mixed = {
            in_frame.l * dry_gain + wet_frame.l * wet_gain,
            in_frame.r * dry_gain + wet_frame.r * wet_gain
        };

        // 9. Reverb
        float rev_l, rev_r;
        s.reverb.Process(mixed.l, mixed.r, &rev_l, &rev_r);
        mixed = {rev_l, rev_r};

        // 10. Output
        output[i] = mixed;
    }

    s.prev_wet_len = num_frames;
}

int ParticulesProcessor::ActiveGrainCount() const {
    return impl_ ? impl_->grain_engine.ActiveGrainCount() : 0;
}

bool ParticulesProcessor::GrainTriggeredThisBlock() const {
    return impl_ ? impl_->grain_engine.GrainTriggeredThisBlock() : false;
}

float ParticulesProcessor::InputLevel() const {
    return impl_ ? impl_->auto_gain.InputLevel() : 0.0f;
}

float ParticulesProcessor::AutoGainDb() const {
    return impl_ ? impl_->auto_gain.GainDb() : 0.0f;
}

void ParticulesProcessor::TriggerAutoGainCalibration() {
    if (impl_) impl_->auto_gain.StartCalibration();
}

void ParticulesProcessor::ClearBuffer() {
    if (impl_) impl_->recording_buffer.ImmediateClear();
}

void ParticulesProcessor::LoadScale(const double* ratios, uint32_t num_notes) {
    if (impl_) impl_->grain_engine.LoadScale(ratios, num_notes);
}

void ParticulesProcessor::ClearScale() {
    if (impl_) impl_->grain_engine.ClearScale();
}

void ParticulesProcessor::SetScaleRoot(int midi_note) {
    if (impl_) impl_->grain_engine.SetScaleRoot(midi_note);
}

} // namespace particules_dsp

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

    // Initial decimation factor (HiFi = 1x); RecordingBuffer::Init already
    // defaults to 1, but set it explicitly to document the tie to
    // prev_quality's kHiFi default (mirrors Particules::Init).
    impl_->recording_buffer.SetDecimationFactor(
        particules_dsp::DecimationFactorForQuality(QualityMode::kHiFi));

    impl_->saturation.Init();
    impl_->quality_processor.Init(sample_rate);

    // Feedback HP filter at 10 Hz to remove DC buildup in the feedback path.
    // Q must be 0.707 (Butterworth): the SVF's default Q=1 peaks at ~1.155x
    // just above cutoff, which pushes the loop gain over 1 for feedback
    // settings >= ~0.87 and grows a ~30 Hz oscillation out of nothing.
    // Particules sets the same Q on its feedback HP for the same reason.
    impl_->feedback_hp_l.Init();
    impl_->feedback_hp_l.SetFrequencyHz(10.0f, sample_rate);
    impl_->feedback_hp_l.SetQ(0.707f);
    impl_->feedback_hp_r.Init();
    impl_->feedback_hp_r.SetFrequencyHz(10.0f, sample_rate);
    impl_->feedback_hp_r.SetQ(0.707f);

    impl_->base_time.Init(sample_rate, kBufferSeconds);
    impl_->engine.Init(&impl_->recording_buffer, sample_rate);
    impl_->shifter.Init(sample_rate);
    impl_->envelope.Init(sample_rate);

    // Slow-random modulation: one shared PRNG, three distinctly-salted LFOs
    // (salts are labels only — see Impl comment; determinism is intentional).
    impl_->mod_rng.Init();
    impl_->ar_time.lfo.Init(&impl_->mod_rng, 1);
    impl_->ar_pitch.lfo.Init(&impl_->mod_rng, 2);
    impl_->ar_shape.lfo.Init(&impl_->mod_rng, 3);
}

void EchosProcessor::SetParameters(const EchosParameters& params) {
    if (!impl_) return;
    impl_->params = params;

    // Guard every float field against NaN at the parameter-ingestion
    // boundary. Several of these drive persistent state that a single NaN
    // permanently poisons: EchoEngine::delay_frames_/target_frames_ (a
    // `state += coeff * (NaN - state)` slew never recovers), the per-sample
    // OnePole-smoothed dry_wet/feedback (same mechanism), and
    // RotaryShifter::phase_ (`phase_ += NaN` poisons it since neither
    // `NaN >= 1` nor `NaN < 0` is ever true, so the wrap-around checks never
    // fire). Adapter-side CV sanitization (Echos.cpp updateSlowParams) is the
    // first line of defense; this is belt-and-braces for any other caller
    // (tests, a future host) and for knob/AR fields the adapter doesn't
    // separately guard. particules_dsp::Clamp() is built on std::min/std::max,
    // which leave a NaN operand unclamped, so nothing upstream reliably
    // sanitizes this -- it has to be fixed at ingestion. Defaults mirror each
    // field's declared default in EchosParameters (types.h).
    EchosParameters& p = impl_->params;
    if (!std::isfinite(p.density))         p.density         = 0.5f;
    if (!std::isfinite(p.time))            p.time            = 0.0f;
    if (!std::isfinite(p.pitch_semitones)) p.pitch_semitones = 0.0f;
    if (!std::isfinite(p.shape))           p.shape           = 0.0f;
    if (!std::isfinite(p.feedback))        p.feedback        = 0.0f;
    if (!std::isfinite(p.dry_wet))         p.dry_wet         = 0.5f;
    if (!std::isfinite(p.density_cv))      p.density_cv      = 0.0f;
    if (!std::isfinite(p.time_cv))         p.time_cv         = 0.0f;
    if (!std::isfinite(p.pitch_cv))        p.pitch_cv        = 0.0f;
    if (!std::isfinite(p.shape_cv))        p.shape_cv        = 0.0f;
    if (!std::isfinite(p.feedback_cv))     p.feedback_cv     = 0.0f;
    if (!std::isfinite(p.dry_wet_cv))      p.dry_wet_cv      = 0.0f;
    if (!std::isfinite(p.time_ar))         p.time_ar         = 0.0f;
    if (!std::isfinite(p.pitch_ar))        p.pitch_ar        = 0.0f;
    if (!std::isfinite(p.shape_ar))        p.shape_ar        = 0.0f;
    if (!std::isfinite(p.input_trim_db))   p.input_trim_db   = 0.0f;
    if (!std::isfinite(p.slew_seconds))    p.slew_seconds    = kSlewSecondsDefault;
    if (!std::isfinite(p.random_lfo_hz))   p.random_lfo_hz   = kRandomLfoHz;
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

    // Drain any deferred buffer clear (post-quality-change) incrementally.
    // Chunk size chosen so draining the whole buffer takes 128 blocks —
    // same derivation Particules uses for its identical constant, aligned
    // with kQualityXfadeSamples (128 × kMaxBlockSize == 8192).
    static constexpr size_t kClearChunkFloats = (kBufferFrames / 128) * 2;
    s.recording_buffer.TickClear(kClearChunkFloats);

    // Quality mode change: new decimation factor, deferred buffer clear +
    // output duck, and notify BaseTimeControl of the new effective buffer
    // duration (DENSITY's manual-mode delay scales with it). Ignored while
    // frozen: clearing/decimating the buffer out from under a frozen slice
    // would corrupt the frozen loop content. Also ignored on the exact block
    // where freeze falls (freeze_falling_edge): EchoEngine::NotifyFreeze's
    // unfreeze continuity math (below, via SetTargets/NotifyFreeze) still
    // needs this block's pre-clear write head/frozen_read_pos to compute
    // delay_frames_ correctly — Clear() resets write_head_ synchronously, so
    // applying the change here would corrupt that math. The deferred change
    // is instead picked up on the first block where both this block and the
    // previous one are unfrozen (one block after the falling edge).
    bool freeze_falling_edge = s.prev_freeze && !s.params.freeze;
    if (s.params.quality != s.prev_quality && !s.params.freeze && !freeze_falling_edge) {
        s.prev_quality = s.params.quality;
        int decimation = particules_dsp::DecimationFactorForQuality(s.params.quality);
        s.recording_buffer.SetDecimationFactor(decimation);
        s.recording_buffer.Clear();
        s.quality_xfade_counter = Impl::kQualityXfadeSamples;
        float effective_seconds = static_cast<float>(kBufferFrames) *
                                   static_cast<float>(decimation) / s.sample_rate;
        s.base_time.SetBufferSeconds(effective_seconds);
    }

    // Block-rate: slow-random LFO rate is cheap to set every block.
    s.ar_time.lfo.SetRate(s.params.random_lfo_hz, s.sample_rate);
    s.ar_pitch.lfo.SetRate(s.params.random_lfo_hz, s.sample_rate);
    s.ar_shape.lfo.SetRate(s.params.random_lfo_hz, s.sample_rate);

    // Block-rate: TIME attenurandomizer modulates the TIME knob before it
    // reaches BaseTimeControl.
    float time_mod = s.ar_time.Process(s.params.time_ar, s.params.time_cv / 5.f,
                                        s.params.time_cv_connected, n);
    float time_knob_eff = particules_dsp::Clamp(s.params.time + time_mod, 0.f, 1.f);

    // Block-rate: resolve base/delay time and push targets into the engine.
    BaseTimeControl::Result bt = s.base_time.Update(
        s.params.density, s.params.density_cv, time_knob_eff,
        s.params.clock_connected, s.params.clock_tick_offset, n);
    float slew_seconds = std::max(s.params.slew_seconds, 1e-3f);
    s.engine.SetTargets(bt.delay_samples, bt.multi_tap, s.params.time_change_mode, slew_seconds);
    // FREEZE: called every block (edge-detected internally). While frozen,
    // TIME/DENSITY can still retarget the slice window/length live.
    s.engine.NotifyFreeze(s.params.freeze, bt.base_samples, bt.slice_index);

    // Block-rate: SHAPE attenurandomizer modulates the repeat envelope's
    // shape. The repeat envelope tracks the base (HOST-rate) period and
    // resyncs to phase 0 on a clock tick.
    float shape_mod = s.ar_shape.Process(s.params.shape_ar, s.params.shape_cv / 5.f,
                                          s.params.shape_cv_connected, n);
    float shape_eff = particules_dsp::Clamp(s.params.shape + shape_mod, 0.f, 1.f);
    s.envelope.SetPeriodSamples(bt.base_samples);
    s.envelope.SetShape(shape_eff);
    if (s.params.clock_tick_offset >= 0) s.envelope.SyncPhase();

    // Block-rate: linear input trim gain.
    float input_gain = particules_dsp::DbToGain(s.params.input_trim_db);

    // Block-rate: pitch-shift ratio for the rotary shifter. PITCH's
    // attenurandomizer is always advanced (kept phase-continuous) even when
    // the CV-direct 1V/oct branch below overrides its contribution.
    float ar_pitch_val = s.ar_pitch.Process(s.params.pitch_ar, s.params.pitch_cv / 5.f,
                                             s.params.pitch_cv_connected, n);
    float pitch_mod_semi;
    if (s.params.pitch_ar > 0.f && s.params.pitch_cv_connected) {
        // CV+CW: exact 1 V/oct at full clockwise AR.
        pitch_mod_semi = s.params.pitch_ar * s.params.pitch_cv * 12.f;
    } else {
        // Unpatched/CCW: random walk or peaked randomization, spanning ±24 st.
        pitch_mod_semi = ar_pitch_val * 24.f;
    }
    float pitch_semi_eff = particules_dsp::Clamp(
        s.params.pitch_semitones + pitch_mod_semi, -24.f, 24.f);
    float shift_ratio = std::fabs(pitch_semi_eff) < kShifterBypassSemitones
                             ? 1.f
                             : std::exp2(pitch_semi_eff / 12.f);
    s.shifter.SetRatio(shift_ratio);

    // Block-rate: FEEDBACK/BLEND CV (no AR), smoothing targets.
    float feedback_knob = particules_dsp::Clamp(
        s.params.feedback + s.params.feedback_cv / 5.f, 0.f, 1.f);
    // FEEDBACK gain range is 0..1.1, not 0..1: per spec, >1 loop gain is
    // signature Beads behavior (runaway/self-oscillation is a feature, not
    // a bug to clamp away). Piecewise so the first 90% of knob travel still
    // maps onto the familiar 0..1 unity-at-max feel (unity lands exactly at
    // knob position 0.9, not squeezed/stretched across the whole range), and
    // the last 10% opens up to 1.1 gain. Saturation::LimitFeedback
    // (per-quality, applied below) bounds the loop regardless of how far
    // past unity this pushes it, so runaway is reachable but never unsafe.
    float feedback_eff = (feedback_knob <= 0.9f)
                              ? feedback_knob / 0.9f
                              : 1.0f + (feedback_knob - 0.9f);
    float dry_wet_eff = particules_dsp::Clamp(
        s.params.dry_wet + s.params.dry_wet_cv / 5.f, 0.f, 1.f);

    // Block-rate: tape mode wow/flutter — a slow pitch-ratio wobble applied
    // to the engine's per-sample read advance for this block. All other
    // modes return exactly 1.0 (no modulation).
    float read_rate_scale = s.quality_processor.GetPitchModulation(s.params.quality, n);
    s.engine.SetReadRateScale(read_rate_scale);

    bool feedback_state_bad = false;

    for (size_t i = 0; i < n; ++i) {
        particules_dsp::OnePole(s.smoothed_dry_wet, dry_wet_eff, 0.05f);
        particules_dsp::OnePole(s.smoothed_feedback, feedback_eff, 0.05f);

        // NaN guard: sanitize input before it can reach the engine/buffer.
        StereoFrame input = in[i];
        if (!std::isfinite(input.l)) input.l = 0.f;
        if (!std::isfinite(input.r)) input.r = 0.f;

        // Pitch shift sits inside the feedback loop (shimmer): the shifted
        // signal is what gets tapped back for feedback and mixed as wet.
        StereoFrame wet = s.shifter.Process(s.engine.ReadWet());

        // Quality mode transition: V-shaped duck on the wet signal while
        // the deferred buffer clear (started above) drains, so the abrupt
        // content change doesn't click. Mirrors Particules' identical duck.
        if (s.quality_xfade_counter > 0) {
            float phase = 1.f - static_cast<float>(s.quality_xfade_counter) /
                                     static_cast<float>(Impl::kQualityXfadeSamples);
            float duck = (phase < 0.5f) ? (1.f - phase * 2.f) : (phase - 0.5f) * 2.f;
            wet.l *= duck;
            wet.r *= duck;
            s.quality_xfade_counter--;
        }

        // Quality output coloring (LP / 12-bit quantize / tape mu-law
        // expand), applied after the shifter, before the repeat envelope.
        wet = s.quality_processor.ProcessOutput(wet, s.params.quality);

        // SHAPE repeat envelope: amplitude-shapes the wet signal per delay
        // period. Feedback normally taps the post-envelope signal (a closed
        // gate quiets the feedback loop too); envelope_pre_feedback instead
        // lets feedback sustain repeats unshaped, chopping only the output.
        StereoFrame fb_src_pre = wet;
        float env_gain = s.envelope.Next();
        wet.l *= env_gain;
        wet.r *= env_gain;
        StereoFrame fb_src = s.params.envelope_pre_feedback ? fb_src_pre : wet;

        StereoFrame fb_in{fb_src.l * s.smoothed_feedback, fb_src.r * s.smoothed_feedback};
        StereoFrame fb = s.saturation.LimitFeedback(fb_in, s.params.quality);
        fb.l = s.feedback_hp_l.ProcessHP(fb.l);
        fb.r = s.feedback_hp_r.ProcessHP(fb.r);
        if (!std::isfinite(fb.l) || !std::isfinite(fb.r)) {
            fb.l = 0.f;
            fb.r = 0.f;
            feedback_state_bad = true;
        }

        float trimmed_l = input.l * input_gain;
        float trimmed_r = input.r * input_gain;

        // Quality input coloring (LP / mono-sum + hiss + mu-law compress),
        // applied to the input+feedback sum before it's written — feedback
        // passes through input processing too (authentic per spec diagram).
        StereoFrame to_write = s.quality_processor.ProcessInput(
            StereoFrame{trimmed_l + fb.l, trimmed_r + fb.r}, s.params.quality);

        // FREEZE: stop writes so the buffer content underneath the frozen
        // slice(s) stays put. The feedback chain above still runs into the
        // void (computed but discarded here) so unfreeze doesn't pop from a
        // stale/decayed feedback state suddenly resuming.
        if (!s.params.freeze) {
            s.recording_buffer.Write(to_write.l, to_write.r);
        }

        // Input trim sits at the very front of the signal flow per spec, so
        // it applies to the dry tap here too, not just the path written to
        // the delay buffer above. Default 0 dB -> gain 1.0 -> no change.
        float mix = s.smoothed_dry_wet;
        out[i].l = trimmed_l * (1.f - mix) + wet.l * mix;
        out[i].r = trimmed_r * (1.f - mix) + wet.r * mix;
    }

    // NaN guard: flush feedback/filter state once per block (not per sample)
    // if it ever went non-finite.
    if (feedback_state_bad || !std::isfinite(s.smoothed_feedback) ||
        !std::isfinite(s.smoothed_dry_wet)) {
        s.smoothed_feedback = feedback_eff;
        s.smoothed_dry_wet = dry_wet_eff;
        s.feedback_hp_l.Reset();
        s.feedback_hp_r.Reset();
    }

    // Record this block's freeze state for next block's falling-edge check
    // (see the quality-change branch above).
    s.prev_freeze = s.params.freeze;
}

void EchosProcessor::ClearBuffer() {
    if (!impl_) return;
    impl_->recording_buffer.ImmediateClear();
    // The feedback HP filters carry their own memory independent of the
    // delay buffer's content. Left un-reset, their residual free-decay
    // output gets written back into the (now silent) buffer and re-read
    // through the still-active feedback loop, regenerating near the
    // feedback gain each round trip — with a short delay time and feedback
    // near unity this circulates far longer than a buffer clear should
    // audibly last. Reset them alongside the buffer so a user-triggered
    // clear actually silences the feedback tail, not just the recording.
    impl_->feedback_hp_l.Reset();
    impl_->feedback_hp_r.Reset();
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

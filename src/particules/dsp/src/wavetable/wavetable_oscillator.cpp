#include "wavetable_oscillator.h"
#include "../util/dsp_utils.h"
#include "../util/interpolation.h"
#include "../util/fast_exp2.h"

namespace particules_dsp {

// Ondes.cpp calls Process() with num_frames == 1 once per audio sample (no
// internal audio-rate block processing), so anything computed "per call"
// here really runs per sample. On MetaModule (Cortex-A7, no -ffast-math)
// exp2f/fmodf are out-of-line libm calls and divides are unpipelined VDIVs,
// so both the pitch-ratio math and the bank/wave region math are cached
// below (value-compare: recompute only when the input actually changed),
// and the fast divide-free Exp2Fast replaces exp2f for the pitch ratio.

void WavetableOscillator::Init(float sample_rate) {
    sample_rate_ = sample_rate;
    provider_ = nullptr;
    phase_ = 0.0f;
    phase_increment_ = 0.0f;
    phase_scale_ = static_cast<float>(kWavetableSize) / sample_rate;
    last_pitch_ = -1e9f;
    last_bank_ = -1.0f;
    last_wave_ = -1.0f;
    num_banks_ = 0;
    waveforms_per_bank_ = 0;
}

void WavetableOscillator::SetProvider(WavetableProvider* provider) {
    provider_ = provider;
    num_banks_ = provider_ ? provider_->NumBanksAvailable() : 0;
    waveforms_per_bank_ = provider_ ? provider_->WaveformsPerBank() : 0;
    last_bank_ = -1.0f;
    last_wave_ = -1.0f;
}

void WavetableOscillator::Process(float pitch_semitones, float bank, float wave,
                                  StereoFrame* output, size_t num_frames) {
    if (!provider_ || num_banks_ == 0 || waveforms_per_bank_ == 0) {
        for (size_t i = 0; i < num_frames; ++i) output[i] = {0.0f, 0.0f};
        return;
    }

    constexpr float kBaseFreq = 261.63f;  // Middle C

    if (pitch_semitones != last_pitch_) {
        last_pitch_ = pitch_semitones;
        float clamped = Clamp(pitch_semitones, -120.0f, 120.0f);
        phase_increment_ = kBaseFreq * SemitonesToRatioFast(clamped) * phase_scale_;
    }

    if (bank != last_bank_ || wave != last_wave_) {
        last_bank_ = bank;
        last_wave_ = wave;

        float bank_pos = bank * static_cast<float>(num_banks_ - 1);
        int bank_lo = static_cast<int>(bank_pos);
        if (bank_lo < 0) bank_lo = 0;
        if (bank_lo >= num_banks_) bank_lo = num_banks_ - 1;
        int bank_hi = bank_lo + 1;
        if (bank_hi >= num_banks_) bank_hi = bank_lo;
        bank_frac_ = bank_pos - static_cast<float>(bank_lo);

        float wave_pos = wave * static_cast<float>(waveforms_per_bank_ - 1);
        int wave_lo = static_cast<int>(wave_pos);
        if (wave_lo < 0) wave_lo = 0;
        if (wave_lo >= waveforms_per_bank_) wave_lo = waveforms_per_bank_ - 1;
        int wave_hi = wave_lo + 1;
        if (wave_hi >= waveforms_per_bank_) wave_hi = wave_lo;
        wave_frac_ = wave_pos - static_cast<float>(wave_lo);

        w_ll_ = provider_->GetWaveform(bank_lo, wave_lo);
        w_lh_ = provider_->GetWaveform(bank_lo, wave_hi);
        w_hl_ = provider_->GetWaveform(bank_hi, wave_lo);
        w_hh_ = provider_->GetWaveform(bank_hi, wave_hi);
    }

    if (!w_ll_ || !w_lh_ || !w_hl_ || !w_hh_) {
        for (size_t i = 0; i < num_frames; ++i) output[i] = {0.0f, 0.0f};
        return;
    }

    for (size_t i = 0; i < num_frames; ++i) {
        int phase_int = static_cast<int>(phase_);
        float phase_frac = phase_ - static_cast<float>(phase_int);
        phase_int = phase_int & (kWavetableSize - 1);
        int next_idx = (phase_int + 1) & (kWavetableSize - 1);

        float s_ll = InterpolateLinear(w_ll_[phase_int], w_ll_[next_idx], phase_frac);
        float s_lh = InterpolateLinear(w_lh_[phase_int], w_lh_[next_idx], phase_frac);
        float s_hl = InterpolateLinear(w_hl_[phase_int], w_hl_[next_idx], phase_frac);
        float s_hh = InterpolateLinear(w_hh_[phase_int], w_hh_[next_idx], phase_frac);

        float sample_lo = Crossfade(s_ll, s_lh, wave_frac_);
        float sample_hi = Crossfade(s_hl, s_hh, wave_frac_);
        float sample = Crossfade(sample_lo, sample_hi, bank_frac_);

        output[i] = {sample, sample};

        // phase_int is already masked into [0, kWavetableSize), so
        // phase_int + phase_frac reconstructs the current phase_ exactly;
        // adding phase_increment_ and wrapping with an exact conditional
        // subtract (increment <= ~1429, so a handful of iterations at most)
        // replaces fmod with bit-exact float subtraction of kWavetableSize
        // (a power of two, so no precision is lost).
        phase_ = static_cast<float>(phase_int) + phase_frac + phase_increment_;
        while (phase_ >= static_cast<float>(kWavetableSize))
            phase_ -= static_cast<float>(kWavetableSize);
    }
}

} // namespace particules_dsp

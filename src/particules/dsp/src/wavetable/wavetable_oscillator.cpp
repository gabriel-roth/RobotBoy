#include "wavetable_oscillator.h"
#include "../util/dsp_utils.h"
#include "../util/interpolation.h"

#include <cmath>

namespace particules_dsp {

void WavetableOscillator::Init(float sample_rate) {
    sample_rate_ = sample_rate;
    provider_ = nullptr;
    phase_ = 0.0f;
    phase_increment_ = 0.0f;
}

void WavetableOscillator::SetProvider(WavetableProvider* provider) {
    provider_ = provider;
}

void WavetableOscillator::Process(float pitch_semitones, float bank, float wave,
                                  StereoFrame* output, size_t num_frames) {
    if (!provider_ || provider_->NumBanksAvailable() == 0) {
        for (size_t i = 0; i < num_frames; ++i) output[i] = {0.0f, 0.0f};
        return;
    }

    constexpr float kBaseFreq = 261.63f;  // Middle C
    float clamped_pitch = Clamp(pitch_semitones, -120.0f, 120.0f);
    float frequency = kBaseFreq * SemitonesToRatio(clamped_pitch);
    phase_increment_ = frequency / sample_rate_ * static_cast<float>(kWavetableSize);

    int num_banks = provider_->NumBanksAvailable();
    int waveforms_per_bank = provider_->WaveformsPerBank();
    if (num_banks == 0 || waveforms_per_bank == 0) {
        for (size_t i = 0; i < num_frames; ++i) output[i] = {0.0f, 0.0f};
        return;
    }

    float bank_pos = bank * static_cast<float>(num_banks - 1);
    int bank_lo = static_cast<int>(bank_pos);
    if (bank_lo < 0) bank_lo = 0;
    if (bank_lo >= num_banks) bank_lo = num_banks - 1;
    int bank_hi = bank_lo + 1;
    if (bank_hi >= num_banks) bank_hi = bank_lo;
    float bank_frac = bank_pos - static_cast<float>(bank_lo);

    float wave_pos = wave * static_cast<float>(waveforms_per_bank - 1);
    int wave_lo = static_cast<int>(wave_pos);
    if (wave_lo < 0) wave_lo = 0;
    if (wave_lo >= waveforms_per_bank) wave_lo = waveforms_per_bank - 1;
    int wave_hi = wave_lo + 1;
    if (wave_hi >= waveforms_per_bank) wave_hi = wave_lo;
    float wave_frac = wave_pos - static_cast<float>(wave_lo);

    const float* w_ll = provider_->GetWaveform(bank_lo, wave_lo);
    const float* w_lh = provider_->GetWaveform(bank_lo, wave_hi);
    const float* w_hl = provider_->GetWaveform(bank_hi, wave_lo);
    const float* w_hh = provider_->GetWaveform(bank_hi, wave_hi);
    if (!w_ll || !w_lh || !w_hl || !w_hh) {
        for (size_t i = 0; i < num_frames; ++i) output[i] = {0.0f, 0.0f};
        return;
    }

    for (size_t i = 0; i < num_frames; ++i) {
        int phase_int = static_cast<int>(phase_);
        float phase_frac = phase_ - static_cast<float>(phase_int);
        phase_int = phase_int & (kWavetableSize - 1);
        int next_idx = (phase_int + 1) & (kWavetableSize - 1);

        float s_ll = InterpolateLinear(w_ll[phase_int], w_ll[next_idx], phase_frac);
        float s_lh = InterpolateLinear(w_lh[phase_int], w_lh[next_idx], phase_frac);
        float s_hl = InterpolateLinear(w_hl[phase_int], w_hl[next_idx], phase_frac);
        float s_hh = InterpolateLinear(w_hh[phase_int], w_hh[next_idx], phase_frac);

        float sample_lo = Crossfade(s_ll, s_lh, wave_frac);
        float sample_hi = Crossfade(s_hl, s_hh, wave_frac);
        float sample = Crossfade(sample_lo, sample_hi, bank_frac);

        output[i] = {sample, sample};

        phase_ += phase_increment_;
        float table_size_f = static_cast<float>(kWavetableSize);
        phase_ = std::fmod(phase_, table_size_f);
        if (phase_ < 0.0f) phase_ += table_size_f;
    }
}

} // namespace particules_dsp

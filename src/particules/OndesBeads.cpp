#include "Ondes.hpp"
#include "ondes_pitch_map.hpp"

#include "ondes_voice.h"
#include "stmlib/dsp/hysteresis_quantizer.h"

#include <algorithm>
#include <atomic>
#include <cmath>

// Ondes' Beads engine: the wavetable voice of Mutable Instruments Beads (the
// oscillator Beads plays when nothing is patched into its audio inputs),
// running on the Beads source: 12 banks of 16 band-limited waves, with
// Position morphing through a bank and Bank switching banks via a short
// crossfade. The context menu can oversample the voice (ondes_voice.h).

static constexpr int kOndesNumBanks = 12;  // beads/dsp/synth.h kNumBanks
static_assert(WAV_INTEGRATED_WAVES_SIZE ==
	kOndesNumBanks * beads::kNumWavesPerTable * (beads::kTableSize + 4),
	"Beads wavetable layout");

// Ondes keeps VCV's 0 V = C4 tuning; Beads' voice is rooted on C3.
static constexpr float kOndesTuningSemitones = 12.f;
// Beads' Digital (16-bit) setting; Ondes has no Quality control.
static constexpr int kOndesBitShift = 16;

// One cycle of the wave the voice is playing: Beads stores each wave
// integrated, so the shape is the table's first difference.
static float ondesWaveSample(int bank, float position, int i) {
	float wave = rack::math::clamp(position, 0.f, 1.f) * float(beads::kNumWavesPerTable - 1);
	int wave_integral = std::min(int(wave), int(beads::kNumWavesPerTable) - 2);
	float wave_fractional = wave - float(wave_integral);
	const int16_t* a = beads::wav_integrated_waves
		+ (bank * beads::kNumWavesPerTable + wave_integral) * (beads::kTableSize + 4);
	const int16_t* b = a + beads::kTableSize + 4;
	float da = float(a[i + 2]) - float(a[i + 1]);
	float db = float(b[i + 2]) - float(b[i + 1]);
	return da + (db - da) * wave_fractional;
}

// The wave scaled to its own peak, at index i of n points. Stateless: the
// peak is rescanned per call (256 samples), which is cheap for a display.
static float ondesBeadsTrace(int bank, float position, int i, int n) {
	float peak = 1e-6f;
	for (int k = 0; k < (int)beads::kTableSize; ++k)
		peak = std::max(peak, std::fabs(ondesWaveSample(bank, position, k)));
	const int idx = n == (int)beads::kTableSize ? i : (int)((int64_t)i * beads::kTableSize / n);
	return ondesWaveSample(bank, position, idx % (int)beads::kTableSize) / peak;
}

float OndesPreviewSample(int i, int n) { return ondesBeadsTrace(0, 0.f, i, n); }

namespace {

class OndesBeads : public OndesEngine {
 public:
	explicit OndesBeads(Ondes& m) : m_(m) {
		onSampleRateChange(APP->engine->getSampleRate());
	}

	void onSampleRateChange(float sample_rate) override {
		sample_rate_ = sample_rate;
		voice_.Init(sample_rate_, m_.oversampling_);
		// Beads' synth: 12 banks, 0.048 of hysteresis between them.
		bank_quantizer_.Init(kOndesNumBanks, 0.048f, false);
	}

	void process(const rack::Module::ProcessArgs&) override {
		using rack::math::clamp;
		float pitch = ondesKnobToSemitones(m_.params[Ondes::PITCH_PARAM].getValue())
		            + m_.inputs[Ondes::VOCT_INPUT].getVoltage() * 12.f
		            + kOndesTuningSemitones;
		// Beads' SemitonesToRatio table covers +-128 semitones.
		pitch = std::isfinite(pitch) ? clamp(pitch, -120.f, 120.f) : 0.f;
		float position = m_.params[Ondes::POSITION_PARAM].getValue()
			+ m_.inputs[Ondes::POSITION_INPUT].getVoltage() * 0.2f * m_.params[Ondes::POSITION_AMT_PARAM].getValue();
		position = std::isfinite(position) ? clamp(position, 0.f, 1.f) : 0.f;
		float bank = m_.params[Ondes::BANK_PARAM].getValue()
			+ m_.inputs[Ondes::BANK_INPUT].getVoltage() * 0.2f * m_.params[Ondes::BANK_AMT_PARAM].getValue();
		bank = std::isfinite(bank) ? clamp(bank, 0.f, 1.f) : 0.f;

		// A menu change restarts the voice at its new rate.
		if (m_.oversampling_ != voice_.oversampling())
			voice_.Init(sample_rate_, m_.oversampling_);

		const int bank_index = bank_quantizer_.Process(bank);
		last_bank_.store(bank_index, std::memory_order_relaxed);
		last_wave_.store(position, std::memory_order_relaxed);

		const float out = voice_.Render(bank_index, kOndesBitShift, pitch, position);
		m_.outputs[Ondes::OUT_OUTPUT].setVoltage(out * 5.f);
	}

	float displaySample(int i, int n) const override {
		const int bank = rack::math::clamp(last_bank_.load(std::memory_order_relaxed), 0, kOndesNumBanks - 1);
		return ondesBeadsTrace(bank, last_wave_.load(std::memory_order_relaxed), i, n);
	}

 private:
	Ondes& m_;
	robotboy_beads::OndesVoice voice_;
	stmlib::HysteresisQuantizer2 bank_quantizer_;
	float sample_rate_ = 48000.f;
	// Last bank (0-11) and position (0-1), read by the panel display. A
	// UI-thread read of an audio-thread write is a benign race (display only).
	std::atomic<int> last_bank_{0};
	std::atomic<float> last_wave_{0.f};
};

}  // namespace

std::unique_ptr<OndesEngine> MakeOndesBeads(Ondes& m) {
	return std::make_unique<OndesBeads>(m);
}

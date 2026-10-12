#include "Ondes.hpp"
#include "ondes_pitch_map.hpp"

#include "stmlib/dsp/dsp.h"
#include "beads/resources.h"
#include "beads/dsp/frame.h"
#include "beads/dsp/granular_player.h"
#include "beads/dsp/wavetable_grain.h"   // table layout + InterpolateWaveHermite
#include "beads/dsp/wavetable_voice.h"
#include "stmlib/dsp/hysteresis_quantizer.h"

#include <algorithm>
#include <atomic>
#include <cmath>

// Ondes' Beads engine: the wavetable voice of Mutable Instruments Beads (the
// oscillator Beads plays when nothing is patched into its audio inputs),
// running on the Beads source: 12 banks of 16 band-limited waves, with
// Position morphing through a bank and Bank switching banks via a short
// crossfade.

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
		// Position and pitch are computed here, so the voice sees plain
		// values: no internal randomization, no CV path of its own.
		auto plain = [](beads::RandomizedParameter* r) {
			r->base = 0.f;
			r->attenuverter = 0.f;
			r->cv = 0.f;
			r->has_cv = false;
		};
		plain(&parameters_.time);
		plain(&parameters_.size);
		plain(&parameters_.shape);
		plain(&parameters_.pitch);
		parameters_.monophonic_mode = false;
		parameters_.pitch_lock = nullptr;

		onSampleRateChange(APP->engine->getSampleRate());
	}

	void onSampleRateChange(float sample_rate) override {
		voice_.Init(sample_rate);
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

		parameters_.pitch.base = pitch;
		parameters_.time.base = position;
		const int bank_index = bank_quantizer_.Process(bank);
		last_bank_.store(bank_index, std::memory_order_relaxed);
		last_wave_.store(position, std::memory_order_relaxed);

		beads::FloatFrame out;
		out.l = out.r = 0.f;
		const bool freeze = false;
		voice_.Render(bank_index, kOndesBitShift, &freeze, parameters_, &out, 1);
		m_.outputs[Ondes::OUT_OUTPUT].setVoltage(out.l * 5.f);
	}

	float displaySample(int i, int n) const override {
		const int bank = rack::math::clamp(last_bank_.load(std::memory_order_relaxed), 0, kOndesNumBanks - 1);
		return ondesBeadsTrace(bank, last_wave_.load(std::memory_order_relaxed), i, n);
	}

 private:
	Ondes& m_;
	beads::WavetableVoice voice_;
	stmlib::HysteresisQuantizer2 bank_quantizer_;
	beads::Parameters parameters_;
	// Last bank (0-11) and position (0-1), read by the panel display. A
	// UI-thread read of an audio-thread write is a benign race (display only).
	std::atomic<int> last_bank_{0};
	std::atomic<float> last_wave_{0.f};
};

}  // namespace

std::unique_ptr<OndesEngine> MakeOndesBeads(Ondes& m) {
	return std::make_unique<OndesBeads>(m);
}

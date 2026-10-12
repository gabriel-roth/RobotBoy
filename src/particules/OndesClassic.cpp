#include "Ondes.hpp"
#include "RackWavetableProvider.hpp"
#include "WavetableFrame.hpp"
#include "ondes_pitch_map.hpp"
#include "dsp/src/wavetable/wavetable_oscillator.h"

#include <atomic>
#include <cmath>

// Ondes' classic engine: the v2.1 oscillator (particules_dsp::
// WavetableOscillator) over all 24 Plaits-derived banks. v2.1's "Waveform
// banks" menu is gone, so every bank group is always enabled.

namespace {

class OndesClassic : public OndesEngine {
 public:
	explicit OndesClassic(Ondes& m) : m_(m) {
		osc_.Init(APP->engine->getSampleRate());
		osc_.SetProvider(&provider_);
	}

	void onSampleRateChange(float sample_rate) override {
		osc_.Init(sample_rate);
		osc_.SetProvider(&provider_);
	}

	void process(const rack::Module::ProcessArgs&) override {
		using rack::math::clamp;
		float pitch = ondesKnobToSemitones(m_.params[Ondes::PITCH_PARAM].getValue())
		            + m_.inputs[Ondes::VOCT_INPUT].getVoltage() * 12.f;
		float position = clamp(
			m_.params[Ondes::POSITION_PARAM].getValue()
			+ m_.inputs[Ondes::POSITION_INPUT].getVoltage() * 0.2f * m_.params[Ondes::POSITION_AMT_PARAM].getValue(),
			0.f, 1.f);
		float bank = clamp(
			m_.params[Ondes::BANK_PARAM].getValue()
			+ m_.inputs[Ondes::BANK_INPUT].getVoltage() * 0.2f * m_.params[Ondes::BANK_AMT_PARAM].getValue(),
			0.f, 1.f);

		last_bank_.store(bank, std::memory_order_relaxed);
		last_wave_.store(position, std::memory_order_relaxed);

		particules_dsp::StereoFrame out;
		osc_.Process(pitch, bank, position, &out, 1);
		m_.outputs[Ondes::OUT_OUTPUT].setVoltage(out.l * 5.f);  // oscillator is mono: out.l == out.r
	}

	float displaySample(int i, int n) const override {
		const int size = particules_dsp::kWavetableSize;
		const int idx = n == size ? i : (int)((int64_t)i * size / n);
		return robotboy::wavetableFrameSample(provider_,
			last_bank_.load(std::memory_order_relaxed),
			last_wave_.load(std::memory_order_relaxed), idx % size);
	}

 private:
	Ondes& m_;
	particules_dsp::WavetableOscillator osc_;
	RackWavetableProvider provider_;   // all groups enabled
	std::atomic<float> last_bank_{0.f};
	std::atomic<float> last_wave_{0.f};
};

}  // namespace

std::unique_ptr<OndesEngine> MakeOndesClassic(Ondes& m) {
	return std::make_unique<OndesClassic>(m);
}

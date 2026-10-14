#pragma once
// Ondes' oscillator core, without Rack: Beads' wavetable voice, optionally
// run at a multiple of the host rate and decimated back down. Beads' voice
// has little anti-aliasing of its own (an integrated wavetable and two
// gentle low-pass filters), so its brighter waves alias audibly from about
// C3 up; oversampling pushes most of that above the audible band. With
// oversampling 1 it is the bare Beads voice, sample for sample.

#include "stmlib/dsp/dsp.h"
#include "beads/resources.h"
#include "beads/dsp/frame.h"
#include "beads/dsp/granular_player.h"
#include "beads/dsp/wavetable_grain.h"
#include "beads/dsp/wavetable_voice.h"
#include "host/resampler.h"

namespace robotboy_beads {

class OndesVoice {
 public:
	static constexpr int kMaxOversampling = 4;

	OndesVoice() {
		// Position and pitch arrive as plain values: no internal
		// randomization, no CV path of the voice's own.
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
	}

	// Restarts the voice (phase, bank crossfade) at the given host rate.
	void Init(float sample_rate, int oversampling) {
		oversampling_ = oversampling < 1 ? 1
			: oversampling > kMaxOversampling ? kMaxOversampling : oversampling;
		voice_.Init(sample_rate * float(oversampling_));
		if (oversampling_ > 1)
			decimator_.Init(sample_rate * float(oversampling_), sample_rate);
	}

	int oversampling() const { return oversampling_; }

	// One host-rate sample, about -1..1. semitones is relative to Beads'
	// C3 root; bit_shift is Beads' output resolution (16 = Digital).
	float Render(int bank, int bit_shift, float semitones, float position) {
		parameters_.pitch.base = semitones;
		parameters_.time.base = position;
		const bool freeze = false;
		if (oversampling_ == 1) {
			beads::FloatFrame out;
			out.l = out.r = 0.f;
			voice_.Render(bank, bit_shift, &freeze, parameters_, &out, 1);
			return out.l;
		}
		for (int i = 0; i < oversampling_; ++i) {
			beads::FloatFrame out;
			out.l = out.r = 0.f;
			voice_.Render(bank, bit_shift, &freeze, parameters_, &out, 1);
			decimator_.Push(&out.l);
		}
		// Silent while the decimator's filter fills (its group delay).
		float sample = 0.f;
		while (decimator_.Pop(&sample)) {}
		return sample;
	}

 private:
	beads::WavetableVoice voice_;
	beads::Parameters parameters_;
	Resampler<1> decimator_;
	int oversampling_ = 1;
};

}  // namespace robotboy_beads

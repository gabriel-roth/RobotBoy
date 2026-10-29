#include "LyrebirdEngine.hpp"

#include <cmath>

namespace shapes {

static constexpr int kRebuildBudget = 512;

LyrebirdEngine::LyrebirdEngine() {
	for (auto& p : pool_)
		p = std::make_unique<Take>();
	cur_.store(pool_[0].get());
	spare_ = pool_[1].get();
	undo_ = pool_[2].get();
	snap_ = pool_[3].get();
}

void LyrebirdEngine::setSampleRate(double sr) {
	sampleRate_ = sr > 1.0 ? sr : 1.0;
}

double LyrebirdEngine::syncRatio(double speedOct) {
	return std::exp2(std::round(std::clamp(sanitize(speedOct, 0.0), -4.0, 4.0)));
}

double LyrebirdEngine::freeRatio(double speedOct) {
	return std::exp2(std::clamp(sanitize(speedOct, 0.0), -4.0, 4.0));
}

double LyrebirdEngine::speedRatio(const LyrebirdControls& c) const {
	return c.sync ? syncRatio(c.speedOct) : freeRatio(c.speedOct);
}

double LyrebirdEngine::effectiveBars(const Take& t) {
	if (t.mode == TakeMode::Sync)
		return t.bars > 0.0 ? t.bars : 1.0;
	double want = t.seconds * t.bpm / 240.0;
	if (!(want > 0.0) || !std::isfinite(want))
		return 1.0;
	double best = kLengthBars[0];
	double bestDist = 1e300;
	for (double b : kLengthBars) {
		double d = std::fabs(std::log(b / want));
		if (d < bestDist) {
			bestDist = d;
			best = b;
		}
	}
	return best;
}

double LyrebirdEngine::smoothHalfWidth(const Take& t, double smoothMs) {
	if (!(t.seconds > 0.0) || t.length <= 0)
		return 0.0;
	double ms = std::clamp(sanitize(smoothMs, 0.0), 0.0, 1e4);
	return ms / 2000.0 * ((double) t.length / t.seconds);
}

void LyrebirdEngine::restart() {
	active_ = false;
	freePhase_ = 0.0;
	// The transport doesn't move with the take, so wrap counting restarts from
	// wherever it is now rather than from zero
	resync_ = true;
}

// Runs at the start of every sample, so the UI's requests take effect promptly
void LyrebirdEngine::service() {
	if (loadReq_.load()) {
		// The loaded take becomes current; the old one is kept for undo and
		// the undo slot's buffer is free for the UI to fill next
		Take* old = cur_.load();
		cur_.store(spare_);
		spare_ = undo_;
		undo_ = old;
		restart();
		loadReq_.store(false);
	}
	if (undoReq_.load()) {
		Take* old = cur_.load();
		cur_.store(undo_);
		undo_ = old;
		restart();
		undoReq_.store(false);
	}
	if (snapReq_.load()) {
		// Deliberate: one full copy per save request, so the UI can write the
		// file without racing the audio thread
		snap_->copyFrom(*cur_.load());
		snapReq_.store(false);
		snapReady_.store(true);
	}
}

double LyrebirdEngine::finish(const LyrebirdControls& c, double v) const {
	double scale = sanitize(c.scale, 1.0);
	double offset = sanitize(c.offset, 0.0);
	return std::clamp(sanitize(v * scale + offset, 0.0), -10.0, 10.0);
}

// Smoothed take value at playhead ph (0..1)
double LyrebirdEngine::outputFor(const LyrebirdControls& c, const Take& t, double ph) {
	double span = c.loop ? (double) t.length : (double) (t.length - 1);
	double idx = std::clamp(sanitize(ph, 0.0), 0.0, 1.0) * span;
	double hw = smoothHalfWidth(t, c.smoothMs);
	return finish(c, smoother_.at(t, idx, hw, c.loop));
}

LyrebirdStep LyrebirdEngine::process(const LyrebirdControls& c, const Timebase::Step& tb) {
	service();
	LyrebirdStep out;
	const Take& t = *cur_.load();

	if (t.empty()) {
		out.volts = finish(c, sanitize(c.source, 0.0));
		out.status = status_;
		return out;
	}

	// Keep the smoothing sums in step with the take
	if (&t != smoothedTake_ || t.version != smoothedVersion_ || t.length != smoothedLength_) {
		smoother_.invalidate();
		smoothedTake_ = &t;
		smoothedVersion_ = t.version;
		smoothedLength_ = t.length;
	}
	if (!smoother_.ready())
		smoother_.rebuildStep(t, kRebuildBudget);

	double ratio = speedRatio(c);
	if (tb.reset) {
		restart();
		active_ = true;
	}

	bool syncLoop = c.sync && c.loop;
	// Position through the take, 0..1, before REVERSE; resting means a ONCE
	// take that is not playing
	double ph = 0.0;
	bool resting = false;
	if (c.sync) {
		double x = (double) tb.cycle + sanitize(tb.pos, 0.0);
		double bars = effectiveBars(t);
		x = x * ratio / bars;
		if (c.loop) {
			double wraps = std::floor(x + 1e-9);
			// A new take, TRIG, a mode change or a SPEED change moves x without
			// a wrap, so re-anchor instead of reporting one
			bool jumped = resync_ || !prevSyncLoop_ || ratio != lastRatio_ || bars != lastBars_;
			if (!jumped && wraps > lastWraps_)
				out.eoc = true;
			lastWraps_ = wraps;
			resync_ = false;
			ph = frac(x + 1e-9);
			if (ph > 1.0 - 1e-6)
				ph = 0.0;
		}
		else if (active_) {
			if (x >= 1.0 - 1e-9) {
				out.eoc = true;
				active_ = false;
				resting = true;
			}
			else
				ph = x;
		}
		else
			resting = true;
	}
	else {
		double step = ratio / (std::max(t.seconds, 1e-3) * sampleRate_);
		if (c.loop) {
			freePhase_ += step;
			if (freePhase_ >= 1.0 - 1e-9) {
				freePhase_ -= std::floor(freePhase_ + 1e-9);
				freePhase_ = std::max(freePhase_, 0.0);
				out.eoc = true;
			}
			ph = freePhase_;
		}
		else if (active_) {
			freePhase_ += step;
			if (freePhase_ >= 1.0 - 1e-9) {
				out.eoc = true;
				active_ = false;
				resting = true;
			}
			else
				ph = freePhase_;
		}
		else
			resting = true;
	}

	prevSyncLoop_ = syncLoop;
	lastRatio_ = ratio;
	lastBars_ = c.sync ? effectiveBars(t) : 0.0;

	if (resting) {
		// Rest at the end of the pass in the direction of play
		switch (c.idle) {
			case IdleOutput::Zero:
				out.volts = finish(c, 0.0);
				out.playhead = c.reverse ? 0.0 : 1.0;
				out.status = status_;
				return out;
			case IdleOutput::First:
				ph = 0.0;
				break;
			case IdleOutput::Last:
				ph = 1.0;
				break;
		}
		if (c.reverse)
			ph = 1.0 - ph;
		out.playhead = ph;
		out.volts = outputFor(c, t, ph);
		out.status = status_;
		return out;
	}

	if (c.reverse)
		ph = c.loop ? frac(1.0 - ph) : 1.0 - ph;
	out.playhead = ph;
	out.volts = outputFor(c, t, ph);
	out.status = status_;
	return out;
}

// Audio thread only: it reads the smoother's sums, which that thread rebuilds
void LyrebirdEngine::renderTrace(const LyrebirdControls& c, float* raw, float* out, int n) {
	const Take& t = *cur_.load();
	if (n <= 0)
		return;
	if (t.empty()) {
		float v = (float) finish(c, sanitize(c.source, 0.0));
		for (int i = 0; i < n; i++) {
			raw[i] = 0.f;
			out[i] = v;
		}
		return;
	}
	double hw = smoothHalfWidth(t, c.smoothMs);
	for (int i = 0; i < n; i++) {
		double p = n > 1 ? (double) i / (n - 1) : 0.0;
		double idx = p * (t.length - 1);
		int k = std::min((int) idx, t.length - 1);
		double f = idx - k;
		double a = t.samples[k];
		double b = k + 1 < t.length ? t.samples[k + 1] : a;
		raw[i] = (float) (a + (b - a) * f);
		out[i] = (float) finish(c, smoother_.at(t, idx, hw, c.loop));
	}
}

}  // namespace shapes

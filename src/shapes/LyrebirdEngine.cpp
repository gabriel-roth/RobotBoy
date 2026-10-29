#include "LyrebirdEngine.hpp"

#include <cmath>
#include <limits>

namespace shapes {

static constexpr int kRebuildBudget = 512;
// Movement start: 0.5% of the knob's 10 V span, the same for CV
static constexpr double kMoveThreshold = 0.05;
// Crossfades at punch edges, in what is written and in OUT
static constexpr double kFadeMs = 10.0;

LyrebirdEngine::LyrebirdEngine() {
	for (auto& p : pool_)
		p = std::make_unique<Take>();
	cur_.store(pool_[0].get());
	spare_ = pool_[1].get();
	undo_ = pool_[2].get();
	snap_ = pool_[3].get();
	written_.assign(kMaxTakeSamples, 0);
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
	servicing_ = true;
	if (loadReq_.load()) {
		// A recording or punch in progress is dropped: the take from before
		// it becomes current again, so it is what the load keeps for undo
		if (status_ == Status::Recording || status_ == Status::Punching)
			abandonRec();
		else
			finishWrites();
		// The loaded take becomes current; the old one is kept for undo and
		// the undo slot's buffer is free for the UI to fill next
		Take* old = cur_.load();
		cur_.store(spare_);
		spare_ = undo_;
		undo_ = old;
		restart();
		loadReq_.store(false);
	}
	for (int n = undoReq_.exchange(0); n > 0; n--) {
		// Undo keeps what was recorded so far, so a second undo brings it back
		finishWrites();
		Take* old = cur_.load();
		cur_.store(undo_);
		undo_ = old;
		restart();
	}
	if (snapReq_.load()) {
		// Deliberate: one full copy per save request, so the UI can write the
		// file without racing the audio thread
		snap_->copyFrom(*cur_.load());
		snapReq_.store(false);
		snapReady_.store(true);
	}
	servicing_ = false;
}

// Ends whatever is being written as if its button had been released
void LyrebirdEngine::finishWrites() {
	if (status_ == Status::Recording)
		endRec(false);
	else if (status_ == Status::Punching || punchTail_)
		endPunch();
	status_ = Status::Idle;
	// An armed PUNCH with no take would otherwise keep owning the next stop
	recByPunch_ = false;
}

// Drops a recording or punch: REC and PUNCH both left the take from before
// them in the undo slot, so swapping it back discards the partial one
void LyrebirdEngine::abandonRec() {
	Take* partial = cur_.load();
	cur_.store(undo_);
	undo_ = partial;
	status_ = Status::Idle;
	punchTail_ = false;
	punchGain_ = 0.0;
	blockIdx_ = -1;
	recByPunch_ = false;
	restart();
}

int LyrebirdEngine::syncIndex(double x) const {
	const Take& t = *cur_.load();
	double p = x / t.bars;
	p -= std::floor(p);
	int k = (int) std::floor(p * t.length + 0.5);
	return k >= t.length ? k - t.length : std::max(k, 0);
}

void LyrebirdEngine::startRec(const LyrebirdControls& c, double x) {
	if (status_ == Status::Punching || punchTail_)
		endPunch();
	// The new take is written into the old undo buffer; the current take becomes the undo
	Take* old = cur_.load();
	Take* t = undo_;
	t->clear();
	t->modified = true;
	t->bpm = std::clamp(sanitize(c.bpm, 120.0), 1.0, 1000.0);
	if (c.sync) {
		t->mode = TakeMode::Sync;
		t->bars = kLengthBars[std::clamp(c.lengthIndex, 0, 6)];
		t->seconds = t->bars * 240.0 / t->bpm;
		// Capped: a very long take is stored at a lower rate instead
		t->length = std::clamp((int) std::lround(t->seconds * kTakeRate), 1, kMaxTakeSamples);
		std::fill(written_.begin(), written_.begin() + t->length, 0);
		recElapsed_ = 0.0;
		recSegX_ = x;
		recSegBase_ = 0.0;
	}
	else {
		t->mode = TakeMode::Free;
		recSamples_ = 0;
	}
	undo_ = old;
	cur_.store(t);
	status_ = Status::Recording;
	blockIdx_ = -1;
	blockSum_ = 0.0;
	blockCount_ = 0;
	// OUT carries on from where it was, then follows the source
	monitor_.reset(lastRaw_);
	monitorMix_ = 1.0;
}

void LyrebirdEngine::recordSample(const LyrebirdControls& c, const Timebase::Step& tb, double x, LyrebirdStep& out) {
	Take& t = *cur_.load();
	double src = std::clamp(sanitize(c.source, 0.0), -10.0, 10.0);
	if (t.mode == TakeMode::Sync) {
		// Counted as transport travelled, so TRIG mid-take doesn't stretch or cut it
		if (tb.reset) {
			recSegBase_ = recElapsed_;
			recSegX_ = x;
		}
		else
			recElapsed_ = recSegBase_ + std::max(x - recSegX_, 0.0);
		if (recElapsed_ >= t.bars - 1e-9) {
			endRec(true);
			return;
		}
		int k = syncIndex(x);
		if (blockIdx_ < 0)
			recStart_ = k;
		else if (k != blockIdx_) {
			t.samples[blockIdx_] = (float) (blockSum_ / blockCount_);
			written_[blockIdx_] = 1;
		}
		if (k != blockIdx_) {
			blockIdx_ = k;
			blockSum_ = 0.0;
			blockCount_ = 0;
		}
		out.playhead = (double) k / t.length;
	}
	else {
		// Each 1 ms block is centred on its sample, so take time i ms plays at i ms
		int k = (int) std::floor(recSamples_ * kTakeRate / sampleRate_ + 0.5);
		if (k >= kMaxTakeSamples) {
			out.hitLimit = true;
			endRec(true);
			return;
		}
		if (k != blockIdx_) {
			if (blockIdx_ >= 0) {
				// Below 1 kHz a sample spans several blocks: hold across them
				float v = (float) (blockSum_ / blockCount_);
				for (int i = blockIdx_; i < k; i++)
					t.samples[i] = v;
				t.length = k;
				t.seconds = (double) k / kTakeRate;
			}
			blockIdx_ = k;
			blockSum_ = 0.0;
			blockCount_ = 0;
		}
		recSamples_++;
		out.playhead = 1.0;
	}
	blockSum_ += src;
	blockCount_++;
}

// full: Sync reached LENGTH, or Free reached the limit
void LyrebirdEngine::endRec(bool full) {
	Take& t = *cur_.load();
	bool pending = blockIdx_ >= 0 && blockCount_ > 0;
	float v = pending ? (float) (blockSum_ / blockCount_) : 0.f;
	if (t.mode == TakeMode::Sync) {
		int n = t.length;
		// A full take's last block lands back on its first, which already has data
		if (pending && !(full && written_[blockIdx_])) {
			t.samples[blockIdx_] = v;
			written_[blockIdx_] = 1;
		}
		// Hold the last written value over anything not reached: the rest of an
		// early stop, or what a TRIG skipped
		int first = -1;
		for (int j = 0; j < n && first < 0; j++)
			if (written_[(recStart_ + j) % n])
				first = (recStart_ + j) % n;
		float last = first >= 0 ? t.samples[first] : 0.f;
		for (int j = 0; j < n; j++) {
			int i = (recStart_ + j) % n;
			if (written_[i])
				last = t.samples[i];
			else
				t.samples[i] = last;
		}
	}
	else {
		int len = full ? kMaxTakeSamples
		               : std::clamp((int) std::lround(recSamples_ * kTakeRate / sampleRate_), 1, kMaxTakeSamples);
		int lastWritten = std::min(t.length, len) - 1;
		if (pending && blockIdx_ < len) {
			t.samples[blockIdx_] = v;
			lastWritten = blockIdx_;
		}
		if (lastWritten < 0) {
			t.samples[0] = v;
			lastWritten = 0;
		}
		for (int i = lastWritten + 1; i < len; i++)
			t.samples[i] = t.samples[lastWritten];
		t.length = len;
		t.seconds = (double) len / kTakeRate;
		t.bars = effectiveBars(t);
	}
	t.version = newTakeVersion();
	status_ = Status::Idle;
	blockIdx_ = -1;
	recByPunch_ = false;
	restart();
	if (!servicing_)
		recsDone_.fetch_add(1);
}

void LyrebirdEngine::startPunch(double raw) {
	if (punchTail_) {
		// Still fading out of the last punch: carry on from where it is
		punchTail_ = false;
		status_ = Status::Punching;
		return;
	}
	// One copy per punch, so undo can bring back the take from before it
	Take* t = cur_.load();
	undo_->copyFrom(*t);
	t->modified = true;
	status_ = Status::Punching;
	punchGain_ = 0.0;
	blockIdx_ = -1;
	blockSum_ = 0.0;
	blockCount_ = 0;
	// Already on the monitor (just after a REC or punch): it stays continuous as it is
	if (monitorMix_ <= 0.0)
		monitor_.reset(raw);
}

void LyrebirdEngine::punchWrite(Take& t, int k, double v) {
	double old = t.samples[k];
	t.samples[k] = (float) std::clamp(old + (v - old) * punchGain_, -10.0, 10.0);
}

// Writes at the playhead, filling every take sample it crossed since the last
// one. maxStep is the most take samples real playback can move in one sample:
// a bigger move (REVERSE, TRIG, a Sync SPEED change) is a jump, and the
// samples it skipped weren't crossed.
void LyrebirdEngine::punchSample(const LyrebirdControls& c, Take& t, double ph, int maxStep) {
	int n = t.length;
	double span = c.loop ? (double) n : (double) (n - 1);
	int k = (int) std::floor(std::clamp(ph, 0.0, 1.0) * span + 0.5);
	k = c.loop ? k % n : std::min(k, n - 1);
	if (blockIdx_ < 0)
		blockIdx_ = k;
	else if (k != blockIdx_) {
		double avg = blockSum_ / blockCount_;
		int d = k - blockIdx_;
		if (c.loop) {
			if (d > n / 2)
				d -= n;
			else if (d < -n / 2)
				d += n;
		}
		int dir = d > 0 ? 1 : -1;
		int i = blockIdx_;
		for (int steps = std::abs(d) > maxStep ? 1 : std::abs(d); steps > 0; steps--) {
			punchWrite(t, i, avg);
			i = c.loop ? (i + dir + n) % n : std::clamp(i + dir, 0, n - 1);
		}
		blockIdx_ = k;
		blockSum_ = 0.0;
		blockCount_ = 0;
	}
	blockSum_ += std::clamp(sanitize(c.source, 0.0), -10.0, 10.0);
	blockCount_++;
}

void LyrebirdEngine::endPunch() {
	Take* t = cur_.load();
	// The block being averaged was crossed too
	if (blockIdx_ >= 0 && blockCount_ > 0 && blockIdx_ < t->length)
		punchWrite(*t, blockIdx_, blockSum_ / blockCount_);
	// The smoother rebuilds from the new data from here on
	t->version = newTakeVersion();
	if (status_ == Status::Punching)
		status_ = Status::Idle;
	punchTail_ = false;
	punchGain_ = 0.0;
	blockIdx_ = -1;
	if (!servicing_)
		punchesDone_.fetch_add(1);
}

int LyrebirdEngine::maxPunchStep(const LyrebirdControls& c, const Take& t, double ratio) const {
	// Takes per audio sample at the current tempo and SPEED
	double perSample;
	if (c.sync)
		perSample = ratio * std::clamp(sanitize(c.bpm, 120.0), 1.0, 1000.0) / 240.0 / effectiveBars(t) / sampleRate_;
	else
		perSample = ratio / (std::max(t.seconds, 1e-3) * sampleRate_);
	// Headroom for a clock pulling the transport into line
	double step = std::ceil(1.5 * perSample * t.length) + 1.0;
	return (int) std::min(step, (double) t.length);
}

double LyrebirdEngine::finish(double v) {
	return std::clamp(sanitize(v, 0.0), -10.0, 10.0);
}

// Smoothed take value at playhead ph (0..1)
double LyrebirdEngine::outputFor(const LyrebirdControls& c, const Take& t, double ph) {
	return finish(rawFor(c, t, ph));
}

// As outputFor, before the clamp
double LyrebirdEngine::rawFor(const LyrebirdControls& c, const Take& t, double ph) {
	double span = c.loop ? (double) t.length : (double) (t.length - 1);
	double idx = std::clamp(sanitize(ph, 0.0), 0.0, 1.0) * span;
	double hw = smoothHalfWidth(t, c.smoothMs);
	return sanitize(smoother_.at(t, idx, hw, c.loop), 0.0);
}

LyrebirdStep LyrebirdEngine::process(const LyrebirdControls& c, const Timebase::Step& tb) {
	service();
	LyrebirdStep out;
	double x = (double) tb.cycle + sanitize(tb.pos, 0.0);
	double src = std::clamp(sanitize(c.source, 0.0), -10.0, 10.0);
	monitor_.set(std::clamp(sanitize(c.smoothMs, 0.0), 0.0, 1e4), sampleRate_);

	// Toggle reacts to presses (rising edges), Hold to the level
	bool recEdge = c.rec && !prevRec_;
	bool punchEdge = c.punch && !prevPunch_;
	prevRec_ = c.rec;
	prevPunch_ = c.punch;
	bool hold = c.buttons == RecordButtons::Hold;
	bool ownLevel = recByPunch_ ? c.punch : c.rec;
	bool ownEdge = recByPunch_ ? punchEdge : recEdge;
	bool ownStop = hold ? !ownLevel : ownEdge;
	Status statusIn = status_;

	switch (status_) {
		case Status::Idle:
			if (recEdge || (punchEdge && cur_.load()->empty())) {
				recByPunch_ = !recEdge;
				if (c.startOn == StartOn::Movement) {
					status_ = Status::Armed;
					armValue_ = src;
				}
				else
					startRec(c, x);
			}
			break;
		case Status::Armed:
			if (ownStop) {
				status_ = Status::Idle;
				recByPunch_ = false;
			}
			else if (std::fabs(src - armValue_) > kMoveThreshold)
				startRec(c, x);
			break;
		case Status::Recording:
			if (ownStop)
				endRec(false);
			break;
		case Status::PunchArmed:
		case Status::Punching:
			// REC takes over from a punch: the punch ends as it would on release
			if (recEdge) {
				recByPunch_ = false;
				if (c.startOn == StartOn::Movement) {
					if (status_ == Status::Punching)
						punchTail_ = true;
					status_ = Status::Armed;
					armValue_ = src;
				}
				else
					startRec(c, x);
			}
			break;
		default:
			break;
	}
	// A press used by REC above mustn't also start a punch below
	if (status_ != statusIn || recByPunch_)
		punchEdge = false;

	if (status_ == Status::Recording) {
		// OUT monitors the live source while the take is written
		recordSample(c, tb, x, out);
		lastRaw_ = monitor_.process(src);
		out.volts = finish(lastRaw_);
		out.status = status_;
		return out;
	}

	const Take& t = *cur_.load();

	if (t.empty()) {
		lastRaw_ = src;
		out.volts = finish(src);
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
	// While punching the sums keep describing the take from before the punch
	// (its copy is in the undo slot), so the crossfade into the punch reads
	// data that isn't being overwritten under it
	bool wasWriting = writing();
	const Take& before = wasWriting ? *undo_ : t;
	if (!smoother_.ready())
		smoother_.rebuildStep(before, kRebuildBudget);

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
		double bars = effectiveBars(t);
		double xt = x * ratio / bars;  // through the take
		if (c.loop) {
			double wraps = std::floor(xt + 1e-9);
			// A new take, TRIG, a mode change or a SPEED change moves xt without
			// a wrap, so re-anchor instead of reporting one
			bool jumped = resync_ || !prevSyncLoop_ || ratio != lastRatio_ || bars != lastBars_;
			if (!jumped && wraps > lastWraps_)
				out.eoc = true;
			lastWraps_ = wraps;
			resync_ = false;
			ph = frac(xt + 1e-9);
			if (ph > 1.0 - 1e-6)
				ph = 0.0;
		}
		else if (active_) {
			if (xt >= 1.0 - 1e-9) {
				out.eoc = true;
				active_ = false;
				resting = true;
			}
			else
				ph = xt;
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

	double v;
	if (resting) {
		// Rest at the end of the pass in the direction of play
		switch (c.idle) {
			case IdleOutput::Zero:
				ph = 1.0;
				break;
			case IdleOutput::First:
				ph = 0.0;
				break;
			case IdleOutput::Last:
				ph = 1.0;
				break;
		}
		if (c.reverse)
			ph = 1.0 - ph;
		v = c.idle == IdleOutput::Zero ? 0.0 : rawFor(c, before, ph);
	}
	else {
		if (c.reverse)
			ph = c.loop ? frac(1.0 - ph) : 1.0 - ph;
		v = rawFor(c, before, ph);
	}
	out.playhead = ph;

	// Punch-in: in ONCE only during a pass, and it ends with the pass
	bool playing = !resting;
	bool punchStop = hold ? !c.punch : punchEdge;
	switch (status_) {
		case Status::Idle:
			if (punchEdge && playing) {
				if (c.startOn == StartOn::Movement && !punchTail_) {
					status_ = Status::PunchArmed;
					armValue_ = src;
				}
				else
					startPunch(v);
			}
			break;
		case Status::PunchArmed:
			if (punchStop || !playing)
				status_ = Status::Idle;
			else if (std::fabs(src - armValue_) > kMoveThreshold)
				startPunch(v);
			break;
		case Status::Punching:
			if (!playing)
				endPunch();
			else if (punchStop) {
				status_ = Status::Idle;
				punchTail_ = true;
			}
			break;
		default:
			break;
	}
	double fade = 1.0 / (kFadeMs * 0.001 * sampleRate_);
	if (status_ == Status::Punching || punchTail_) {
		if (!playing)
			endPunch();
		else {
			// Both fades run in real time, so a fast playhead fades over more take samples
			punchGain_ = std::clamp(punchGain_ + (punchTail_ ? -fade : fade), 0.0, 1.0);
			monitorMix_ = std::min(monitorMix_ + fade, 1.0);
			if (punchTail_ && punchGain_ <= 0.0)
				endPunch();
			else
				punchSample(c, *cur_.load(), ph, maxPunchStep(c, t, ratio));
		}
	}
	else if (monitorMix_ > 0.0 && (smoother_.ready() || smoothHalfWidth(t, c.smoothMs) <= 0.5)) {
		// OUT stays on the monitor through the tail, and until the sums
		// describe the new data, so it fades to what will keep playing
		monitorMix_ = std::max(monitorMix_ - fade, 0.0);
	}
	if (monitorMix_ > 0.0) {
		double m = monitor_.process(src);
		v += (m - v) * monitorMix_;
	}

	lastRaw_ = v;
	out.volts = finish(v);
	out.status = status_;
	return out;
}

// Audio thread only: it reads the smoother's sums, which that thread rebuilds
void LyrebirdEngine::renderTrace(const LyrebirdControls& c, float* raw, float* out, int n) {
	const Take& t = *cur_.load();
	if (n <= 0)
		return;
	if (t.empty()) {
		float v = (float) finish(sanitize(c.source, 0.0));
		for (int i = 0; i < n; i++) {
			raw[i] = 0.f;
			out[i] = v;
		}
		return;
	}
	double hw = smoothHalfWidth(t, c.smoothMs);
	// The sums lag a take being written, so draw that from the raw data
	bool live = writing();
	// A Sync take being recorded is full length from the start, but only what
	// has been written means anything: the rest of its buffer is old data
	bool partial = status_ == Status::Recording && t.mode == TakeMode::Sync;
	bool pending = blockIdx_ >= 0 && blockCount_ > 0;
	auto has = [&](int k) {
		return !partial || written_[k] || (pending && k == blockIdx_);
	};
	auto value = [&](int k) {
		return (!partial || written_[k]) ? (double) t.samples[k] : blockSum_ / blockCount_;
	};
	const float none = std::numeric_limits<float>::quiet_NaN();
	for (int i = 0; i < n; i++) {
		double p = n > 1 ? (double) i / (n - 1) : 0.0;
		double idx = p * (t.length - 1);
		int k = std::min((int) idx, t.length - 1);
		if (!has(k)) {
			raw[i] = out[i] = none;
			continue;
		}
		double f = idx - k;
		double a = value(k);
		double b = k + 1 < t.length && has(k + 1) ? value(k + 1) : a;
		raw[i] = (float) (a + (b - a) * f);
		out[i] = (float) finish(live ? raw[i] : smoother_.at(t, idx, hw, c.loop));
	}
}

}  // namespace shapes

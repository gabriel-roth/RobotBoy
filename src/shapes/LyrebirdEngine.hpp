#pragma once
#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

#include "OutputStage.hpp"
#include "Take.hpp"
#include "TakeSmoother.hpp"
#include "Timebase.hpp"

namespace shapes {

enum class RecordButtons { Toggle, Hold };
enum class StartOn { Press, Movement };
enum class IdleOutput { Last, First, Zero };
enum class Status { Idle, Armed, Recording, PunchArmed, Punching };

struct LyrebirdControls {
	double source = 0;  // volts, the selected source
	double bpm = 120;  // the transport's tempo, stored with a new take
	bool sourceIsKnob = true;
	bool sync = true;
	bool loop = true;
	int lengthIndex = 2;
	double speedOct = 0;  // knob + CV, octaves
	double smoothMs = 0;
	double scale = 1;
	double offset = 0;
	bool reverse = false;
	bool rec = false;
	bool punch = false;  // button OR gate, level
	RecordButtons buttons = RecordButtons::Toggle;
	StartOn startOn = StartOn::Press;
	IdleOutput idle = IdleOutput::Last;
};

struct LyrebirdStep {
	double volts = 0;
	bool eoc = false;
	double playhead = 0;  // 0..1 within the take
	Status status = Status::Idle;
	bool hitLimit = false;  // a Free take reached 3 min this sample
};

// Records and plays back one gesture. The UI thread never touches the audio
// thread's takes directly: it fills spare() and asks for a swap, and the
// audio thread services the request at the start of process().
class LyrebirdEngine {
public:
	LyrebirdEngine();

	void setSampleRate(double sr);
	LyrebirdStep process(const LyrebirdControls& c, const Timebase::Step& tb);

	// Current take, for display and saving (tear-tolerant)
	const Take& take() const {
		return *cur_.load();
	}

	// The UI fills this only while !loadPending(), then calls requestLoad()
	Take& spare() {
		return *spare_;
	}
	void requestLoad() {
		loadReq_.store(true);
	}
	bool loadPending() const {
		return loadReq_.load();
	}
	// Counted, so two requests made before the audio thread gets to them
	// swap twice rather than once
	void requestUndo() {
		undoReq_.fetch_add(1);
	}

	// Takes finished by REC and PUNCH themselves; a write ended by a load or
	// undo request doesn't count
	uint32_t recsFinished() const {
		return recsDone_.load();
	}
	uint32_t punchesFinished() const {
		return punchesDone_.load();
	}

	// A copy of the current take the UI can save without racing the audio thread
	void requestSnapshot() {
		snapReady_.store(false);
		snapReq_.store(true);
	}
	bool snapshotReady() const {
		return snapReady_.load();
	}
	const Take& snapshot() const {
		return *snap_;
	}
	void releaseSnapshot() {
		snapReady_.store(false);
	}

	double speedRatio(const LyrebirdControls& c) const;
	// Audio thread only. Points evenly spaced across the take: raw as stored, out as OUT would play it.
	// While a Sync take is being recorded, points not yet written are NaN in both.
	void renderTrace(const LyrebirdControls& c, float* raw, float* out, int n);

	static double syncRatio(double speedOct);
	static double freeRatio(double speedOct);
	// The whole number of bars a take occupies when played in Sync
	static double effectiveBars(const Take& t);
	// SMOOTH is the full window width in take time; this is half of it, in take samples
	static double smoothHalfWidth(const Take& t, double smoothMs);

private:
	void service();
	void restart();
	double outputFor(const LyrebirdControls& c, const Take& t, double ph);
	double rawFor(const LyrebirdControls& c, const Take& t, double ph);
	double finish(const LyrebirdControls& c, double v) const;

	// Recording
	void startRec(const LyrebirdControls& c, double x);
	void recordSample(const LyrebirdControls& c, const Timebase::Step& tb, double x, LyrebirdStep& out);
	void endRec(bool full);
	void abandonRec();
	int syncIndex(double x) const;
	// Punch-in
	void startPunch(double raw);
	void punchSample(const LyrebirdControls& c, Take& t, double ph, int maxStep);
	int maxPunchStep(const LyrebirdControls& c, const Take& t, double ratio) const;
	void punchWrite(Take& t, int k, double v);
	bool writing() const {
		return status_ == Status::Recording || status_ == Status::Punching || punchTail_;
	}
	void endPunch();
	void finishWrites();

	double sampleRate_ = 48000.0;
	std::unique_ptr<Take> pool_[4];
	std::atomic<Take*> cur_;
	Take* spare_;
	Take* undo_;
	Take* snap_;
	std::atomic<bool> loadReq_{false}, snapReq_{false}, snapReady_{false};
	std::atomic<int> undoReq_{0};
	std::atomic<uint32_t> recsDone_{0}, punchesDone_{0};
	bool servicing_ = false;  // writes ended here are the UI's doing, not finished takes

	TakeSmoother smoother_;
	const Take* smoothedTake_ = nullptr;
	uint32_t smoothedVersion_ = 0;
	int smoothedLength_ = -1;

	// Playback position
	bool active_ = false;  // a ONCE pass is running
	double freePhase_ = 0.0;  // Free: 0..1 through the take
	double lastWraps_ = 0.0;  // whole wraps already reported
	bool resync_ = true;
	bool prevSyncLoop_ = false;
	double lastRatio_ = 1.0;
	double lastBars_ = 0.0;
	Status status_ = Status::Idle;

	// Buttons and the live source
	bool prevRec_ = false, prevPunch_ = false;
	bool recByPunch_ = false;  // PUNCH with no take records, and owns the stop
	double armValue_ = 0.0;
	Slew monitor_;
	double lastRaw_ = 0.0;  // last playback value before SCALE/OFFSET

	// One 1 ms input block being averaged, centred on take sample blockIdx_
	double blockSum_ = 0.0;
	int blockCount_ = 0;
	int blockIdx_ = -1;

	// REC
	int recStart_ = 0;  // Sync: index the take started at
	double recElapsed_ = 0.0;  // Sync: bars of transport so far
	double recSegX_ = 0.0;  // Sync: transport position where counting last restarted
	double recSegBase_ = 0.0;
	int64_t recSamples_ = 0;  // Free: audio samples recorded
	std::vector<uint8_t> written_;  // Sync: indices written, so stop can fill the rest

	// PUNCH: weight of the live source in what is written; the tail fades it back out
	double punchGain_ = 0.0;
	bool punchTail_ = false;
	// Weight of the live monitor in OUT: up while recording or punching, and
	// back down only once the new data can be played as it will stay
	double monitorMix_ = 0.0;
};

}  // namespace shapes

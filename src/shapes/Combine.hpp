#pragma once
#include "OutputStage.hpp"
#include <algorithm>
#include <cstdint>

// Combine: three operators (a wave each, or external CV) mixed by one of
// eleven algorithms and faded by a per-cycle window. Rack-free; process()
// never allocates or locks.
namespace shapes {

// Saved by index: Weaver's file loading maps the pre-loop Input index (8) to Input
enum Wave { Sine, Triangle, RampUp, RampDown, Square, Envelope, SteppedRandom, SmoothRandom, SteppedLoop, SmoothLoop, Input, kNumWaves };

// The random waves that repeat each operator cycle.
inline bool isLoop(Wave wave) {
	return wave == SteppedLoop || wave == SmoothLoop;
}

enum Algorithm { Mix, Scale, Scale2, Ring, Bend, Max, Min, Xfade, Fold, SampleHold, Gate, kNumAlgorithms };

constexpr int kNumRatios = 15;

double ratioValue(int index);
const char* ratioLabel(int index);
// What the PARAM knob does for this wave, as the wave display shows it.
const char* paramName(Wave wave);
const char* algorithmName(Algorithm algorithm);
const char* algorithmFormula(Algorithm algorithm);

struct OperatorControls {
	Wave wave = Sine;
	int ratio = 3;  // index into the ratio list (x1)
	double param = 0.5;
	double phase = 0.0;
	double level = 1.0;
	// Used only by the Input wave
	double inputVolts = 0.0;
	double inputGain = 1.0;
	// Used only by the loop waves: picks the pattern
	uint32_t loopSeed = 0;
};

struct CombineControls {
	OperatorControls op[3];
	Algorithm algorithm = Mix;
	double window = 0.0;
};

struct CombineFrame {
	double result;
	double raw[3];  // each operator's wave, before LEVEL (Input: slewed and gained)
	double scaled[3];  // raw * LEVEL
};

// One internal wave at absolute operator phase abs (cycles). Random waves
// are seeded by the operator letter. Input returns its unslewed value.
double evalOperator(const OperatorControls& op, double abs, int opIndex);

class CombineEngine {
public:
	void setSampleRate(double sr);
	// t is the master position in cycles since reset.
	CombineFrame process(const CombineControls& c, double t, bool reverse);
	// Peak of |result| for Fit (at least |the latest result|).
	double peak() const {
		return peakValue;
	}
	// For displays: the result and one operator's wave at master position t,
	// using the current phase offsets, without changing any state. The result
	// comes from the formulas, so it is only meaningful without Input.
	double resultAt(const CombineControls& c, double t, bool reverse) const;
	double operatorAt(const CombineControls& c, int i, double t, bool reverse) const;
	// Call on each cycle wrap, before that sample's process().
	void cycleWrapped();
	void reset();
	// Points of the formula peak scan done per sample. 0, the default, does
	// each scan in one sample; a spread scan takes 256 / pointsPerSample
	// samples, during which Fit uses the last scan's peak as well.
	void setScanSpread(int pointsPerSample) {
		scanSpread = std::max(pointsPerSample, 0);
	}

private:
	// The formula peak is rescanned when the controls change, at most this
	// often (unspread), and on each new cycle when cycles differ.
	static constexpr int kScanInterval = 64;
	static constexpr int kScanPoints = 256;
	static constexpr double kHysteresis = 0.05;

	double opAbs(const CombineControls& c, int i, double t) const;
	// combineRaw from the original JS prototype (not in this repo) for internal waves, using the current offsets.
	double formulaRaw(const CombineControls& c, double t) const;
	double inputValue(const CombineControls& c, int i);
	// Whether the scan's peak still holds for these controls in this cycle.
	bool scanCurrent(const CombineControls& c, int64_t cycle) const;
	void scanStarted(const CombineControls& c, int64_t cycle);
	void updateHold(const CombineControls& c, double te, const double* r);

	double sampleRate = 48000.0;

	// Per operator
	double offset[3] = {};
	double lastRatio[3] = {};
	bool haveRatio[3] = {};
	Slew slew[3];
	double slewParam[3] = {-1.0, -1.0, -1.0};
	bool slewActive[3] = {};

	// S&H
	bool holdValid = false;
	double heldA = 0.0;
	int64_t holdCycle = 0;  // B cycle the held value came from (internal B)
	bool holdFromInput = false;  // held on an Input B edge
	bool bHigh = false;
	bool bSeen = false;

	// Fit peak
	double peakValue = 1.0;
	double scanPeak = 0.0;
	int64_t scanCycle = 0;
	bool scanValid = false;
	int sinceScan = 0;
	// What the last scan was made from
	CombineControls scanControls;
	double scanOffset[3] = {};
	// A spread scan in progress
	int scanSpread = 0;
	bool scanning = false;
	int scanPoint = 0;
	double scanPartial = 0.0;
	double runningPeak = 0.0;
	double prevPeak = 0.0;
};

}  // namespace shapes

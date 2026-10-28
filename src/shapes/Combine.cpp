#include "Combine.hpp"
#include "Prng.hpp"
#include <algorithm>
#include <cmath>

namespace shapes {

namespace {

constexpr double kTau = 2.0 * M_PI;

const double kRatioValues[kNumRatios] = {0.125, 0.25, 0.5, 1, 2, 3, 4, 5, 6, 7, 8, 12, 16, 24, 32};
const char* const kRatioLabels[kNumRatios] = {"1/8", "1/4", "1/2", "x1", "x2", "x3", "x4", "x5", "x6", "x7", "x8", "x12", "x16", "x24", "x32"};
const char* const kParamNames[kNumWaves] = {"SKEW", "SYMMETRY", "CURVE", "CURVE", "WIDTH", "ATTACK", "STEPS", "DENSITY", "SLEW"};
const char* const kAlgorithmNames[kNumAlgorithms] = {"MIX", "SCALE", "SCALE 2", "RING", "BEND", "MAX", "MIN", "XFADE", "FOLD", "S&H", "GATE"};
const char* const kAlgorithmFormulas[kNumAlgorithms] = {
	"A+B+C",
	"(A scaled by B) + C",
	"A*B*C",
	"(A*B)+C",
	"(A bent by B) + C",
	"max(A,B)+C",
	"min(A,B)+C",
	"(A,B) xfaded by C",
	"fold(A+B)+C",
	"(A sampled by B) + C",
	"(A gated by B) + C",
};

double lerp(double a, double b, double t) {
	return a + (b - a) * t;
}

double foldTri(double x) {
	double u = frac((x + 1.0) / 4.0);
	return u < 0.5 ? u * 4.0 - 1.0 : 3.0 - u * 4.0;
}

// An operator's wave as 0..1: the envelope already is, the rest are bipolar.
// A hot input is clamped so Scale and Xfade can't overshoot.
double uni(double rv, Wave w) {
	double u = w == Envelope ? rv : (rv + 1.0) / 2.0;
	return w == Input ? std::clamp(u, 0.0, 1.0) : u;
}

// Positive depth pulls toward zero at the modulator's low points; negative
// depth at its high points.
double depthMod(double rv, Wave w, double d) {
	return d >= 0.0 ? 1.0 - d * (1.0 - uni(rv, w)) : 1.0 + d * uni(rv, w);
}

int clampRatio(int index) {
	return std::clamp(index, 0, kNumRatios - 1);
}

// The algorithm and Window, given the operators' raw values, A bent by B
// (Bend) and A's held value (S&H).
double mix(const CombineControls& c, const double* r, double bentA, double heldA, double t) {
	const OperatorControls& A = c.op[0];
	const OperatorControls& B = c.op[1];
	const OperatorControls& C = c.op[2];
	double sa = r[0] * A.level, sb = r[1] * B.level, sc = r[2] * C.level;
	double y;
	switch (c.algorithm) {
		case Mix: y = sa + sb + sc; break;
		case Scale: y = sa * depthMod(r[1], B.wave, B.level) + sc; break;
		case Scale2: y = sa * depthMod(r[1], B.wave, B.level) * depthMod(r[2], C.wave, C.level); break;
		case Ring: y = sa * sb + sc; break;
		case Bend: y = bentA * A.level + sc; break;
		case Max: y = std::max(sa, sb) + sc; break;
		case Min: y = std::min(sa, sb) + sc; break;
		case Xfade: {
			double d = std::fabs(C.level);
			y = lerp(sa, sb, uni(r[2], C.wave) * d + (1.0 - d) * 0.5);
			break;
		}
		case Fold: y = foldTri(sa + sb) + sc; break;
		case SampleHold: y = heldA * A.level + sc; break;
		case Gate: y = (B.level == 0.0 || r[1] * B.level > 0.0 ? sa : 0.0) + sc; break;
		default: y = sa + sb + sc;
	}
	if (c.window > 0.0) {
		double p = frac(t);
		double w = std::min(1.0, p / (0.3 * c.window)) * std::min(1.0, (1.0 - p) / (0.7 * c.window));
		w = w * w * (3.0 - 2.0 * w);
		y *= w;
	}
	return y;
}

// Reverse mirrors the position within the cycle; the cycle start stays in
// its own cycle (p > 0 ? 1 - p : 0)
double mirror(double t, bool reverse) {
	return reverse && frac(t) > 0.0 ? std::floor(t) + 1.0 - frac(t) : t;
}

bool anyInput(const CombineControls& c) {
	return c.op[0].wave == Input || c.op[1].wave == Input || c.op[2].wave == Input;
}

}  // namespace

double ratioValue(int index) {
	return kRatioValues[clampRatio(index)];
}

const char* ratioLabel(int index) {
	return kRatioLabels[clampRatio(index)];
}

const char* paramName(Wave wave) {
	return wave >= 0 && wave < kNumWaves ? kParamNames[wave] : "";
}

const char* algorithmName(Algorithm algorithm) {
	return algorithm >= 0 && algorithm < kNumAlgorithms ? kAlgorithmNames[algorithm] : "";
}

const char* algorithmFormula(Algorithm algorithm) {
	return algorithm >= 0 && algorithm < kNumAlgorithms ? kAlgorithmFormulas[algorithm] : "";
}

double evalOperator(const OperatorControls& op, double abs, int opIndex) {
	double sh = op.param;
	double p = frac(abs);
	double seed = 'A' + opIndex;
	switch (op.wave) {
		case Sine: {
			double k = 0.05 + 0.9 * sh;
			double q = p < k ? 0.5 * p / k : 0.5 + 0.5 * (p - k) / (1.0 - k);
			return std::sin(kTau * q);
		}
		case Triangle: {
			double k = 0.02 + 0.96 * sh;
			return p < k ? -1.0 + 2.0 * p / k : 1.0 - 2.0 * (p - k) / (1.0 - k);
		}
		case RampUp: return -1.0 + 2.0 * std::pow(p, std::pow(2.0, (sh - 0.5) * 4.0));
		case RampDown: return 1.0 - 2.0 * std::pow(p, std::pow(2.0, (sh - 0.5) * 4.0));
		case Square: return p < 0.02 + 0.96 * sh ? 1.0 : -1.0;
		case Envelope: {
			double a = 0.01 + 0.9 * sh;
			return p < a ? 1.0 - std::pow(1.0 - p / a, 2.0) : std::pow(1.0 - (p - a) / (1.0 - a), 3.0);
		}
		case SteppedRandom: {
			double n = 1.0 + std::round(sh * 7.0);
			return hash3(seed, std::floor(abs * n), 77) * 2.0 - 1.0;
		}
		case SmoothRandom: {
			double u = abs * (1.0 + sh * 7.0);
			double i = std::floor(u);
			double f = u - i;
			double s = f * f * (3.0 - 2.0 * f);
			return lerp(hash3(seed, i, 31), hash3(seed, i + 1.0, 31), s) * 2.0 - 1.0;
		}
		case Input: return op.inputVolts / 5.0 * op.inputGain;
		default: return 0.0;
	}
}

void CombineEngine::setSampleRate(double sr) {
	sampleRate = sr > 1.0 ? sr : 1.0;
	for (int i = 0; i < 3; i++)
		slewParam[i] = -1.0;  // recompute the coefficients
}

double CombineEngine::opAbs(const CombineControls& c, int i, double t) const {
	return t * ratioValue(c.op[i].ratio) + c.op[i].phase + offset[i];
}

double CombineEngine::formulaRaw(const CombineControls& c, double t) const {
	double r[3];
	for (int i = 0; i < 3; i++)
		r[i] = evalOperator(c.op[i], opAbs(c, i, t), i);
	double bentA = 0.0, heldA = 0.0;
	if (c.algorithm == Bend) {
		bentA = evalOperator(c.op[0], opAbs(c, 0, t) + r[1] * c.op[1].level * 0.5, 0);
	}
	else if (c.algorithm == SampleHold) {
		// A at the time B's current cycle began
		double rb = ratioValue(c.op[1].ratio);
		double ph = c.op[1].phase + offset[1];
		double k = std::floor(t * rb + ph);
		heldA = evalOperator(c.op[0], opAbs(c, 0, (k - c.op[1].phase - offset[1]) / rb), 0);
	}
	return mix(c, r, bentA, heldA, t);
}

double CombineEngine::inputValue(const CombineControls& c, int i) {
	const OperatorControls& op = c.op[i];
	double v = sanitize(sanitize(op.inputVolts, 0.0) / 5.0 * sanitize(op.inputGain, 1.0), 0.0);
	double p = std::clamp(sanitize(op.param, 0.0), 0.0, 1.0);
	if (p != slewParam[i]) {
		// PARAM sweeps the time constant exponentially from 1 ms to 1 s; 0 is off
		slew[i].set(p > 0.0 ? std::pow(1000.0, p) : 0.0, sampleRate);
		slewParam[i] = p;
	}
	if (!slewActive[i]) {
		slew[i].reset(v);
		slewActive[i] = true;
	}
	return slew[i].process(v);
}

void CombineEngine::updateHold(const CombineControls& c, double te, const double* r) {
	bool bInput = c.op[1].wave == Input;
	bool edge = false;
	if (bInput) {
		// Rising zero crossing with hysteresis, tracked whatever the algorithm
		if (!bSeen) {
			bHigh = r[1] > 0.0;
			bSeen = true;
		}
		else if (!bHigh && r[1] > kHysteresis) {
			bHigh = true;
			edge = true;
		}
		else if (bHigh && r[1] < -kHysteresis) {
			bHigh = false;
		}
	}
	else {
		bSeen = false;
	}

	if (c.algorithm != SampleHold) {
		holdValid = false;
		return;
	}

	if (holdValid && holdFromInput != bInput)
		holdValid = false;  // B switched between a wave and Input
	double sampleAt = te;  // time A is sampled at
	if (bInput) {
		if (holdValid && !edge)
			return;
	}
	else {
		// Event at the start of each B cycle; A is taken at that exact moment
		double rb = ratioValue(c.op[1].ratio);
		double kd = std::floor(opAbs(c, 1, te));
		int64_t k = std::isfinite(kd) ? (int64_t) std::clamp(kd, -9e18, 9e18) : 0;
		if (holdValid && k == holdCycle)
			return;
		holdCycle = k;
		sampleAt = (kd - c.op[1].phase - offset[1]) / rb;
	}
	heldA = c.op[0].wave == Input ? r[0] : sanitize(evalOperator(c.op[0], opAbs(c, 0, sampleAt), 0), 0.0);
	holdFromInput = bInput;
	holdValid = true;
}

CombineFrame CombineEngine::process(const CombineControls& c, double t, bool reverse) {
	t = sanitize(t, 0.0);
	double te = mirror(t, reverse);

	// A ratio change moves the offset so the phase carries on from where it was
	for (int i = 0; i < 3; i++) {
		double ratio = ratioValue(c.op[i].ratio);
		if (haveRatio[i] && ratio != lastRatio[i])
			offset[i] += te * (lastRatio[i] - ratio);
		lastRatio[i] = ratio;
		haveRatio[i] = true;
	}

	CombineFrame f;
	for (int i = 0; i < 3; i++) {
		if (c.op[i].wave == Input) {
			f.raw[i] = inputValue(c, i);
		}
		else {
			slewActive[i] = false;
			f.raw[i] = sanitize(evalOperator(c.op[i], opAbs(c, i, te), i), 0.0);
		}
		f.scaled[i] = sanitize(f.raw[i] * c.op[i].level, 0.0);
	}

	double bentA = 0.0;
	if (c.algorithm == Bend) {
		// Bending a live input would need its future; it passes through
		bentA = c.op[0].wave == Input ? f.raw[0]
		                               : evalOperator(c.op[0], opAbs(c, 0, te) + f.raw[1] * c.op[1].level * 0.5, 0);
	}
	updateHold(c, te, f.raw);

	double y = sanitize(mix(c, f.raw, bentA, heldA, te), 0.0);
	f.result = y;

	double a = std::fabs(y);
	runningPeak = std::max(runningPeak, a);
	if (anyInput(c)) {
		// No formula for a live input: measure, never below the last cycle's peak
		scanValid = false;
		peakValue = std::max(prevPeak, runningPeak);
	}
	else {
		int64_t cycle = (int64_t) std::clamp(std::floor(te), -9e18, 9e18);
		if (!scanValid || cycle != scanCycle || ++sinceScan >= kScanInterval) {
			double pk = 0.0;
			for (int k = 0; k < 256; k++)
				pk = std::max(pk, std::fabs(formulaRaw(c, cycle + k / 256.0)));
			scanPeak = sanitize(pk, 0.0);
			scanCycle = cycle;
			scanValid = true;
			sinceScan = 0;
		}
		// The 256-point scan can miss a sharp peak; the live value keeps Fit in range
		peakValue = std::max(scanPeak, a);
	}
	return f;
}

double CombineEngine::resultAt(const CombineControls& c, double t, bool reverse) const {
	return sanitize(formulaRaw(c, mirror(sanitize(t, 0.0), reverse)), 0.0);
}

double CombineEngine::operatorAt(const CombineControls& c, int i, double t, bool reverse) const {
	i = std::clamp(i, 0, 2);
	return sanitize(evalOperator(c.op[i], opAbs(c, i, mirror(sanitize(t, 0.0), reverse)), i), 0.0);
}

void CombineEngine::cycleWrapped() {
	prevPeak = runningPeak;
	runningPeak = 0.0;
}

void CombineEngine::reset() {
	for (int i = 0; i < 3; i++) {
		offset[i] = 0.0;
		haveRatio[i] = false;
	}
	holdValid = false;
	bSeen = false;
	peakValue = 1.0;
	scanValid = false;
	sinceScan = 0;
	runningPeak = 0.0;
	prevPeak = 0.0;
}

}  // namespace shapes

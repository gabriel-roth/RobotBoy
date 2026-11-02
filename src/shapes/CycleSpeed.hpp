#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

// Weaver's SPEED: runs the cycle at 1/4x to 4x the timebase's, in whole
// octaves. Each speed's cycles start on multiples of 1 / speed timebase
// cycles since reset, so a change waits for a timebase cycle start that
// both speeds share: the shape stays in step with the clock and carries on
// from where it was, with no jump. Set immediate and a change is taken at
// once instead: the position still carries on, but cycle starts leave the
// clock's grid until a reset.
namespace shapes {

class CycleSpeed {
public:
	static constexpr int kMin = -2;
	static constexpr int kMax = 2;

	struct Step {
		double t;  // cycles since reset at this speed
		bool wrapped;  // crossed a cycle start this sample (never on reset)
	};

	// Takes effect at the next shared cycle start, or at once when immediate.
	void setTarget(int octaves) {
		target = std::clamp(octaves, kMin, kMax);
	}

	// Takes effect at once (load, initialize); the position may jump.
	void jumpTo(int octaves) {
		target = std::clamp(octaves, kMin, kMax);
		applied = target;
		offset = 0.0;
		resync = true;
	}

	void setImmediate(bool on) {
		immediate = on;
	}

	int octaves() const {
		return applied;
	}
	int targetOctaves() const {
		return target;
	}

	// The timebase's step: whole cycles, position in the cycle, and whether
	// it reset or wrapped this sample.
	Step process(int64_t cycle, double pos, bool reset, bool wrapped) {
		if (reset) {
			applied = target;
			offset = 0.0;
			resync = true;
		}
		else if (immediate && target != applied) {
			// Keeps t where it is at the new rate
			offset += (scale(applied) - scale(target)) * ((double) cycle + pos);
			applied = target;
		}
		else if (wrapped && target != applied) {
			// The slower speed's cycle is the longer one
			int64_t every = int64_t(1) << std::max(0, -std::min(applied, target));
			if (cycle % every == 0) {
				// Both speeds' positions are whole here, so the offset stays whole
				offset += (scale(applied) - scale(target)) * (double) cycle;
				applied = target;
			}
		}
		double t = scale(applied) * ((double) cycle + pos) + offset;
		int64_t whole = (int64_t) std::floor(t);
		bool w = !resync && whole > last;
		// A clock's backward align can't make the same start count twice
		last = resync ? whole : std::max(last, whole);
		resync = false;
		return {t, w};
	}

	static double scale(int octaves) {
		return std::ldexp(1.0, octaves);
	}

	// As the display shows it: 1/4, 1/2, x1, x2, x4
	static const char* label(int octaves) {
		static const char* const kLabels[] = {"1/4", "1/2", "x1", "x2", "x4"};
		return kLabels[std::clamp(octaves, kMin, kMax) - kMin];
	}

private:
	int applied = 0;
	int target = 0;
	double offset = 0.0;
	bool immediate = false;
	int64_t last = 0;
	bool resync = true;
};

}  // namespace shapes

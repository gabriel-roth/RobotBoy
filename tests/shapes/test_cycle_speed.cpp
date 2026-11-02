// CycleSpeed tests: scaling, waiting for a shared cycle start, continuity,
// and one wrap per cycle start.
#include "shapes/CycleSpeed.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <string>

using namespace shapes;

// A fake timebase: n steps per cycle.
struct Clock {
	int n = 64;
	int64_t cycle = 0;
	int k = 0;
	bool first = true;
	CycleSpeed::Step step(CycleSpeed& s) {
		bool reset = first, wrapped = false;
		if (!first && ++k == n) {
			k = 0;
			cycle++;
			wrapped = true;
		}
		first = false;
		return s.process(cycle, (double) k / n, reset, wrapped);
	}
};

static void scalesAndWraps() {
	for (int oct = CycleSpeed::kMin; oct <= CycleSpeed::kMax; oct++) {
		CycleSpeed s;
		s.jumpTo(oct);
		Clock c;
		int wraps = 0;
		CycleSpeed::Step st{};
		for (int i = 0; i < 8 * 64; i++) {
			st = c.step(s);
			wraps += st.wrapped;
		}
		// 8 timebase cycles, less the last step
		double want = std::ldexp(1.0, oct) * (8.0 - 1.0 / 64);
		assert(std::fabs(st.t - want) < 1e-9);
		// Cycle starts within those 8 cycles, not counting the reset
		int starts = (int) std::floor(std::ldexp(1.0, oct) * (8.0 - 1.0 / 64));
		assert(wraps == starts);
	}
}

// A change waits for a timebase cycle start both speeds share, then the
// position carries on without a jump, still on the clock's grid.
static void changeWaitsAndStaysContinuous() {
	struct Case {
		int from, to;
		int64_t switchCycle;
	};
	// Set the target during cycle 1
	const Case cases[] = {{0, 1, 2}, {1, 2, 2}, {0, -1, 2}, {0, -2, 4}, {2, -2, 4}, {-1, 0, 2}, {-2, 1, 4}};
	for (const Case& cs : cases) {
		CycleSpeed s;
		s.jumpTo(cs.from);
		Clock c;
		double prev = 0.0;
		bool switched = false;
		for (int i = 0; i < 12 * 64; i++) {
			if (c.cycle == 1 && c.k == 10)
				s.setTarget(cs.to);
			CycleSpeed::Step st = c.step(s);
			if (i > 0) {
				double dt = st.t - prev;
				double rate = std::ldexp(1.0, s.octaves()) / 64;
				double oldRate = std::ldexp(1.0, switched ? s.octaves() : cs.from) / 64;
				// No jump: each step advances at the speed before or after the change
				assert(std::fabs(dt - rate) < 1e-9 || std::fabs(dt - oldRate) < 1e-9);
			}
			if (!switched && s.octaves() == cs.to) {
				switched = true;
				assert(c.cycle == cs.switchCycle && c.k == 0);
				// Cycle starts stay on multiples of 1 / speed timebase cycles
				double grid = std::max(1.0, std::ldexp(1.0, -cs.to));
				assert(std::fabs(std::remainder((double) c.cycle, grid)) < 1e-9);
				assert(std::fabs(st.t - std::round(st.t)) < 1e-9);
			}
			prev = st.t;
		}
		assert(switched);
	}
}

// Immediate: the change is taken on the next step, with no jump
static void immediateChangeIsContinuous() {
	const int pairs[][2] = {{0, 1}, {0, -2}, {2, -1}, {-1, 2}};
	for (const auto& pr : pairs) {
		CycleSpeed s;
		s.setImmediate(true);
		s.jumpTo(pr[0]);
		Clock c;
		double prev = 0.0;
		int sinceSet = -1;
		for (int i = 0; i < 4 * 64; i++) {
			if (c.cycle == 1 && c.k == 10) {
				s.setTarget(pr[1]);
				sinceSet = 0;
			}
			CycleSpeed::Step st = c.step(s);
			if (sinceSet == 0)
				assert(s.octaves() == pr[1]);
			if (i > 0) {
				// The step that takes the change still covers its interval at the old rate
				int oct = sinceSet > 0 ? pr[1] : pr[0];
				assert(std::fabs(st.t - prev - std::ldexp(1.0, oct) / 64) < 1e-9);
			}
			if (sinceSet >= 0)
				sinceSet++;
			prev = st.t;
		}
	}
}

static void backwardAlignDoesNotRefire() {
	CycleSpeed s;
	s.jumpTo(1);
	s.process(0, 0.0, true, false);
	assert(s.process(0, 0.51, false, false).wrapped);
	// A clock pulls the position back over the start, then forward again
	assert(!s.process(0, 0.49, false, false).wrapped);
	assert(!s.process(0, 0.52, false, false).wrapped);
}

static void labels() {
	assert(std::string(CycleSpeed::label(-2)) == "1/4");
	assert(std::string(CycleSpeed::label(0)) == "x1");
	assert(std::string(CycleSpeed::label(2)) == "x4");
	assert(std::string(CycleSpeed::label(9)) == "x4");
}

int main() {
	scalesAndWraps();
	changeWaitsAndStaysContinuous();
	immediateChangeIsContinuous();
	backwardAlignDoesNotRefire();
	labels();
	std::printf("test_cycle_speed: all passed\n");
	return 0;
}

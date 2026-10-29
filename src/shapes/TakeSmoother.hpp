#pragma once
#include "Take.hpp"
#include <vector>

namespace shapes {

// Zero-phase smoothing of a take: a triangular window centred on the query
// point, so the result is not delayed. Second-order prefix sums make every
// query O(1) whatever the width; they are built a few samples at a time so the
// audio thread never stalls after a take changes.
class TakeSmoother {
public:
	TakeSmoother();

	// The take changed: sums are stale until rebuildStep says ready again
	void invalidate();
	// Call once per sample with a small budget; true when the sums are ready
	bool rebuildStep(const Take& take, int budget);
	bool ready() const {
		return ready_;
	}

	// index is fractional, halfWidth in samples (<= 0.5 is plain linear
	// interpolation), wrap treats the take as a loop, otherwise ends clamp.
	// While not ready this returns the raw take.
	double at(const Take& take, double index, double halfWidth, bool wrap) const;

private:
	double rawAt(const Take& take, int n, int i, bool wrap) const;
	double prefix(long n, bool wrap) const;
	double prefix2(long n, bool wrap) const;
	double smoothed(long k, long w, bool wrap) const;

	// Sums run over (sample - offset) so a flat take stays exactly flat
	std::vector<double> p_, q_;
	double offset_ = 0.0;
	int n_ = 0;     // length the sums describe
	int built_ = 0;  // samples summed so far
	bool ready_ = false;
	bool started_ = false;
};

}  // namespace shapes

#include "TakeSmoother.hpp"
#include <algorithm>
#include <cmath>

namespace shapes {

TakeSmoother::TakeSmoother() : p_(kMaxTakeSamples + 1, 0.0), q_(kMaxTakeSamples + 1, 0.0) {}

void TakeSmoother::invalidate() {
	ready_ = false;
	started_ = false;
	built_ = 0;
}

bool TakeSmoother::rebuildStep(const Take& take, int budget) {
	if (ready_ && started_ && take.length == n_)
		return true;
	int len = std::min(std::max(take.length, 0), kMaxTakeSamples);
	if (!started_ || len != n_) {
		// The take may have changed length since the last pass: start over
		started_ = true;
		ready_ = false;
		built_ = 0;
		n_ = len;
		// Any value inside the data's range works; midrange of a coarse scan
		// keeps the sums small and makes a constant take exactly zero
		double lo = 0.0, hi = 0.0;
		if (n_ > 0) {
			lo = hi = take.samples[0];
			for (int s = 1; s <= 64; s++) {
				double v = take.samples[(long)(n_ - 1) * s / 64];
				lo = std::min(lo, v);
				hi = std::max(hi, v);
			}
		}
		offset_ = 0.5 * (lo + hi);
		p_[0] = 0.0;
		q_[0] = 0.0;
	}
	int end = std::min(n_, built_ + std::max(budget, 1));
	for (int i = built_; i < end; i++) {
		double x = (double) take.samples[i] - offset_;
		p_[i + 1] = p_[i] + x;
		q_[i + 1] = q_[i] + p_[i];
	}
	built_ = end;
	if (built_ >= n_)
		ready_ = true;
	return ready_;
}

double TakeSmoother::rawAt(const Take& take, int n, int i, bool wrap) const {
	if (n <= 0)
		return 0.0;
	if (wrap) {
		i %= n;
		if (i < 0)
			i += n;
	}
	else {
		i = std::min(std::max(i, 0), n - 1);
	}
	return take.samples[i];
}

// P(n) = sum of shifted samples [0, n), the take extended past both ends
double TakeSmoother::prefix(long n, bool wrap) const {
	const long N = n_;
	if (wrap) {
		long c = n >= 0 ? n / N : -((-n + N - 1) / N);
		long r = n - c * N;
		return p_[r] + (double) c * p_[N];
	}
	if (n < 0)
		return (double) n * (p_[1] - p_[0]);
	if (n > N)
		return p_[N] + (double) (n - N) * (p_[N] - p_[N - 1]);
	return p_[n];
}

// Q(n) = sum of P(m) for m < n, with the same extension
double TakeSmoother::prefix2(long n, bool wrap) const {
	const long N = n_;
	if (wrap) {
		long c = n >= 0 ? n / N : -((-n + N - 1) / N);
		long r = n - c * N;
		double dc = (double) c;
		return q_[r] + dc * q_[N] + 0.5 * dc * (dc - 1.0) * (double) N * p_[N] + (double) r * dc * p_[N];
	}
	if (n < 0) {
		double x0 = p_[1] - p_[0];
		return x0 * (double) n * (double) (n - 1) * 0.5;
	}
	if (n > N) {
		double xl = p_[N] - p_[N - 1];
		double m = (double) (n - N);
		return q_[N] + m * p_[N] + xl * m * (m - 1.0) * 0.5;
	}
	return q_[n];
}

// Triangular window of integer half-width w (weights w - |j|) at sample k.
// It is two boxes of width w convolved, so it falls out of Q's second difference.
double TakeSmoother::smoothed(long k, long w, bool wrap) const {
	if (!wrap)
		k = std::min(std::max(k, 0L), (long) n_ - 1);
	double d = prefix2(k + w + 1, wrap) - 2.0 * prefix2(k + 1, wrap) + prefix2(k - w + 1, wrap);
	return d / ((double) w * (double) w) + offset_;
}

double TakeSmoother::at(const Take& take, double index, double halfWidth, bool wrap) const {
	int n = ready_ ? n_ : take.length;
	if (n <= 0 || !std::isfinite(index))
		return 0.0;
	if (!wrap)
		index = std::min(std::max(index, 0.0), (double) (n - 1));
	double fl = std::floor(index);
	long i0 = (long) fl;
	double f = index - fl;
	bool raw = !ready_ || !(halfWidth > 1.0);
	if (raw) {
		double a = rawAt(take, n, (int) i0, wrap);
		if (f == 0.0)
			return a;
		return a + (rawAt(take, n, (int) i0 + 1, wrap) - a) * f;
	}
	if (halfWidth > 1e7)
		halfWidth = 1e7;
	long w0 = (long) std::floor(halfWidth);
	double wf = halfWidth - (double) w0;
	// Half-width 1 is the raw sample, so fractional widths blend smoothly into it
	auto yw = [&](long k) {
		double a = smoothed(k, w0, wrap);
		if (wf > 0.0)
			a += (smoothed(k, w0 + 1, wrap) - a) * wf;
		return a;
	};
	double a = yw(i0);
	if (f == 0.0)
		return a;
	return a + (yw(i0 + 1) - a) * f;
}

}  // namespace shapes

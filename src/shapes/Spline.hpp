#pragma once
#include <algorithm>
#include <cmath>

// Cubic Hermite spline through nodes, with a Smooth control
// shared by every node-based shape: 0 steps, 0.5 lines, 1 curves.
namespace shapes {

struct SplinePoint {
	double x, y;
};

struct Spline {
	static constexpr int kCapacity = 32;
	int n;
	double X[kCapacity], Y[kCapacity], M[kCapacity];
};

// With wrap, two extra nodes are added on each side, shifted by whole
// periods, so the curve joins smoothly across the loop point. Takes at most
// kCapacity points (kCapacity - 4 with wrap); extra points are ignored.
inline void makeSpline(Spline& sp, const SplinePoint* pts, int n, bool wrap) {
	sp.n = 0;
	if (n <= 0)
		return;
	if (wrap) {
		n = std::min(n, Spline::kCapacity - 4);
		for (int k = -2; k < n + 2; k++) {
			int i = ((k % n) + n) % n;
			// JS Math.floor(k / n) for negative k
			int shift = k < 0 ? -((-k + n - 1) / n) : k / n;
			sp.X[sp.n] = pts[i].x + shift;
			sp.Y[sp.n] = pts[i].y;
			sp.n++;
		}
	}
	else {
		n = std::min(n, Spline::kCapacity);
		for (int i = 0; i < n; i++) {
			sp.X[i] = pts[i].x;
			sp.Y[i] = pts[i].y;
		}
		sp.n = n;
	}
	for (int i = 0; i < sp.n; i++) {
		int a = std::max(i - 1, 0), b = std::min(i + 1, sp.n - 1);
		double d = sp.X[b] - sp.X[a];
		sp.M[i] = d > 1e-9 ? (sp.Y[b] - sp.Y[a]) / d : 0.0;
	}
}

inline double evalSpline(const Spline& sp, double x, double smooth) {
	if (sp.n < 2)
		return sp.n == 1 ? sp.Y[0] : 0.0;
	int i = 0;
	while (i < sp.n - 2 && sp.X[i + 1] <= x)
		i++;
	double h = sp.X[i + 1] - sp.X[i];
	if (h <= 1e-9)
		return sp.Y[i + 1];
	double u = (x - sp.X[i]) / h;
	u = u < 0.0 ? 0.0 : u > 1.0 ? 1.0 : u;
	double y0 = sp.Y[i], y1 = sp.Y[i + 1];
	double lin = y0 + (y1 - y0) * u;
	if (smooth < 0.5)
		return y0 + (lin - y0) * (smooth * 2);
	double u2 = u * u, u3 = u2 * u;
	double cub = (2 * u3 - 3 * u2 + 1) * y0 + (u3 - 2 * u2 + u) * h * sp.M[i] + (-2 * u3 + 3 * u2) * y1 + (u3 - u2) * h * sp.M[i + 1];
	return lin + (cub - lin) * ((smooth - 0.5) * 2);
}

}  // namespace shapes

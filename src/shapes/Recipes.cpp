#include "Recipes.hpp"
#include "Prng.hpp"
#include <algorithm>
#include <cmath>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace shapes {

namespace {

constexpr double kTau = M_PI * 2;
constexpr int kSalt = 10;

double lerp(double a, double b, double t) {
	return a + (b - a) * t;
}

double clamp(double x, double a, double b) {
	return x < a ? a : x > b ? b : x;
}

double frac(double x) {
	return x - std::floor(x);
}

// Bipolar from unipolar.
double E(double x) {
	return 2 * x - 1;
}

double gauss(double x, double w) {
	return std::exp(-0.5 * (x / w) * (x / w));
}

double smooth01(double x) {
	x = clamp(x, 0, 1);
	return x * x * (3 - 2 * x);
}

// JS Math.round: halves round toward +infinity.
double jsRound(double x) {
	double f = std::floor(x);
	return x - f >= 0.5 ? f + 1 : f;
}

// floor(x) mod n, always in [0, n), safe for any finite x.
int wrapIndex(double x, int n) {
	double m = std::fmod(std::floor(x), (double) n);
	if (m < 0)
		m += n;
	int i = (int) m;
	return i >= n ? 0 : i;
}

double fmSweep(double v) {
	return (v < 0 ? -1 : 1) * (.5 + .5 * std::fabs(v));
}

// Looped table lookup on the same Smooth scale as the node shapes: 0 steps,
// 0.5 lines, 1 curves.
double tableAt(const double* a, int n, double p, double sm) {
	double x = p * n, fl = std::floor(x), u = x - fl;
	int i = wrapIndex(fl, n);
	double y0 = a[i], y1 = a[(i + 1) % n], lin = lerp(y0, y1, u);
	if (sm < 0.5)
		return lerp(y0, lin, sm * 2);
	double ym = a[(i + n - 1) % n], y2 = a[(i + 2) % n], u2 = u * u, u3 = u2 * u;
	double cr = 0.5 * (2 * y0 + (y1 - ym) * u + (2 * ym - 5 * y0 + 4 * y1 - y2) * u2 + (3 * y0 - ym - 3 * y1 + y2) * u3);
	return lerp(lin, cr, (sm - 0.5) * 2);
}

enum RecipeId {
	BOUNCE,
	SPRING,
	RATCHET,
	FLAM,
	BOWED,
	STUTTER,
	HEART,
	HARM,
	FOLD,
	FM,
	CHIRP,
	BEAT,
	ARP,
	RINGING,
	DEVIL,
	NODES,
	FRACTAL,
	GLITCH,
};

struct Param {
	double lo, hi;
	bool isInt = false;
	// Ignores the seed and Drift and steps through its values in equal
	// slices of Variation.
	bool byV = false;
	// Ignores the seed and Drift and runs from low to high across Variation.
	bool alongV = false;
};

constexpr Param P(double lo, double hi) {
	return Param{lo, hi};
}
constexpr Param I(double lo, double hi) {
	return Param{lo, hi, true};
}

struct Recipe {
	const char* name;
	const char* family;
	// Curated type: a seed plus the Variation range [v0, v1] that the knob's
	// 0..1 maps onto. Seed 0 is the recipe's home shape: every setting at
	// mid-range, all sweeping upward together.
	int seed;
	double v0, v1;
	int numParams;
	Param params[kMaxRecipeParams];
};

// Each curated seed and range was picked offline from sweeps over 151
// candidate seeds, then trimmed by ear where an end turned dull.
const Recipe kRecipes[kNumRecipes] = {
	{"Bounce", "Gestures", 140, 0, 1, 3, {P(.35, .85), I(3, 12), P(0, .6)}},
	{"Spring", "Gestures", 134, 0.075, 1, 3, {P(1.5, 14), P(1, 10), P(0, 1)}},
	{"Ratchet", "Gestures", 144, 0, 1, 4, {I(3, 16), P(-.8, .8), P(.5, 1), P(3, 20)}},
	{"Flam", "Gestures", 147, 0, 1, 4, {P(.02, .25), I(1, 3), P(.3, .9), P(2, 10)}},
	{"Bowed", "Gestures", 51, 0.05, 1, 4, {P(.1, .5), I(4, 20), P(0, .3), P(.7, .95)}},
	{"Stutter", "Gestures", 60, 0, 1, 3, {I(4, 24), P(.2, .8), P(1, 6)}},
	{"Heartbeat", "Gestures", 60, 0, 1, 5, {P(.05, .3), P(.006, .018), P(.15, .7), P(.15, .5), P(.2, .45)}},
	// Trimmed at 0.75: past it the swings halve and the fundamental takes over.
	{"Harmonics", "Cycles", 11, 0, 0.75, 6, {I(4, 24), P(2, 18), P(.5, 4), P(-1, 1), P(-1, 1), P(0, 1)}},
	{"Folded sine", "Cycles", 0, 0, 1, 7, {P(3.5, 8), I(2, 5), P(.2, .8), P(.6, 1), I(2, 4), P(-1, 1), P(-.5, .5)}},
	// Ranges kept narrow: audio-FM indexes made dozens of wiggles per cycle
	// and long flat-topped stalls. Below 0.1, Ratio is 1 (dull).
	{"FM", "Cycles", 72, 0.1, 1, 5, {I(1, 4), P(1, 3.5), P(-1, 1), I(1, 9), P(0, 1.5)}},
	{"Chirp", "Cycles", 34, 0, 1, 5, {P(1, 4), P(8, 24), Param{0, 1, false, false, true}, P(-1, 1), P(.6, 1)}},
	{"Beating", "Cycles", 116, 0, 1, 6, {I(14, 40), I(1, 3), I(2, 5), P(.5, 1), P(.5, 1), P(1.5, 4)}},
	{"Arpeggio", "Steps", 17, 0, 1, 4, {I(3, 12), P(.4, 1), Param{0, 2, true, true, false}, P(0, .5)}},
	{"Ringing steps", "Steps", 118, 0, 1, 3, {I(2, 6), P(2, 8), P(3, 10)}},
	{"Devil's staircase", "Steps", 60, 0, 1, 3, {P(.1, .6), I(2, 6), P(.3, .7)}},
	{"Random nodes", "Chaos", 127, 0, 0.8, 3, {I(3, 24), P(0, 1), P(0, 1)}},
	{"Fractal line", "Chaos", 35, 0, 1, 2, {P(.45, .85), I(4, 8)}},
	{"Glitch", "Chaos", 114, 0, 1, 3, {I(6, 24), P(.1, .6), I(4, 16)}},
};

int checkRecipe(int recipe) {
	return recipe < 0 || recipe >= kNumRecipes ? 0 : recipe;
}

double devilF(double x, int d, double a, double lean) {
	if (d == 0)
		return x;
	if (x < a)
		return lean * devilF(x / a, d - 1, a, lean);
	if (x < 1 - a)
		return lean;
	return lean + (1 - lean) * devilF((x - 1 + a) / a, d - 1, a, lean);
}

}  // namespace

const char* recipeName(int recipe) {
	return kRecipes[checkRecipe(recipe)].name;
}

const char* recipeFamily(int recipe) {
	return kRecipes[checkRecipe(recipe)].family;
}

// Each setting gets a base position from the seed. Variation slides every
// setting along a direction the seed also picks, so one knob sweeps the
// whole shape coherently. Each sweep is a straight segment inside the
// setting's range: when the base sits near a limit the segment is shifted
// inward rather than bouncing off it, so the sweep never retraces itself.
void recipeVals(int recipe, double variation, double drift, int64_t cycle, double out[kMaxRecipeParams]) {
	const Recipe& rc = kRecipes[checkRecipe(recipe)];
	// Outside 0..1 the sweeps would leave their ranges (and the tables).
	double V = std::isfinite(variation) ? clamp(variation, 0, 1) : 0.0;
	drift = std::isfinite(drift) ? clamp(drift, 0, 1) : 0.0;
	V = rc.v0 + (rc.v1 - rc.v0) * V;

	Mulberry32 g = seedRng(rc.seed, kSalt);
	bool home = rc.seed == 0;
	// With a byV setting, Variation is cut into its slices and the other
	// settings sweep their whole path within each slice, so every slice gets
	// the same variety.
	double Vs = V;
	for (int i = 0; i < rc.numParams; i++) {
		if (rc.params[i].byV) {
			double k = rc.params[i].hi - rc.params[i].lo + 1;
			Vs = V * k - std::min(k - 1, std::floor(V * k));
		}
	}
	for (int i = 0; i < kMaxRecipeParams; i++)
		out[i] = 0.0;
	for (int i = 0; i < rc.numParams; i++) {
		const Param& pp = rc.params[i];
		// The seed's stream is drawn for every setting, used or not, so each
		// setting's draws stay put.
		double u0 = 0.5, dir = 1;
		if (!home) {
			u0 = g.next();
			double sign = g.next() < .5 ? -1 : 1;
			dir = sign * (.5 + .5 * g.next());
		}
		if (pp.byV) {
			out[i] = std::min(pp.hi, pp.lo + std::floor(V * (pp.hi - pp.lo + 1)));
			continue;
		}
		if (pp.alongV) {
			out[i] = lerp(pp.lo, pp.hi, V);
			continue;
		}
		double half = std::fabs(dir) / 2, c = clamp(u0, half, 1 - half);
		double u = c + (Vs - .5) * dir;
		// At full Drift each setting moves up to +-30% of its range.
		if (drift > 0) {
			double h = hash3Bits(toInt32(rc.seed * 31.0 + kSalt), (uint32_t) i, (uint32_t) cycle) / 4294967296.0;
			u = clamp(u + drift * .6 * (h - .5), 0, 1);
		}
		double x = lerp(pp.lo, pp.hi, u);
		out[i] = pp.isInt ? jsRound(x) : x;
	}
}

void buildRecipe(RecipeInstance& out, int recipe, double variation, double drift, int64_t cycle) {
	recipe = checkRecipe(recipe);
	const Recipe& rc = kRecipes[recipe];
	out.recipe = recipe;
	out.scale = 1.0;
	recipeVals(recipe, variation, drift, cycle, out.vals);
	const double* v = out.vals;
	auto& t = out.tab;
	Mulberry32 rnd = seedRng(rc.seed, kSalt + 1);

	switch (recipe) {
		case BOUNCE: {
			double e = v[0], tt = 0;
			int n = std::min((int) v[1], 15);
			for (int k = 0; k <= n; k++) {
				double d = 2 * std::pow(e, k);
				t.bounce.start[k] = tt;
				t.bounce.dur[k] = d;
				t.bounce.height[k] = std::pow(e, 2 * k);
				tt += d;
			}
			t.bounce.count = n + 1;
			t.bounce.total = tt;
		} break;
		case RATCHET: {
			int n = std::min((int) v[0], 16);
			double g = std::pow(2, v[1] * 1.6);
			for (int k = 0; k <= n; k++)
				t.ratchet.onset[k] = std::pow((double) k / n, g);
		} break;
		// A fundamental plus a formant: a peak in the spectrum around one
		// partial, so a ripple always rides on the backbone. Odd/even thins
		// one set of partials; Brightness sweep moves a cutoff through the
		// partials across the cycle.
		case HARM: {
			double form = v[1], wid = v[2], oe = v[4];
			for (int k = 1; k <= 24; k++) {
				bool odd = k % 2 == 1;
				double keep = odd ? std::min(1.0, 1 - oe) : std::min(1.0, 1 + oe);
				double level = k == 1 ? 1 : (.15 / k + .6 * gauss(k - form, wid)) * keep;
				t.harm.amp[k - 1] = level * (.5 + .5 * rnd.next());
				t.harm.phase[k - 1] = rnd.next() * kTau;
			}
		} break;
		case CHIRP: {
			double lo = v[0], hi = v[1], dir = v[2], tim = v[3], g = std::pow(3, tim);
			auto up = [g](double q) { return std::pow(q, g); };
			// Rate profile 0..1: rising, peaked mid-cycle, falling. With
			// Change point > 0 the change comes late.
			auto m = [&](double q) {
				double tr = up(1 - std::fabs(2 * q - 1));
				return dir < .5 ? lerp(up(q), tr, dir * 2) : lerp(tr, 1 - up(q), dir * 2 - 1);
			};
			const int N = 1024;
			double* ph = t.chirp.phase;
			ph[0] = 0;
			for (int i = 0; i < N; i++)
				ph[i + 1] = ph[i] + (lo + (hi - lo) * m((i + .5) / N)) / N;
			// Scale to a whole number of waves so the loop point doesn't jump.
			t.chirp.k = std::max(1.0, jsRound(ph[N])) / ph[N];
		} break;
		case BEAT:
			t.beat.ph1 = rnd.next() * kTau;
			t.beat.ph2 = rnd.next() * kTau;
			break;
		case ARP: {
			int n = std::min((int) v[0], 12), pat = (int) v[2];
			double span = v[1];
			// Up-down peaks mid-pattern and doesn't repeat its bottom note at
			// the loop point.
			for (int i = 0; i < n; i++) {
				double u = (double) i / (n - 1);
				double shape = pat == 0 ? u : pat == 1 ? 1 - std::fabs(2.0 * i / n - 1) : 1 - u;
				t.arp.level[i] = .5 + span * (shape - .5);
			}
		} break;
		case RINGING:
			for (int i = 0; i < 6; i++)
				t.ringing.level[i] = E(rnd.next()) * .8;
			break;
		case NODES: {
			int n = std::min((int) v[0], 24);
			double reg = v[1];
			SplinePoint raw[24];
			for (int i = 0; i < 24; i++) {
				raw[i].x = rnd.next();
				raw[i].y = E(rnd.next());
			}
			// Stable insertion sort by x over the first n.
			for (int i = 1; i < n; i++) {
				SplinePoint q = raw[i];
				int j = i - 1;
				while (j >= 0 && raw[j].x > q.x) {
					raw[j + 1] = raw[j];
					j--;
				}
				raw[j + 1] = q;
			}
			for (int i = 0; i < n; i++)
				raw[i].x = lerp(raw[i].x, (i + .5) / n, reg);
			makeSpline(t.nodes, raw, n, true);
		} break;
		case FRACTAL: {
			// Midpoint displacement. The table is single precision, as built
			// originally, so the shape matches to the float.
			const int N = 256;
			double rough = v[0];
			int depth = (int) v[1];
			float a[N + 1] = {};
			double dv[N];
			for (int i = 0; i < N; i++)
				dv[i] = E(rnd.next());
			for (int L = 0, step = N; step > 1; L++, step >>= 1) {
				int h = step >> 1;
				for (int m = h; m < N; m += step)
					a[m] = (float) (((double) a[m - h] + (double) a[m + h]) / 2 + (L < depth ? dv[m] * std::pow(rough, L) : 0));
			}
			for (int i = 0; i < N; i++)
				t.fractal.table[i] = a[i];
		} break;
		case GLITCH: {
			int n = std::min((int) v[0], 24);
			double w[24];
			for (int i = 0; i < 24; i++) {
				w[i] = .3 + .7 * rnd.next();
				t.glitch.level[i] = E(rnd.next());
				t.glitch.burst[i] = rnd.next();
			}
			t.glitch.salt = std::floor(rnd.next() * 1e9);
			double s = 0, c = 0;
			for (int i = 0; i < n; i++)
				s += w[i];
			t.glitch.edges[0] = 0;
			for (int i = 0; i < n; i++) {
				c += w[i];
				t.glitch.edges[i + 1] = c / s;
			}
		} break;
		default:
			break;
	}

	double pk = 0;
	for (int i = 0; i < 512; i++) {
		double y = std::fabs(out.raw(i / 512.0));
		if (y > pk)
			pk = y;
	}
	out.scale = pk > 1e-3 ? 1.0 / pk : 1.0;
}

double RecipeInstance::raw(double p) const {
	if (!std::isfinite(p))
		p = 0;
	const double* v = vals;
	switch (recipe) {
		case BOUNCE: {
			double sq = v[2], x = p * tab.bounce.total;
			for (int k = 0; k < tab.bounce.count; k++) {
				double t0 = tab.bounce.start[k], d = tab.bounce.dur[k];
				if (x < t0 + d) {
					double u = (x - t0) / d * 2 - 1;
					return E(tab.bounce.height[k] * std::pow(std::max(0.0, 1 - u * u), 1 - sq));
				}
			}
			return -1;
		}
		case SPRING: {
			double f = v[0], d = v[1], kick = v[2];
			return std::exp(-d * p) * lerp(std::cos(kTau * f * p), std::sin(kTau * f * p), kick);
		}
		case RATCHET: {
			int n = std::min((int) v[0], 16);
			double fade = v[2], snap = v[3];
			const double* on = tab.ratchet.onset;
			for (int k = 0; k < n; k++) {
				if (p < on[k + 1]) {
					double u = (p - on[k]) / (on[k + 1] - on[k]);
					return E(std::pow(fade, k) * std::min(1.0, u / .03) * std::exp(-snap * u));
				}
			}
			return -1;
		}
		case FLAM: {
			double gap = v[0], lv = v[2], dec = v[3], y = 0;
			int g = (int) v[1];
			for (int i = 0; i <= g; i++) {
				double t0 = i * gap;
				bool main = i == g;
				if (p >= t0)
					y = std::max(y, (main ? 1 : lv) * std::min(1.0, (p - t0) / .004) * std::exp(-dec * (main ? 1 : 4) * (p - t0)));
			}
			return E(y);
		}
		case BOWED: {
			double att = v[0], vf = v[1], dep = v[2], rel = v[3];
			double env = smooth01(p / att) * (p > rel ? 1 - smooth01((p - rel) / (1 - rel)) : 1);
			double vib = dep * smooth01((p - att) / (rel - att)) * std::sin(kTau * vf * p);
			return E(clamp((.8 + vib) * env, 0, 1));
		}
		case STUTTER: {
			double n = v[0], duty = v[1], dec = v[2];
			return E(frac(p * n) < duty ? std::exp(-dec * p) : 0);
		}
		// An ECG trace (P, QRS, T) around a flat baseline, so it swings both
		// ways and mixes a sharp spike with broad humps.
		case HEART: {
			double pa = v[0], w = v[1], sd = v[2], ta = v[3], td = v[4];
			double r = .2, t = r + td, tw = .045 + .1 * td;
			// The T wave rises slower than it falls, as on a real trace.
			double tWave = ta * gauss(p - t, p < t ? tw * 1.4 : tw);
			return pa * gauss(p - .07, .022) - .25 * sd * gauss(p - (r - 2.2 * w), w) + gauss(p - r, w) - sd * gauss(p - (r + 2.4 * w), 1.2 * w) + tWave;
		}
		case HARM: {
			int n = (int) v[0];
			double sweep = v[3], spread = v[5];
			// Positive sweep opens the cutoff toward mid-cycle and closes it
			// again (jagged middle); negative does the opposite (smooth
			// middle). The cutoff is the same at both ends, so the loop point
			// doesn't jump.
			double sn = std::sin(M_PI * p), s = sn * sn;
			double cut = 1 + (n + 1) * (sweep >= 0 ? s : 1 - s);
			double y = 0;
			for (int k = 1; k <= n && k <= 24; k++) {
				double m = k == 1 ? 1 : lerp(1, smooth01((cut - k) / 2 + .5), std::fabs(sweep));
				y += tab.harm.amp[k - 1] * m * std::sin(kTau * k * p + tab.harm.phase[k - 1] * spread);
			}
			return y;
		}
		// Two sines through a wavefolder. Bloom swells the drive and the
		// level together, several times a cycle, so the folds breathe: quiet
		// and plain between swells, loud and folded at each one. Lean tilts
		// every swell's peak early or late.
		case FOLD: {
			double drive = v[0], k = v[1], mix = v[2], bloom = v[3], n = v[4], lean = v[5], bias = v[6];
			double pk = .5 + .35 * lean, u = frac(p * n);
			double q = u < pk ? .5 * u / pk : .5 + .5 * (u - pk) / (1 - pk);
			double sq = std::sin(M_PI * q);
			double sw = lerp(1, sq * sq, bloom);
			return sw * std::sin(M_PI / 2 * drive * sw * (std::sin(kTau * p) + mix * std::sin(kTau * k * p) + bias));
		}
		// Cascaded FM: a second modulator bends the first. Index sweep makes
		// the brightness decay (negative) or grow (positive) across the
		// cycle, and the level follows it, like a struck or bowed FM tone.
		// The sweep never sits near 0, so there's always a contour.
		case FM: {
			double k1 = v[0], idx = v[1], k2 = v[3], idx2 = v[4], sw = fmSweep(v[2]);
			double env = sw >= 0 ? lerp(1, p, sw) : lerp(1, 1 - p, -sw);
			return (.3 + .7 * env) * std::sin(kTau * p + idx * env * std::sin(kTau * k1 * p + idx2 * std::sin(kTau * k2 * p)));
		}
		// Direction follows Variation, so the knob travels from speeding up,
		// through up-and-back (fastest mid-cycle), to slowing down. The
		// Window's floor keeps the amplitude swell at every position.
		case CHIRP: {
			const int N = 1024;
			double win = v[4], x = p * N;
			double fl = std::min((double) (N - 1), std::floor(x));
			int i = fl < 0 ? 0 : (int) fl;
			const double* ph = tab.chirp.phase;
			return std::sin(kTau * tab.chirp.k * lerp(ph[i], ph[i + 1], x - i)) * lerp(1, std::sin(M_PI * p), win);
		}
		// Three close partials: their differences give a slow beat with a
		// faster beat nested inside it. Rectify and Sharpness turn the
		// carrier peaks into discrete pulses.
		case BEAT: {
			double f = v[0], b1 = v[1], m = v[2], depth = v[3], rect = v[4], sharp = v[5], b2 = b1 * m;
			double x = (std::sin(kTau * f * p) + depth * std::sin(kTau * (f + b1) * p + tab.beat.ph1) + depth * std::sin(kTau * (f + b2) * p + tab.beat.ph2)) / (1 + 2 * depth);
			return lerp(x, E(std::pow(std::fabs(x), sharp)), rect);
		}
		// Pattern follows Variation directly in equal thirds (Up, Up-down,
		// Down); the other settings repeat their sweep in each third.
		case ARP: {
			int n = std::min((int) v[0], 12);
			double glide = v[3], x = p * n, fl = std::floor(x), u = x - fl;
			int i = wrapIndex(fl, n);
			double a = tab.arp.level[i], b = tab.arp.level[(i + 1) % n];
			return E(glide > 0 && u > 1 - glide ? lerp(a, b, smooth01((u - 1 + glide) / glide)) : a);
		}
		case RINGING: {
			int n = std::min((int) v[0], 6);
			double f = v[1], d = v[2], x = p * n, fl = std::floor(x), u = x - fl;
			int k = wrapIndex(fl, n);
			double L = tab.ringing.level[k], prev = tab.ringing.level[(k + n - 1) % n];
			return L + (prev - L) * std::exp(-d * u) * std::cos(kTau * f * u);
		}
		case DEVIL: {
			double gap = v[0], lean = v[2];
			return E(devilF(p, (int) v[1], (1 - gap) / 2, lean));
		}
		case NODES:
			return evalSpline(tab.nodes, p, v[2]);
		case FRACTAL:
			return tableAt(tab.fractal.table, 256, p, .5);
		case GLITCH: {
			int n = std::min((int) v[0], 24);
			double bp = v[1], rate = v[2];
			const double* edges = tab.glitch.edges;
			for (int i = 0; i < n; i++) {
				if (p < edges[i + 1]) {
					if (tab.glitch.burst[i] >= bp)
						return tab.glitch.level[i];
					return E(hash3(tab.glitch.salt, i, std::floor((p - edges[i]) / (edges[i + 1] - edges[i]) * rate)));
				}
			}
			return tab.glitch.level[n - 1];
		}
		default:
			return 0;
	}
}

double RecipeInstance::eval(double p) const {
	double y = raw(p) * scale;
	return std::isfinite(y) ? y : 0.0;
}

}  // namespace shapes

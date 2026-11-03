// Combine engine tests: waves and presets against reference vectors, ratio
// continuity, the Input wave, S&H and Bend with Input, Fit range guarantee
// and the preset table.
#include "reference.hpp"
#include "shapes/Combine.hpp"
#include "shapes/OutputStage.hpp"
#include "shapes/Prng.hpp"
#include "shapes/WeaverPresets.hpp"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

using namespace shapes;

static int failures = 0;

static void check(bool ok, const char* what, int a = 0, int b = 0) {
	if (!ok) {
		if (failures < 20)
			std::printf("  FAIL %s [%d][%d]\n", what, a, b);
		failures++;
	}
}

static const double T = 0.99999;

static int ratioIndexOf(double r) {
	for (int i = 0; i < kNumRatios; i++)
		if (ratioValue(i) == r)
			return i;
	return -1;
}

static void ratioTable() {
	check(kNumRatios == 15, "ratio count");
	check(ratioIndexOf(24.0) == 13, "x24 index");
	check(std::strcmp(ratioLabel(0), "1/8") == 0, "1/8 label");
	check(std::strcmp(ratioLabel(3), "x1") == 0, "x1 label");
	check(std::strcmp(ratioLabel(14), "x32") == 0, "x32 label");
	check(ratioValue(14) == 32.0, "x32 value");
}

// The random waves have a wider range than the reference's, so only the
// deterministic waves are compared
static void wavesMatch() {
	for (int w = 0; w < SteppedRandom; w++) {
		for (int s = 0; s < 2; s++) {
			OperatorControls op{};
			op.wave = (Wave) w;
			op.ratio = 3;
			op.param = s == 0 ? 0.2 : 0.7;
			op.level = 1.0;
			for (int k = 0; k < 65; k++) {
				double got = evalOperator(op, k / 64.0 * T, 0);
				double want = ref::waveRaw[w][s][k];
				if (!(std::fabs(got - want) <= 1e-6)) {
					if (failures < 20)
						std::printf("  wave %d shape %d k %d: got %.17g want %.17g\n", w, s, k, got, want);
					failures++;
				}
			}
		}
	}
}

// The reference keeps the prototype's preset order and its "\xC3\x97" for
// "x"; Weaver puts the random presets last, and has retuned some presets
// for a wider range or dropped their window, or for the random waves' wider
// range, so those have no reference
static int referencePreset(const char* name) {
	for (const char* retuned : {"Envelope + triangle x16", "Swell with gated ripple", "Sampled falling ramp", "Random accents, fading", "Random swell",
	                            "Phase-bent sine", "Max with random steps", "Random stutter"})
		if (std::strcmp(name, retuned) == 0)
			return -1;
	for (int m = 0; m < 15; m++) {
		std::string r = ref::presetNames[m];
		size_t times = r.find("\xC3\x97");
		if (times != std::string::npos)
			r.replace(times, 2, "x");
		if (r == name)
			return m;
	}
	return -1;
}

static void presetsMatchReference() {
	check(std::strcmp(ref::presetNames[0], "Envelope + triangle \xC3\x97" "16") == 0, "reference name 0");
	for (int n = 0; n < 15; n++) {
		const CombineControls& c = kWeaverPresets[n].controls;
		const int m = referencePreset(kWeaverPresets[n].name);
		if (m < 0)
			continue;
		const double* v = ref::presetValues[m];
		for (int i = 0; i < 3; i++) {
			check(c.op[i].wave == (Wave) (int) v[i * 5], "preset wave", n, i);
			check(ratioValue(c.op[i].ratio) == ref::presetRatios[m][i], "preset ratio", n, i);
			check(c.op[i].level == v[i * 5 + 2], "preset level", n, i);
			check(c.op[i].phase == v[i * 5 + 3], "preset phase", n, i);
			check(c.op[i].param == v[i * 5 + 4], "preset param", n, i);
		}
		check(c.algorithm == (Algorithm) (int) v[15], "preset algorithm", n);
		check(v[16] == 0.0, "preset window", n);

		// The prototype used LEVEL as depth in these; Weaver uses it as amplitude,
		// so only the operators are compared
		bool sameAlgorithm = c.algorithm != Scale && c.algorithm != Scale2 && c.algorithm != Gate;
		CombineEngine e;
		e.setSampleRate(48000.0);
		for (int k = 0; k < 129; k++) {
			CombineFrame f = e.process(c, k / 64.0 * T, false);
			if (sameAlgorithm && !(std::fabs(f.result - ref::presetRaw[m][k]) <= 1e-6)) {
				if (failures < 20)
					std::printf("  preset %d k %d: got %.17g want %.17g\n", n, k, f.result, ref::presetRaw[m][k]);
				failures++;
			}
			for (int i = 0; i < 3; i++)
				check(std::fabs(f.raw[i] - ref::presetParts[m][k][i]) <= 1e-6, "preset part", n, k * 3 + i);
		}
	}
}

static CombineControls quiet() {
	CombineControls c = kWeaverPresets[0].controls;
	for (int i = 0; i < 3; i++) {
		c.op[i].wave = Sine;
		c.op[i].ratio = 3;
		c.op[i].param = 0.5;
		c.op[i].phase = 0.0;
		c.op[i].level = 0.0;
	}
	c.algorithm = Mix;
	return c;
}

static void ratioChangeKeepsPhaseContinuous() {
	CombineControls c = quiet();
	c.op[1].ratio = ratioIndexOf(4.0);
	c.op[1].level = 1.0;
	CombineEngine e;
	e.setSampleRate(48000.0);
	double dt = 1.0 / 24000.0;
	double t = 0.0;
	double before = 0.0;
	for (; t < 0.37; t += dt)
		before = e.process(c, t, false).raw[1];
	c.op[1].ratio = ratioIndexOf(5.0);
	double after = e.process(c, t, false).raw[1];
	check(std::fabs(before - after) < 0.05, "continuous on ratio change");
	// The new ratio runs on from the phase the old one reached
	double want = evalOperator(c.op[1], t * 4.0 + 0.05 * 5.0, 1);
	double got = e.process(c, t + 0.05, false).raw[1];
	check(std::fabs(got - want) <= 1e-9, "new ratio advances from the old phase");
	e.reset();
	double z = e.process(c, 0.1, false).raw[1];
	check(std::fabs(z - evalOperator(c.op[1], 0.1 * 5.0, 1)) <= 1e-12, "reset clears the offset");
}

static void inputPassesThroughWithGainAndSlew() {
	CombineControls c = quiet();
	c.op[0].wave = Input;
	c.op[0].param = 0.0;
	c.op[0].level = 0.8;
	c.op[0].inputGain = 1.0;
	CombineEngine e;
	e.setSampleRate(48000.0);
	const double volts[] = {0.0, 2.5, -5.0, 3.3, 10.0};
	for (int n = 0; n < 5; n++) {
		c.op[0].inputVolts = volts[n];
		CombineFrame f = e.process(c, n * 0.01, false);
		check(std::fabs(f.result - volts[n] / 5.0 * 0.8) <= 1e-12, "input passthrough", n);
		check(std::fabs(f.raw[0] - volts[n] / 5.0) <= 1e-12, "input raw part", n);
	}
	c.op[0].inputGain = 2.0;
	c.op[0].inputVolts = 1.0;
	check(std::fabs(e.process(c, 0.1, false).result - 1.0 / 5.0 * 2.0 * 0.8) <= 1e-12, "input gain");

	// Slew: PARAM 0.5 is a ~31.6 ms time constant
	c.op[0].inputGain = 1.0;
	c.op[0].level = 1.0;
	c.op[0].inputVolts = 0.0;
	c.op[0].param = 0.5;
	CombineEngine s;
	s.setSampleRate(48000.0);
	s.process(c, 0.0, false);
	c.op[0].inputVolts = 5.0;
	double tau = 0.001 * std::pow(1000.0, 0.5);
	int n = (int) std::round(tau * 48000.0);
	double y = 0.0;
	for (int i = 0; i < n; i++)
		y = s.process(c, i / 48000.0, false).result;
	check(y > 0.6 && y < 0.66, "slew reaches 1 - 1/e after one time constant");
	for (int i = 0; i < 48000; i++)
		y = s.process(c, i / 48000.0, false).result;
	check(std::fabs(y - 1.0) < 1e-6, "slew settles");
}

static void sampleHoldOnInputBRisingEdge() {
	CombineControls c = quiet();
	c.algorithm = SampleHold;
	c.op[0].wave = RampUp;
	c.op[0].level = 1.0;
	c.op[1].wave = Input;
	c.op[1].param = 0.0;
	c.op[1].level = 1.0;
	c.op[1].inputGain = 1.0;
	c.op[1].inputVolts = -5.0;
	OperatorControls a = c.op[0];
	CombineEngine e;
	e.setSampleRate(48000.0);
	e.process(c, 0.1, false);
	double held = e.process(c, 0.2, false).result;
	check(std::fabs(held - evalOperator(a, 0.1, 0)) <= 1e-12, "holds the first sample until an edge");
	// Small wiggle inside the hysteresis band is not an edge
	c.op[1].inputVolts = 0.1;
	check(std::fabs(e.process(c, 0.25, false).result - held) <= 1e-12, "no edge inside hysteresis");
	c.op[1].inputVolts = 0.5;  // 0.1, just past the hysteresis
	double y = e.process(c, 0.3, false).result;
	check(std::fabs(y - evalOperator(a, 0.3, 0)) <= 1e-12, "samples A on the rising edge");
	check(std::fabs(e.process(c, 0.5, false).result - y) <= 1e-12, "holds while B stays high");
	c.op[1].inputVolts = -5.0;
	check(std::fabs(e.process(c, 0.55, false).result - y) <= 1e-12, "falling edge does not sample");
	c.op[1].inputVolts = 5.0;
	check(std::fabs(e.process(c, 0.6, false).result - evalOperator(a, 0.6, 0)) <= 1e-12, "second rising edge");

	// A = Input holds A's actual value at B's cycle start
	CombineControls d = quiet();
	d.algorithm = SampleHold;
	d.op[0].wave = Input;
	d.op[0].param = 0.0;
	d.op[0].level = 1.0;
	d.op[0].inputGain = 1.0;
	d.op[1].ratio = ratioIndexOf(4.0);
	CombineEngine h;
	h.setSampleRate(48000.0);
	d.op[0].inputVolts = 1.0;
	h.process(d, 0.30, false);  // B cycle 1 (0.25..0.5)
	d.op[0].inputVolts = 2.0;
	check(std::fabs(h.process(d, 0.40, false).result - 0.2) <= 1e-12, "input A held within B cycle");
	d.op[0].inputVolts = 3.0;
	check(std::fabs(h.process(d, 0.51, false).result - 0.6) <= 1e-12, "input A sampled at next B cycle");
}

static void bendWithInputAPassesThrough() {
	CombineControls c = quiet();
	c.algorithm = Bend;
	c.op[0].wave = Input;
	c.op[0].param = 0.0;
	c.op[0].level = 0.5;
	c.op[0].inputGain = 1.0;
	c.op[1].level = 1.0;
	c.op[2].level = 0.3;
	CombineEngine e;
	e.setSampleRate(48000.0);
	for (int k = 0; k < 20; k++) {
		c.op[0].inputVolts = -4.0 + 0.4 * k;
		double t = k * 0.047;
		CombineFrame f = e.process(c, t, false);
		double want = c.op[0].inputVolts / 5.0 * 0.5 + evalOperator(c.op[2], t, 2) * 0.3;
		check(std::fabs(f.result - want) <= 1e-12, "bend passes input A", k);
	}
}

static void fitNeverExceedsRange() {
	CombineControls c = quiet();
	c.op[0].wave = Input;
	c.op[0].param = 0.0;
	c.op[0].level = 1.0;
	c.op[0].inputGain = 1.0;
	c.op[1].level = 1.0;
	CombineEngine e;
	e.setSampleRate(1000.0);
	double t = 0.0;
	int64_t cycle = 0;
	for (int i = 0; i < 5000; i++) {
		if ((int64_t) std::floor(t) != cycle) {
			cycle = (int64_t) std::floor(t);
			e.cycleWrapped();
		}
		c.op[0].inputVolts = 20.0 * std::sin(i * 0.013) + (i % 97 == 0 ? 20.0 : 0.0);
		double y = e.process(c, t, (i / 700) % 2 == 1).result;
		double out = treat(y, Treat::Fit, e.peak());
		check(std::fabs(out) <= 1.0 + 1e-9, "fit in range (input)", i);
		t += 0.0031;
	}
	// Formula peak on every preset, with ratios changing mid-cycle, scanned
	// at once and spread
	for (int spread : {0, 1, 4}) {
		for (int n = 0; n < 15; n++) {
			CombineControls p = kWeaverPresets[n].controls;
			CombineEngine f;
			f.setSampleRate(1000.0);
			f.setScanSpread(spread);
			for (int i = 0; i < 3000; i++) {
				if (i == 1234)
					p.op[0].ratio = (p.op[0].ratio + 3) % kNumRatios;
				double y = f.process(p, i * 0.0017, false).result;
				check(std::fabs(treat(y, Treat::Fit, f.peak())) <= 1.0 + 1e-9, "fit in range (preset)", n, i);
			}
		}
	}
}

// The peak a full scan of this cycle gives, as Fit uses it
static double scannedPeak(const CombineEngine& e, const CombineControls& c, double t, double y) {
	double pk = 0.0;
	for (int k = 0; k < 256; k++)
		pk = std::max(pk, std::fabs(e.resultAt(c, std::floor(t) + k / 256.0, false)));
	return std::max(pk, std::fabs(y));
}

// Scans are skipped while nothing changes, but once settled the peak is the
// full scan's, after a control change and in a new cycle (random waves differ
// from cycle to cycle)
static void peakFollowsControlsAndCycles() {
	for (int spread : {0, 1, 3}) {
		for (int n = 0; n < 15; n++) {
			CombineControls c = kWeaverPresets[n].controls;
			CombineEngine e;
			e.setSampleRate(48000.0);
			e.setScanSpread(spread);
			const double dt = 1.0 / 2000.0;
			double t = 0.1, y = 0.0;
			auto run = [&](int samples) {
				for (int i = 0; i < samples; i++, t += dt)
					y = e.process(c, t, false).result;
			};
			auto expect = [&](const char* what) {
				double want = scannedPeak(e, c, t - dt, y);
				check(std::fabs(e.peak() - want) <= 1e-9 * std::max(1.0, want), what, spread, n);
			};
			run(400);
			expect("settled peak");
			c.op[0].level *= 2.5;
			run(400);
			expect("peak after a level change");
			// Past the next cycle start
			run(1400);
			expect("peak in a new cycle");
		}
	}
}

static void outputsAreFinite() {
	CombineControls c = kWeaverPresets[10].controls;
	c.op[0].wave = Input;
	c.op[0].inputVolts = NAN;
	c.op[0].inputGain = INFINITY;
	CombineEngine e;
	e.setSampleRate(48000.0);
	for (int i = 0; i < 100; i++) {
		CombineFrame f = e.process(c, i * 0.01, false);
		check(std::isfinite(f.result) && std::isfinite(e.peak()), "finite output", i);
		for (int k = 0; k < 3; k++)
			check(std::isfinite(f.raw[k]) && std::isfinite(f.scaled[k]), "finite parts", i, k);
	}
}

static void reverseMirrorsTime() {
	const CombineControls& c = kWeaverPresets[4].controls;
	CombineEngine fwd, rev;
	fwd.setSampleRate(48000.0);
	rev.setSampleRate(48000.0);
	double a = fwd.process(c, 1.3, false).result;
	double b = rev.process(c, 1.7, true).result;
	check(std::fabs(a - b) <= 1e-12, "reverse evaluates at 1 - p");

	// At a cycle start reverse stays in that cycle (p = 0 maps to 0, not 1).
	// Stepped random draws a new value each cycle, so the next cycle differs.
	const CombineControls& r = kWeaverPresets[11].controls;
	bool nextDiffers = false;
	for (int k = 1; k <= 5; k++) {
		CombineEngine f, b2, n;
		f.setSampleRate(48000.0);
		b2.setSampleRate(48000.0);
		n.setSampleRate(48000.0);
		double want = f.process(r, (double) k, false).result;
		check(std::fabs(b2.process(r, (double) k, true).result - want) <= 1e-12, "reverse at a cycle start", k);
		nextDiffers = nextDiffers || std::fabs(n.process(r, k + 1.0, false).result - want) > 1e-6;
	}
	check(nextDiffers, "test preset changes from cycle to cycle");
}

static void hotInputDoesNotOvershoot() {
	// B = Input drives Scale's depth; a hot input must not push it past 1
	CombineControls c = quiet();
	c.algorithm = Scale;
	c.op[0].wave = Square;
	c.op[0].level = 1.0;
	c.op[1].wave = Input;
	c.op[1].param = 0.0;
	c.op[1].level = 1.0;
	c.op[1].inputGain = 2.0;
	CombineEngine e;
	e.setSampleRate(48000.0);
	c.op[1].inputVolts = 10.0;
	double sa = evalOperator(c.op[0], 0.1, 0);
	check(std::fabs(e.process(c, 0.1, false).result - sa) <= 1e-12, "scale depth clamped high");
	c.op[1].inputVolts = -10.0;
	check(std::fabs(e.process(c, 0.1, false).result) <= 1e-12, "scale depth clamped low");

	// C = Input drives Xfade; hot input stops at B (or A), never beyond
	CombineControls x = quiet();
	x.algorithm = Xfade;
	x.op[0].wave = Square;
	x.op[0].level = 1.0;
	x.op[1].wave = Square;
	x.op[1].param = 0.0;  // mostly low
	x.op[1].level = 1.0;
	x.op[2].wave = Input;
	x.op[2].param = 0.0;
	x.op[2].level = 1.0;
	x.op[2].inputGain = 2.0;
	CombineEngine g;
	g.setSampleRate(48000.0);
	x.op[2].inputVolts = 10.0;
	double sb = evalOperator(x.op[1], 0.25, 1);
	check(sb != evalOperator(x.op[0], 0.25, 0), "xfade test needs A != B");
	check(std::fabs(g.process(x, 0.25, false).result - sb) <= 1e-12, "xfade clamped at B");
	x.op[2].inputVolts = -10.0;
	double sa2 = evalOperator(x.op[0], 0.25, 0);
	check(std::fabs(g.process(x, 0.25, false).result - sa2) <= 1e-12, "xfade clamped at A");
}

static void algorithmsFollowLevel() {
	// A = +1 and B = -1 nearly all cycle; the controlling operator is a ramp,
	// so its 0..1 value at time t is t
	CombineControls c = quiet();
	c.op[0].wave = Square;
	c.op[0].param = 1.0;
	c.op[0].level = 1.0;
	c.op[1].wave = Square;
	c.op[1].param = 0.0;
	c.op[1].level = 1.0;
	c.op[2].wave = RampUp;
	c.algorithm = Xfade;
	CombineEngine e;
	c.op[2].level = 1.0;
	check(std::fabs(e.resultAt(c, 0.9, false) + 0.8) <= 1e-9, "xfade toward B");
	c.op[2].level = -1.0;
	check(std::fabs(e.resultAt(c, 0.9, false) - 0.8) <= 1e-9, "negative level reverses xfade");
	c.op[2].level = 0.0;
	check(std::fabs(e.resultAt(c, 0.9, false)) <= 1e-9, "xfade level 0 is an even mix");

	c = quiet();
	c.op[0].wave = Square;
	c.op[0].param = 1.0;
	c.op[0].level = 1.0;
	c.op[1].wave = RampUp;
	c.algorithm = Gate;
	c.op[1].level = 1.0;
	check(e.resultAt(c, 0.7, false) == 0.0, "gate closed below half scale");
	check(e.resultAt(c, 0.8, false) == 1.0, "gate open above half scale");
	c.op[1].level = -1.0;
	check(e.resultAt(c, 0.2, false) == 1.0, "negative level opens on B's low part");
	check(e.resultAt(c, 0.3, false) == 0.0, "negative level closes above");
	c.op[1].level = 0.5;
	check(e.resultAt(c, 0.95, false) == 0.0, "gate at half level never reaches the threshold");
	c.op[1].level = 0.0;
	check(e.resultAt(c, 0.99, false) == 0.0, "gate level 0 stays closed");

	// Scale: B at its LEVEL, moved to 0..1, is A's gain
	c.algorithm = Scale;
	c.op[1].level = 1.0;
	check(std::fabs(e.resultAt(c, 0.9, false) - 0.9) <= 1e-9, "scale follows B");
	c.op[1].level = -1.0;
	check(std::fabs(e.resultAt(c, 0.9, false) - 0.1) <= 1e-9, "negative level inverts B");
	c.op[1].level = 0.0;
	check(std::fabs(e.resultAt(c, 0.9, false) - 0.5) <= 1e-9, "scale level 0 is a steady half");
}

static void displayAccessorsMatchProcess() {
	// After a ratio change the offsets move; the display accessors follow them
	CombineControls c = kWeaverPresets[4].controls;
	CombineEngine e;
	e.setSampleRate(1000.0);
	for (int i = 0; i < 800; i++) {
		if (i == 300)
			c.op[1].ratio = 6;
		double t = i * 0.0021;
		bool rev = i >= 500;
		CombineFrame f = e.process(c, t, rev);
		check(std::fabs(e.resultAt(c, t, rev) - f.result) <= 1e-12, "resultAt matches process", i);
		for (int k = 0; k < 3; k++)
			check(std::fabs(e.operatorAt(c, k, t, rev) - f.raw[k]) <= 1e-12, "operatorAt matches process", i, k);
	}
}

static bool isAscii(const char* s) {
	for (; *s; s++)
		if ((unsigned char) *s > 126 || (unsigned char) *s < 32)
			return false;
	return true;
}

static void presetsAreAscii() {
	for (int n = 0; n < 15; n++)
		check(isAscii(kWeaverPresets[n].name), "ascii preset name", n);
	check(std::strcmp(kWeaverPresets[0].name, "Envelope + triangle x16") == 0, "preset 0 name");
	check(std::strcmp(kWeaverPresets[13].name, "Random stutter") == 0, "preset 13 name");
	for (int w = 0; w < kNumWaves; w++)
		check(isAscii(paramName((Wave) w)), "ascii param name", w);
	for (int a = 0; a < kNumAlgorithms; a++)
		check(isAscii(algorithmName((Algorithm) a)) && isAscii(algorithmFormula((Algorithm) a)), "ascii algorithm", a);
	for (int r = 0; r < kNumRatios; r++)
		check(isAscii(ratioLabel(r)), "ascii ratio", r);
	check(std::strcmp(paramName(Input), "SLEW") == 0, "input param name");
	check(std::strcmp(paramName(SmoothRandom), "DENSITY") == 0, "density param name");
	check(std::strcmp(algorithmName(SampleHold), "S&H") == 0, "S&H name");
	check(std::strcmp(algorithmFormula(Scale), "(A scaled by B) + C") == 0, "scale formula");
}

static void presetMatchFindsDefault() {
	check(presetMatches(kWeaverPresets[0].controls) == 0, "default preset matches");
	for (int n = 0; n < 15; n++)
		check(presetMatches(kWeaverPresets[n].controls) == n, "each preset matches itself", n);
	CombineControls c = kWeaverPresets[11].controls;
	c.op[1].param += 5e-5;
	c.op[0].inputVolts = 3.0;
	c.op[2].inputGain = 1.7;
	check(presetMatches(c) == 11, "tolerance and input fields ignored");
	c.op[1].param += 1e-3;
	check(presetMatches(c) == -1, "edited settings match nothing");
	c = kWeaverPresets[11].controls;
	c.algorithm = Ring;
	check(presetMatches(c) == -1, "algorithm must match");
}

static void loopWavesRepeatEachCycle() {
	check(isLoop(SteppedLoop) && isLoop(SmoothLoop) && !isLoop(SteppedRandom) && !isLoop(Input), "isLoop");
	check(Input == 10 && kNumWaves == 11, "Input stays last");
	check(std::strcmp(paramName(SteppedLoop), "STEPS") == 0 && std::strcmp(paramName(SmoothLoop), "DENSITY") == 0, "loop param names");
	for (Wave w : {SteppedLoop, SmoothLoop}) {
		for (double param : {0.0, 0.3, 1.0}) {
			OperatorControls op{};
			op.wave = w;
			op.param = param;
			op.loopSeed = 12345;
			double lo = 1.0, hi = -1.0;
			for (int k = 0; k < 200; k++) {
				double x = k / 200.0;
				double v = evalOperator(op, x, 0);
				lo = std::min(lo, v);
				hi = std::max(hi, v);
				check(v >= -1.0 && v <= 1.0, "loop in range", w, k);
				check(std::fabs(evalOperator(op, x + 3.0, 0) - v) < 1e-9, "loop repeats", w, k);
				check(evalOperator(op, x, 2) == v, "loop ignores the operator letter", w, k);
			}
			check(hi - lo > 0.05, "loop moves", w, (int) (param * 10));
			OperatorControls other = op;
			other.loopSeed = 999;
			bool differs = false;
			for (int k = 0; k < 64; k++)
				differs = differs || evalOperator(other, k / 64.0, 0) != evalOperator(op, k / 64.0, 0);
			check(differs, "seed picks the pattern", w, (int) (param * 10));
		}
	}
	// Stepped: 4 + round(12 * param) steps per cycle
	OperatorControls st{};
	st.wave = SteppedLoop;
	st.loopSeed = 7;
	for (double param : {0.0, 0.5, 1.0}) {
		st.param = param;
		int changes = 0;
		for (int k = 1; k < 960; k++)
			changes += evalOperator(st, k / 960.0, 0) != evalOperator(st, (k - 1) / 960.0, 0);
		int steps = 4 + (int) std::round(param * 12.0);
		check(changes == steps - 1, "step count", steps, changes);
	}
	// Stepped random: 4 + round(12 * param) steps per cycle
	OperatorControls sr{};
	sr.wave = SteppedRandom;
	for (double param : {0.0, 0.5, 1.0}) {
		sr.param = param;
		int changes = 0;
		for (int k = 1; k < 960; k++)
			changes += evalOperator(sr, k / 960.0, 0) != evalOperator(sr, (k - 1) / 960.0, 0);
		int steps = 4 + (int) std::round(param * 12.0);
		check(changes == steps - 1, "stepped random step count", steps, changes);
	}
	// Smooth random: 6 to 16 points per cycle, at the hashed integers
	OperatorControls smr{};
	smr.wave = SmoothRandom;
	for (double param : {0.0, 1.0}) {
		smr.param = param;
		double n = param == 0.0 ? 6.0 : 16.0;
		check(evalOperator(smr, 1.0 / n, 0) == hash3('A', 1.0, 31) * 2.0 - 1.0, "smooth random density", (int) n);
	}
	// Smooth: continuous through the loop point
	OperatorControls sm{};
	sm.wave = SmoothLoop;
	sm.param = 0.5;
	sm.loopSeed = 42;
	check(std::fabs(evalOperator(sm, 1.0 - 1e-9, 0) - evalOperator(sm, 0.0, 0)) < 1e-6, "smooth loop joins up");
}

// A preset loaded mid-cycle changes ratios, which the engine carries on
// smoothly with phase offsets; realign() drops them, so the operators line
// up with the cycle as if the preset had played from reset.
static void realignLinesUpWithTheCycle() {
	CombineEngine e;
	e.setSampleRate(48000.0);
	double t = 0.0;
	for (int k = 0; k < 1000; k++, t += 0.000731)
		e.process(kWeaverPresets[0].controls, t, false);
	const CombineControls& c = kWeaverPresets[7].controls;
	e.process(c, t, false);
	CombineEngine clean;
	double before = 0.0;
	for (double p : {0.0, 0.13, 0.5, 0.77})
		before = std::max(before, std::fabs(e.resultAt(c, 2.0 + p, false) - clean.resultAt(c, p, false)));
	check(before > 0.1, "offset before realign");
	e.realign();
	e.process(c, t, false);
	for (double p : {0.0, 0.13, 0.5, 0.77})
		check(std::fabs(e.resultAt(c, 2.0 + p, false) - clean.resultAt(c, p, false)) <= 1e-12, "realigned", (int) (p * 100));
}

// A ratio change applied at a cycle start, with ratioChangesAt() given that
// exact start, leaves a whole-number offset: the operators stay lined up with
// the cycle, though the sample processed is just past the start.
static void ratioChangeAtCycleStartStaysAligned() {
	CombineControls c = kWeaverPresets[7].controls;
	CombineEngine e;
	e.setSampleRate(48000.0);
	double t = 0.0;
	for (; t < 3.0; t += 0.000731)
		e.process(c, t, false);
	c.op[0].ratio = 5;
	c.op[1].ratio = 6;
	e.ratioChangesAt(3.0);
	e.process(c, t, false);
	CombineEngine clean;
	for (double p : {0.0, 0.13, 0.5, 0.77})
		check(std::fabs(e.resultAt(c, 4.0 + p, false) - clean.resultAt(c, p, false)) <= 1e-9, "aligned at cycle start", (int) (p * 100));
}

int main() {
	ratioTable();
	wavesMatch();
	presetsMatchReference();
	ratioChangeKeepsPhaseContinuous();
	inputPassesThroughWithGainAndSlew();
	sampleHoldOnInputBRisingEdge();
	bendWithInputAPassesThrough();
	fitNeverExceedsRange();
	peakFollowsControlsAndCycles();
	outputsAreFinite();
	reverseMirrorsTime();
	hotInputDoesNotOvershoot();
	algorithmsFollowLevel();
	displayAccessorsMatchProcess();
	presetsAreAscii();
	presetMatchFindsDefault();
	loopWavesRepeatEachCycle();
	realignLinesUpWithTheCycle();
	ratioChangeAtCycleStartStaysAligned();
	if (failures) {
		std::printf("test_combine: %d failures\n", failures);
		return 1;
	}
	std::printf("test_combine: all passed\n");
	return 0;
}

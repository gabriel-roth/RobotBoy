// Desktop CPU benchmark for the bird modules. Not a test (run.sh skips it).
// Build from the repo root; -O2 -funsafe-math-optimizations are the MetaModule flags:
//   g++ -std=c++20 -O2 -funsafe-math-optimizations -Isrc \
//     -o "$(mktemp -d)/bench" tests/shapes/bench_bird_cpu.cpp src/shapes/{Recipes,SkylarkEngine,Combine,LyrebirdEngine,TakeSmoother,TakeFile}.cpp
// The Particules reference is compiled in only with -DBENCH_PARTICULES and the Beads sources:
//   ... -DBENCH_PARTICULES -DTEST -Dstmlib=robotboy_stmlib -Isrc/beads -Isrc/beads/vendor -Wno-vla-cxx-extension \
//     src/beads/host/*.cpp src/beads/vendor/beads/dsp/{delay_clock,delay_controller,mu_law,processor,trigger_generator}.cc \
//     src/beads/vendor/beads/resources.cc src/beads/vendor/stmlib/dsp/units.cc src/beads/vendor/stmlib/utils/random.cc
// Each case runs 10 s of 48 kHz audio and prints ns/sample and % of a 48 kHz sample budget.
// --worst runs only the worst-case modulation cases instead: every control on a
// slow LFO plus knob-sized noise, timed per 64-sample block, as MetaModule's
// deadline is per block; --still runs the same cases with the controls held,
// --jitter held but with the noise, as a mapped knob nobody is turning.
// Then --hash prints a hash of each case's output instead of timing it, and a
// last argument runs only the cases whose names contain it.
// Add -DMETAMODULE for the MetaModule constants.
#include "shapes/Combine.hpp"
#include "shapes/LyrebirdEngine.hpp"
#include "shapes/Timebase.hpp"
#include "shapes/SkylarkEngine.hpp"
#include "shapes/WeaverPresets.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <new>

#ifdef BENCH_PARTICULES
#include "host/engine_host.h"
#endif

using namespace shapes;

static constexpr double kSr = 48000.0;
static constexpr int kSeconds = 10;
static constexpr int kN = (int) kSr * kSeconds;
static constexpr int kTracePoints = 256;
static constexpr int kPublishInterval = 2048;
static volatile double sink;

static void report(const char* name, double ns) {
	printf("%-44s %8.1f ns/sample  %5.2f%% of budget\n", name, ns, ns / (1e9 / kSr) * 100.0);
}

static void run(const char* name, const std::function<void(int)>& body) {
	for (int rep = 0; rep < 1; rep++) body(kN / 10);  // warm up
	auto t0 = std::chrono::steady_clock::now();
	body(kN);
	auto t1 = std::chrono::steady_clock::now();
	report(name, std::chrono::duration<double, std::nano>(t1 - t0).count() / kN);
}

// Skylark's audio thread: the engine, plus a snapshot copy each publish (the
// trace is drawn up on the UI thread). spread is the engine's scan spread
// (MetaModule: 1). Also prints the slowest sample, as bursts overload a
// MetaModule audio block even when the average is low (timing each sample
// adds about 15 ns to the average). Desktop checks every 64 samples, MetaModule 256.
// shapes: also step the shape every 2400 samples, as turning SHAPE does.
static void skylark(const char* name, bool sweep, int spread, bool shapes = false) {
	double worst = 0.0;
	run(name, [&](int n) {
		SkylarkEngine e;
		e.setScanSpread(spread);
		Timebase tb;
		tb.setSampleRate(kSr);
		TimebaseInput in;
		SkylarkControls c;
		c.shape = 0;  // Bounce
		static SkylarkEngine snapshot;
		double acc = 0;
		int since = 0;
		worst = 0.0;
		for (int i = 0; i < n; i++) {
			auto t0 = std::chrono::steady_clock::now();
			if (sweep)
				c.variation = 0.5 + 0.5 * std::sin(2.0 * M_PI * i / kSr);
			if (shapes)
				c.shape = (i / 2400) % kNumRecipes;
			Timebase::Step s = tb.process(in);
			e.setControls(c);
			double raw = e.raw(s.pos, s.cycle);
			acc += toVolts(treat(raw, Treat::Fit, e.peak()), Range::Bipolar5);
			if (++since >= kPublishInterval) {
				since = 0;
				snapshot = e;
				acc += snapshot.peak();
			}
			worst = std::max(worst, std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count());
		}
		sink = acc;
	});
	printf("%-44s %8.1f us slowest sample\n", "", worst);
}

// Weaver's audio thread: the engine, plus a snapshot copy each publish (the
// traces are drawn up on the UI thread). spread is the engine's scan spread
// (MetaModule: 1); modulate sweeps A's PARAM every sample, as a CV would.
static void weaver(const char* name, CombineControls c, int spread, bool modulate = false) {
	run(name, [&](int n) {
		CombineEngine e;
		e.setSampleRate(kSr);
		e.setScanSpread(spread);
		Timebase tb;
		tb.setSampleRate(kSr);
		TimebaseInput in;
		static CombineEngine snapshot;
		double acc = 0;
		int since = 0;
		double param = c.op[0].param;
		for (int i = 0; i < n; i++) {
			Timebase::Step s = tb.process(in);
			if (s.wrapped)
				e.cycleWrapped();
			for (int k = 0; k < 3; k++)
				if (c.op[k].wave == Input)
					c.op[k].inputVolts = 2.0 * std::sin(2.0 * M_PI * (3 + k) * i / kSr);
			if (modulate)
				c.op[0].param = std::clamp(param + 0.3 * std::sin(2.0 * M_PI * i / kSr), 0.0, 1.0);
			CombineFrame f = e.process(c, (double) s.cycle + s.pos, false);
			acc += toVolts(treat(f.result, Treat::Fit, e.peak()), Range::Bipolar5);
			if (++since >= kPublishInterval) {
				since = 0;
				snapshot = e;
				acc += snapshot.peak();
			}
		}
		sink = acc;
	});
}

static void lyrebird() {
	run("Lyrebird 4-bar take, Smooth 40 ms", [&](int n) {
		LyrebirdEngine e;
		e.setSampleRate(kSr);
		Timebase tb;
		tb.setSampleRate(kSr);
		TimebaseInput in;
		in.bpm = 120.0;
		LyrebirdControls c;
		c.smoothMs = 40;
		c.lengthIndex = 4;
		Take& t = e.spare();
		t.clear();
		t.mode = TakeMode::Sync;
		t.bars = 4.0;
		t.bpm = 120.0;
		t.seconds = 4 * 240.0 / 120.0;
		t.length = (int) (t.seconds * kTakeRate);
		for (int i = 0; i < t.length; i++)
			t.samples[i] = (float) (4.0 * std::sin(2.0 * M_PI * 5.0 * i / t.length) + std::sin(2.0 * M_PI * 37.0 * i / t.length));
		e.requestLoad();
		e.process(c, Timebase::Step{0.0, 0, false, false});
		double acc = 0;
		int since = 0;
		std::vector<float> raw(kTracePoints), out(kTracePoints);
		for (int i = 0; i < n; i++) {
			LyrebirdStep s = e.process(c, tb.process(in));
			acc += s.volts;
			if (++since >= kPublishInterval) {
				since = 0;
				e.renderTrace(c, raw.data(), out.data(), kTracePoints);
				acc += out[3];
			}
		}
		sink = acc;
	});
}


// ---- Worst case: every control moving ----

// MetaModule knobs and CV inputs jitter by a few thousandths of their range
static constexpr double kJitter = 0.003;
static constexpr int kBlock = 64;

struct Noise {
	uint32_t s = 12345;
	double next() {  // uniform in [-1, 1)
		s = s * 1664525u + 1013904223u;
		return (double) (s >> 8) / 8388608.0 - 1.0;
	}
};

// The worst cases run the engines as the modules do, every kControlInterval
// samples with the outputs ramped between; --per-sample (before the mode)
// runs them every sample, as before control rate
static int gRate = kControlInterval;

// --still holds every control at its base value, for the static cost;
// --jitter holds them too, but with the noise
static bool gStill = false;
static bool gJitter = false;

// base + an LFO of the given depth and rate + jitter, clamped to [lo, hi]
static double moving(Noise& nz, int i, double base, double depth, double hz, double lo = 0.0, double hi = 1.0) {
	if (gStill)
		return std::clamp(base, lo, hi);
	// A triangle LFO: a sine here would cost more than some of the engines
	double ph = hz * i / kSr;
	double tri = 4.0 * std::fabs(ph - std::floor(ph + 0.5)) - 1.0;
	double v = base + (gJitter ? 0.0 : depth * tri) + kJitter * (hi - lo) * nz.next();
	return std::clamp(v, lo, hi);
}

// Runs body(i) per sample, timed per block; prints the average and the
// slowest block. Each case runs three times and keeps the smallest slowest
// block: the OS preempting the bench lands at random, a real burst every time.
static const char* gFilter = nullptr;
// --hash (with --worst or --still): print a hash of each case's output instead
// of timing it, to check a change is bit-identical
static bool gHash = false;

static void worstCase(const char* name, const std::function<void()>& setup, const std::function<double(int)>& body) {
	if (gFilter && !strstr(name, gFilter))
		return;
	if (gHash) {
		setup();
		uint64_t h = 1469598103934665603ull;
		for (int i = 0; i < kN; i++) {
			double v = body(i);
			uint64_t bits;
			memcpy(&bits, &v, sizeof bits);
			h = (h ^ bits) * 1099511628211ull;
		}
		printf("%-52s %016llx\n", name, (unsigned long long) h);
		return;
	}
	double worst = 1e30, avgNs = 1e30;
	for (int rep = 0; rep < 3; rep++) {
		setup();
		for (int i = 0; i < kN / 10; i++) sink = body(i);
		setup();
		double acc = 0, repWorst = 0, total = 0;
		for (int b = 0; b < kN / kBlock; b++) {
			auto t0 = std::chrono::steady_clock::now();
			for (int k = 0; k < kBlock; k++)
				acc += body(b * kBlock + k);
			double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count();
			total += us;
			repWorst = std::max(repWorst, us);
		}
		sink = acc;
		worst = std::min(worst, repWorst);
		avgNs = std::min(avgNs, total * 1000.0 / kN);
	}
	printf("%-52s %7.1f ns/sample  worst block %6.1f us (%5.1fx average)\n", name, avgNs, worst, worst * 1000.0 / kBlock / avgNs);
}

static void skylarkWorst(const char* name, int shape, double drift, double variationHz) {
	static SkylarkEngine e, snapshot;
	static Timebase tb;
	static TimebaseInput in;
	static Noise nz;
	static int since;
	static Ramp ramp;
	worstCase(name, [&] {
		e = SkylarkEngine();
		e.setScanSpread(gRate > 1 ? 2 : 1);
		ramp = Ramp();
		tb = Timebase();
		tb.setSampleRate(kSr);
		in = TimebaseInput();
		in.bpm = 120.0;
		nz = Noise();
		since = 0;
	}, [&](int i) {
		Timebase::Step s = tb.process(in);
		double v = 0.0;
		if (i % gRate == 0) {
			SkylarkControls c;
			c.shape = shape;
			c.variation = moving(nz, i, 0.5, 0.45, variationHz);
			// Away from 0, where the transforms could skip work
			c.timing = moving(nz, i, 0.3, 0.5, 0.13, -1.0, 1.0);
			c.lift = moving(nz, i, -0.2, 0.5, 0.17, -1.0, 1.0);
			c.symmetry = moving(nz, i, 0.25, 0.5, 0.11, -1.0, 1.0);
			c.tilt = moving(nz, i, 0.15, 0.5, 0.19, -1.0, 1.0);
			c.drift = drift > 0.0 ? moving(nz, i, drift, 0.0, 0.0) : 0.0;
			e.setControls(c);
			ramp.target(toVolts(treat(e.raw(s.pos, s.cycle), Treat::Fit, e.peak()), Range::Bipolar5), gRate);
		}
		v += ramp.next();
		if (++since >= kPublishInterval) {
			since = 0;
			snapshot = e;
			v += snapshot.peak();
		}
		return v;
	});
}

static void weaverWorst(const char* name, CombineControls base) {
	static CombineEngine e, snapshot;
	static Timebase tb;
	static TimebaseInput in;
	static Noise nz;
	static int since;
	static Ramp ramps[4];
	worstCase(name, [&] {
		e = CombineEngine();
		e.setSampleRate(kSr / gRate);
		e.setScanSpread(1);
		for (Ramp& r : ramps)
			r = Ramp();
		tb = Timebase();
		tb.setSampleRate(kSr);
		in = TimebaseInput();
		nz = Noise();
		since = 0;
	}, [&](int i) {
		Timebase::Step s = tb.process(in);
		if (s.wrapped)
			e.cycleWrapped();
		if (i % gRate == 0) {
			CombineControls c = base;
			for (int k = 0; k < 3; k++) {
				OperatorControls& o = c.op[k];
				o.param = moving(nz, i, 0.5, 0.4, 0.1 + 0.07 * k);
				o.phase = moving(nz, i, 0.25, 0.2, 0.05 + 0.03 * k);
				o.level = moving(nz, i, 0.7, 0.25, 0.09 + 0.04 * k);
				if (o.wave == Input)
					o.inputVolts = moving(nz, i, 0.0, 2.0, 3 + k, -5.0, 5.0);
			}
			CombineFrame f = e.process(c, (double) s.cycle + s.pos, false);
			ramps[0].target(toVolts(treat(f.result, Treat::Fit, e.peak()), Range::Bipolar5), gRate);
			for (int k = 0; k < 3; k++)
				ramps[1 + k].target(toVolts(treat(f.raw[k], Treat::Clip, 1.0), Range::Bipolar5), gRate);
		}
		double v = ramps[0].next() + ramps[1].next() + ramps[2].next() + ramps[3].next();
		if (++since >= kPublishInterval) {
			since = 0;
			snapshot = e;
			v += snapshot.peak();
		}
		return v;
	});
}

static void lyrebirdWorst(const char* name, double smoothBase, bool loop) {
	static LyrebirdEngine e;
	static Timebase tb;
	static TimebaseInput in;
	static Noise nz;
	static int since;
	static std::vector<float> raw(kTracePoints), out(kTracePoints);
	worstCase(name, [&] {
		e.~LyrebirdEngine();
		new (&e) LyrebirdEngine();
		e.setSampleRate(kSr);
		e.setControlInterval(gRate);
		tb = Timebase();
		tb.setSampleRate(kSr);
		in = TimebaseInput();
		in.bpm = 120.0;
		nz = Noise();
		since = 0;
		Take& t = e.spare();
		t.clear();
		t.mode = TakeMode::Sync;
		t.bars = 4.0;
		t.bpm = 120.0;
		t.seconds = 4 * 240.0 / 120.0;
		t.length = (int) (t.seconds * kTakeRate);
		for (int i = 0; i < t.length; i++)
			t.samples[i] = (float) (4.0 * std::sin(2.0 * M_PI * 5.0 * i / t.length) + std::sin(2.0 * M_PI * 37.0 * i / t.length));
		e.requestLoad();
		LyrebirdControls c;
		c.lengthIndex = 4;
		e.process(c, Timebase::Step{0.0, 0, false, false});
	}, [&](int i) {
		LyrebirdControls c;
		c.lengthIndex = 4;
		c.loop = loop;
		c.source = moving(nz, i, 0.0, 4.0, 0.3, -10.0, 10.0);
		c.speedOct = moving(nz, i, 0.0, 1.0, 0.05, -4.0, 4.0);
		// The module's cubic Smooth law, the knob moving under it
		double k = moving(nz, i, smoothBase, smoothBase > 0.0 ? 0.15 : 0.0, 0.08);
		c.smoothMs = smoothBase > 0.0 ? 1000.0 * k * k * k : 0.0;
		LyrebirdStep s = e.process(c, tb.process(in));
		double v = s.volts;
		// The module draws the trace up a point at a time between publishes
		constexpr int stride = kPublishInterval / kTracePoints;
		if (since % stride == 0)
			e.renderTrace(c, raw.data(), out.data(), kTracePoints, since / stride, since / stride + 1);
		if (++since >= kPublishInterval) {
			since = 0;
			v += out[3];
		}
		return v;
	});
}

static void worstCases() {
#ifdef METAMODULE
	printf("MetaModule constants (-DMETAMODULE)\n");
#else
	printf("Desktop constants (no -DMETAMODULE)\n");
#endif
	char name[96];
	for (int shape = 0; shape < kNumRecipes; shape++) {
		snprintf(name, sizeof(name), "Skylark %s, all moving", recipeName(shape));
		skylarkWorst(name, shape, 0.0, 0.2);
	}
	skylarkWorst("Skylark Bounce, all moving, Variation 5 Hz", 0, 0.0, 5.0);
	skylarkWorst("Skylark Bounce, all moving, Drift 0.5", 0, 0.5, 0.2);
	for (int p = 0; p < kNumWeaverPresets; p++) {
		snprintf(name, sizeof(name), "Weaver preset %d, all moving", p + 1);
		weaverWorst(name, kWeaverPresets[p].controls);
	}
	CombineControls in3 = kWeaverPresets[0].controls;
	for (int i = 0; i < 3; i++)
		in3.op[i].wave = Input;
	weaverWorst("Weaver three Input operators, all moving", in3);
	lyrebirdWorst("Lyrebird Smooth off, all moving", 0.0, true);
	lyrebirdWorst("Lyrebird Smooth ~40 ms, all moving, loop", 0.34, true);
	lyrebirdWorst("Lyrebird Smooth ~300 ms, all moving, loop", 0.67, true);
	lyrebirdWorst("Lyrebird Smooth ~300 ms, all moving, once", 0.67, false);
}

#ifdef BENCH_PARTICULES
namespace {
beads::RandomizedParameter Knob(float base) {
	beads::RandomizedParameter r;
	r.base = base; r.attenuverter = 0.0f; r.cv = 0.0f; r.has_cv = false;
	return r;
}
struct FixedControls : robotboy_beads::ControlSource {
	robotboy_beads::BlockControls c;
	FixedControls() {
		c.parameters.time = Knob(0.0f);
		c.parameters.size = Knob(0.6f);
		c.parameters.shape = Knob(0.5f);
		c.parameters.pitch = Knob(0.0f);
		c.parameters.monophonic_mode = false;
		c.parameters.pitch_lock = nullptr;
		c.density.base = 0.8f; c.density.cv = 0.0f;
		c.mix.feedback = 0.0f; c.mix.dry_wet = 1.0f; c.mix.reverb_amount = 0.0f; c.mix.output_gate = false;
		c.seed_button = beads::SEED_BUTTON_STATE_LOCKED;
		c.seed_patched = false;
		c.gain_setting = 1;
	}
	void ReadControls(robotboy_beads::BlockControls* out) override { *out = c; }
};
}  // namespace

static void particulesWorst() {
	static FixedControls controls;
	static robotboy_beads::EngineHost* host;
	worstCase("Particules granular, Bright (reference)", [&] {
		delete host;
		host = new robotboy_beads::EngineHost();
		host->Init(beads::Processor::MODE_GRANULAR, true, (float) kSr);
		host->SetQuality(robotboy_beads::Quality::kBright);
		host->SetInputs(true, false);
	}, [&](int i) {
		return host->Process({(float) (0.5 * std::sin(2.0 * M_PI * 330.0 * i / kSr)), 0.0f, false, false}, &controls).l;
	});
}

static void particules() {
	run("Particules granular, Quality Bright (reference)", [&](int n) {
		FixedControls controls;
		robotboy_beads::EngineHost host;
		host.Init(beads::Processor::MODE_GRANULAR, true, (float) kSr);
		host.SetQuality(robotboy_beads::Quality::kBright);
		host.SetInputs(true, false);
		double acc = 0;
		for (int i = 0; i < n; i++)
			acc += host.Process({(float) (0.5 * std::sin(2.0 * M_PI * 330.0 * i / kSr)), 0.0f, false, false}, &controls).l;
		sink = acc;
	});
}
#endif

int main(int argc, char** argv) {
	if (argc > 1 && !strcmp(argv[1], "--per-sample")) {
		gRate = 1;
		argc--;
		argv++;
	}
	if (argc > 1 && (!strcmp(argv[1], "--worst") || !strcmp(argv[1], "--still") || !strcmp(argv[1], "--jitter"))) {
		gStill = !strcmp(argv[1], "--still");
		gJitter = !strcmp(argv[1], "--jitter");
		int a = 2;
		if (argc > a && !strcmp(argv[a], "--hash")) {
			gHash = true;
			a++;
		}
		gFilter = argc > a ? argv[a] : nullptr;
		worstCases();
#ifdef BENCH_PARTICULES
		particulesWorst();
#endif
		return 0;
	}
	skylark("Skylark Bounce, static", false, 0);
	skylark("Skylark Bounce, Variation 1 Hz sine", true, 0);
	skylark("Skylark Bounce, Variation swept, spread 1 (MM)", true, 1);
	skylark("Skylark stepping through the shapes", false, 0, true);
	skylark("Skylark stepping through the shapes, spread 1 (MM)", false, 1, true);
	weaver("Weaver preset 1 (Envelope + triangle x16)", kWeaverPresets[0].controls, 0);
	weaver("Weaver preset 1, scan spread 1 (MM)", kWeaverPresets[0].controls, 1);
	weaver("Weaver preset 1, PARAM swept", kWeaverPresets[0].controls, 0, true);
	weaver("Weaver preset 1, PARAM swept, spread 1 (MM)", kWeaverPresets[0].controls, 1, true);
	weaver("Weaver preset 12 (random steps), spread 1 (MM)", kWeaverPresets[11].controls, 1);
	CombineControls in3 = kWeaverPresets[0].controls;
	for (int i = 0; i < 3; i++) {
		in3.op[i].wave = Input;
		in3.op[i].level = 1.0;
	}
	weaver("Weaver three Input operators", in3, 0);
	lyrebird();
#ifdef BENCH_PARTICULES
	particules();
#else
	printf("Particules reference: not built (-DBENCH_PARTICULES)\n");
#endif
}

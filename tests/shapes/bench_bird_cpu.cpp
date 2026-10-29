// Desktop CPU benchmark for the bird modules. Not a test (run.sh skips it).
// Build from the repo root; -O2 -funsafe-math-optimizations are the MetaModule flags:
//   g++ -std=c++20 -O2 -funsafe-math-optimizations -Isrc \
//     -o "$(mktemp -d)/bench" tests/shapes/bench_bird_cpu.cpp src/shapes/{Recipes,SkylarkEngine,Combine,LyrebirdEngine,TakeSmoother,TakeFile}.cpp
// The Particules reference is compiled in only with -DBENCH_PARTICULES and the Beads sources:
//   ... -DBENCH_PARTICULES -DTEST -Dstmlib=robotboy_stmlib -Isrc/beads -Isrc/beads/vendor -Wno-vla-cxx-extension \
//     src/beads/host/*.cpp src/beads/vendor/beads/dsp/{delay_clock,delay_controller,mu_law,processor,trigger_generator}.cc \
//     src/beads/vendor/beads/resources.cc src/beads/vendor/stmlib/dsp/units.cc src/beads/vendor/stmlib/utils/random.cc
// Each case runs 10 s of 48 kHz audio and prints ns/sample and % of a 48 kHz sample budget.
#include "shapes/Combine.hpp"
#include "shapes/LyrebirdEngine.hpp"
#include "shapes/Timebase.hpp"
#include "shapes/SkylarkEngine.hpp"
#include "shapes/WeaverPresets.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <functional>

#ifdef BENCH_PARTICULES
#include "host/engine_host.h"
#endif

using namespace shapes;

static constexpr double kSr = 48000.0;
static constexpr int kSeconds = 10;
static constexpr int kN = (int) kSr * kSeconds;
static constexpr int kTracePoints = 256, kWavePoints = 64;
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

static void skylark(const char* name, bool sweep) {
	run(name, [&](int n) {
		SkylarkEngine e;
		Timebase tb;
		tb.setSampleRate(kSr);
		TimebaseInput in;
		SkylarkControls c;
		c.shape = 0;  // Bounce
		float trace[kTracePoints];
		double acc = 0;
		int since = 0;
		for (int i = 0; i < n; i++) {
			if (sweep)
				c.variation = 0.5 + 0.5 * std::sin(2.0 * M_PI * i / kSr);
			Timebase::Step s = tb.process(in);
			e.setControls(c);
			double raw = e.raw(s.pos, s.cycle);
			acc += toVolts(treat(raw, Treat::Fit, e.peak()), Range::Bipolar5);
			if (++since >= kPublishInterval) {
				since = 0;
				e.renderCycle(trace, kTracePoints, Treat::Fit, Range::Bipolar5);
				acc += trace[7];
			}
		}
		sink = acc;
	});
}

// Mirrors Weaver's renderFormulas
static void renderFormulas(float* result, float (*scaled)[kTracePoints], float (*wave)[kWavePoints],
                           const CombineEngine& e, const CombineControls& c, double cycle) {
	double peak = e.peak();
	for (int k = 0; k < kTracePoints; k++) {
		double t = cycle + std::min((double) k / (kTracePoints - 1), 1.0 - 1e-9);
		result[k] = (float) sanitize(treat(e.resultAt(c, t, false), Treat::Fit, peak), 0.0);
		for (int i = 0; i < 3; i++)
			scaled[i][k] = (float) (e.operatorAt(c, i, t, false) * c.op[i].level);
	}
	for (int i = 0; i < 3; i++)
		for (int k = 0; k < kWavePoints; k++)
			wave[i][k] = (float) sanitize(evalOperator(c.op[i], (double) k / (kWavePoints - 1) * 0.99999, i), 0.0);
}

static void weaver(const char* name, CombineControls c) {
	run(name, [&](int n) {
		CombineEngine e;
		e.setSampleRate(kSr);
		Timebase tb;
		tb.setSampleRate(kSr);
		TimebaseInput in;
		float result[kTracePoints], scaled[3][kTracePoints], wave[3][kWavePoints];
		double acc = 0;
		int since = 0;
		for (int i = 0; i < n; i++) {
			Timebase::Step s = tb.process(in);
			if (s.wrapped)
				e.cycleWrapped();
			for (int k = 0; k < 3; k++)
				if (c.op[k].wave == Input)
					c.op[k].inputVolts = 2.0 * std::sin(2.0 * M_PI * (3 + k) * i / kSr);
			CombineFrame f = e.process(c, (double) s.cycle + s.pos, false);
			acc += toVolts(treat(f.result, Treat::Fit, e.peak()), Range::Bipolar5);
			if (++since >= kPublishInterval) {
				since = 0;
				renderFormulas(result, scaled, wave, e, c, (double) s.cycle);
				acc += result[5];
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

int main() {
	skylark("Skylark Bounce, static", false);
	skylark("Skylark Bounce, Variation 1 Hz sine", true);
	weaver("Weaver preset 1 (Envelope + triangle x16)", kWeaverPresets[0].controls);
	CombineControls in3 = kWeaverPresets[0].controls;
	for (int i = 0; i < 3; i++) {
		in3.op[i].wave = Input;
		in3.op[i].level = 1.0;
	}
	weaver("Weaver three Input operators", in3);
	lyrebird();
#ifdef BENCH_PARTICULES
	particules();
#else
	printf("Particules reference: not built (-DBENCH_PARTICULES)\n");
#endif
}

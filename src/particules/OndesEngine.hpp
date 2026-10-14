#pragma once
// Not <rack.hpp> directly: with src/ on the include path, rack.hpp's own
// <plugin.hpp> would resolve to ours before rack is declared.
#include "plugin.hpp"

#include <memory>

// The Ondes oscillator: the Beads wavetable voice. The Ondes module owns the
// params and ports, and keeps the engine alive for its whole lifetime, which
// lets the panel display read it from the UI thread.
struct Ondes;

struct OndesEngine {
	virtual ~OndesEngine() = default;
	virtual void process(const rack::Module::ProcessArgs& args) = 0;
	virtual void onSampleRateChange(float sample_rate) = 0;
	// The normalized trace value (about -1..1) of the wave this engine last
	// played, at index i of n points. UI thread; reads only atomics and
	// static tables.
	virtual float displaySample(int i, int n) const = 0;
};

std::unique_ptr<OndesEngine> MakeOndesBeads(Ondes& m);

// The Beads trace for bank 0, wave 0: the module browser thumbnail, which
// has no module.
float OndesPreviewSample(int i, int n);

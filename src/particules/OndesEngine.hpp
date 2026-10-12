#pragma once
// Not <rack.hpp> directly: with src/ on the include path, rack.hpp's own
// <plugin.hpp> would resolve to ours before rack is declared.
#include "plugin.hpp"

#include <memory>

// One Ondes oscillator: the Beads wavetable voice or the v2.1 (classic) one.
// The Ondes module owns the params, ports and the mode flag. Both engines
// are tiny (no audio buffers), so the module keeps both alive for its whole
// lifetime and switching only changes which one process() calls; that is
// what lets the panel display read the active engine from the UI thread.
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
std::unique_ptr<OndesEngine> MakeOndesClassic(Ondes& m);

// The Beads trace for bank 0, wave 0: the module browser thumbnail, which
// has no module.
float OndesPreviewSample(int i, int n);

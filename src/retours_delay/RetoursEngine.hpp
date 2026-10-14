#pragma once
// Not <rack.hpp> directly: with src/ on the include path, rack.hpp's own
// <plugin.hpp> would resolve to ours before rack is declared.
#include "plugin.hpp"

#include <memory>

// The Retours engine: the Beads delay. The Retours module owns the params,
// ports and saved settings; the engine reads them through the module and
// keeps only runtime state.
//
// Only the audio thread touches the engine (and Rack's locked calls: reset,
// sample rate). The UI thread works through the module: clear and
// clear-tempo requests and the menu's clear state are atomics there, and
// the engine's menu items are built from module state alone.
struct Retours;

struct RetoursEngine {
	virtual ~RetoursEngine() = default;
	virtual bool ok() const = 0;                         // buffers allocated
	virtual void process(const rack::Module::ProcessArgs& args) = 0;
	virtual void onSampleRateChange(float sample_rate) = 0;
	virtual void onReset() = 0;                          // engine-side reset
	virtual bool clearDisabled() const = 0;              // buffer empty: greys Clear buffer
	virtual bool clearTempoDisabled() const = 0;         // no saved tempo: greys Clear saved tempo
};

// Sized for the host's current sample rate.
std::unique_ptr<RetoursEngine> MakeRetoursBeads(Retours& m, float sample_rate);

// The engine's menu items, built from module state only.
void AppendRetoursBeadsMenu(Retours* m, rack::ui::Menu* menu);

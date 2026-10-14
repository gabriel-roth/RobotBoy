#pragma once
// Not <rack.hpp> directly: with src/ on the include path, rack.hpp's own
// <plugin.hpp> would resolve to ours before rack is declared.
#include "plugin.hpp"

#include <memory>

// The Particules engine: the Beads granular processor. The Particules module
// owns the params, ports and saved settings; the engine reads them through
// the module and keeps only runtime state.
//
// Only the audio thread touches the engine (and Rack's locked calls: reset,
// sample rate, port changes). The UI thread works through the module: clear
// requests and the menu's frozen/clear state are atomics there, and the
// engine's menu items are built from module state alone.
struct Particules;

struct ParticulesEngine {
	virtual ~ParticulesEngine() = default;
	virtual bool ok() const = 0;                         // buffers allocated
	virtual void process(const rack::Module::ProcessArgs& args) = 0;
	virtual void onSampleRateChange(float sample_rate) = 0;
	virtual void onReset() = 0;                          // engine-side reset
	virtual void onPortChange(const rack::Module::PortChangeEvent& e) {}
	virtual bool clearDisabled() const = 0;              // buffer empty: greys Clear buffer
};

// Sized for the host's current sample rate.
std::unique_ptr<ParticulesEngine> MakeParticulesBeads(Particules& m, float sample_rate);

// The engine's menu items, built from module state only.
void AppendParticulesBeadsMenu(Particules* m, rack::ui::Menu* menu);

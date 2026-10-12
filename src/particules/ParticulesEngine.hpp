#pragma once
// Not <rack.hpp> directly: with src/ on the include path, rack.hpp's own
// <plugin.hpp> would resolve to ours before rack is declared.
#include "plugin.hpp"

#include <atomic>
#include <memory>

// One Particules engine: the Beads granular processor or the v2.1 (classic)
// one. The Particules module owns the params, ports, saved settings and the
// mode flag; the active engine reads them through the module and keeps only
// runtime state, so it can be destroyed and rebuilt on a mode switch.
//
// Only the audio thread touches an engine (and Rack's locked calls: reset,
// sample rate, port changes). The UI thread works through the module: clear
// requests and the menu's frozen/clear state are atomics there, and each
// mode's menu items are built from module state alone.
struct Particules;

struct ParticulesEngine {
	virtual ~ParticulesEngine() = default;
	virtual bool ok() const = 0;                         // buffers allocated
	virtual void process(const rack::Module::ProcessArgs& args) = 0;
	virtual void onSampleRateChange(float sample_rate) = 0;
	virtual void onReset() = 0;                          // engine-side reset
	virtual void onPortChange(const rack::Module::PortChangeEvent& e) {}
	virtual bool clearDisabled() const = 0;              // buffer empty: greys Clear buffer
	virtual bool frozen() const = 0;                     // last process() freeze state
};

// Both factories size the engine for Particules::sample_rate_.
std::unique_ptr<ParticulesEngine> MakeParticulesBeads(Particules& m);
std::unique_ptr<ParticulesEngine> MakeParticulesClassic(Particules& m);

// Each mode's engine-only menu items, built from module state only (never
// from the engine, which the audio thread may be replacing).
void AppendParticulesBeadsMenu(Particules* m, rack::ui::Menu* menu);

// How many engines each factory has built, process-wide. Lets the headless
// mode-switch test check that a patch loaded into one mode never builds the
// other engine.
extern std::atomic<int> g_particules_beads_builds;
extern std::atomic<int> g_particules_classic_builds;

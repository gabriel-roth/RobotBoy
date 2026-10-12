#pragma once
// Not <rack.hpp> directly: with src/ on the include path, rack.hpp's own
// <plugin.hpp> would resolve to ours before rack is declared.
#include "plugin.hpp"

#include <atomic>
#include <memory>

// One Retours engine: the Beads delay or the v2.1 (classic) one. The
// Retours module owns the params, ports, saved settings and the mode flag;
// the active engine reads them through the module and keeps only runtime
// state, so it can be destroyed and rebuilt on a mode switch.
//
// Only the audio thread touches an engine (and Rack's locked calls: reset,
// sample rate). The UI thread works through the module: clear and
// clear-tempo requests and the menu's frozen/clear state are atomics there,
// and each mode's menu items are built from module state alone.
struct Retours;

struct RetoursEngine {
	virtual ~RetoursEngine() = default;
	virtual bool ok() const = 0;                         // buffers allocated
	virtual void process(const rack::Module::ProcessArgs& args) = 0;
	virtual void onSampleRateChange(float sample_rate) = 0;
	virtual void onReset() = 0;                          // engine-side reset
	virtual bool clearDisabled() const = 0;              // buffer empty: greys Clear buffer
	virtual bool clearTempoDisabled() const = 0;         // no saved tempo: greys Clear saved tempo
	virtual bool frozen() const = 0;                     // last process() slice state
};

// Both factories size the engine for Retours::sample_rate_.
std::unique_ptr<RetoursEngine> MakeRetoursBeads(Retours& m);
std::unique_ptr<RetoursEngine> MakeRetoursClassic(Retours& m);

// Each mode's engine-only menu items, built from module state only (never
// from the engine, which the audio thread may be replacing).
void AppendRetoursBeadsMenu(Retours* m, rack::ui::Menu* menu);
void AppendRetoursClassicMenu(Retours* m, rack::ui::Menu* menu);

// How many engines each factory has built, process-wide (headless tests).
extern std::atomic<int> g_retours_beads_builds;
extern std::atomic<int> g_retours_classic_builds;

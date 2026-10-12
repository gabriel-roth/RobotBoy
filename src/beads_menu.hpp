// Shared pieces of the Particules and Retours wrappers around the Beads
// engine: menu undo, and the input-gain menu (Beads' auto gain, or a fixed
// manual gain) with its live input-level readout.
//
// The gain menu expects the module to have: bool auto_gain_,
// float manual_gain_db_, std::atomic<bool> recalibrate_requested_,
// float input_peak_ (+-5 V = 1.0) and robotboy_beads::EngineHost host_.
// A module without host_ overloads BeadsMenuGainDb(Module*) instead.

#pragma once

#include "plugin.hpp"
#include "particules/particules_gain_display.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

// Beads' manual gain byte for a gain in dB: the firmware maps byte g to
// 1 + 39 * (g / 255)^4 (gain_control.cc); 0 means auto gain.
inline uint8_t BeadsManualGainByte(float db) {
	const float gain = std::pow(10.f, clamp(db, 0.f, 32.f) / 20.f);
	const float g = 255.f * std::pow(std::max(gain - 1.f, 0.f) / 39.f, 0.25f);
	return (uint8_t)clamp((int)std::lround(g), 1, 255);
}

// Wrap a context-menu mutation in a whole-module undo snapshot. Because the
// snapshot is the module's full JSON (params + data), one helper covers
// every menu field with no per-item code. VCV-only: MetaModule has no undo
// stack, so there it just runs the mutation.
#ifndef METAMODULE
template <typename F>
static void withMenuUndo(Module* module, const char* label, F&& mutate) {
	json_t* oldJ = APP->engine->moduleToJson(module);
	mutate();
	history::ModuleChange* h = new history::ModuleChange;
	h->name = label;
	h->moduleId = module->id;
	h->oldModuleJ = oldJ;
	h->newModuleJ = APP->engine->moduleToJson(module);
	APP->history->push(h);
}
#else
template <typename F>
static void withMenuUndo(Module*, const char*, F&& mutate) {
	mutate();
}
#endif

// The gain auto gain applies, for the Gain item's readout.
template <class M>
inline float BeadsMenuGainDb(M* module) {
	return module->host_.gain_db();
}

template <class M>
struct BeadsManualGainQuantity : Quantity {
	M* module;

	BeadsManualGainQuantity(M* m) : module(m) {}

	void setValue(float value) override {
		if (module) {
			module->manual_gain_db_ = clamp(value, getMinValue(), getMaxValue());
		}
	}

	float getValue() override {
		return module ? module->manual_gain_db_ : getDefaultValue();
	}

	float getMinValue() override { return 0.0f; }
	float getMaxValue() override { return 32.0f; }
	float getDefaultValue() override { return 0.0f; }
	std::string getLabel() override { return "Manual gain"; }
	std::string getUnit() override { return " dB"; }
	std::string getDisplayValueString() override {
		return string::f("%.1f", getValue());
	}
};

#ifndef METAMODULE
struct BeadsManualGainSlider : ui::Slider {
	BeadsManualGainSlider(Quantity* q) {
		quantity = q;
		box.size.x = 200.0f;
	}
	~BeadsManualGainSlider() {
		delete quantity;
	}
};
#endif

template <class M>
inline void appendBeadsGainMenu(Menu* menu, M* module) {
	// --- Auto gain: a checkable toggle ---
	// Turning it on (again) restarts Beads' level learning; turning it off
	// switches to the manual gain below.
	menu->addChild(createBoolMenuItem("Auto gain", "",
		[=]() { return module->auto_gain_; },
		[=](bool on) {
			withMenuUndo(module, on ? "enable auto gain" : "disable auto gain",
				[=]() { module->auto_gain_ = on; });
			if (on) module->recalibrate_requested_.store(true);
		}
	));

	// --- Gain: shows the auto gain, or sets the manual gain ---
	// Disabled while auto gain is on (its right-hand text then shows the
	// gain auto is applying); with auto gain off it opens the manual
	// setting. MetaModule menus don't animate, so the state is also set at
	// menu-open time, not only in step().
	struct GainItem : MenuItem {
		M* module;

		void Refresh() {
			disabled = module->auto_gain_;
			rightText = module->auto_gain_
				? string::f("%.1f dB", BeadsMenuGainDb(module))
				: string::f("%.1f dB ▸", module->manual_gain_db_);
		}

		Menu* createChildMenu() override {
			if (module->auto_gain_) return nullptr;
			Menu* m = new Menu;
#ifdef METAMODULE
			for (int db = 0; db <= 32; db++) {
				m->addChild(createCheckMenuItem(
					string::f("%d dB", db), "",
					[this, db]() { return (int)std::round(module->manual_gain_db_) == db; },
					[this, db]() { module->manual_gain_db_ = (float)db; }
				));
			}
#else
			m->addChild(new BeadsManualGainSlider(new BeadsManualGainQuantity<M>(module)));
#endif
			return m;
		}

		void step() override {
			Refresh();
			MenuItem::step();
		}
	};
	{
		auto* item = new GainItem;
		item->text = "Gain";
		item->module = module;
		item->Refresh();
		menu->addChild(item);
	}

	// --- Input level readout (display-only) ---
	// On VCV, step() refreshes the label live while the menu is open.
	// On MetaModule menus don't animate, so the label is computed once
	// at menu-open time — acceptable.
	struct InputLevelItem : MenuItem {
		M* module;
#ifndef METAMODULE
		void step() override {
			rightText = FormatInputLevelDb(module->input_peak_);
			MenuItem::step();
		}
#endif
	};
	{
		auto* item = new InputLevelItem;
		item->module = module;
		item->text = "Input";
		item->rightText = FormatInputLevelDb(module->input_peak_);
		item->disabled = true;
		menu->addChild(item);
	}
}

// Scorched cassette's synthetic tape hiss (expects bool tape_hiss_).
template <class M>
inline void appendBeadsHissMenu(Menu* menu, M* module) {
	menu->addChild(createBoolMenuItem("Tape hiss (Scorched cassette)", "",
		[=]() { return module->tape_hiss_; },
		[=](bool on) {
			withMenuUndo(module, on ? "enable tape hiss" : "disable tape hiss",
				[=]() { module->tape_hiss_ = on; });
		}
	));
}

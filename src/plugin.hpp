#pragma once
#include <rack.hpp>
#include <cmath>

using namespace rack;

extern Plugin* pluginInstance;

extern Model* modelLoooop;
extern Model* modelLop;
extern Model* modelMF20Filter;
extern Model* modelOnbetap;
extern Model* modelParticules;
extern Model* modelOndes;
extern Model* modelVespid;
extern Model* modelRetours;
extern Model* modelLucarne;
extern Model* modelSkylark;
extern Model* modelWeaver;
extern Model* modelLyrebird;

// A module whose menu-undo snapshots should leave something out (content that
// a settings change mustn't bring back) edits them here.
struct MenuUndoFilter {
	virtual ~MenuUndoFilter() = default;
	virtual void filterMenuUndoJson(json_t* moduleJ) = 0;
};

// Wraps a context-menu change in a whole-module undo step: the snapshot is the
// module's full JSON (params and data), so one helper covers every menu
// setting. MetaModule has no undo, so there it just makes the change.
template <typename F>
void withMenuUndo(Module* module, const char* name, F&& change) {
#ifdef METAMODULE
	change();
#else
	MenuUndoFilter* filter = dynamic_cast<MenuUndoFilter*>(module);
	json_t* oldModuleJ = APP->engine->moduleToJson(module);
	if (filter)
		filter->filterMenuUndoJson(oldModuleJ);
	change();
	history::ModuleChange* h = new history::ModuleChange;
	h->name = name;
	h->moduleId = module->id;
	h->oldModuleJ = oldModuleJ;
	h->newModuleJ = APP->engine->moduleToJson(module);
	if (filter)
		filter->filterMenuUndoJson(h->newModuleJ);
	APP->history->push(h);
#endif
}

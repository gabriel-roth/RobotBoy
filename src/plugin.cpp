#include "plugin.hpp"
#include "Lucarne.hpp"
#include "lucarne/ColorScheme.hpp"

#ifdef METAMODULE_BUILTIN
// Combined MetaModule build (both the .mmplugin build and the simulator
// built-in build enter through init_RobotBoy). Loooop/Löp are provided by native
// SmartCore cores; MF-20/Onbetap/Vespid/Particules/Ondes/Retours/Lucarne by
// the VCV adapter. This one function registers all nine so the same code path
// works whether it is called by metamodule/register.cc's init() (.mmplugin)
// or by the simulator's generated init_RobotBoy dispatch (built-in). The VCV
// Model* modelLoooop/modelLop are not compiled into the MM build, so they are
// not referenced here.
namespace MetaModule {
	void register_loooop_modules();
	void register_lop_modules();
}
extern Plugin* pluginInstance;
void init_RobotBoy(Plugin* p) {
	pluginInstance = p;
	// Native cores register directly into the MetaModule registry.
	MetaModule::register_loooop_modules();
	MetaModule::register_lop_modules();
	// Adapter modules: p->addModel(...) calls register_module internally.
	p->addModel(modelMF20Filter);
	p->addModel(modelOnbetap);
	p->addModel(modelParticules);
	p->addModel(modelOndes);
	p->addModel(modelVespid);
	p->addModel(modelRetours);
	p->addModel(modelLucarne);
}
#else
// VCV Rack build: standard single init() registering all twelve modules
// (Skylark, Weaver and Lyrebird are VCV-only until their MetaModule port).
Plugin* pluginInstance;
void init(Plugin* p) {
	pluginInstance = p;
	p->addModel(modelLoooop);
	p->addModel(modelLop);
	p->addModel(modelMF20Filter);
	p->addModel(modelOnbetap);
	p->addModel(modelParticules);
	p->addModel(modelOndes);
	p->addModel(modelVespid);
	p->addModel(modelRetours);
	p->addModel(modelLucarne);
	p->addModel(modelSkylark);
	p->addModel(modelWeaver);
	p->addModel(modelLyrebird);
}

// Rack's settings.json carries the Lucarne "Make default" schemes.
extern "C" json_t* settingsToJson() {
	json_t* rootJ = json_object();
	json_object_set_new(rootJ, "lucarneParticulesColorScheme",
		json_string(lucarne::scheme(lucarneDefaultScheme[kSlotParticules]).id));
	json_object_set_new(rootJ, "lucarneRetoursColorScheme",
		json_string(lucarne::scheme(lucarneDefaultScheme[kSlotRetours]).id));
	return rootJ;
}

extern "C" void settingsFromJson(json_t* rootJ) {
	lucarneDefaultScheme[kSlotParticules] = lucarne::schemeFromPatch(
		json_string_value(json_object_get(rootJ, "lucarneParticulesColorScheme")));
	lucarneDefaultScheme[kSlotRetours] = lucarne::schemeFromPatch(
		json_string_value(json_object_get(rootJ, "lucarneRetoursColorScheme")));
}
#endif

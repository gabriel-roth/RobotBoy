#include "plugin.hpp"

#ifdef METAMODULE_BUILTIN
// Combined MetaModule build (both the .mmplugin build and the simulator
// built-in build enter through init_RobotBoy). Loooop/Löp are provided by native
// SmartCore cores; MF-20/Particules/Ondes by the VCV adapter. This one function
// registers all five so the same code path works whether it is called by
// metamodule/register.cc's init() (.mmplugin) or by the simulator's generated
// init_RobotBoy dispatch (built-in). The VCV Model* modelLoooop/modelLop are not
// compiled into the MM build, so they are not referenced here.
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
	p->addModel(modelParticules);
	p->addModel(modelOndes);
}
#else
// VCV Rack build: standard single init() registering all five modules.
Plugin* pluginInstance;
void init(Plugin* p) {
	pluginInstance = p;
	p->addModel(modelLoooop);
	p->addModel(modelLop);
	p->addModel(modelMF20Filter);
	p->addModel(modelParticules);
	p->addModel(modelOndes);
	p->addModel(modelYellowjacket);
}
#endif

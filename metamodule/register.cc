#include <rack.hpp>

// Combined registration entry defined in src/plugin.cpp under METAMODULE_BUILTIN.
// Registers all eight modules (native Loooop/Löp cores + the MF-20, Onbetap,
// Particules, Ondes, Vespid, and Retours VCV adapters).
void init_RobotBoy(rack::Plugin* p);

#ifndef SIMULATOR
// .mmplugin build: we own the single plugin instance and the init() entry point.
rack::Plugin* pluginInstance = nullptr;

// The MetaModule SDK declares init() as extern "C" in
// rack-interface/include/plugin/callbacks.hpp, so defining it here (matching
// that declaration) exports the unmangled `init` symbol required by
// -Wl,--require-defined=init. The firmware invokes it with a Plugin already
// populated from plugin.json (brand slug "RobotBoy").
//
// In the simulator built-in build (SIMULATOR defined) the simulator provides
// pluginInstance and generates a call to init_RobotBoy(Plugin*) directly; a bare
// `init` or a second pluginInstance definition here would collide with the
// other built-in plugins, so both are omitted in that build.
void init(rack::plugin::Plugin* p) {
	init_RobotBoy(p);
}
#endif

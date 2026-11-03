// Weaver's preset file names: which files are presets, their order, SAVE's
// next name and the factory presets' names.
#include "shapes/WeaverPresetFiles.hpp"
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

using namespace shapes;

static void presetFiles() {
	assert(isPresetFileName("Preset 001.json"));
	assert(isPresetFileName("x.JSON"));
	assert(!isPresetFileName(".json"));
	assert(!isPresetFileName(".weaver-factory-presets"));
	assert(!isPresetFileName("._Preset 001.json"));  // macOS's resource fork files
	assert(!isPresetFileName("LYREBIRD-001.wav"));
	std::vector<std::string> s = sortedPresetNames({"b.json", "LYREBIRD-001.wav", "Preset 002.json", "01 Sine.json",
	                                                "A.json", kFactoryMarker, "preset 001.json"});
	assert((s == std::vector<std::string>{"01 Sine.json", "A.json", "b.json", "preset 001.json", "Preset 002.json"}));
}

static void nextNames() {
	assert(nextPresetName({}) == "Preset 001.json");
	assert(nextPresetName({"01 Sine.json", "Preset 007.json", "preset 3.JSON", "Preset x.json"}) == "Preset 008.json");
	assert(presetNumber("Preset 012.json") == 12);
	assert(presetNumber("Preset .json") == -1);
	assert(presetNumber("Presets 1.json") == -1);
	assert(presetNumber("Preset 1.wav") == -1);
}

static void factoryNames() {
	assert(factoryPresetName(0) == "01 Envelope + triangle x16");
	assert(factoryPresetName(kNumWeaverPresets - 1) == "15 Random swell");
	// Factory names sort ahead of SAVE's, in factory order
	std::vector<std::string> names = {"Preset 001.json"};
	for (int n = kNumWeaverPresets - 1; n >= 0; n--)
		names.push_back(factoryPresetName(n) + ".json");
	std::vector<std::string> s = sortedPresetNames(names);
	for (int n = 0; n < kNumWeaverPresets; n++)
		assert(s[n] == factoryPresetName(n) + ".json");
	assert(s.back() == "Preset 001.json");
}

static void roots() {
	assert(isVolumeRoot("sdc:/"));
	assert(isVolumeRoot("usb:"));
	assert(!isVolumeRoot("sdc:/RobotBoy"));
	assert(!isVolumeRoot("/Users/me/Rack2/RobotBoy/Weaver"));
	assert(!isVolumeRoot(""));
}

static void factoryMarker() {
	// Version 1 markers are one line with no version and no hashes
	FactoryMarker v1 = parseFactoryMarker("Weaver's factory presets were written to this folder.\n");
	assert(v1.version == 1 && v1.files.empty());
	assert(shouldWriteFactoryFile(v1, "01 Sine.json", true, 123));
	assert(!shouldWriteFactoryFile(v1, "01 Sine.json", false, 0));

	FactoryMarker m;
	m.version = 2;
	m.files = {{"01 Envelope + triangle x16.json", textHash("as written")}, {"02 Deleted.json", 0x0000abcd},
	           {"03 Edited.json", 0}};
	FactoryMarker r = parseFactoryMarker(formatFactoryMarker(m));
	assert(r.version == 2 && r.files == m.files);
	assert(shouldWriteFactoryFile(r, "01 Envelope + triangle x16.json", true, textHash("as written")));
	assert(!shouldWriteFactoryFile(r, "01 Envelope + triangle x16.json", true, textHash("edited")));
	assert(!shouldWriteFactoryFile(r, "02 Deleted.json", false, 0));
	assert(!shouldWriteFactoryFile(r, "03 Edited.json", true, 0));
	assert(shouldWriteFactoryFile(r, "16 New preset.json", false, 0));
	assert(!shouldWriteFactoryFile(r, "16 New preset.json", true, 1));
	assert(textHash("a") != textHash("b"));
}

int main() {
	presetFiles();
	nextNames();
	factoryNames();
	roots();
	factoryMarker();
	printf("weaver preset file tests passed\n");
	return 0;
}

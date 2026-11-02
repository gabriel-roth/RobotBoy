#pragma once
#include "LyrebirdPaths.hpp"
#include "WeaverPresets.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

// Weaver's preset files: where they live and how they're named.
namespace shapes {

// Where MetaModule looks for presets, in order: a Weaver or RobotBoy folder
// on the card, then on a USB drive, then either root
inline const std::vector<std::string> kWeaverMetaModuleFolders = {
	"sdc:/RobotBoy/Weaver", "sdc:/Weaver", "sdc:/RobotBoy",
	"usb:/RobotBoy/Weaver", "usb:/Weaver", "usb:/RobotBoy",
	"sdc:/", "usb:/",
};

// Written beside the factory presets once they're in a folder, so deleting
// them all doesn't bring them back
inline const char* const kFactoryMarker = ".weaver-factory-presets";

// "sdc:/" or "sdc:": the factory presets are never written to a volume root
inline bool isVolumeRoot(const std::string& p) {
	return isVolumePath(p) && (p.size() == 4 || (p.size() == 5 && p[4] == '/'));
}

inline std::string lowerCase(std::string s) {
	for (char& c : s)
		c = (char) std::tolower((unsigned char) c);
	return s;
}

inline bool isPresetFileName(const std::string& s) {
	return s.size() > 5 && s[0] != '.' && lowerCase(s.substr(s.size() - 5)) == ".json";
}

// The preset files among names, by name, ignoring case
inline std::vector<std::string> sortedPresetNames(std::vector<std::string> names) {
	names.erase(std::remove_if(names.begin(), names.end(), [](const std::string& s) { return !isPresetFileName(s); }),
	            names.end());
	std::sort(names.begin(), names.end(), [](const std::string& a, const std::string& b) {
		std::string la = lowerCase(a), lb = lowerCase(b);
		return la != lb ? la < lb : a < b;
	});
	return names;
}

// "01 Envelope + triangle x16": a factory preset's name, as file stem and on screen
inline std::string factoryPresetName(int n) {
	char buf[64];
	snprintf(buf, sizeof buf, "%02d %s", n + 1, kWeaverPresets[n].name);
	return buf;
}

// N from "Preset N.json", or -1
inline int presetNumber(const std::string& name) {
	std::string s = lowerCase(name);
	const std::string pre = "preset ";
	if (s.size() < pre.size() + 6 || s.compare(0, pre.size(), pre) != 0 || !isPresetFileName(s))
		return -1;
	std::string digits = s.substr(pre.size(), s.size() - pre.size() - 5);
	if (digits.empty() || digits.size() > 6)
		return -1;
	for (char c : digits)
		if (!std::isdigit((unsigned char) c))
			return -1;
	return atoi(digits.c_str());
}

// SAVE's file name: one past the highest "Preset N" in the folder
inline std::string nextPresetName(const std::vector<std::string>& names) {
	int highest = 0;
	for (const std::string& s : names)
		highest = std::max(highest, presetNumber(s));
	char buf[32];
	snprintf(buf, sizeof buf, "Preset %03d.json", highest + 1);
	return buf;
}

}  // namespace shapes

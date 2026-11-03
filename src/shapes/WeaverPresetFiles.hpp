#pragma once
#include "LyrebirdPaths.hpp"
#include "WeaverPresets.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
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

// Bump when a factory preset changes: a folder written by an older version
// gets the new files, except ones the user has edited or deleted
inline constexpr int kFactoryPresetsVersion = 2;

// FNV-1a, to tell whether a factory file is still as Weaver wrote it
inline uint32_t textHash(const std::string& s) {
	uint32_t h = 2166136261u;
	for (char c : s)
		h = (h ^ (uint8_t) c) * 16777619u;
	return h;
}

// The marker's contents: the version that wrote the factory files and each
// file's hash as written. 0 is never a match, so that file is left alone.
struct FactoryMarker {
	int version = 1;  // version 1 markers have no version line and no hashes
	std::vector<std::pair<std::string, uint32_t>> files;
};

inline std::string formatFactoryMarker(const FactoryMarker& m) {
	std::string s = "Weaver's factory presets were written to this folder.\nversion " + std::to_string(m.version) + "\n";
	char hex[16];
	for (const auto& f : m.files) {
		snprintf(hex, sizeof hex, "%08x ", (unsigned) f.second);
		s += hex + f.first + "\n";
	}
	return s;
}

inline FactoryMarker parseFactoryMarker(const std::string& text) {
	FactoryMarker m;
	size_t start = 0;
	while (start < text.size()) {
		size_t end = text.find('\n', start);
		if (end == std::string::npos)
			end = text.size();
		std::string line = text.substr(start, end - start);
		start = end + 1;
		if (line.compare(0, 8, "version ") == 0) {
			m.version = atoi(line.c_str() + 8);
		} else if (line.size() > 9 && line[8] == ' ' && std::all_of(line.begin(), line.begin() + 8, [](char c) {
			           return std::isxdigit((unsigned char) c);
		           })) {
			m.files.emplace_back(line.substr(9), (uint32_t) strtoul(line.substr(0, 8).c_str(), nullptr, 16));
		}
	}
	return m;
}

// Whether to write a factory preset's file into a folder whose marker is
// older: a file still as written is replaced, a missing one only if the
// marker doesn't list it (a new factory preset), an edited one never. Version 1
// came before release and has no hashes; its files are taken as unedited.
inline bool shouldWriteFactoryFile(const FactoryMarker& m, const std::string& file, bool exists, uint32_t hashNow) {
	if (m.version <= 1)
		return exists;
	for (const auto& f : m.files)
		if (f.first == file)
			return exists && f.second != 0 && f.second == hashNow;
	return !exists;
}

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

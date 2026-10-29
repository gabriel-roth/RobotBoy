#pragma once
#include <functional>
#include <string>
#include <vector>

namespace shapes {

// A MetaModule volume path: three letters, then ":/" ("sdc:/", "usb:/")
inline bool isVolumePath(const std::string& p) {
	if (p.size() < 5)
		return false;
	for (int i = 0; i < 3; i++) {
		char c = p[i];
		if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')))
			return false;
	}
	return p[3] == ':' && p[4] == '/';
}

// A saved folder if this platform can use it, else "" for the default folder.
// Each platform ignores the other's paths.
inline std::string usableFolder(const std::string& saved, bool onMetaModule) {
	if (onMetaModule)
		return isVolumePath(saved) ? saved : "";
	return !saved.empty() && !isVolumePath(saved) ? saved : "";
}

// The first candidate that is a directory, or "" when none is
inline std::string pickFolder(const std::vector<std::string>& candidates,
                              const std::function<bool(const std::string&)>& isDirectory) {
	for (const std::string& c : candidates)
		if (isDirectory(c))
			return c;
	return "";
}

}  // namespace shapes

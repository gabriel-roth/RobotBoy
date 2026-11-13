#pragma once
#include "plugin.hpp"
#include "shapes/LyrebirdPaths.hpp"

#ifdef METAMODULE
#include "filesystem/dirent.h"
#endif

#include <atomic>
#include <string>
#include <vector>

// Folder access that works on both platforms, for the modules that keep
// files (Lyrebird's takes, Weaver's presets).

#ifdef METAMODULE
// MetaModule's opendir() drops a path's volume ("sdc:/x" opens "x" on
// whichever drive FatFS used last), so a card that isn't there can look as
// if it is. Repeating the volume gets it through to FatFS. A firmware that
// keeps the volume would reject the repeated form, so that form is used only
// once a repeated volume root has opened.
inline std::atomic<bool> volumeRepeatWorks{false};

inline DIR* openFolder(const std::string& path) {
	if (!volumeRepeatWorks.load()) {
		for (const char* root : {"sdc:/", "usb:/"}) {
			if (DIR* d = opendir(shapes::withVolumeRepeated(root).c_str())) {
				closedir(d);
				volumeRepeatWorks.store(true);
				break;
			}
		}
	}
	return opendir((volumeRepeatWorks.load() ? shapes::withVolumeRepeated(path) : path).c_str());
}
#endif

// Whether a folder exists. MetaModule's stat() calls everything a file (and
// fails on volume roots), so system::isDirectory is always false there:
// open it as a folder instead.
inline bool isFolder(const std::string& path) {
#ifdef METAMODULE
	DIR* d = openFolder(path);
	if (d)
		closedir(d);
	return d != nullptr;
#else
	return system::isDirectory(path);
#endif
}

// The file names in dir, or none when it can't be read
inline std::vector<std::string> fileNames(const std::string& dir) {
	std::vector<std::string> names;
	try {
#ifdef METAMODULE
		// Listed through openFolder, which reaches the right drive
		if (DIR* d = openFolder(dir)) {
			while (dirent* e = readdir(d)) {
				if (!e->d_name[0])
					break;
				names.push_back(e->d_name);
			}
			closedir(d);
		}
#else
		for (const std::string& path : system::getEntries(dir))
			names.push_back(system::getFilename(path));
#endif
	}
	catch (std::exception&) {
	}
	return names;
}

// Makes dir if need be; whether it's there. MetaModule firmware before
// 2.4.2 can't make folders: createDirectories returns false there.
inline bool ensureDirectory(const std::string& dir) {
	try {
		if (!isFolder(dir))
			system::createDirectories(dir);
		return isFolder(dir);
	}
	catch (std::exception&) {
		return false;
	}
}

#ifdef METAMODULE
// A module's folder ("RobotBoy/Weaver") on the card, or on a USB drive when
// there's no card; "" with neither
inline std::string driveFolder(const std::string& folder) {
	for (const char* root : {"sdc:/", "usb:/"})
		if (isFolder(root))
			return root + folder;
	return "";
}
#endif

// Where a Save as dialog opens: dir, made if need be. A MetaModule that
// can't make folders opens at the drive's root instead.
inline std::string dialogFolder(const std::string& dir) {
	if (dir.empty() || ensureDirectory(dir))
		return dir;
	return shapes::isVolumePath(dir) ? dir.substr(0, 5) : dir;
}

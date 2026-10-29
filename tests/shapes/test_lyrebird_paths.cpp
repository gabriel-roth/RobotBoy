// Lyrebird's folder rules: volume paths, which saved folder a platform keeps,
// and MetaModule's default folder.
#include "shapes/LyrebirdPaths.hpp"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

using namespace shapes;

static void volumePaths() {
	assert(isVolumePath("sdc:/RobotBoy/Lyrebird"));
	assert(isVolumePath("usb:/x.wav"));
	assert(isVolumePath("sdc:/"));
	// A volume root as getDirectory gives it ("sdc:/x.wav" -> "sdc:")
	assert(isVolumePath("sdc:"));
	assert(isVolumePath("USB:"));
	assert(!isVolumePath("sd:"));
	assert(!isVolumePath("sdcx"));
	assert(!isVolumePath("sdc:x"));
	assert(!isVolumePath("/Users/me/Documents/Rack2/RobotBoy/Lyrebird"));
	assert(!isVolumePath("C:/Users/me"));  // Windows drive: one letter
	assert(!isVolumePath("C:\\Users\\me"));
	assert(!isVolumePath("s1c:/x"));
	assert(!isVolumePath(""));
}

static void usableFolders() {
	assert(usableFolder("/Users/me/takes", true) == "");
	assert(usableFolder("sdc:/takes", true) == "sdc:/takes");
	assert(usableFolder("sdc:/takes", false) == "");
	assert(usableFolder("/Users/me/takes", false) == "/Users/me/takes");
	assert(usableFolder("", false) == "" && usableFolder("", true) == "");
	// A file at a volume root: MetaModule keeps the root, VCV ignores it
	assert(usableFolder("sdc:", true) == "sdc:/");
	assert(usableFolder("usb:", true) == "usb:/");
	assert(usableFolder("sdc:", false) == "");
	assert(usableFolder("sdc:/", true) == "sdc:/");
}

static void repeatedVolumes() {
	assert(withVolumeRepeated("sdc:/RobotBoy/Lyrebird") == "sdc:/sdc:/RobotBoy/Lyrebird");
	assert(withVolumeRepeated("usb:/") == "usb:/usb:/");
	assert(withVolumeRepeated("sdc:") == "sdc:/sdc:/");
	assert(withVolumeRepeated("/Users/me/takes") == "/Users/me/takes");
	assert(withVolumeRepeated("") == "");
}

static void pickedFolders() {
	auto only = [](std::string d) { return [d](const std::string& p) { return p == d; }; };
	std::vector<std::string> c = {"sdc:/RobotBoy/Lyrebird", "sdc:/", "usb:/"};
	assert(pickFolder(c, only("sdc:/RobotBoy/Lyrebird")) == "sdc:/RobotBoy/Lyrebird");
	assert(pickFolder(c, only("sdc:/")) == "sdc:/");
	assert(pickFolder(c, only("usb:/")) == "usb:/");
	assert(pickFolder(c, only("nor:/")) == "");
	// The first that exists wins
	assert(pickFolder(c, [](const std::string&) { return true; }) == "sdc:/RobotBoy/Lyrebird");
	assert(pickFolder({}, [](const std::string&) { return true; }) == "");

	// MetaModule's search order: named folders on the card, then on USB,
	// then the card root, then the USB root
	const std::vector<std::string>& m = kMetaModuleFolders;
	assert(pickFolder(m, only("sdc:/Lyrebird")) == "sdc:/Lyrebird");
	assert(pickFolder(m, only("usb:/RobotBoy")) == "usb:/RobotBoy");
	auto any = [](std::vector<std::string> ds) {
		return [ds](const std::string& p) { return std::find(ds.begin(), ds.end(), p) != ds.end(); };
	};
	assert(pickFolder(m, any({"sdc:/RobotBoy", "sdc:/Lyrebird"})) == "sdc:/Lyrebird");
	assert(pickFolder(m, any({"usb:/Lyrebird", "sdc:/RobotBoy"})) == "sdc:/RobotBoy");
	assert(pickFolder(m, any({"sdc:/", "usb:/RobotBoy/Lyrebird"})) == "usb:/RobotBoy/Lyrebird");
	assert(pickFolder(m, any({"usb:/", "sdc:/"})) == "sdc:/");
	assert(pickFolder(m, only("usb:/")) == "usb:/");
	assert(m.size() == 8 && m.front() == "sdc:/RobotBoy/Lyrebird" && m.back() == "usb:/");
}

static void sharedPaths() {
	SharedPath<16> p;
	assert(p.get() == "");
	assert(p.set("sdc:/takes"));
	assert(p.get() == "sdc:/takes");
	assert(p.set("usb:/"));
	assert(p.get() == "usb:/");
	// Too long to hold: the default folder, rather than a cut-off path
	assert(p.set("sdc:/a/very/long/folder/name"));
	assert(p.get() == "");
	assert(p.set(std::string(16, 'x')) && p.get() == std::string(16, 'x'));
}

// A reader never sees a mix of two paths, only one of them or ""
static void sharedPathConcurrent() {
	SharedPath<255> p;
	const std::string a = "sdc:/" + std::string(200, 'a'), b = "usb:/" + std::string(100, 'b');
	p.set(a);
	std::thread writer([&]() {
		for (int i = 0; i < 200000; i++)
			p.set(i % 2 ? a : b);
	});
	for (int i = 0; i < 200000; i++) {
		std::string s = p.get();
		assert(s == a || s == b || s == "");
	}
	writer.join();
}

int main() {
	volumePaths();
	usableFolders();
	repeatedVolumes();
	pickedFolders();
	sharedPaths();
	sharedPathConcurrent();
	printf("lyrebird path tests passed\n");
	return 0;
}

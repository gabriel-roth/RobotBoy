// Lyrebird's folder rules: volume paths, which saved folder a platform keeps,
// and MetaModule's default folder.
#include "shapes/LyrebirdPaths.hpp"
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
	assert(!isVolumePath("sdc:"));
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
	pickedFolders();
	sharedPaths();
	sharedPathConcurrent();
	printf("lyrebird path tests passed\n");
	return 0;
}

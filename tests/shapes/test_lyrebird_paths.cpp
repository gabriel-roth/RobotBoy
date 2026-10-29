// Lyrebird's folder rules: volume paths, which saved folder a platform keeps,
// and MetaModule's default folder.
#include "shapes/LyrebirdPaths.hpp"
#include <cassert>
#include <cstdio>
#include <string>
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

int main() {
	volumePaths();
	usableFolders();
	pickedFolders();
	printf("lyrebird path tests passed\n");
	return 0;
}

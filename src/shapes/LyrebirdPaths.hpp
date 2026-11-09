#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace shapes {

// A MetaModule volume path: three letters and ":", then "/..." ("sdc:/",
// "usb:/x.wav") or nothing ("sdc:", the folder of a file at a volume root)
inline bool isVolumePath(const std::string& p) {
	if (p.size() < 4)
		return false;
	for (int i = 0; i < 3; i++) {
		char c = p[i];
		if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')))
			return false;
	}
	return p[3] == ':' && (p.size() == 4 || p[4] == '/');
}

// A saved folder if this platform can use it, else "" for the default folder.
// Each platform ignores the other's paths. A bare volume ("sdc:") becomes
// its root ("sdc:/").
inline std::string usableFolder(const std::string& saved, bool onMetaModule) {
	if (onMetaModule) {
		if (!isVolumePath(saved))
			return "";
		return saved.size() == 4 ? saved + "/" : saved;
	}
	return !saved.empty() && !isVolumePath(saved) ? saved : "";
}

// "sdc:/" or "sdc:"
inline bool isVolumeRoot(const std::string& p) {
	return isVolumePath(p) && (p.size() == 4 || (p.size() == 5 && p[4] == '/'));
}

// A volume path with its volume repeated ("sdc:/x" -> "sdc:/sdc:/x"; a bare
// "sdc:" -> "sdc:/sdc:/"); anything else unchanged. MetaModule's opendir()
// strips the first volume and hands the rest to FatFS, which then reads the
// second as the drive instead of using whichever drive it used last.
inline std::string withVolumeRepeated(const std::string& p) {
	if (!isVolumePath(p))
		return p;
	std::string vol = p.substr(0, 4) + "/";
	return vol + vol + (p.size() > 5 ? p.substr(5) : "");
}

// Where MetaModule looks for takes, in order: a Lyrebird or RobotBoy folder
// on the card, then on a USB drive, then either root
inline const std::vector<std::string> kMetaModuleFolders = {
	"sdc:/RobotBoy/Lyrebird", "sdc:/Lyrebird", "sdc:/RobotBoy",
	"usb:/RobotBoy/Lyrebird", "usb:/Lyrebird", "usb:/RobotBoy",
	"sdc:/", "usb:/",
};

// The folder SAVE makes on MetaModule, on the card or else USB, when
// there's only a drive's root to save to
inline const std::vector<std::string> kLyrebirdOwnFolders = {"sdc:/RobotBoy/Lyrebird", "usb:/RobotBoy/Lyrebird"};

// The first candidate that is a directory, or "" when none is
inline std::string pickFolder(const std::vector<std::string>& candidates,
                              const std::function<bool(const std::string&)>& isDirectory) {
	for (const std::string& c : candidates)
		if (isDirectory(c))
			return c;
	return "";
}

// The module's own folder for SAVE to make on MetaModule: with no folder
// chosen and only a drive's root (or nothing) to save to, the first of
// folders whose drive is there; else "". Firmware 2.4.1 and earlier can't
// make folders, so there it isn't made.
inline std::string folderToMake(bool chosen, const std::string& current, const std::vector<std::string>& folders,
                                const std::function<bool(const std::string&)>& isDirectory) {
	if (chosen || !(current.empty() || isVolumeRoot(current)))
		return "";
	for (const std::string& f : folders)
		if (isVolumePath(f) && isDirectory(f.substr(0, 5)))
			return f;
	return "";
}

// A MetaModule drive as a bit: 1 the card, 2 USB, 0 anything else
inline unsigned driveBit(const std::string& p) {
	if (p.compare(0, 4, "sdc:") == 0)
		return 1;
	if (p.compare(0, 4, "usb:") == 0)
		return 2;
	return 0;
}

// Drives that look present but aren't. MetaModule 2.4.1 keeps a removed card
// or USB drive mounted, so its root still opens though nothing on it can be
// read or written. A failed write adds the drive; reaching it for real
// removes it.
struct StaleDrives {
	void add(const std::string& p) {
		bits_.fetch_or(driveBit(p));
	}
	void remove(const std::string& p) {
		bits_.fetch_and(~driveBit(p));
	}
	bool has(const std::string& p) const {
		return bits_.load() & driveBit(p);
	}

private:
	std::atomic<unsigned> bits_{0};
};

// A path one thread writes while others read it, with no lock to wait on: a
// thread may interrupt another mid-write, so nobody spins. Fixed size, so
// nothing is freed under a reader. A read that keeps meeting a write, or a
// write that keeps meeting another, gives up: the read returns "" (the
// default folder) and the write is dropped, never a mix of two paths.
template <size_t N>
struct SharedPath {
	// False when another write held it throughout. Too long a path is stored as "".
	bool set(const std::string& path) {
		const std::string& s = path.size() <= N ? path : std::string();
		uint32_t seq = 0;
		for (int i = 0;; i++) {
			if (i == kTries)
				return false;
			seq = seq_.load(std::memory_order_relaxed);
			if (!(seq & 1) && seq_.compare_exchange_weak(seq, seq + 1, std::memory_order_acq_rel))
				break;
		}
		std::atomic_thread_fence(std::memory_order_release);
		for (size_t i = 0; i <= N; i++)
			chars_[i].store(i < s.size() ? s[i] : 0, std::memory_order_relaxed);
		seq_.store(seq + 2, std::memory_order_release);
		return true;
	}

	std::string get() const {
		char buf[N + 1];
		for (int i = 0; i < kTries; i++) {
			uint32_t a = seq_.load(std::memory_order_acquire);
			if (a & 1)
				continue;
			for (size_t k = 0; k < N; k++)
				buf[k] = chars_[k].load(std::memory_order_relaxed);
			buf[N] = 0;
			std::atomic_thread_fence(std::memory_order_acquire);
			if (seq_.load(std::memory_order_relaxed) == a)
				return buf;
		}
		return "";
	}

private:
	static constexpr int kTries = 1000;
	std::atomic<char> chars_[N + 1] = {};
	std::atomic<uint32_t> seq_{0};
};

}  // namespace shapes

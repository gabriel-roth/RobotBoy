#pragma once
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <vector>

namespace shapes {

// Takes are stored at a low fixed rate: gestures and CV need little more.
constexpr int kTakeRate = 1000;
constexpr int kMaxTakeSamples = 180000;
constexpr double kLengthBars[7] = {0.25, 0.5, 1, 2, 4, 8, 16};

enum class TakeMode { Sync, Free };

// Versions are unique across every take buffer, not counted per buffer, so two
// different takes never compare equal by version
inline uint32_t newTakeVersion() {
	static std::atomic<uint32_t> counter{0};
	return ++counter;
}

// One recorded gesture, in volts. The buffer is allocated once so the audio
// thread never has to allocate or resize it.
struct Take {
	std::vector<float> samples;
	int length = 0;
	TakeMode mode = TakeMode::Sync;
	double bars = 1.0;
	double seconds = 0.0;  // always set; Sync: bars * 240 / bpm
	double bpm = 120.0;
	char name[64] = "";
	bool modified = false;
	uint32_t version = newTakeVersion();

	Take() : samples(kMaxTakeSamples, 0.f) {}

	bool empty() const {
		return length <= 0;
	}

	// Keeps the buffer; only the bookkeeping resets
	void clear() {
		length = 0;
		mode = TakeMode::Sync;
		bars = 1.0;
		seconds = 0.0;
		bpm = 120.0;
		name[0] = 0;
		modified = false;
		version = newTakeVersion();
	}

	// Both buffers are already full size, so this never allocates
	void copyFrom(const Take& o) {
		if (this == &o)
			return;
		std::copy(o.samples.begin(), o.samples.begin() + std::max(o.length, 0), samples.begin());
		length = o.length;
		mode = o.mode;
		bars = o.bars;
		seconds = o.seconds;
		bpm = o.bpm;
		memcpy(name, o.name, sizeof name);
		modified = o.modified;
		version = o.version;  // a snapshot compares equal to its source
	}

	float at(int i) const {
		return (i >= 0 && i < length) ? samples[i] : 0.f;
	}
};

}  // namespace shapes

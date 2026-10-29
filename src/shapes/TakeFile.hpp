#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "Take.hpp"

namespace shapes {

// Mono float32 WAV at the take rate (value = volts / 10), with a "lyrb"
// chunk that keeps the take's timing so it loads back exactly.
bool writeTakeWav(const std::string& path, const Take& take);

enum class LoadError { None, CantOpen, NotWav, Unsupported, Empty };

struct LoadResult {
	LoadError error = LoadError::None;
	bool truncated = false;
};

// Loads our own files exactly, and other WAVs as Free takes (first channel,
// resampled to the take rate).
LoadResult readTakeWav(const std::string& path, Take& out);

// int16 over +-10 V, for embedding in patch files
std::vector<uint8_t> packSamples16(const Take& take);
// Sets only samples and length; metadata is the caller's
bool unpackSamples16(const uint8_t* data, size_t n, Take& out);

std::string takeMetaJson(const Take& take);
bool parseTakeMeta(const std::string& json, Take& out);

// File browsing helpers
std::vector<std::string> sortedWavNames(std::vector<std::string> names);
int stepName(const std::vector<std::string>& sorted, const std::string& current, int delta);
std::string nextTakeName(const std::vector<std::string>& names);

}  // namespace shapes

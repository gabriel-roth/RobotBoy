// Take storage tests: WAV round trip, foreign WAV import, packing, file names.
#include "shapes/TakeFile.hpp"
#include <cassert>
#include <clocale>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <sys/resource.h>
#include <sys/stat.h>

using namespace shapes;

static const char* kDir = "../build/tests/";

static std::string tmpPath(const char* name) {
	return std::string(kDir) + name;
}

static void writeBytes(const std::string& path, const std::vector<uint8_t>& b) {
	FILE* f = fopen(path.c_str(), "wb");
	assert(f);
	if (!b.empty())
		fwrite(b.data(), 1, b.size(), f);
	fclose(f);
}

static std::vector<uint8_t> readBytes(const std::string& path) {
	std::vector<uint8_t> b;
	FILE* f = fopen(path.c_str(), "rb");
	assert(f);
	uint8_t buf[4096];
	size_t n;
	while ((n = fread(buf, 1, sizeof buf, f)) > 0)
		b.insert(b.end(), buf, buf + n);
	fclose(f);
	return b;
}

static void put32(std::vector<uint8_t>& b, uint32_t v) {
	for (int i = 0; i < 4; i++)
		b.push_back((v >> (8 * i)) & 0xff);
}
static void put16(std::vector<uint8_t>& b, uint16_t v) {
	b.push_back(v & 0xff);
	b.push_back(v >> 8);
}
static void putTag(std::vector<uint8_t>& b, const char* t) {
	b.insert(b.end(), t, t + 4);
}

// Builds a WAV with a fmt chunk and (optionally) a data chunk
static std::vector<uint8_t> buildWav(int format, int channels, int rate, int bits,
                                     const std::vector<uint8_t>& data, bool withData = true,
                                     uint32_t declaredDataSize = 0xffffffffu) {
	std::vector<uint8_t> b;
	putTag(b, "RIFF");
	put32(b, 0);
	putTag(b, "WAVE");
	putTag(b, "fmt ");
	put32(b, 16);
	put16(b, format);
	put16(b, channels);
	put32(b, rate);
	put32(b, rate * channels * bits / 8);
	put16(b, channels * bits / 8);
	put16(b, bits);
	if (withData) {
		putTag(b, "data");
		put32(b, declaredDataSize == 0xffffffffu ? (uint32_t) data.size() : declaredDataSize);
		b.insert(b.end(), data.begin(), data.end());
	}
	uint32_t riff = (uint32_t) b.size() - 8;
	for (int i = 0; i < 4; i++)
		b[4 + i] = (riff >> (8 * i)) & 0xff;
	return b;
}

static void wavRoundTrip() {
	Take t;
	t.mode = TakeMode::Sync;
	t.bars = 4;
	t.bpm = 120;
	t.seconds = 8;
	t.length = 8000;
	for (int i = 0; i < t.length; i++)
		t.samples[i] = -10.f + 20.f * i / (t.length - 1);
	std::string p = tmpPath("lyr_roundtrip.wav");
	assert(writeTakeWav(p, t));
	Take r;
	LoadResult res = readTakeWav(p, r);
	assert(res.error == LoadError::None && !res.truncated);
	assert(r.length == 8000);
	for (int i = 0; i < r.length; i++)
		assert(std::fabs(r.samples[i] - t.samples[i]) < 1e-6f);
	assert(r.mode == TakeMode::Sync);
	assert(r.bars == 4);
	assert(r.bpm == 120);
	assert(std::fabs(r.seconds - 8.0) < 1e-9);
	assert(std::string(r.name) == "lyr_roundtrip");
}

static void freeMetaRoundTrip() {
	Take t;
	t.mode = TakeMode::Free;
	t.seconds = 12.3;
	t.length = 12300;
	for (int i = 0; i < t.length; i++)
		t.samples[i] = 3.f;
	std::string p = tmpPath("lyr_free.wav");
	assert(writeTakeWav(p, t));
	Take r;
	assert(readTakeWav(p, r).error == LoadError::None);
	assert(r.mode == TakeMode::Free);
	assert(std::fabs(r.seconds - 12.3) < 1e-9);
	assert(r.length == 12300);
}

static void wavHeaderIsFloatMono() {
	Take t;
	t.length = 100;
	t.seconds = 0.1;
	t.mode = TakeMode::Free;
	std::string p = tmpPath("lyr_header.wav");
	assert(writeTakeWav(p, t));
	std::vector<uint8_t> b = readBytes(p);
	assert(b.size() > 44);
	assert(b[20] == 3 && b[21] == 0);
	assert(b[22] == 1 && b[23] == 0);
	uint32_t rate = b[24] | (b[25] << 8) | (b[26] << 16) | ((uint32_t) b[27] << 24);
	assert(rate == 1000);
	std::string s(b.begin(), b.end());
	assert(s.find("lyrb") != std::string::npos);
}

static void readsForeignWavs() {
	// 16-bit stereo 48 kHz, 0.5 s, first channel +0.5, second channel -0.25
	{
		std::vector<uint8_t> d;
		for (int i = 0; i < 24000; i++) {
			put16(d, 16384);
			put16(d, (uint16_t) (int16_t) -8192);
		}
		std::string p = tmpPath("lyr_f16.wav");
		writeBytes(p, buildWav(1, 2, 48000, 16, d));
		Take r;
		LoadResult res = readTakeWav(p, r);
		assert(res.error == LoadError::None);
		assert(r.mode == TakeMode::Free);
		assert(std::abs(r.length - 500) <= 1);
		assert(std::fabs(r.seconds - 0.5) < 0.01);
		assert(r.bpm == 120);
		for (int i = 0; i < r.length; i++)
			assert(std::fabs(r.samples[i] - 5.f) < 1e-3f);
	}
	// 24-bit mono 1 kHz
	{
		std::vector<uint8_t> d;
		for (int i = 0; i < 100; i++) {
			int32_t v = i % 2 ? -4194304 : 2097152;  // -0.5, +0.25
			d.push_back(v & 0xff);
			d.push_back((v >> 8) & 0xff);
			d.push_back((v >> 16) & 0xff);
		}
		std::string p = tmpPath("lyr_f24.wav");
		writeBytes(p, buildWav(1, 1, 1000, 24, d));
		Take r;
		assert(readTakeWav(p, r).error == LoadError::None);
		assert(r.length == 100);
		assert(std::fabs(r.samples[0] - 2.5f) < 1e-4f);
		assert(std::fabs(r.samples[1] + 5.f) < 1e-4f);
	}
	// 8-bit unsigned
	{
		std::vector<uint8_t> d;
		for (int i = 0; i < 50; i++)
			d.push_back(192);  // +0.5
		std::string p = tmpPath("lyr_f8.wav");
		writeBytes(p, buildWav(1, 1, 1000, 8, d));
		Take r;
		assert(readTakeWav(p, r).error == LoadError::None);
		assert(r.length == 50);
		assert(std::fabs(r.samples[10] - 5.f) < 1e-4f);
	}
	// float64
	{
		std::vector<uint8_t> d;
		double v = 0.3;
		for (int i = 0; i < 40; i++) {
			uint8_t buf[8];
			memcpy(buf, &v, 8);
			d.insert(d.end(), buf, buf + 8);
		}
		std::string p = tmpPath("lyr_f64.wav");
		writeBytes(p, buildWav(3, 1, 1000, 64, d));
		Take r;
		assert(readTakeWav(p, r).error == LoadError::None);
		assert(r.length == 40);
		assert(std::fabs(r.samples[5] - 3.f) < 1e-4f);
	}
	// float32 with NaN, Inf and overs
	{
		std::vector<uint8_t> d;
		float vals[4] = {NAN, INFINITY, 5.f, -5.f};
		for (float v : vals) {
			uint8_t buf[4];
			memcpy(buf, &v, 4);
			d.insert(d.end(), buf, buf + 4);
		}
		std::string p = tmpPath("lyr_fnan.wav");
		writeBytes(p, buildWav(3, 1, 1000, 32, d));
		Take r;
		assert(readTakeWav(p, r).error == LoadError::None);
		assert(r.length == 4);
		assert(r.samples[0] == 0.f && r.samples[1] == 0.f);
		assert(r.samples[2] == 10.f && r.samples[3] == -10.f);
	}
	// Upsampling: 500 Hz ramp doubles in length, linear in between
	{
		std::vector<uint8_t> d;
		for (int i = 0; i < 10; i++) {
			float v = i * 0.1f;
			uint8_t buf[4];
			memcpy(buf, &v, 4);
			d.insert(d.end(), buf, buf + 4);
		}
		std::string p = tmpPath("lyr_up.wav");
		writeBytes(p, buildWav(3, 1, 500, 32, d));
		Take r;
		assert(readTakeWav(p, r).error == LoadError::None);
		assert(r.length == 20);
		assert(std::fabs(r.samples[0]) < 1e-4f);
		assert(std::fabs(r.samples[1] - 0.5f) < 1e-3f);
		assert(std::fabs(r.samples[2] - 1.f) < 1e-3f);
	}
}

static void truncatesLongWav() {
	std::vector<uint8_t> d;
	float v = 0.1f;
	for (int i = 0; i < 200000; i++) {
		uint8_t buf[4];
		memcpy(buf, &v, 4);
		d.insert(d.end(), buf, buf + 4);
	}
	std::string p = tmpPath("lyr_long.wav");
	writeBytes(p, buildWav(3, 1, 1000, 32, d));
	Take r;
	LoadResult res = readTakeWav(p, r);
	assert(res.error == LoadError::None);
	assert(r.length == kMaxTakeSamples);
	assert(res.truncated);
}

static void rejectsGarbage() {
	Take r;
	assert(readTakeWav(tmpPath("lyr_does_not_exist.wav"), r).error == LoadError::CantOpen);

	std::vector<uint8_t> junk;
	uint32_t x = 12345;
	for (int i = 0; i < 200; i++) {
		x = x * 1664525u + 1013904223u;
		junk.push_back(x >> 24);
	}
	junk[0] = 'X';
	std::string p = tmpPath("lyr_junk.wav");
	writeBytes(p, junk);
	assert(readTakeWav(p, r).error == LoadError::NotWav);
	writeBytes(p, std::vector<uint8_t>());
	assert(readTakeWav(p, r).error == LoadError::NotWav);

	// RIFF/WAVE with fmt only
	writeBytes(p, buildWav(1, 1, 1000, 16, {}, false));
	assert(readTakeWav(p, r).error == LoadError::Empty);

	// RIFF/WAVE with nothing at all
	{
		std::vector<uint8_t> b;
		putTag(b, "RIFF");
		put32(b, 4);
		putTag(b, "WAVE");
		writeBytes(p, b);
		assert(readTakeWav(p, r).error == LoadError::Empty);
	}

	// Data size larger than the file: loads what exists
	{
		std::vector<uint8_t> d;
		for (int i = 0; i < 10; i++)
			put16(d, 16384);
		writeBytes(p, buildWav(1, 1, 1000, 16, d, true, 100000));
		LoadResult res = readTakeWav(p, r);
		assert(res.error == LoadError::None);
		assert(r.length == 10);
	}
	// Data chunk with zero bytes
	writeBytes(p, buildWav(1, 1, 1000, 16, {}, true, 100000));
	assert(readTakeWav(p, r).error == LoadError::Empty);

	// MP3 format tag
	{
		std::vector<uint8_t> d(64, 0);
		writeBytes(p, buildWav(0x55, 1, 1000, 16, d));
		assert(readTakeWav(p, r).error == LoadError::Unsupported);
	}
	// Truncated fmt chunk
	{
		std::vector<uint8_t> b;
		putTag(b, "RIFF");
		put32(b, 20);
		putTag(b, "WAVE");
		putTag(b, "fmt ");
		put32(b, 16);
		put16(b, 1);
		writeBytes(p, b);
		LoadError e = readTakeWav(p, r).error;
		assert(e == LoadError::Empty || e == LoadError::Unsupported);
	}
}

static void packRoundTrip() {
	Take t;
	t.length = 1000;
	for (int i = 0; i < t.length; i++)
		t.samples[i] = 10.f * std::sin(i * 0.05f);
	std::vector<uint8_t> b = packSamples16(t);
	assert(b.size() == 2000);
	Take r;
	assert(unpackSamples16(b.data(), b.size(), r));
	assert(r.length == 1000);
	for (int i = 0; i < r.length; i++)
		assert(std::fabs(r.samples[i] - t.samples[i]) < 20.0 / 65535.0);
	assert(!unpackSamples16(b.data(), 1999, r));
	std::vector<uint8_t> big((kMaxTakeSamples + 1) * 2, 0);
	assert(!unpackSamples16(big.data(), big.size(), r));
	std::vector<uint8_t> max(kMaxTakeSamples * 2, 0);
	assert(unpackSamples16(max.data(), max.size(), r) && r.length == kMaxTakeSamples);
}

// Standard base64, as Rack's string::toBase64 writes it for the patch JSON
static std::string toBase64(const std::vector<uint8_t>& b) {
	static const char* k = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	std::string s;
	for (size_t i = 0; i < b.size(); i += 3) {
		uint32_t v = (uint32_t) b[i] << 16;
		if (i + 1 < b.size())
			v |= (uint32_t) b[i + 1] << 8;
		if (i + 2 < b.size())
			v |= b[i + 2];
		s += k[(v >> 18) & 63];
		s += k[(v >> 12) & 63];
		s += i + 1 < b.size() ? k[(v >> 6) & 63] : '=';
		s += i + 2 < b.size() ? k[v & 63] : '=';
	}
	return s;
}

static std::vector<uint8_t> fromBase64(const std::string& s) {
	std::vector<uint8_t> b;
	uint32_t v = 0;
	int bits = 0;
	for (char c : s) {
		int d;
		if (c >= 'A' && c <= 'Z')
			d = c - 'A';
		else if (c >= 'a' && c <= 'z')
			d = c - 'a' + 26;
		else if (c >= '0' && c <= '9')
			d = c - '0' + 52;
		else if (c == '+')
			d = 62;
		else if (c == '/')
			d = 63;
		else
			break;
		v = (v << 6) | (uint32_t) d;
		bits += 6;
		if (bits >= 8) {
			bits -= 8;
			b.push_back((v >> bits) & 0xff);
		}
	}
	return b;
}

// The path a take takes through the patch JSON: a full 3 min take survives
// within 1 LSB, and data cut mid-sample is refused
static void takeJsonRoundTrip() {
	Take t;
	t.length = kMaxTakeSamples;
	for (int i = 0; i < t.length; i++)
		t.samples[i] = 9.9f * std::sin(i * 0.0013f) + 0.1f * std::sin(i * 0.7f);
	std::string text = toBase64(packSamples16(t));
	std::vector<uint8_t> b = fromBase64(text);
	assert(b.size() == 2 * (size_t) kMaxTakeSamples);
	Take r;
	assert(unpackSamples16(b.data(), b.size(), r));
	assert(r.length == kMaxTakeSamples);
	for (int i = 0; i < r.length; i++)
		assert(std::fabs(r.samples[i] - t.samples[i]) <= 20.0 / 65534.0 + 1e-6);
	std::vector<uint8_t> cut = fromBase64(text.substr(0, text.size() - 6));
	assert(cut.size() & 1);
	assert(!unpackSamples16(cut.data(), cut.size(), r));
}

static void metaRoundTrip() {
	Take t;
	t.mode = TakeMode::Sync;
	t.bars = 0.25;
	t.bpm = 133.5;
	t.seconds = t.bars * 240 / t.bpm;
	std::string j = takeMetaJson(t);
	Take r;
	assert(parseTakeMeta(j, r));
	assert(r.mode == TakeMode::Sync && r.bars == 0.25 && r.bpm == 133.5);
	assert(std::fabs(r.seconds - t.seconds) < 1e-12);
	assert(!parseTakeMeta("not json", r));
	assert(!parseTakeMeta("{\"version\":1,\"mode\":\"wat\"}", r));
}

static void takeBasics() {
	Take a, b;
	assert(a.empty());
	a.length = 5;
	a.samples[2] = 1.5f;
	a.mode = TakeMode::Free;
	a.seconds = 3;
	assert(!a.empty());
	b.copyFrom(a);
	assert(b.length == 5 && b.samples[2] == 1.5f && b.mode == TakeMode::Free && b.seconds == 3);
	assert(b.version == a.version);
	assert(b.at(2) == 1.5f && b.at(-1) == 0.f && b.at(5) == 0.f);
	b.clear();
	assert(b.empty());
}

static void namesAndStepping() {
	std::vector<std::string> s = sortedWavNames({"b.wav", "A.WAV", "x.txt"});
	assert((s == std::vector<std::string>{"A.WAV", "b.wav"}));
	assert(stepName(s, "A.WAV", 1) == 1);
	assert(stepName(s, "b.wav", 1) == 0);
	assert(stepName(s, "A.WAV", -1) == 1);
	assert(stepName(s, "b.wav", -1) == 0);
	assert(stepName(s, "zzz", 1) == 0);
	assert(stepName(s, "zzz", -1) == 1);
	assert(stepName({}, "a", 1) == -1);
	assert(nextTakeName({"LYREBIRD-001.wav", "lyrebird-007.WAV"}) == "LYREBIRD-008.wav");
	assert(nextTakeName({}) == "LYREBIRD-001.wav");
	assert(nextTakeName({"other.wav"}) == "LYREBIRD-001.wav");
}

// A data chunk far past the take limit loads truncated, reading only what the
// take needs: a 4 GB (sparse) file loads in a few ms rather than being read whole
// Peak resident memory so far, in bytes
static uint64_t peakRss() {
	struct rusage u;
	getrusage(RUSAGE_SELF, &u);
#ifdef __APPLE__
	return (uint64_t) u.ru_maxrss;
#else
	return (uint64_t) u.ru_maxrss * 1024;
#endif
}

// A 4 GB data chunk (sparse on disk) loads truncated without reading it all
// into memory. Skipped where the filesystem can't make sparse files, rather
// than writing 4 GB.
static void hugeWavReadsOnlyWhatItNeeds() {
	const uint64_t claimed = 0xfffffffcull;
	for (int ours = 0; ours < 2; ours++) {
		std::vector<uint8_t> b = buildWav(3, 1, ours ? 1000 : 48000, 32, {}, false);
		if (ours) {
			std::string meta = "{\"version\":1,\"mode\":\"free\",\"seconds\":1000.0,\"bpm\":120.0}";
			putTag(b, "lyrb");
			put32(b, (uint32_t) meta.size());
			b.insert(b.end(), meta.begin(), meta.end());
			if (meta.size() & 1)
				b.push_back(0);
		}
		putTag(b, "data");
		put32(b, (uint32_t) claimed);
		size_t dataAt = b.size();
		for (int i = 0; i < 1000; i++) {
			float v = 0.25f;
			uint8_t buf[4];
			memcpy(buf, &v, 4);
			b.insert(b.end(), buf, buf + 4);
		}
		std::string p = tmpPath("lyr_huge.wav");
		writeBytes(p, b);
		// Extend to the claimed size without writing it
		FILE* f = fopen(p.c_str(), "r+b");
		assert(f);
		bool extended = fseek(f, (long) (dataAt + claimed - 1), SEEK_SET) == 0 && fputc(0, f) == 0;
		extended = fclose(f) == 0 && extended;
		struct stat st;
		bool sparse = extended && stat(p.c_str(), &st) == 0 && (uint64_t) st.st_size == dataAt + claimed
			&& (uint64_t) st.st_blocks * 512 < 64ull * 1024 * 1024;
		if (!sparse) {
			remove(p.c_str());
			printf("hugeWavReadsOnlyWhatItNeeds: no sparse files here, skipped\n");
			return;
		}

		Take r;
		uint64_t before = peakRss();
		LoadResult res = readTakeWav(p, r);
		uint64_t grew = peakRss() - before;
		remove(p.c_str());
		assert(res.error == LoadError::None && res.truncated);
		assert(r.length == kMaxTakeSamples);
		assert(std::fabs(r.samples[0] - 2.5f) < 1e-5f);
		assert(r.samples[kMaxTakeSamples - 1] == 0.f);
		// Reading the whole chunk would need 4 GB; the take itself is under 1 MB
		assert(grew < 64ull * 1024 * 1024);
	}
}

// Crafted metadata stays in the ranges the module can record and display
static void metaIsBounded() {
	Take r;
	assert(parseTakeMeta("{\"version\":1,\"mode\":\"sync\",\"bars\":1e300,\"bpm\":1e9}", r));
	assert(r.bars == 1.0 && r.bpm == 1000.0);
	assert(std::isfinite(r.seconds) && r.seconds > 0.0);
	assert(parseTakeMeta("{\"version\":1,\"mode\":\"sync\",\"bars\":3,\"bpm\":0.001}", r));
	assert(r.bars == 1.0 && r.bpm == 1.0 && r.seconds == 240.0);
	assert(parseTakeMeta("{\"version\":1,\"mode\":\"sync\",\"bars\":0.25,\"bpm\":120}", r));
	assert(r.bars == 0.25);
	assert(parseTakeMeta("{\"version\":1,\"mode\":\"free\",\"seconds\":1e300,\"bpm\":120}", r));
	assert(std::isfinite(r.seconds) && r.seconds <= 16 * 240.0);
	assert(parseTakeMeta("{\"version\":1,\"mode\":\"free\",\"seconds\":1e-300,\"bpm\":120}", r));
	assert(r.seconds >= 1e-3);
	assert(!parseTakeMeta("{\"version\":1,\"mode\":\"free\",\"seconds\":-1,\"bpm\":120}", r));
	assert(!parseTakeMeta("{\"version\":1,\"mode\":\"sync\",\"bars\":1,\"bpm\":0}", r));
}

// A comma decimal locale (GTK sets one on Linux) changes neither the JSON nor its parsing
static void metaIgnoresLocale() {
	const char* names[] = {"de_DE.UTF-8", "de_DE.utf8", "de_DE", "fr_FR.UTF-8", "fr_FR"};
	bool set = false;
	for (const char* n : names)
		if (setlocale(LC_NUMERIC, n)) {
			set = true;
			break;
		}
	if (!set) {
		printf("metaIgnoresLocale: no comma locale installed, skipped\n");
		return;
	}
	Take t;
	t.mode = TakeMode::Sync;
	t.bars = 0.25;
	t.bpm = 133.5;
	std::string j = takeMetaJson(t);
	Take r;
	bool ok = parseTakeMeta(j, r);
	bool parsed = parseTakeMeta("{\"version\":1,\"mode\":\"free\",\"seconds\":12.5,\"bpm\":97.25}", r);
	double secs = r.seconds, bpm = r.bpm;
	setlocale(LC_NUMERIC, "C");
	assert(j.find(',', j.find("bpm")) == std::string::npos);
	assert(j.find("133.5") != std::string::npos && j.find("0.25") != std::string::npos);
	assert(ok && parsed);
	assert(secs == 12.5 && bpm == 97.25);
}

// Every take, in any buffer, gets a version of its own
static void versionsAreUnique() {
	Take a, b;
	assert(a.version != b.version);
	a.clear();
	b.clear();
	assert(a.version != b.version);
	uint32_t before = b.version;
	uint8_t two[2] = {0, 0};
	assert(unpackSamples16(two, 2, b));
	assert(b.version != before && b.version != a.version);
	b.copyFrom(a);
	assert(b.version == a.version);
}

int main() {
	wavRoundTrip();
	freeMetaRoundTrip();
	wavHeaderIsFloatMono();
	readsForeignWavs();
	truncatesLongWav();
	rejectsGarbage();
	packRoundTrip();
	takeJsonRoundTrip();
	metaRoundTrip();
	takeBasics();
	namesAndStepping();
	hugeWavReadsOnlyWhatItNeeds();
	metaIsBounded();
	metaIgnoresLocale();
	versionsAreUnique();
	printf("take file tests passed\n");
	return 0;
}

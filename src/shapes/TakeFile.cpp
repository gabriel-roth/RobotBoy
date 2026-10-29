#include "TakeFile.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <clocale>
#include <cstring>

namespace shapes {

namespace {

float sanitize(double v) {
	if (!std::isfinite(v))
		return 0.f;
	return (float) std::min(10.0, std::max(-10.0, v));
}

void put16(std::vector<uint8_t>& b, uint16_t v) {
	b.push_back(v & 0xff);
	b.push_back(v >> 8);
}

void put32(std::vector<uint8_t>& b, uint32_t v) {
	for (int i = 0; i < 4; i++)
		b.push_back((v >> (8 * i)) & 0xff);
}

void putTag(std::vector<uint8_t>& b, const char* t) {
	b.insert(b.end(), t, t + 4);
}

uint32_t get16(const uint8_t* p) {
	return p[0] | (p[1] << 8);
}

uint32_t get32(const uint8_t* p) {
	return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t) p[3] << 24);
}

std::string lower(std::string s) {
	for (char& c : s)
		c = (char) std::tolower((unsigned char) c);
	return s;
}

bool endsWithWav(const std::string& s) {
	return s.size() >= 4 && lower(s.substr(s.size() - 4)) == ".wav";
}

std::string fmtDouble(double v) {
	char buf[40];
	snprintf(buf, sizeof buf, "%.15g", v);
	// The C locale may use a comma (GTK sets it on Linux); JSON always uses a point
	for (char* p = buf; *p; p++)
		if (*p == ',')
			*p = '.';
	// Keep it looking like a JSON float
	if (!strpbrk(buf, ".eE"))
		strncat(buf, ".0", sizeof buf - strlen(buf) - 1);
	return buf;
}

// Finds `"key":` and returns the position of its value, or npos
size_t findValue(const std::string& json, const char* key) {
	std::string k = std::string("\"") + key + "\"";
	size_t p = json.find(k);
	if (p == std::string::npos)
		return p;
	p += k.size();
	while (p < json.size() && std::isspace((unsigned char) json[p]))
		p++;
	if (p >= json.size() || json[p] != ':')
		return std::string::npos;
	p++;
	while (p < json.size() && std::isspace((unsigned char) json[p]))
		p++;
	return p < json.size() ? p : std::string::npos;
}

bool findNumber(const std::string& json, const char* key, double& out) {
	size_t p = findValue(json, key);
	if (p == std::string::npos)
		return false;
	// strtod follows the C locale's decimal point, so swap the JSON point for it
	const lconv* lc = localeconv();
	std::string point = lc && lc->decimal_point && *lc->decimal_point ? lc->decimal_point : ".";
	std::string num;
	for (size_t i = p; i < json.size() && num.size() < 64; i++) {
		char ch = json[i];
		if (ch == '.')
			num += point;
		else if (std::isdigit((unsigned char) ch) || ch == '-' || ch == '+' || ch == 'e' || ch == 'E')
			num += ch;
		else
			break;
	}
	char* end = nullptr;
	double v = strtod(num.c_str(), &end);
	if (num.empty() || end == num.c_str() || !std::isfinite(v))
		return false;
	out = v;
	return true;
}

bool findString(const std::string& json, const char* key, std::string& out) {
	size_t p = findValue(json, key);
	if (p == std::string::npos || json[p] != '"')
		return false;
	size_t e = json.find('"', p + 1);
	if (e == std::string::npos)
		return false;
	out = json.substr(p + 1, e - p - 1);
	return true;
}

// 64-bit offsets, so a file over 2 GB can't wrap long on Windows
bool seekTo(FILE* f, uint64_t pos) {
#ifdef _WIN32
	return _fseeki64(f, (long long) pos, SEEK_SET) == 0;
#else
	return fseek(f, (long) pos, SEEK_SET) == 0;
#endif
}

uint64_t fileSize(FILE* f) {
#ifdef _WIN32
	if (_fseeki64(f, 0, SEEK_END) != 0)
		return 0;
	long long n = _ftelli64(f);
#else
	if (fseek(f, 0, SEEK_END) != 0)
		return 0;
	long n = ftell(f);
#endif
	return n > 0 ? (uint64_t) n : 0;
}

bool readAt(FILE* f, uint64_t pos, uint8_t* buf, size_t n) {
	return seekTo(f, pos) && fread(buf, 1, n, f) == n;
}

struct FileCloser {
	FILE* f;
	~FileCloser() {
		if (f)
			fclose(f);
	}
};

// Reads frames in order through a small buffer, so only the frames a take
// needs are ever read, however long the file
struct FrameReader {
	FILE* f;
	uint64_t start;  // file offset of frame 0
	size_t frameBytes;
	size_t frames;
	std::vector<uint8_t> buf;
	size_t first = 0, count = 0;  // frames held in buf

	FrameReader(FILE* f, uint64_t start, size_t frameBytes, size_t frames)
	    : f(f), start(start), frameBytes(frameBytes), frames(frames) {
		buf.resize(frameBytes * std::max<size_t>(1, 65536 / frameBytes));
	}

	// Frame i's bytes, or null past what the file holds
	const uint8_t* frame(size_t i) {
		if (i >= frames)
			return nullptr;
		if (i < first || i >= first + count) {
			size_t want = std::min(buf.size() / frameBytes, frames - i);
			count = 0;
			first = i;
			if (!seekTo(f, start + (uint64_t) i * frameBytes))
				return nullptr;
			count = fread(buf.data(), 1, want * frameBytes, f) / frameBytes;
			if (count == 0)
				return nullptr;
		}
		return buf.data() + (i - first) * frameBytes;
	}
};

std::string fileStem(const std::string& path) {
	size_t slash = path.find_last_of("/\\");
	std::string base = slash == std::string::npos ? path : path.substr(slash + 1);
	size_t dot = base.find_last_of('.');
	if (dot != std::string::npos && dot > 0)
		base = base.substr(0, dot);
	return base;
}

// Parses "LYREBIRD-NNN.wav" (any case); returns -1 if it isn't one
int takeNumber(const std::string& name) {
	std::string s = lower(name);
	const std::string pre = "lyrebird-";
	if (s.size() < pre.size() + 5 || s.compare(0, pre.size(), pre) != 0 || !endsWithWav(s))
		return -1;
	std::string digits = s.substr(pre.size(), s.size() - pre.size() - 4);
	if (digits.empty() || digits.size() > 6)
		return -1;
	for (char c : digits)
		if (!std::isdigit((unsigned char) c))
			return -1;
	return atoi(digits.c_str());
}

}  // namespace

std::string takeMetaJson(const Take& t) {
	std::string s = "{\"version\":1,";
	if (t.mode == TakeMode::Sync)
		s += "\"mode\":\"sync\",\"bars\":" + fmtDouble(t.bars);
	else
		s += "\"mode\":\"free\",\"seconds\":" + fmtDouble(t.seconds);
	s += ",\"bpm\":" + fmtDouble(t.bpm) + "}";
	return s;
}

// Longer than any take: the longest Sync take is 16 bars at 1 BPM
static constexpr double kMaxMetaSeconds = 16 * 240.0;

bool parseTakeMeta(const std::string& json, Take& out) {
	double version = 0, bpm = 0;
	std::string mode;
	if (!findNumber(json, "version", version) || version < 1)
		return false;
	if (!findString(json, "mode", mode) || !findNumber(json, "bpm", bpm) || bpm <= 0.0)
		return false;
	// Held to what the module can record, as patch takes are
	bpm = std::min(std::max(bpm, 1.0), 1000.0);
	if (mode == "sync") {
		double bars = 0;
		if (!findNumber(json, "bars", bars) || bars <= 0.0)
			return false;
		out.mode = TakeMode::Sync;
		out.bars = 1.0;
		for (double b : kLengthBars)
			if (std::fabs(bars - b) < 1e-9)
				out.bars = b;
		out.bpm = bpm;
		out.seconds = out.bars * 240.0 / bpm;
		return true;
	}
	if (mode == "free") {
		double seconds = 0;
		if (!findNumber(json, "seconds", seconds) || seconds <= 0.0)
			return false;
		out.mode = TakeMode::Free;
		out.seconds = std::min(std::max(seconds, 1e-3), kMaxMetaSeconds);
		out.bpm = bpm;
		return true;
	}
	return false;
}

bool writeTakeWav(const std::string& path, const Take& take) {
	int n = std::max(0, std::min(take.length, kMaxTakeSamples));
	std::string meta = takeMetaJson(take);
	uint32_t metaSize = (uint32_t) meta.size();
	uint32_t metaPad = metaSize & 1;
	double secs = take.seconds > 0.0 ? take.seconds : (double) n / kTakeRate;
	int rate = std::max(1, (int) std::lround(n / secs));

	std::vector<uint8_t> b;
	b.reserve(80 + metaSize + 4 * (size_t) n);
	putTag(b, "RIFF");
	put32(b, 0);  // patched below
	putTag(b, "WAVE");
	putTag(b, "fmt ");
	put32(b, 16);
	put16(b, 3);  // IEEE float
	put16(b, 1);
	put32(b, (uint32_t) rate);
	put32(b, (uint32_t) rate * 4);
	put16(b, 4);
	put16(b, 32);
	putTag(b, "lyrb");
	put32(b, metaSize);
	b.insert(b.end(), meta.begin(), meta.end());
	if (metaPad)
		b.push_back(0);
	putTag(b, "data");
	put32(b, (uint32_t) n * 4);
	for (int i = 0; i < n; i++) {
		float v = sanitize(take.samples[i]) / 10.f;
		uint32_t u;
		memcpy(&u, &v, 4);
		put32(b, u);
	}
	uint32_t riffSize = (uint32_t) b.size() - 8;
	for (int i = 0; i < 4; i++)
		b[4 + i] = (riffSize >> (8 * i)) & 0xff;

	FILE* f = fopen(path.c_str(), "wb");
	if (!f)
		return false;
	bool ok = fwrite(b.data(), 1, b.size(), f) == b.size();
	ok = (fclose(f) == 0) && ok;
	return ok;
}

LoadResult readTakeWav(const std::string& path, Take& out) {
	LoadResult res;
	FileCloser fc{fopen(path.c_str(), "rb")};
	FILE* f = fc.f;
	if (!f) {
		res.error = LoadError::CantOpen;
		return res;
	}
	// Only headers and the frames a take needs are read: folder stepping can
	// land on a file of any size
	const uint64_t size = fileSize(f);
	uint8_t head[12];
	if (size < 12 || !readAt(f, 0, head, 12) || memcmp(head, "RIFF", 4) != 0 || memcmp(head + 8, "WAVE", 4) != 0) {
		res.error = LoadError::NotWav;
		return res;
	}

	bool haveFmt = false;
	uint32_t format = 0, channels = 0, rate = 0, bits = 0;
	bool haveData = false;
	uint64_t dataPos = 0, dataBytes = 0;
	std::string meta;

	// Chunk walk; every read is bounds checked against the file size
	uint64_t pos = 12;
	while (pos + 8 <= size) {
		uint8_t ch[8];
		if (!readAt(f, pos, ch, 8))
			break;
		uint64_t len = get32(ch + 4);
		uint64_t body = pos + 8;
		uint64_t avail = size - body;
		uint64_t use = std::min(len, avail);
		if (memcmp(ch, "fmt ", 4) == 0 && use >= 16) {
			uint8_t fb[26] = {};
			size_t n = (size_t) std::min<uint64_t>(use, sizeof fb);
			if (!readAt(f, body, fb, n))
				break;
			format = get16(fb);
			channels = get16(fb + 2);
			rate = get32(fb + 4);
			bits = get16(fb + 14);
			// Extensible: the real format is the first two bytes of the sub-format
			if (format == 0xFFFE && use >= 26)
				format = get16(fb + 24);
			haveFmt = true;
		}
		else if (memcmp(ch, "lyrb", 4) == 0) {
			// Ours is a few dozen bytes; anything much bigger isn't
			if (use <= 4096) {
				meta.resize((size_t) use);
				if (use && !readAt(f, body, (uint8_t*) &meta[0], (size_t) use))
					meta.clear();
			}
		}
		else if (memcmp(ch, "data", 4) == 0 && !haveData) {
			haveData = true;
			dataPos = body;
			dataBytes = use;
		}
		if (len > avail)
			break;
		pos = body + len + (len & 1);
	}

	if (haveFmt && format != 1 && format != 3) {
		res.error = LoadError::Unsupported;
		return res;
	}
	if (!haveFmt || !haveData || dataBytes == 0) {
		res.error = haveFmt || !haveData ? LoadError::Empty : LoadError::Unsupported;
		return res;
	}
	bool okBits = format == 1 ? (bits == 8 || bits == 16 || bits == 24 || bits == 32)
	                          : (bits == 32 || bits == 64);
	if (channels < 1 || channels > 64 || rate < 1 || !okBits) {
		res.error = LoadError::Unsupported;
		return res;
	}

	const size_t bytesPer = bits / 8;
	const size_t frameBytes = bytesPer * channels;
	const size_t frames = (size_t) (dataBytes / frameBytes);
	if (frames == 0) {
		res.error = LoadError::Empty;
		return res;
	}
	FrameReader reader(f, dataPos, frameBytes, frames);

	// First channel, in volts; frames are asked for in order
	auto frameVolts = [&](size_t i) -> double {
		const uint8_t* p = reader.frame(i);
		if (!p)
			return 0.0;
		double v;
		if (format == 3) {
			if (bits == 32) {
				uint32_t u = get32(p);
				float x;
				memcpy(&x, &u, 4);
				v = x;
			}
			else {
				uint64_t u = (uint64_t) get32(p) | ((uint64_t) get32(p + 4) << 32);
				double x;
				memcpy(&x, &u, 8);
				v = x;
			}
		}
		else if (bits == 8) {
			v = (p[0] - 128) / 128.0;
		}
		else if (bits == 16) {
			v = (int16_t) get16(p) / 32768.0;
		}
		else if (bits == 24) {
			int32_t s = (int32_t) ((uint32_t) p[0] << 8 | (uint32_t) p[1] << 16 | (uint32_t) p[2] << 24);
			v = (s >> 8) / 8388608.0;
		}
		else {
			v = (int32_t) get32(p) / 2147483648.0;
		}
		return std::isfinite(v) ? v * 10.0 : 0.0;
	};

	Take parsed;  // metadata staging; avoids a half-written `out` on failure
	bool ours = !meta.empty() && parseTakeMeta(meta, parsed);

	out.clear();
	int n = 0;
	if (ours) {
		// Our own files hold take samples verbatim, whatever the rate field says
		n = (int) std::min<size_t>(frames, kMaxTakeSamples);
		res.truncated = frames > (size_t) kMaxTakeSamples;
		for (int i = 0; i < n; i++)
			out.samples[i] = sanitize(frameVolts(i));
		out.mode = parsed.mode;
		out.bars = parsed.bars;
		out.seconds = parsed.seconds;
		out.bpm = parsed.bpm;
	}
	else {
		double ratio = (double) rate / kTakeRate;  // source frames per take sample
		size_t total = (size_t) ((double) frames / ratio);
		if (total < 1)
			total = 1;
		res.truncated = total > (size_t) kMaxTakeSamples;
		n = (int) std::min<size_t>(total, kMaxTakeSamples);
		for (int i = 0; i < n; i++) {
			double v;
			if (ratio > 1.0) {
				// Box average over the span this output sample covers
				size_t a = (size_t) (i * ratio);
				size_t e = std::max(a + 1, (size_t) ((i + 1) * ratio));
				e = std::min(e, frames);
				a = std::min(a, e - 1);
				double sum = 0;
				for (size_t k = a; k < e; k++)
					sum += frameVolts(k);
				v = sum / (double) (e - a);
			}
			else if (ratio < 1.0) {
				double x = i * ratio;
				size_t a = std::min((size_t) x, frames - 1);
				size_t c = std::min(a + 1, frames - 1);
				double frac = x - (double) a;
				v = frameVolts(a) * (1.0 - frac) + frameVolts(c) * frac;
			}
			else {
				v = frameVolts((size_t) i);
			}
			out.samples[i] = sanitize(v);
		}
		out.mode = TakeMode::Free;
		out.bpm = 120.0;
		out.seconds = (double) frames / rate;
		if (res.truncated)
			out.seconds = (double) n / kTakeRate;
	}
	out.length = n;
	out.modified = false;
	std::string stem = fileStem(path);
	snprintf(out.name, sizeof out.name, "%s", stem.substr(0, 63).c_str());
	return res;
}

std::vector<uint8_t> packSamples16(const Take& take) {
	int n = std::max(0, std::min(take.length, kMaxTakeSamples));
	std::vector<uint8_t> b;
	b.reserve(2 * (size_t) n);
	for (int i = 0; i < n; i++) {
		long s = std::lround(sanitize(take.samples[i]) / 10.0 * 32767.0);
		s = std::min(32767L, std::max(-32767L, s));
		put16(b, (uint16_t) (int16_t) s);
	}
	return b;
}

bool unpackSamples16(const uint8_t* data, size_t n, Take& out) {
	if ((n & 1) || n / 2 > (size_t) kMaxTakeSamples || (n && !data))
		return false;
	size_t count = n / 2;
	for (size_t i = 0; i < count; i++)
		out.samples[i] = sanitize((int16_t) get16(data + 2 * i) / 32767.0 * 10.0);
	out.length = (int) count;
	out.version = newTakeVersion();
	return true;
}

std::vector<std::string> sortedWavNames(std::vector<std::string> names) {
	names.erase(std::remove_if(names.begin(), names.end(), [](const std::string& s) { return !endsWithWav(s); }),
	            names.end());
	std::sort(names.begin(), names.end(), [](const std::string& a, const std::string& b) {
		std::string la = lower(a), lb = lower(b);
		return la != lb ? la < lb : a < b;
	});
	return names;
}

int stepName(const std::vector<std::string>& sorted, const std::string& current, int delta) {
	int n = (int) sorted.size();
	if (n == 0)
		return -1;
	auto it = std::find(sorted.begin(), sorted.end(), current);
	if (it == sorted.end())
		return delta < 0 ? n - 1 : 0;
	int i = (int) (it - sorted.begin());
	return ((i + delta) % n + n) % n;
}

std::string nextTakeName(const std::vector<std::string>& names) {
	int highest = 0;
	for (const std::string& s : names)
		highest = std::max(highest, takeNumber(s));
	char buf[32];
	snprintf(buf, sizeof buf, "LYREBIRD-%03d.wav", highest + 1);
	return buf;
}

}  // namespace shapes

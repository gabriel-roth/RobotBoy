#pragma once
#include <atomic>
#include <cstddef>
#include <limits>
#include <string>

namespace shapes {

// Upper case ASCII for the display font, at most max characters. Anything
// else, a whole UTF-8 sequence at a time, shows as '?'.
inline std::string screenText(const std::string& s, size_t max) {
	std::string out;
	for (size_t i = 0; i < s.size() && out.size() < max; i++) {
		unsigned char ch = (unsigned char) s[i];
		if (ch >= 0x80 && ch < 0xc0)
			continue;
		out += ch >= 32 && ch < 127 ? (char) (ch >= 'a' && ch <= 'z' ? ch - 'a' + 'A' : ch) : '?';
	}
	return out;
}

// A short message shown until a given time. One thread sets it while another
// reads it: a read during a set can at worst mix two messages for a frame.
// The text is fixed size and always terminated, so nothing is ever freed
// under the reader.
template <size_t N>
struct ScreenMessage {
	void set(const std::string& text, double until) {
		std::string s = screenText(text, N);
		for (size_t i = 0; i <= N; i++)
			chars[i].store(i < s.size() ? s[i] : 0, std::memory_order_relaxed);
		untilTime.store(until, std::memory_order_release);
	}

	// Copies the text into out (N + 1 chars); true while it's still showing
	bool get(char* out, double now) const {
		double until = untilTime.load(std::memory_order_acquire);
		for (size_t i = 0; i < N; i++)
			out[i] = chars[i].load(std::memory_order_relaxed);
		out[N] = 0;
		return now < until;
	}

private:
	std::atomic<char> chars[N + 1] = {};
	std::atomic<double> untilTime{-std::numeric_limits<double>::infinity()};
};

}  // namespace shapes

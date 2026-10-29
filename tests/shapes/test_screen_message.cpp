// Lyrebird's footer message: screen text, a fixed buffer and an expiry time.
#include "shapes/ScreenMessage.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>

using namespace shapes;

static void screenTextRules() {
	assert(screenText("abc", 18) == "ABC");
	assert(screenText("abcdef", 3) == "ABC");
	assert(screenText("a\tb", 18) == "A?B");
	// A whole UTF-8 sequence is one '?'
	assert(screenText("caf\xc3\xa9!", 18) == "CAF?!");
	assert(screenText("\xe2\x80\x94", 18) == "?");
	assert(screenText("", 18) == "");
}

static void messageLifetime() {
	ScreenMessage<18> m;
	char out[19];
	assert(!m.get(out, 0.0));  // nothing yet
	assert(out[0] == 0);
	m.set("saved take-03", 10.0);
	assert(m.get(out, 5.0) && std::string(out) == "SAVED TAKE-03");
	assert(!m.get(out, 10.0));  // expired at until
	m.set("a very long message that keeps going", 10.0);
	assert(m.get(out, 0.0) && strlen(out) == 18);
	assert(std::string(out) == "A VERY LONG MESSAG");
	m.set("caf\xc3\xa9", 10.0);
	assert(m.get(out, 0.0) && std::string(out) == "CAF?");
}

int main() {
	screenTextRules();
	messageLifetime();
	printf("screen message tests passed\n");
	return 0;
}

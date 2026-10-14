#include "Lucarne.hpp"
#include "BeadsModule.hpp"
#include "lucarne/GrainLayout.hpp"
#include "lucarne/ScopeLayout.hpp"

#include <algorithm>
#include <array>
#include <cmath>

using robotboy_beads::GrainFeed;
using robotboy_beads::GrainLiveFrame;
using robotboy_beads::GrainStartEvent;

// Display rect (mm). VCV: the SCREEN rect in res/Lucarne.svg
// (panel-specs/lucarne.yaml), tall, with the key printed below it.
// MetaModule: 16:9, so it fills the screen fullscreen at 320 x 180; its
// faceplate (panel-specs/lucarne-mm.yaml) has no key.
static constexpr float kScreenX = 3.48f, kScreenY = 14.f, kScreenW = 54.f;
#ifdef METAMODULE
static constexpr float kScreenH = 30.375f;
#else
static constexpr float kScreenH = 60.f;
#endif

// With nothing attached the screen is cleared. On VCV it stays blank: the
// instruction is printed on the panel (Lucarne-Empty.svg). MetaModule's
// faceplate has no room for it, so the screen says it.
#ifdef METAMODULE
static const char* const kNoSource = "Add from the Expanders menu of Particules or Retours";
#else
static const char* const kNoSource = "";
#endif

enum class Source { None, Particules, Retours };

struct LucarneDisplay : widget::Widget {
	Lucarne* module = nullptr;
	Source source = Source::None;   // what sits on the left; the panel swap reads it

	// Particules: grains over the buffer. Retours (running or Slice): the echo scope.
	lucarne::GrainLayout grainView;
	lucarne::ScopeLayout scopeView;

	// Where reading the source's feed has got to.
	const GrainFeed* feed = nullptr;   // the feed eventCursor belongs to
	uint32_t eventCursor = 0;
	bool scopePrimed = false;   // false: read the whole scope ring next time
	uint32_t scopeEpoch = 0;    // the scope ring's epoch when last read
	double lastTime = -1.0;

	// Copies of the feed, refilled every frame.
	std::array<int8_t, GrainFeed::kColumns> peakLo{}, peakHi{};
	std::array<int8_t, GrainFeed::kScopeEntries> scopeLo{}, scopeHi{};
	std::array<GrainStartEvent, GrainFeed::kEvents> events{};
	GrainLiveFrame frame;
	bool demoPrimed = false;

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer == 1) {
			const double now = system::getTime();
			const float dt = lastTime < 0.0 ? 0.f : float(std::clamp(now - lastTime, 0.0, 0.5));
			lastTime = now;
			grainView.setSize(box.size.x, box.size.y);
			scopeView.setSize(box.size.x, box.size.y);

			BeadsModule* left = module ? dynamic_cast<BeadsModule*>(module->leftExpander.module) : nullptr;
			source = !left ? Source::None : left->delayMode ? Source::Retours : Source::Particules;
			if (!module) {
				lucarne::GrainLayout::Input in = demoInput();
				in.dt = dt;
				drawGrains(args, in);
			}
			else if (!left) {
				feed = nullptr;
				scopeView.clear();
				scopePrimed = false;
				grainView.clear();
				paint(args, grainView.getDrawList(), kNoSource);
			}
			else {
				drawFeed(args, left->grainFeed, left->tapsModulated.load(std::memory_order_relaxed), dt);
			}
		}
		Widget::drawLayer(args, layer);
	}

	void drawFeed(const DrawArgs& args, const GrainFeed& f, bool modulated, float dt) {
		if (feed != &f) {   // new source: start at its newest event
			feed = &f;
			eventCursor = f.event_write();
			scopeView.clear();
			scopePrimed = false;
		}
		f.ReadPeaks(peakLo.data(), peakHi.data());
		// Events first, then the frame: the frame is at least as new, so
		// every event's grain is already in it. Without a frame, leave the
		// events unread.
		const uint32_t before = eventCursor;
		const int numEvents = f.ReadEvents(&eventCursor, events.data(), int(events.size()));
		const bool haveFrame = f.ReadFrame(&frame);
		if (!haveFrame)
			eventCursor = before;

		if (haveFrame && frame.tap_mode) {
			grainView.clear();
			scopeView.update(scopeInput(f, modulated, dt));
			paint(args, scopeView.getDrawList(), nullptr);
			return;
		}
		lucarne::GrainLayout::Input in;
		in.epoch = f.epoch();
		in.peakLo = peakLo.data();
		in.peakHi = peakHi.data();
		in.frame = haveFrame ? &frame : nullptr;
		in.events = events.data();
		in.numEvents = haveFrame ? numEvents : 0;
		in.dt = dt;
		drawGrains(args, in);
	}

	void drawGrains(const DrawArgs& args, const lucarne::GrainLayout::Input& in) {
		scopeView.clear();
		scopePrimed = false;
		grainView.update(in);
		paint(args, grainView.getDrawList(), nullptr);
	}

	// The scope's input: every tap from the frame, and as much of the
	// feed's scope ring as this frame's window can show.
	lucarne::ScopeLayout::Input scopeInput(const GrainFeed& f, bool modulated, float dt) {
		lucarne::ScopeLayout::Input in;
		in.maxDelay = std::max<int32_t>(frame.max_delay, 1);
		in.frozen = frame.frozen;
		in.modulated = modulated;
		in.dt = dt;
		const int32_t m = in.maxDelay;
		// Delay-line indices: larger = older.
		auto age = [&](int32_t index) { return float(((index - frame.write_index) % m + m) % m); };
		float longest = 0.f;
		for (int i = 0; i < GrainLiveFrame::kTaps; i++) {
			const auto& t = frame.taps[i];
			lucarne::ScopeLayout::Tap tap{age(t.read_index), t.level, i};
			if (t.slice) {
				tap.slice = true;
				tap.sliceOldest = age(t.slice_start);
				tap.sliceLength = float(t.slice_length);
			}
			// Silent taps too: ScopeLayout holds them through Shape's dips,
			// so the ring read below must reach them.
			in.taps[in.numTaps++] = tap;
			longest = std::max(longest, tap.slice ? tap.sliceOldest : tap.delay);
		}

		const GrainFeed::ScopeInfo info = f.ReadScopeInfo();
		in.epoch = info.epoch;
		in.samplesPerEntry = std::max<int32_t>(info.samples_per_entry, 1);
		// Zero until the feed's first ScopeReset; keep the Input default
		// then so the zoom floor never reaches zero.
		if (info.line_rate > 0.f)
			in.lineRate = info.line_rate;
		// After a source or epoch change the window snaps, so read everything.
		int want = GrainFeed::kScopeEntries;
		if (scopePrimed && info.epoch == scopeEpoch)
			want = lucarne::ScopeLayout::entriesToRead(scopeView.getWindow(), longest,
				in.modulated || in.frozen, in.samplesPerEntry, GrainFeed::kScopeEntries);
		int n = f.ReadScope(info.head, scopeLo.data(), scopeHi.data(), want);
		// A reset during the copy may go unnoticed by ReadScope: recheck.
		if (f.ReadScopeInfo().epoch != info.epoch)
			n = 0;
		scopePrimed = n > 0;
		scopeEpoch = info.epoch;
		in.lo = scopeLo.data();
		in.hi = scopeHi.data();
		in.entries = n;
		return in;
	}

	// Module browser: a decaying sine and a few grains at different pitches.
	lucarne::GrainLayout::Input demoInput() {
		for (int c = 0; c < GrainFeed::kColumns; c++) {
			const float env = 1.f - float(c) / GrainFeed::kColumns;
			const float v = std::fabs(std::sin(c * 0.35f)) * env;
			peakHi[c] = int8_t(v * 110.f);
			peakLo[c] = int8_t(-v * 100.f);
		}
		static const float kSpeeds[6] = {1.f, 1.5f, 2.f, 0.5f, 0.75f, -1.f};
		static const float kPans[6] = {0.5f, 0.15f, 0.8f, 0.35f, 0.95f, 0.6f};
		frame = GrainLiveFrame();
		frame.write_index = 30000;
		frame.max_delay = 384000;
		for (int i = 0; i < 6; i++) {
			const int32_t start = 60000 + i * 50000;
			frame.slots[i] = {1, start - int32_t(kSpeeds[i] * 12000.f), 0.9f, false, 12000.f};
			events[i] = {1, i, start, kSpeeds[i], 24000.f, kPans[i]};
		}
		lucarne::GrainLayout::Input in;
		in.peakLo = peakLo.data();
		in.peakHi = peakHi.data();
		in.frame = &frame;
		in.events = events.data();
		// The grains are born once, then play on.
		in.numEvents = demoPrimed ? 0 : 6;
		demoPrimed = true;
		return in;
	}

	void paint(const DrawArgs& args, const lucarne::DrawList& l, const char* message) {
		NVGcontext* vg = args.vg;
		nvgSave(vg);
		nvgScissor(vg, 0.f, 0.f, box.size.x, box.size.y);
		nvgBeginPath(vg);
		nvgRect(vg, 0.f, 0.f, box.size.x, box.size.y);
		nvgFillColor(vg, nvgRGB(l.background.r, l.background.g, l.background.b));
		nvgFill(vg);
		for (const lucarne::Rect& r : l.under) {
			nvgBeginPath(vg);
			nvgRect(vg, r.x, r.y, r.w, r.h);
			nvgFillColor(vg, nvgRGB(r.color.r, r.color.g, r.color.b));
			nvgFill(vg);
		}
		if (!l.wave.empty()) {
			nvgBeginPath(vg);
			for (const lucarne::Rect& r : l.wave)
				nvgRect(vg, r.x, r.y, r.w, r.h);
			nvgFillColor(vg, nvgRGB(l.waveColor.r, l.waveColor.g, l.waveColor.b));
			nvgFill(vg);
		}
		for (const lucarne::Rect& r : l.marks) {
			nvgBeginPath(vg);
			nvgRect(vg, r.x, r.y, r.w, r.h);
			nvgFillColor(vg, nvgRGB(r.color.r, r.color.g, r.color.b));
			nvgFill(vg);
		}
		if (message && *message) {
			std::shared_ptr<window::Font> font = APP->window->loadFont(asset::system("res/fonts/ShareTechMono-Regular.ttf"));
			if (font) {
				nvgFontFaceId(vg, font->handle);
				// Sized from the width so both display shapes match; wrapped
				// to 86% of the width and centered vertically.
				nvgFontSize(vg, box.size.x * 0.05f);
				nvgFillColor(vg, nvgRGB(0x80, 0x8a, 0x94));
				nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_TOP);
				const float wrap = box.size.x * 0.86f;
				const float left = (box.size.x - wrap) * 0.5f;
				float bounds[4];
				nvgTextBoxBounds(vg, left, 0.f, wrap, message, nullptr, bounds);
				const float top = (box.size.y - (bounds[3] - bounds[1])) * 0.5f;
				nvgTextBox(vg, left, top, wrap, message, nullptr);
			}
		}
		nvgRestore(vg);
	}
};

struct LucarneWidget : ModuleWidget {
	LucarneDisplay* display;
#ifndef METAMODULE
	// Three VCV panels, identical but for the key area: Lucarne-Empty.svg has
	// the setup instruction (nothing attached, and the module browser),
	// Lucarne.svg the Particules key, Lucarne-Retours.svg the Retours key.
	// step() swaps them when the display's source changes. MetaModule's
	// faceplate has no key.
	std::shared_ptr<window::Svg> emptySvg, particulesSvg, retoursSvg;
	Source showing = Source::None;
#endif

	LucarneWidget(Lucarne* module) {
		setModule(module);
#ifdef METAMODULE
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Lucarne.svg")));
#else
		emptySvg = window::Svg::load(asset::plugin(pluginInstance, "res/Lucarne-Empty.svg"));
		particulesSvg = window::Svg::load(asset::plugin(pluginInstance, "res/Lucarne.svg"));
		retoursSvg = window::Svg::load(asset::plugin(pluginInstance, "res/Lucarne-Retours.svg"));
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Lucarne-Empty.svg")));
#endif
		addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewBlack>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewBlack>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		display = createWidget<LucarneDisplay>(mm2px(Vec(kScreenX, kScreenY)));
		display->box.size = mm2px(Vec(kScreenW, kScreenH));
		display->module = module;
		addChild(display);
	}

#ifndef METAMODULE
	void step() override {
		if (display->source != showing) {
			showing = display->source;
			if (SvgPanel* panel = dynamic_cast<SvgPanel*>(getPanel())) {
				panel->setBackground(showing == Source::Retours ? retoursSvg
					: showing == Source::Particules ? particulesSvg
					: emptySvg);
				panel->fb->setDirty();   // the SDK headers don't promise setBackground redraws
			}
		}
		ModuleWidget::step();
	}
#endif
};

Model* modelLucarne = createModel<Lucarne, LucarneWidget>("Lucarne");

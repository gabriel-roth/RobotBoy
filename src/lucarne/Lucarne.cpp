#include "Lucarne.hpp"
#include "GrainLayout.hpp"
#include "ScopeLayout.hpp"
#include "particules/Particules.hpp"
#include "retours_delay/Retours.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

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

// The module on Lucarne's left, if it is one Lucarne can show.
static Source FindSource(Module* left, robotboy_beads::GrainFeed** feed) {
	if (auto* p = dynamic_cast<Particules*>(left)) { *feed = &p->grain_feed_; return Source::Particules; }
	if (auto* r = dynamic_cast<Retours*>(left)) { *feed = &r->grain_feed_; return Source::Retours; }
	*feed = nullptr;
	return Source::None;
}

struct LucarneDisplay : widget::Widget {
	Lucarne* module = nullptr;
	lucarne::GrainLayout layout;
	// Retours (running or Slice): the echo scope instead of the grain layout.
	lucarne::ScopeLayout scope;
	bool scope_primed = false;    // false: read the whole scope ring next time
	uint32_t scope_epoch = 0;     // the scope ring's epoch when last read
	int8_t scope_lo[robotboy_beads::GrainFeed::kScopeEntries] = {};
	int8_t scope_hi[robotboy_beads::GrainFeed::kScopeEntries] = {};
	const robotboy_beads::GrainFeed* feed = nullptr;   // the feed event_cursor belongs to
	uint32_t event_cursor = 0;
	Source source = Source::None;   // what sits on the left; the panel swap reads it
	double last_time = -1.0;
	int8_t lo[robotboy_beads::GrainFeed::kColumns] = {};
	int8_t hi[robotboy_beads::GrainFeed::kColumns] = {};
	robotboy_beads::GrainLiveFrame frame;
	std::vector<robotboy_beads::GrainStartEvent> events =
		std::vector<robotboy_beads::GrainStartEvent>(robotboy_beads::GrainFeed::kEvents);
	bool demo_primed = false;

	// Module browser: a decaying sine and a few grains at different pitches.
	lucarne::GrainLayout::Input demoInput() {
		using namespace robotboy_beads;
		for (int c = 0; c < GrainFeed::kColumns; ++c) {
			const float env = 1.f - float(c) / GrainFeed::kColumns;
			const float v = std::fabs(std::sin(c * 0.35f)) * env;
			hi[c] = int8_t(v * 110.f);
			lo[c] = int8_t(-v * 100.f);
		}
		frame = GrainLiveFrame();
		frame.write_index = 30000;
		frame.max_delay = 384000;
		static const float kSpeeds[6] = {1.f, 1.5f, 2.f, 0.5f, 0.75f, -1.f};
		static const float kPans[6] = {0.5f, 0.15f, 0.8f, 0.35f, 0.95f, 0.6f};
		lucarne::GrainLayout::Input in;
		int n = 0;
		if (!demo_primed) {
			for (int i = 0; i < 6; ++i) {
				const int32_t start = 60000 + i * 50000;
				events[size_t(i)] = {1, i, start, kSpeeds[i], 24000.f, kPans[i]};
				++n;
			}
			demo_primed = true;
		}
		for (int i = 0; i < 6; ++i) {
			const int32_t start = 60000 + i * 50000;
			frame.slots[i] = {1, start - int32_t(kSpeeds[i] * 12000.f), 0.9f, false, 12000.f};
		}
		in.peak_lo = lo;
		in.peak_hi = hi;
		in.frame = &frame;
		in.events = events.data();
		in.num_events = n;
		return in;
	}

	// Fill a ScopeLayout::Input from the feed's scope ring and the frame.
	lucarne::ScopeLayout::Input scopeInput(const robotboy_beads::GrainFeed& f,
	                                       const robotboy_beads::GrainLiveFrame& fr, float dt) {
		using robotboy_beads::GrainFeed;
		lucarne::ScopeLayout::Input in;
		in.max_delay = std::max<int32_t>(fr.max_delay, 1);
		in.frozen = fr.frozen;
		in.dt = dt;
		float longest = 0.f;
		for (int i = 0; i < robotboy_beads::GrainLiveFrame::kTaps && in.num_taps < robotboy_beads::GrainLiveFrame::kTaps; ++i) {
			const auto& t = fr.taps[i];
			if (!(t.level > lucarne::GrainLayout::kTapMinLevel)) continue;
			// Delay-line indices: larger = older.
			const int32_t m = in.max_delay;
			auto age = [&](int32_t index) { return float(((index - fr.write_index) % m + m) % m); };
			lucarne::ScopeLayout::Tap tap{age(t.read_index), t.level, i};
			if (t.slice) {
				tap.slice = true;
				tap.slice_oldest = age(t.slice_start);
				tap.slice_length = float(t.slice_length);
			}
			in.taps[in.num_taps++] = tap;
			longest = std::max(longest, tap.slice ? tap.slice_oldest : tap.delay);
		}
		const GrainFeed::ScopeInfo info = f.ReadScopeInfo();
		in.epoch = info.epoch;
		in.samples_per_entry = std::max<int32_t>(info.samples_per_entry, 1);
		// Zero until the feed's first ScopeReset; keep the Input default
		// then so the zoom floor never reaches zero.
		if (info.line_rate > 0.f) in.line_rate = info.line_rate;
		// Read only what this frame's window can show: it eases from the
		// current window toward the longest tap x kZoomMargin, so the larger
		// of those bounds it. After a source or epoch change the window
		// snaps, so read everything.
		int want = GrainFeed::kScopeEntries;
		if (scope_primed && info.epoch == scope_epoch) {
			const float span = std::max(scope.window(), lucarne::ScopeLayout::kZoomMargin * longest);
			want = int(std::min<double>(GrainFeed::kScopeEntries,
			                            std::ceil(double(span) / in.samples_per_entry) + 2.0));
		}
		int n = f.ReadScope(info.head, scope_lo, scope_hi, want);
		// A reset during the copy may go unnoticed by ReadScope: recheck.
		if (f.ReadScopeInfo().epoch != info.epoch) n = 0;
		scope_primed = n > 0;
		scope_epoch = info.epoch;
		in.lo = scope_lo;
		in.hi = scope_hi;
		in.entries = n;
		return in;
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer == 1) {
			const double now = system::getTime();
			const float dt = last_time < 0.0 ? 0.f : float(std::min(std::max(now - last_time, 0.0), 0.5));
			last_time = now;
			layout.SetSize(box.size.x, box.size.y);
			scope.SetSize(box.size.x, box.size.y);
			bool show_scope = false;

			const char* message = nullptr;
			lucarne::GrainLayout::Input in;
			robotboy_beads::GrainFeed* src_feed = nullptr;
			source = module ? FindSource(module->leftExpander.module, &src_feed) : Source::None;
			if (!module) {
				in = demoInput();
			} else if (source == Source::None) {
				message = kNoSource;
				feed = nullptr;
			} else if (src_feed->classic.load(std::memory_order_relaxed)) {
				message = source == Source::Retours
					? "Retours is set to Classic mode. Lucarne only works in Beads mode."
					: "Particules is set to Classic mode. Lucarne only works in Beads mode.";
				feed = nullptr;
			} else {
				auto& f = *src_feed;
				if (feed != &f) {   // new source: start at its newest event
					feed = &f;
					event_cursor = f.event_write();
					scope.Clear();
					scope_primed = false;
				}
				f.ReadPeaks(lo, hi);
				// Events first, then the frame: the frame is at least as new, so
				// every event's grain is already in it. Without a frame, leave the
				// events unread.
				const uint32_t before = event_cursor;
				const int n_events = f.ReadEvents(&event_cursor, events.data(), int(events.size()));
				const bool have = f.ReadFrame(&frame);
				if (!have) event_cursor = before;
				in.epoch = f.epoch();
				in.peak_lo = lo;
				in.peak_hi = hi;
				in.frame = have ? &frame : nullptr;
				in.events = events.data();
				in.num_events = have ? n_events : 0;
				if (have && lucarne::GrainLayout::ScopeView(frame)) {
					show_scope = true;
					scope.Update(scopeInput(f, frame, dt));
				}
			}
			in.dt = dt;
			if (show_scope) {
				layout.Clear();
				paint(args, scope.draw_list(), message);
			} else {
				scope.Clear();
				scope_primed = false;
				if (message)
					layout.Clear();
				else
					layout.Update(in);
				paint(args, layout.draw_list(), message);
			}
		}
		Widget::drawLayer(args, layer);
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
			for (const lucarne::Rect& r : l.wave) nvgRect(vg, r.x, r.y, r.w, r.h);
			nvgFillColor(vg, nvgRGB(l.wave_color.r, l.wave_color.g, l.wave_color.b));
			nvgFill(vg);
		}
		for (const lucarne::Rect& r : l.marks) {
			nvgBeginPath(vg);
			nvgRect(vg, r.x, r.y, r.w, r.h);
			nvgFillColor(vg, nvgRGB(r.color.r, r.color.g, r.color.b));
			nvgFill(vg);
		}
		if (message && *message) {
			auto font =
				APP->window->loadFont(asset::system("res/fonts/ShareTechMono-Regular.ttf"));
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
#ifndef METAMODULE
	// Three VCV panels, identical but for the key area: Lucarne-Empty.svg has
	// the setup instruction (nothing attached, and the module browser), Lucarne.svg the
	// Particules key, Lucarne-Retours.svg the Retours key. step() swaps them
	// when the display's source changes. MetaModule's faceplate has no key.
	std::shared_ptr<window::Svg> empty_svg, particules_svg, retours_svg;
	LucarneDisplay* display = nullptr;
	Source showing = Source::None;

	void step() override {
		const Source want = display ? display->source : Source::None;
		if (want != showing) {
			showing = want;
			if (auto* panel = dynamic_cast<SvgPanel*>(getPanel())) {
				panel->setBackground(want == Source::Retours ? retours_svg
				                     : want == Source::Particules ? particules_svg
				                     : empty_svg);
				panel->fb->setDirty();   // the SDK headers don't promise setBackground redraws
			}
		}
		ModuleWidget::step();
	}
#endif

	LucarneWidget(Lucarne* module) {
		setModule(module);
#ifndef METAMODULE
		empty_svg = window::Svg::load(asset::plugin(pluginInstance, "res/Lucarne-Empty.svg"));
		particules_svg = window::Svg::load(asset::plugin(pluginInstance, "res/Lucarne.svg"));
		retours_svg = window::Svg::load(asset::plugin(pluginInstance, "res/Lucarne-Retours.svg"));
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Lucarne-Empty.svg")));
#else
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Lucarne.svg")));
#endif
		addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewBlack>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewBlack>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		auto* screen = new LucarneDisplay();
		screen->module = module;
		screen->box.pos = mm2px(Vec(kScreenX, kScreenY));
		screen->box.size = mm2px(Vec(kScreenW, kScreenH));
		addChild(screen);
#ifndef METAMODULE
		display = screen;
#endif
	}
};

Model* modelLucarne = createModel<Lucarne, LucarneWidget>("Lucarne");

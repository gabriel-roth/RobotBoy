#include "Lucarne.hpp"
#include "GrainLayout.hpp"
#include "particules/Particules.hpp"

#include <cmath>
#include <vector>

// Display rect (mm), matching the SCREEN rect in res/Lucarne.svg
// (panel-specs/lucarne.yaml).
static constexpr float kScreenX = 3.48f, kScreenY = 14.f, kScreenW = 54.f, kScreenH = 30.375f;

#ifdef METAMODULE
static const char* const kNoParticules = "Add from Particules' Expanders menu";
#else
static const char* const kNoParticules = "Place right of Particules";
#endif

struct LucarneDisplay : widget::Widget {
	Lucarne* module = nullptr;
	lucarne::GrainLayout layout;
	const robotboy_beads::GrainFeed* feed = nullptr;   // the feed event_cursor belongs to
	uint32_t event_cursor = 0;
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
		lucarne::GrainLayout::Input in;
		int n = 0;
		if (!demo_primed) {
			for (int i = 0; i < 6; ++i) {
				const int32_t start = 60000 + i * 50000;
				events[size_t(i)] = {1, i, start, kSpeeds[i], 24000.f};
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
		in.quality = 1;
		return in;
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer == 1) {
			const double now = system::getTime();
			const float dt = last_time < 0.0 ? 0.f : float(std::min(std::max(now - last_time, 0.0), 0.5));
			last_time = now;
			layout.SetSize(box.size.x, box.size.y);

			const char* message = nullptr;
			lucarne::GrainLayout::Input in;
			Particules* p = module ? dynamic_cast<Particules*>(module->leftExpander.module) : nullptr;
			if (!module) {
				in = demoInput();
			} else if (!p) {
				message = kNoParticules;
				feed = nullptr;
			} else if (p->grain_feed_.classic.load(std::memory_order_relaxed)) {
				message = "Beads mode only";
				feed = nullptr;
			} else {
				auto& f = p->grain_feed_;
				if (feed != &f) {   // new Particules: start at its newest event
					feed = &f;
					event_cursor = f.event_write();
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
				in.quality = p->quality_state_;
			}
			in.dt = dt;
			if (message)
				layout.Clear();
			else
				layout.Update(in);
			paint(args, message);
		}
		Widget::drawLayer(args, layer);
	}

	void paint(const DrawArgs& args, const char* message) {
		NVGcontext* vg = args.vg;
		const lucarne::DrawList& l = layout.draw_list();
		nvgSave(vg);
		nvgScissor(vg, 0.f, 0.f, box.size.x, box.size.y);
		nvgBeginPath(vg);
		nvgRect(vg, 0.f, 0.f, box.size.x, box.size.y);
		nvgFillColor(vg, nvgRGB(l.background.r, l.background.g, l.background.b));
		nvgFill(vg);
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
		if (message) {
			auto font =
				APP->window->loadFont(asset::system("res/fonts/ShareTechMono-Regular.ttf"));
			if (font) {
				nvgFontFaceId(vg, font->handle);
				nvgFontSize(vg, box.size.y * 0.09f);
				nvgFillColor(vg, nvgRGB(0x80, 0x8a, 0x94));
				nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
				nvgText(vg, box.size.x * 0.5f, box.size.y * 0.5f, message, nullptr);
			}
		}
		nvgRestore(vg);
	}
};

struct LucarneWidget : ModuleWidget {
	LucarneWidget(Lucarne* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Lucarne.svg")));
		addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewBlack>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewBlack>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		auto* display = new LucarneDisplay();
		display->module = module;
		display->box.pos = mm2px(Vec(kScreenX, kScreenY));
		display->box.size = mm2px(Vec(kScreenW, kScreenH));
		addChild(display);
	}
};

Model* modelLucarne = createModel<Lucarne, LucarneWidget>("Lucarne");

#include "Lucarne.hpp"

// Display rect (mm), matching the SCREEN rect in res/Lucarne.svg
// (panel-specs/lucarne.yaml).
static constexpr float kScreenX = 3.48f, kScreenY = 14.f, kScreenW = 54.f, kScreenH = 30.375f;

struct LucarneDisplay : widget::Widget {
	Lucarne* module = nullptr;

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer == 1) {
			nvgBeginPath(args.vg);
			nvgRect(args.vg, 0.f, 0.f, box.size.x, box.size.y);
			nvgFillColor(args.vg, nvgRGB(0x10, 0x14, 0x18));
			nvgFill(args.vg);
		}
		Widget::drawLayer(args, layer);
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
